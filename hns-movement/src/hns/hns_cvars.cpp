#include "hns_cvars.h"
#include "hns_config.h"
#include "hns_player.h"
#include "utils/utils.h"

#include "tier0/memdbgon.h"

namespace hns::cvars
{
	static_global ConVarRefAbstract *refs[HNSCVAR_COUNT] {};
	static_global CVValue_t enforced[HNSCVAR_COUNT];
	// What the plugin last wrote. Used to tell our own writes apart from outside changes.
	static_global CVValue_t lastWritten[HNSCVAR_COUNT];
	static_global bool hasWritten[HNSCVAR_COUNT] {};
	static_global bool warned[HNSCVAR_COUNT] {};

	// CVValue_t holds a CUtlString in its union, so it has no copy assignment. Only the primitive members are used here.
	static_function void SetValue(CVValue_t &dst, Type type, f32 value)
	{
		if (type == Type::Bool)
		{
			dst.m_bValue = value != 0.0f;
		}
		else if (type == Type::Int)
		{
			dst.m_i32Value = (i32)value;
		}
		else
		{
			dst.m_fl32Value = value;
		}
	}

	static_function void CopyValue(CVValue_t &dst, Type type, const CVValue_t &src)
	{
		if (type == Type::Bool)
		{
			dst.m_bValue = src.m_bValue;
		}
		else if (type == Type::Int)
		{
			dst.m_i32Value = src.m_i32Value;
		}
		else
		{
			dst.m_fl32Value = src.m_fl32Value;
		}
	}

	static_function bool ValuesEqual(Type type, const CVValue_t &a, const CVValue_t &b)
	{
		if (type == Type::Bool)
		{
			return a.m_bValue == b.m_bValue;
		}
		if (type == Type::Int)
		{
			return a.m_i32Value == b.m_i32Value;
		}
		return a.m_fl32Value == b.m_fl32Value;
	}

	static_function void FormatValue(Type type, const CVValue_t &value, char *buffer, size_t size)
	{
		if (type == Type::Bool)
		{
			V_snprintf(buffer, size, "%s", value.m_bValue ? "true" : "false");
		}
		else if (type == Type::Int)
		{
			V_snprintf(buffer, size, "%d", value.m_i32Value);
		}
		else
		{
			V_snprintf(buffer, size, "%g", value.m_fl32Value);
		}
	}

	bool Init(char *error, size_t maxlen)
	{
		std::string missing;
		for (i32 i = 0; i < HNSCVAR_COUNT; i++)
		{
			const Definition &def = definitions[i];
			if (def.type == Type::Bool)
			{
				refs[i] = new CConVarRef<bool>(def.name);
			}
			else if (def.type == Type::Int)
			{
				refs[i] = new CConVarRef<int>(def.name);
			}
			else
			{
				refs[i] = new CConVarRef<float>(def.name);
			}
			if (!refs[i]->IsValidRef() || !refs[i]->IsConVarDataAvailable())
			{
				missing += missing.empty() ? "" : ", ";
				missing += def.name;
			}
		}
		if (!missing.empty())
		{
			V_snprintf(error, maxlen, "Missing movement cvars: %s", missing.c_str());
			HNS_LOG_WARN(LogChannel::Cvar, "%s\n", error);
			return false;
		}
		// Some of these are FCVAR_DEVELOPMENTONLY / FCVAR_HIDDEN in the shipped game. CS2KZ unlocks every cvar on the
		// server; we only unlock the ones we own.
		for (i32 i = 0; i < HNSCVAR_COUNT; i++)
		{
			refs[i]->RemoveFlags(FCVAR_HIDDEN | FCVAR_DEVELOPMENTONLY | FCVAR_DEFENSIVE);
		}
		ResetToDefaults();
		return true;
	}

	void Cleanup()
	{
		for (i32 i = 0; i < HNSCVAR_COUNT; i++)
		{
			delete refs[i];
			refs[i] = nullptr;
		}
	}

	const CVValue_t &Get(HNSMovementCvar cvar)
	{
		return enforced[cvar];
	}

	f32 GetFloat(HNSMovementCvar cvar)
	{
		switch (definitions[cvar].type)
		{
			case Type::Bool:
				return enforced[cvar].m_bValue ? 1.0f : 0.0f;
			case Type::Int:
				return (f32)enforced[cvar].m_i32Value;
			default:
				return enforced[cvar].m_fl32Value;
		}
	}

	bool GetBool(HNSMovementCvar cvar)
	{
		return GetFloat(cvar) != 0.0f;
	}

	i32 FindByName(const char *name)
	{
		for (i32 i = 0; i < HNSCVAR_COUNT; i++)
		{
			if (KZ_STREQI(definitions[i].name, name) || KZ_STREQI(definitions[i].ConfigName(), name))
			{
				return i;
			}
		}
		return -1;
	}

	bool SetFromString(const char *name, const char *value)
	{
		i32 index = FindByName(name);
		if (index < 0)
		{
			return false;
		}
		const Definition &def = definitions[index];
		if (def.type == Type::Bool)
		{
			bool b = KZ_STREQI(value, "true") || (utils::IsNumeric(value) && atof(value) != 0.0);
			SetValue(enforced[index], def.type, b ? 1.0f : 0.0f);
		}
		else if (def.type == Type::Int)
		{
			SetValue(enforced[index], def.type, (f32)atoi(value));
		}
		else
		{
			SetValue(enforced[index], def.type, (f32)atof(value));
		}
		return true;
	}

	void ResetToDefaults()
	{
		for (i32 i = 0; i < HNSCVAR_COUNT; i++)
		{
			SetValue(enforced[i], definitions[i].type, definitions[i].defaultValue);
		}
	}

	void ApplyAll()
	{
		for (i32 i = 0; i < HNSCVAR_COUNT; i++)
		{
			ConVarData *data = refs[i]->GetConVarData();
			if (!data)
			{
				continue;
			}
			CVValue_t *current = data->Value(-1);
			const Type type = definitions[i].type;

			if (hasWritten[i] && !ValuesEqual(type, *current, lastWritten[i]) && !ValuesEqual(type, *current, enforced[i]))
			{
				if (!warned[i] && hns::config::settings.warnOnExternalChange)
				{
					char now[32], ours[32];
					FormatValue(type, *current, now, sizeof(now));
					FormatValue(type, enforced[i], ours, sizeof(ours));
					HNS_LOG_WARN(LogChannel::Cvar,
								 "%s was changed to %s outside hns-movement. It is enforced at %s; edit hns-movement.cfg and run hns_reload "
								 "instead.\n",
								 definitions[i].name, now, ours);
					warned[i] = true;
				}
			}
			else if (ValuesEqual(type, *current, enforced[i]))
			{
				warned[i] = false;
			}

			if (!ValuesEqual(type, *current, enforced[i]))
			{
				// Direct write, like CS2KZ: Set() would fire change callbacks and replication every tick.
				refs[i]->TypeTraits()->Copy(current, enforced[i]);
			}
			CopyValue(lastWritten[i], type, enforced[i]);
			hasWritten[i] = true;
		}
	}

	void DisableReplication()
	{
		for (i32 i = 0; i < HNSCVAR_COUNT; i++)
		{
			refs[i]->RemoveFlags(FCVAR_REPLICATED);
		}
	}

	void EnableReplication()
	{
		for (i32 i = 0; i < HNSCVAR_COUNT; i++)
		{
			if (refs[i])
			{
				refs[i]->AddFlags(FCVAR_REPLICATED);
			}
		}
	}

	void SendToClient(CPlayerSlot slot)
	{
		utils::SendMultipleConVarValues(slot, refs, enforced, HNSCVAR_COUNT);
	}

	void SendToAllClients()
	{
		for (i32 i = 0; i < MAX_PLAYERS; i++)
		{
			HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(i));
			if (player && player->IsConnectedController() && !player->IsFakeClient())
			{
				SendToClient(player->GetPlayerSlot());
			}
		}
	}

	void PrintTable()
	{
		META_CONPRINTF("%-34s %-10s %-10s %-10s\n", "cvar", "enforced", "game", "cs2kz");
		for (i32 i = 0; i < HNSCVAR_COUNT; i++)
		{
			const Definition &def = definitions[i];
			char ours[32], game[32], kz[32];
			FormatValue(def.type, enforced[i], ours, sizeof(ours));
			ConVarData *data = refs[i]->GetConVarData();
			if (data)
			{
				FormatValue(def.type, *data->Value(-1), game, sizeof(game));
			}
			else
			{
				V_snprintf(game, sizeof(game), "?");
			}
			CVValue_t kzValue;
			SetValue(kzValue, def.type, def.kzValue);
			FormatValue(def.type, kzValue, kz, sizeof(kz));
			META_CONPRINTF("%-34s %-10s %-10s %-10s\n", def.name, ours, game, kz);
		}
	}
} // namespace hns::cvars
