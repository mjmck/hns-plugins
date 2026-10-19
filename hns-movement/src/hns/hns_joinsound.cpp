#include "hns_joinsound.h"
#include "hns_config.h"
#include "hns_player.h"
#include "utils/utils.h"
#include "utils/ctimer.h"
#include "sdk/resourcemanifest.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

#include "tier0/memdbgon.h"

namespace hns::joinsound
{
	struct Entry
	{
		u64 steamID;
		std::string soundEvent;
		f32 volume;
	};

	static_persist std::vector<Entry> entries;
	static_persist std::vector<std::string> precacheFiles;
	// SteamID64 -> Plat_FloatTime of the last play. Survives map changes (the plugin stays loaded).
	static_persist std::unordered_map<u64, f64> lastPlayed;

	struct SlotState
	{
		bool active;     // ClientActive seen this connection.
		bool authorized; // Steam ticket validated this connection.
		bool handled;    // Sound queued (or skipped by the cooldown) for this connection.
	};

	static_persist SlotState slots[MAXPLAYERS + 1];

	static_persist const char exampleFile[] =
		"// hns-movement join sounds. Edit, then run hns_reload (server console or rcon).\n"
		"// When a listed player joins, everyone on the server hears their sound (same EmitSound path as CS2KZ jumpstat sounds).\n"
		"// Only plays after Steam has validated the player's ticket, so a copied name cannot trigger it.\n"
		"//\n"
		"// Custom sounds must come from a workshop addon that every client downloads (MultiAddonManager: mm_extra_addons).\n"
		"// List the addon's soundevents file here so it is added to every map's precache manifest:\n"
		"//   precache \"soundevents/soundevents_hns.vsndevts\"\n"
		"//\n"
		"// One line per player: <SteamID64 | STEAM_1:x:y | [U:1:n]> <sound event name> [volume 0-1, default 1]\n"
		"//   76561198000000000  hns.join.example  1.0\n"
		"// Any stock CS2 sound event also works without an addon, e.g.  76561198000000000  UIPanorama.round_report_odds_up\n"
		"//\n"
		"// Test from the server console: hns_join_sound_test <SteamID | sound event>\n"
		"\n"
		"precache \"soundevents/soundevents_hns.vsndevts\"\n";

	// Reads an unsigned decimal number at s, advancing it. No sscanf: newer glibc headers redirect it to a C23 symbol
	// that the Steam Runtime (glibc 2.31) on the server does not have.
	static_function bool ReadNumber(const char *&s, u64 &out)
	{
		if (!isdigit((unsigned char)*s))
		{
			return false;
		}
		out = 0;
		while (isdigit((unsigned char)*s))
		{
			out = out * 10 + (u64)(*s - '0');
			s++;
		}
		return true;
	}

	// Accepts SteamID64, STEAM_X:Y:Z and [U:1:N] / U:1:N.
	static_function bool ParseSteamID(const std::string &text, u64 &out)
	{
		constexpr u64 base = 76561197960265728ull;
		const char *s = text.c_str();
		u64 x, y, z;
		if (V_strnicmp(s, "STEAM_", 6) == 0)
		{
			s += 6;
			if (ReadNumber(s, x) && *s++ == ':' && ReadNumber(s, y) && *s++ == ':' && ReadNumber(s, z) && *s == '\0' && y <= 1)
			{
				out = base + z * 2 + y;
				return true;
			}
			return false;
		}
		if (*s == '[')
		{
			s++;
		}
		if (*s == 'U' || *s == 'u')
		{
			s++;
			if (*s++ == ':' && ReadNumber(s, x) && x == 1 && *s++ == ':' && ReadNumber(s, z) && (*s == '\0' || (*s == ']' && s[1] == '\0')))
			{
				out = base + z;
				return true;
			}
			return false;
		}
		if (ReadNumber(s, x) && *s == '\0' && x > base)
		{
			out = x;
			return true;
		}
		return false;
	}

	static_function const Entry *FindEntry(u64 steamID)
	{
		for (const Entry &entry : entries)
		{
			if (entry.steamID == steamID)
			{
				return &entry;
			}
		}
		return nullptr;
	}

	bool Load()
	{
		entries.clear();
		precacheFiles.clear();

		std::vector<char> buffer;
		if (!utils::ReadBufferFromFile(HNS_JOINSOUND_PATH, buffer))
		{
			std::vector<char> example(exampleFile, exampleFile + sizeof(exampleFile) - 1);
			if (utils::WriteBufferToFile(HNS_JOINSOUND_PATH, example))
			{
				HNS_LOG_INFO(LogChannel::General, "Created example %s (no join sounds yet).\n", HNS_JOINSOUND_PATH);
			}
			buffer = example;
		}

		std::string text(buffer.begin(), buffer.end());
		size_t start = 0;
		u32 lineNumber = 0;
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
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}

			std::vector<std::string> tokens = hns::config::Tokenize(line);
			if (tokens.empty())
			{
				continue;
			}
			if (tokens.size() < 2)
			{
				HNS_LOG_WARN(LogChannel::General, "%s:%u: '%s' needs a value, ignored.\n", HNS_JOINSOUND_PATH, lineNumber, tokens[0].c_str());
				continue;
			}
			if (KZ_STREQI(tokens[0].c_str(), "precache"))
			{
				precacheFiles.push_back(tokens[1]);
				continue;
			}
			Entry entry;
			if (!ParseSteamID(tokens[0], entry.steamID))
			{
				HNS_LOG_WARN(LogChannel::General, "%s:%u: '%s' is not a SteamID, ignored.\n", HNS_JOINSOUND_PATH, lineNumber, tokens[0].c_str());
				continue;
			}
			entry.soundEvent = tokens[1];
			entry.volume = tokens.size() >= 3 ? std::clamp((f32)atof(tokens[2].c_str()), 0.0f, 1.0f) : 1.0f;
			if (FindEntry(entry.steamID))
			{
				HNS_LOG_WARN(LogChannel::General, "%s:%u: %llu is listed twice, the first line wins.\n", HNS_JOINSOUND_PATH, lineNumber,
							 entry.steamID);
				continue;
			}
			entries.push_back(entry);
		}
		HNS_LOG_INFO(LogChannel::General, "Loaded %u join sound(s) and %u precache file(s) from %s.\n", (u32)entries.size(),
					 (u32)precacheFiles.size(), HNS_JOINSOUND_PATH);
		return true;
	}

	void PlayToAll(const char *soundEvent, f32 volume)
	{
		if (!soundEvent || !*soundEvent)
		{
			return;
		}
		// One single-recipient EmitSound per listener, exactly like CS2KZ's jumpstat sounds, so everyone hears it in 2D.
		for (i32 i = 0; i < MAXPLAYERS; i++)
		{
			HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(i));
			if (!player || !player->IsConnectedController() || player->IsFakeClient())
			{
				continue;
			}
			utils::PlaySoundToClient(CPlayerSlot(i), soundEvent, volume);
		}
	}

	static_function f64 PlayJoinSoundTimer(i32 slot, u64 steamID)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(slot));
		// They may have left (or someone else took the slot) during the delay.
		if (!player || !player->IsConnectedController() || player->GetSteamId64(true) != steamID)
		{
			return 0.0;
		}
		const Entry *entry = FindEntry(steamID);
		if (!entry || !hns::config::settings.joinSound)
		{
			return 0.0;
		}
		HNS_LOG_INFO(LogChannel::General, "Join sound '%s' for %s (%llu).\n", entry->soundEvent.c_str(), player->GetName(), steamID);
		PlayToAll(entry->soundEvent.c_str(), entry->volume);
		return 0.0;
	}

	static_function void TryQueue(HNSPlayer *player)
	{
		i32 slot = player->GetPlayerSlot().Get();
		if (slot < 0 || slot > MAXPLAYERS)
		{
			return;
		}
		SlotState &state = slots[slot];
		if (state.handled || !state.active || !state.authorized || !hns::config::settings.joinSound)
		{
			return;
		}
		// Validated ID only: the engine-reported xuid of an unauthenticated client is not trusted.
		u64 steamID = player->GetSteamId64(true);
		const Entry *entry = FindEntry(steamID);
		state.handled = true;
		if (!entry)
		{
			return;
		}

		f64 now = Plat_FloatTime();
		auto it = lastPlayed.find(steamID);
		if (hns::config::settings.joinSoundCooldown > 0.0f && it != lastPlayed.end()
			&& now - it->second < hns::config::settings.joinSoundCooldown)
		{
			return;
		}
		lastPlayed[steamID] = now;
		StartTimer<i32, u64>(PlayJoinSoundTimer, slot, steamID, (std::max)(hns::config::settings.joinSoundDelay, 0.0f), false, true);
	}

	void OnPlayerActive(HNSPlayer *player)
	{
		if (!player || player->IsFakeClient())
		{
			return;
		}
		i32 slot = player->GetPlayerSlot().Get();
		if (slot < 0 || slot > MAXPLAYERS)
		{
			return;
		}
		slots[slot].active = true;
		// On a map change the ticket can already be validated before ClientActive fires again.
		if (player->authAuthorized)
		{
			slots[slot].authorized = true;
		}
		TryQueue(player);
	}

	void OnPlayerAuthorized(HNSPlayer *player)
	{
		if (!player)
		{
			return;
		}
		i32 slot = player->GetPlayerSlot().Get();
		if (slot < 0 || slot > MAXPLAYERS)
		{
			return;
		}
		slots[slot].authorized = true;
		TryQueue(player);
	}

	void OnPlayerReset(HNSPlayer *player)
	{
		i32 slot = player->GetPlayerSlot().Get();
		if (slot >= 0 && slot <= MAXPLAYERS)
		{
			slots[slot] = {};
		}
	}

	void OnBuildGameSessionManifest(IEntityResourceManifest *manifest)
	{
		if (!manifest)
		{
			return;
		}
		for (const std::string &file : precacheFiles)
		{
			HNS_LOG_INFO(LogChannel::General, "Precache %s\n", file.c_str());
			manifest->AddResource(file.c_str());
		}
	}

	void PrintStatus()
	{
		META_CONPRINTF("---- hns-movement join sounds (%s) ----\n", hns::config::settings.joinSound ? "on" : "off");
		for (const std::string &file : precacheFiles)
		{
			META_CONPRINTF("precache %s\n", file.c_str());
		}
		for (const Entry &entry : entries)
		{
			META_CONPRINTF("%llu  %s  %.2f\n", entry.steamID, entry.soundEvent.c_str(), entry.volume);
		}
	}
} // namespace hns::joinsound

CON_COMMAND_F(hns_join_sound_test, "Play a join sound to everyone: hns_join_sound_test <SteamID from join-sounds.cfg | sound event>", FCVAR_NONE)
{
	if (context.GetPlayerSlot().Get() != -1)
	{
		return;
	}
	if (args.ArgC() < 2)
	{
		hns::joinsound::PrintStatus();
		META_CONPRINTF("Usage: hns_join_sound_test <SteamID | sound event>\n");
		return;
	}
	u64 steamID;
	if (hns::joinsound::ParseSteamID(args.Arg(1), steamID))
	{
		const hns::joinsound::Entry *entry = hns::joinsound::FindEntry(steamID);
		if (!entry)
		{
			META_CONPRINTF("%llu is not in %s.\n", steamID, HNS_JOINSOUND_PATH);
			return;
		}
		hns::joinsound::PlayToAll(entry->soundEvent.c_str(), entry->volume);
		META_CONPRINTF("Played '%s'.\n", entry->soundEvent.c_str());
		return;
	}
	hns::joinsound::PlayToAll(args.Arg(1), 1.0f);
	META_CONPRINTF("Played '%s'.\n", args.Arg(1));
}
