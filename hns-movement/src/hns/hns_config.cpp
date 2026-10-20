#include "hns_config.h"
#include "hns_cvars.h"
#include "hns_joinsound.h"
#include "jumpstats/js_jumpstats.h"
#include "utils/utils.h"

#include <cctype>
#include <string>
#include <vector>

#include "tier0/memdbgon.h"

namespace hns::config
{
	struct SettingDef
	{
		const char *name;
		enum
		{
			Bool,
			Float,
			Int
		} type;
		void *target;
		const char *description;
	};

	static_function const SettingDef *GetSettingDefs(u32 &count)
	{
		// clang-format off
		static_persist const SettingDef defs[] = {
			{"hns_ps_enabled",          SettingDef::Bool,  &settings.psEnabled,            "1 = CS2KZ prestrafe + 250 ground clamp + KZ perf formula. 0 = sv_maxspeed on ground, perf keeps max(landing, takeoff) speed."},
			{"hns_perf_requires_jump",  SettingDef::Bool,  &settings.perfRequiresJump,     "1 = perf speed rewrite only on real jumps (keeps surf speed when skimming/leaving ground). 0 = CS2KZ behaviour."},
			{"hns_ct_freeze_time",      SettingDef::Float, &settings.ctFreezeTime,         "Seconds CTs are frozen at round start; Ts can move and jump. 0 = off."},
			{"hns_freeze_no_falldamage",SettingDef::Bool,  &settings.freezeNoFallDamage,   "1 = sv_falldamage_scale is 0 while CTs are frozen, restored afterwards."},
			{"hns_freeze_effects",      SettingDef::Bool,  &settings.freezeEffects,        "1 = frozen CTs are tinted blue and get the health-shot screen effect."},
			{"hns_no_guns",             SettingDef::Bool,  &settings.noGuns,               "1 = nobody can hold a gun or taser; any that a player gets is removed."},
			{"hns_knife_ct_only",       SettingDef::Bool,  &settings.knifeCTOnly,          "1 = everyone keeps a knife but only CTs can attack with it (Ts keep their grenades)."},
			{"hns_knife_stab_only",     SettingDef::Bool,  &settings.knifeStabOnly,        "1 = knife slash (attack1) is disabled, only the right-click stab works."},
			{"hns_knife_attack_is_stab",SettingDef::Bool,  &settings.knifeAttackIsStab,    "1 = for CTs holding the knife, left click (+attack) stabs like right click (+attack2)."},
			{"hns_equip_knife",         SettingDef::Bool,  &settings.equipKnife,           "1 = switch to the knife when a player spawns or is left holding nothing."},
			{"hns_disable_autokick",    SettingDef::Bool,  &settings.disableAutokick,      "1 = run mp_autokick 0 on every map start (no kicks for suiciding, team kills or team damage)."},
			{"hns_disable_team_intro",  SettingDef::Bool,  &settings.disableTeamIntro,     "1 = run mp_team_intro_time 0 (no frozen team intro after warmup)."},
			{"hns_speed_hud",           SettingDef::Bool,  &settings.speedHud,             "1 = centre-screen HUD with speed, takeoff speed and held keys (mouse wheel jumps included)."},
			{"hns_silent_team_change",  SettingDef::Bool,  &settings.silentTeamChange,     "1 = hide the \"<player> is joining the Terrorists\" chat line on team changes."},
			{"hns_ct_spawn_decoy",      SettingDef::Bool,  &settings.ctSpawnDecoy,         "1 = CTs get a decoy when they spawn."},
			{"hns_t_spawn_grenades",    SettingDef::Bool,  &settings.tSpawnGrenades,       "1 = Ts get a smoke grenade and a flashbang when they spawn."},
			{"hns_knife_cooldown",      SettingDef::Float, &settings.knifeCooldown,        "Seconds a stabbed T takes no further knife damage (tinted red meanwhile). 0 = no cooldown."},
			{"hns_knife_damage",        SettingDef::Float, &settings.knifeDamage,          "Damage of every CT knife hit on a T, backstabs included. 0 = game default."},
			{"hns_join_sound",          SettingDef::Bool,  &settings.joinSound,            "1 = play a server-wide sound when a player listed in join-sounds.cfg joins."},
			{"hns_join_sound_delay",    SettingDef::Float, &settings.joinSoundDelay,       "Seconds after the listed player is in the server and Steam-validated before their join sound plays."},
			{"hns_join_sound_cooldown", SettingDef::Float, &settings.joinSoundCooldown,    "Seconds before the same player's join sound can play again (stops replays on map change). 0 = every connection."},
			{"hns_js",                  SettingDef::Bool,  &settings.jumpstats,            "1 = CS2KZ jumpstats, shown only to the jumper and their spectators (tiers and sounds in jumpstats.cfg). !js / !jssound toggle."},
			{"hns_js_min_tier",         SettingDef::Int,   &settings.jsMinTier,            "Lowest tier reported to the jumper: 0 off, 1 meh, 2 impressive, 3 perfect, 4 godlike, 5 ownage, 6 wrecker."},
			{"hns_js_sound_min_tier",   SettingDef::Int,   &settings.jsSoundMinTier,       "Lowest tier that plays a sound for the jumper (0 = never)."},
			{"hns_js_volume",           SettingDef::Float, &settings.jsVolume,             "Jumpstat sound volume, 0-1."},
			{"hns_js_console",          SettingDef::Bool,  &settings.jsConsole,            "1 = also print the detailed report and strafe table to console."},
			{"hns_js_db",               SettingDef::Bool,  &settings.jsDb,                 "1 = save personal bests and server records to a local SQLite file (needs the sql_mm Metamod plugin)."},
			{"hns_js_db_announce",      SettingDef::Bool,  &settings.jsDbAnnounceRecords,  "1 = announce to everyone when a new personal best puts a player in the server top 10 (JumpTop)."},
			{"hns_team_switch",         SettingDef::Bool,  &settings.teamSwitch,           "1 = swap teams when the CTs win. Keep 0 when ChaseMod handles the team switch."},
			{"hns_t_winstreak_switch",  SettingDef::Int,   &settings.tWinStreakSwitch,     "Also swap teams after this many T wins in a row. 0 = never."},
			{"hns_rampbug_threshold",   SettingDef::Float, &settings.rampbugThreshold,     "Rampbug fix: dot product below which two normals are different planes (CS2KZ: 0.999)."},
			{"hns_rampbug_same_plane",  SettingDef::Float, &settings.rampbugSamePlane,     "Rampbug fix: offset trace hitting a plane above this dot product counts as the same ramp (CS2KZ: 0.99)."},
			{"hns_rampbug_min_distance",SettingDef::Float, &settings.rampbugMinDistance,   "Rampbug fix: offset trace must go this much further to be used (CS2KZ: 1.0)."},
			{"hns_warn_external_change",SettingDef::Bool,  &settings.warnOnExternalChange, "Warn in console when something else changes an enforced cvar."},
			{"hns_debug_perf",          SettingDef::Bool,  &settings.debugPerf,            "Print every takeoff to the server console (development only)."},
		};
		// clang-format on
		count = KZ_ARRAYSIZE(defs);
		return defs;
	}

	static_function bool SetSetting(const char *name, const char *value)
	{
		u32 count;
		const SettingDef *defs = GetSettingDefs(count);
		for (u32 i = 0; i < count; i++)
		{
			if (!KZ_STREQI(defs[i].name, name))
			{
				continue;
			}
			if (defs[i].type == SettingDef::Bool)
			{
				*(bool *)defs[i].target = KZ_STREQI(value, "true") || atof(value) != 0.0;
			}
			else if (defs[i].type == SettingDef::Int)
			{
				*(i32 *)defs[i].target = atoi(value);
			}
			else
			{
				*(f32 *)defs[i].target = (f32)atof(value);
			}
			return true;
		}
		return false;
	}

	std::vector<std::string> Tokenize(const std::string &line)
	{
		std::vector<std::string> tokens;
		size_t i = 0;
		while (i < line.size())
		{
			while (i < line.size() && isspace((unsigned char)line[i]))
			{
				i++;
			}
			if (i >= line.size())
			{
				break;
			}
			if (line.compare(i, 2, "//") == 0)
			{
				break;
			}
			std::string token;
			if (line[i] == '"')
			{
				i++;
				while (i < line.size() && line[i] != '"')
				{
					token += line[i++];
				}
				i++;
			}
			else
			{
				while (i < line.size() && !isspace((unsigned char)line[i]))
				{
					if (line.compare(i, 2, "//") == 0)
					{
						break;
					}
					token += line[i++];
				}
			}
			tokens.push_back(token);
		}
		return tokens;
	}

	bool Load()
	{
		settings = Settings();
		hns::cvars::ResetToDefaults();

		WriteDefaultConfigIfMissing();

		std::vector<char> buffer;
		if (!utils::ReadBufferFromFile(HNS_CONFIG_PATH, buffer))
		{
			HNS_LOG_WARN(LogChannel::General, "Could not read %s, using built-in defaults.\n", HNS_CONFIG_PATH);
			hns::joinsound::Load();
			hns::jumpstats::Load();
			return false;
		}
		std::string text(buffer.begin(), buffer.end());
		size_t start = 0;
		u32 lineNumber = 0;
		u32 applied = 0;
		while (start <= text.size())
		{
			size_t end = text.find('\n', start);
			if (end == std::string::npos)
			{
				end = text.size();
			}
			std::string line = text.substr(start, end - start);
			start = end + 1;
			lineNumber++;

			std::vector<std::string> tokens = Tokenize(line);
			if (tokens.empty())
			{
				continue;
			}
			if (tokens.size() < 2)
			{
				HNS_LOG_WARN(LogChannel::General, "%s:%u: '%s' has no value, ignored.\n", HNS_CONFIG_PATH, lineNumber, tokens[0].c_str());
				continue;
			}
			const char *name = tokens[0].c_str();
			const char *value = tokens[1].c_str();
			if (hns::cvars::SetFromString(name, value) || SetSetting(name, value))
			{
				applied++;
				continue;
			}
			HNS_LOG_WARN(LogChannel::General, "%s:%u: unknown setting '%s'. Non-movement cvars belong in the server cfg.\n", HNS_CONFIG_PATH,
						 lineNumber, name);
		}
		HNS_LOG_INFO(LogChannel::General, "Loaded %u values from %s.\n", applied, HNS_CONFIG_PATH);
		hns::joinsound::Load();
		hns::jumpstats::Load();
		return true;
	}

	// withComments = the hns-movement.cfg form (config names, CS2KZ values). Otherwise plain cvar lines for the fallback cfg.
	static_function std::string FormatCvarLines(bool withComments)
	{
		std::string out;
		char line[256];
		for (i32 i = 0; i < HNSCVAR_COUNT; i++)
		{
			const hns::cvars::Definition &def = hns::cvars::definitions[i];
			const char *name = withComments ? def.ConfigName() : def.name;
			if (def.type == hns::cvars::Type::Bool)
			{
				V_snprintf(line, sizeof(line), "%-34s %s", name, hns::cvars::GetBool((HNSMovementCvar)i) ? "true" : "false");
			}
			else
			{
				V_snprintf(line, sizeof(line), "%-34s %g", name, hns::cvars::GetFloat((HNSMovementCvar)i));
			}
			out += line;
			if (withComments)
			{
				if (def.type == hns::cvars::Type::Bool)
				{
					V_snprintf(line, sizeof(line), " // CS2KZ: %s", def.kzValue != 0.0f ? "true" : "false");
				}
				else
				{
					V_snprintf(line, sizeof(line), " // CS2KZ: %g", def.kzValue);
				}
				if (def.cfgName)
				{
					out += line;
					V_snprintf(line, sizeof(line), " (sets %s)", def.name);
				}
				out += line;
			}
			out += "\n";
		}
		return out;
	}

	bool WriteFallback()
	{
		std::string out = "// Generated by hns-movement (hns_write_fallback). Same values the plugin enforces.\n"
						  "// exec this from server.cfg so the server keeps the HNS movement values even if the plugin fails to load.\n"
						  "// Regenerate after editing hns-movement.cfg.\n\n";
		out += FormatCvarLines(false);
		std::vector<char> buffer(out.begin(), out.end());
		if (!utils::WriteBufferToFile(HNS_FALLBACK_PATH, buffer))
		{
			HNS_LOG_WARN(LogChannel::General, "Failed to write %s.\n", HNS_FALLBACK_PATH);
			return false;
		}
		HNS_LOG_INFO(LogChannel::General, "Wrote %s.\n", HNS_FALLBACK_PATH);
		return true;
	}

	bool WriteDefaultConfigIfMissing()
	{
		std::vector<char> existing;
		char absPath[1024];
		V_snprintf(absPath, sizeof(absPath), "%s/csgo/%s", Plat_GetGameDirectory(), HNS_CONFIG_PATH);
		FILE *fp = fopen(absPath, "rb");
		if (fp)
		{
			fclose(fp);
			return true;
		}
		// Built from the compiled-in defaults, so it always matches this build.
		std::string out = "// hns-movement configuration. Edit, then run hns_reload (server console or rcon).\n"
						  "// Movement cvars listed here are enforced every tick; changing them with the console or a map cfg has no lasting\n"
						  "// effect. Everything else (game_type, mp_*, bot_*, sv_falldamage_scale, mp_solid_enemies) stays in your server cfg.\n\n"
						  "// ---- Plugin settings ----\n";
		u32 count;
		const SettingDef *defs = GetSettingDefs(count);
		char line[512];
		for (u32 i = 0; i < count; i++)
		{
			// Settings are written with their built-in defaults; the targets currently hold the same values after reset.
			if (defs[i].type == SettingDef::Bool)
			{
				V_snprintf(line, sizeof(line), "%-26s %d // %s\n", defs[i].name, *(bool *)defs[i].target ? 1 : 0, defs[i].description);
			}
			else if (defs[i].type == SettingDef::Int)
			{
				V_snprintf(line, sizeof(line), "%-26s %d // %s\n", defs[i].name, *(i32 *)defs[i].target, defs[i].description);
			}
			else
			{
				V_snprintf(line, sizeof(line), "%-26s %g // %s\n", defs[i].name, *(f32 *)defs[i].target, defs[i].description);
			}
			out += line;
		}
		out += "\n// ---- Movement cvars ----\n";
		out += FormatCvarLines(true);
		std::vector<char> buffer(out.begin(), out.end());
		if (!utils::WriteBufferToFile(HNS_CONFIG_PATH, buffer))
		{
			return false;
		}
		HNS_LOG_INFO(LogChannel::General, "Created default %s.\n", HNS_CONFIG_PATH);
		return true;
	}

	void PrintSettings()
	{
		u32 count;
		const SettingDef *defs = GetSettingDefs(count);
		for (u32 i = 0; i < count; i++)
		{
			if (defs[i].type == SettingDef::Bool)
			{
				META_CONPRINTF("%-26s %d\n", defs[i].name, *(bool *)defs[i].target ? 1 : 0);
			}
			else if (defs[i].type == SettingDef::Int)
			{
				META_CONPRINTF("%-26s %d\n", defs[i].name, *(i32 *)defs[i].target);
			}
			else
			{
				META_CONPRINTF("%-26s %g\n", defs[i].name, *(f32 *)defs[i].target);
			}
		}
	}
} // namespace hns::config
