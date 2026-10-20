// Jump reporting, tiers and settings. Reporting rules follow CS2KZ src/kz/jumpstats/jump_reporting.cpp (AGPL-3.0):
// the full report and tier sound use the same code (utils::PlaySoundToClient), but on this server they only go to the
// jumper and the spectators watching them: no broadcast to other players. Player preferences are replaced by server settings in
// hns-movement.cfg plus the per-player !js / !jssound switches; tiers and sounds come from jumpstats.cfg.
#include "js_jumpstats.h"
#include "hns/hns_player.h"
#include "hns/hns_config.h"
#include "sdk/resourcemanifest.h"
#include "utils/utils.h"

#include <string>
#include <vector>

#include "tier0/memdbgon.h"

#define JS_CHAT_PREFIX " {lime}JS {grey}|"

namespace hns::jumpstats
{
	// CS2KZ Classic (kz_mode_ckz.h). Thresholds for meh, impressive, perfect, godlike, ownage, wrecker.
	// clang-format off
	static_persist const f32 defaultTiers[JS_TIERED_JUMPTYPES][DISTANCETIER_COUNT - 1] = {
		{217.0f, 265.0f, 270.0f, 275.0f, 280.0f, 284.0f}, // LJ
		{217.0f, 275.0f, 280.0f, 287.0f, 292.0f, 295.0f}, // BH
		{217.0f, 275.0f, 280.0f, 287.0f, 292.0f, 295.0f}, // MBH
		{217.0f, 275.0f, 280.0f, 287.0f, 292.0f, 295.0f}, // WJ
		{120.0f, 160.0f, 170.0f, 180.0f, 190.0f, 200.0f}, // LAJ
		{217.0f, 260.0f, 265.0f, 270.0f, 275.0f, 278.0f}, // LAH
		{217.0f, 275.0f, 280.0f, 287.0f, 292.0f, 295.0f}, // JB
	};
	// Same sound events as CS2KZ (distanceTierSounds), from the CS2KZ workshop addon 3469155349.
	static_persist const char *defaultSounds[DISTANCETIER_COUNT] = {"", "", "kz.impressive", "kz.perfect", "kz.godlike", "kz.ownage", "kz.wrecker"};
	// clang-format on

	struct Tables
	{
		f32 tiers[JS_TIERED_JUMPTYPES][DISTANCETIER_COUNT - 1];
		std::string sounds[DISTANCETIER_COUNT];
		f32 maxDistance = 500.0f; // CS2KZ: no tier above 500 units
		std::vector<std::string> precacheFiles;
	};

	static_persist Tables tables;

	static_function void ResetTables()
	{
		tables = Tables();
		memcpy(tables.tiers, defaultTiers, sizeof(defaultTiers));
		for (i32 i = 0; i < DISTANCETIER_COUNT; i++)
		{
			tables.sounds[i] = defaultSounds[i];
		}
	}

	static_function i32 ParseJumpType(const char *name)
	{
		for (i32 i = 0; i < JS_TIERED_JUMPTYPES; i++)
		{
			if (KZ_STREQI(name, jumpTypeShortStr[i]))
			{
				return i;
			}
		}
		return -1;
	}

	static_function i32 ParseTier(const char *name)
	{
		for (i32 i = 0; i < DISTANCETIER_COUNT; i++)
		{
			if (KZ_STREQI(name, distanceTierNames[i]))
			{
				return i;
			}
		}
		return -1;
	}

	static_function std::string BuildDefaultFile()
	{
		std::string out = "// hns-movement jumpstats. Edit, then run hns_reload (server console or rcon).\n"
						  "// On/off, report thresholds and volume are in hns-movement.cfg (hns_js_*).\n"
						  "//\n"
						  "// Distance tiers: <jump type> <meh> <impressive> <perfect> <godlike> <ownage> <wrecker>\n"
						  "// A jump gets the highest tier whose distance it reaches. Defaults are CS2KZ Classic (sv_airaccelerate 100,\n"
						  "// sv_air_max_wishspeed 30). This server's movement settings make jumps longer: raise these after playtesting.\n";
		char line[256];
		for (i32 i = 0; i < JS_TIERED_JUMPTYPES; i++)
		{
			const f32 *t = defaultTiers[i];
			V_snprintf(line, sizeof(line), "%-4s %g %g %g %g %g %g\n", jumpTypeShortStr[i], t[0], t[1], t[2], t[3], t[4], t[5]);
			out += line;
		}
		out += "\n// No tier (and no report) above this distance.\n"
			   "max_distance 500\n"
			   "\n// Tier sounds: sound <tier> <sound event>. \"\" = silent. Defaults are CS2KZ's, from workshop addon 3469155349\n"
			   "// (MultiAddonManager: mm_extra_addons). Without the addon mounted they are silent.\n";
		for (i32 i = DistanceTier_Impressive; i < DISTANCETIER_COUNT; i++)
		{
			V_snprintf(line, sizeof(line), "sound %-10s %s\n", distanceTierNames[i], defaultSounds[i]);
			out += line;
		}
		out += "\n// Soundevents files added to every map's precache manifest (same as CS2KZ does for its addon).\n"
			   "precache \"soundevents/soundevents_cs2kz.vsndevts\"\n";
		return out;
	}

	bool Load()
	{
		ResetTables();
		std::vector<char> buffer;
		if (!utils::ReadBufferFromFile(HNS_JUMPSTATS_PATH, buffer))
		{
			std::string text = BuildDefaultFile();
			buffer.assign(text.begin(), text.end());
			if (utils::WriteBufferToFile(HNS_JUMPSTATS_PATH, buffer))
			{
				HNS_LOG_INFO(LogChannel::General, "Created default %s.\n", HNS_JUMPSTATS_PATH);
			}
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
			const char *key = tokens[0].c_str();
			if (KZ_STREQI(key, "precache") && tokens.size() >= 2)
			{
				tables.precacheFiles.push_back(tokens[1]);
				continue;
			}
			if (KZ_STREQI(key, "max_distance") && tokens.size() >= 2)
			{
				tables.maxDistance = (f32)atof(tokens[1].c_str());
				continue;
			}
			if (KZ_STREQI(key, "sound") && tokens.size() >= 2)
			{
				i32 tier = ParseTier(tokens[1].c_str());
				if (tier <= DistanceTier_Meh)
				{
					HNS_LOG_WARN(LogChannel::General, "%s:%u: unknown tier '%s'.\n", HNS_JUMPSTATS_PATH, lineNumber, tokens[1].c_str());
					continue;
				}
				tables.sounds[tier] = tokens.size() >= 3 ? tokens[2] : "";
				continue;
			}
			i32 type = ParseJumpType(key);
			if (type >= 0)
			{
				if (tokens.size() < DISTANCETIER_COUNT)
				{
					HNS_LOG_WARN(LogChannel::General, "%s:%u: %s needs 6 distances, ignored.\n", HNS_JUMPSTATS_PATH, lineNumber, key);
					continue;
				}
				for (i32 i = 0; i < DISTANCETIER_COUNT - 1; i++)
				{
					tables.tiers[type][i] = (f32)atof(tokens[i + 1].c_str());
				}
				continue;
			}
			HNS_LOG_WARN(LogChannel::General, "%s:%u: unknown line '%s'.\n", HNS_JUMPSTATS_PATH, lineNumber, key);
		}
		HNS_LOG_INFO(LogChannel::General, "Loaded %s.\n", HNS_JUMPSTATS_PATH);
		return true;
	}

	DistanceTier GetDistanceTier(JumpType jumpType, f32 distance)
	{
		// No tiers given for 'Invalid' jumps.
		if (jumpType < JumpType_LongJump || jumpType >= JS_TIERED_JUMPTYPES || distance > tables.maxDistance)
		{
			return DistanceTier_None;
		}
		// Get highest tier distance that the jump beats
		DistanceTier tier = DistanceTier_None;
		while (tier + 1 < DISTANCETIER_COUNT && distance >= tables.tiers[jumpType][tier])
		{
			tier = (DistanceTier)(tier + 1);
		}
		return tier;
	}

	static_function DistanceTier ClampTierSetting(i32 value)
	{
		return (DistanceTier)Clamp(value, 0, DISTANCETIER_COUNT - 1);
	}

	static_function f32 FloorTo(f32 value, i32 decimals)
	{
		f32 scale = powf(10.0f, (f32)decimals);
		return floorf(value * scale) / scale;
	}

	static_function void AddSegment(std::string &line, const char *color, const char *value, const char *label)
	{
		char segment[96];
		V_snprintf(segment, sizeof(segment), " {grey}| %s%s {grey}%s", color, value, label);
		line += segment;
	}

	static_function std::string ReleaseString(Jump *jump)
	{
		JumpType type = jump->jumpType;
		f32 release = jump->GetReleaseInTick();
		if ((type != JumpType_LongJump && type != JumpType_LadderJump && type != JumpType_WeirdJump) || release < -20)
		{
			return "";
		}
		char out[32];
		if (release > 10)
		{
			return "{red}\xE2\x9C\x97";
		}
		if (release > 0)
		{
			V_snprintf(out, sizeof(out), "{red}+%.1f", release);
			return out;
		}
		if (release == 0)
		{
			return "{green}\xE2\x9C\x93";
		}
		V_snprintf(out, sizeof(out), "{blue}%.1f", release);
		return out;
	}

	static_function void PrintJumpToChat(HNSPlayer *target, Jump *jump, DistanceTier tier)
	{
		DistanceTier minTier = ClampTierSetting(hns::config::settings.jsMinTier);
		if (minTier == DistanceTier_None || tier < minTier)
		{
			return;
		}
		const char *jumpColor = distanceTierColors[tier];
		char buffer[128];
		std::string line;
		V_snprintf(buffer, sizeof(buffer), "%s %s%s{grey}: %s%.1f", JS_CHAT_PREFIX, jumpColor, jumpTypeShortStr[jump->jumpType], jumpColor,
				   FloorTo(jump->GetDistance(), 1));
		line = buffer;

		char value[32];
		if (jump->block > 0.0f)
		{
			V_snprintf(value, sizeof(value), "%.0f", jump->block);
			AddSegment(line, distanceTierColors[GetDistanceTier(jump->jumpType, jump->block)], value, "Block");
		}
		V_snprintf(value, sizeof(value), "%i", (i32)jump->strafes.size());
		AddSegment(line, "{olive}", value, jump->strafes.size() == 1 ? "Strafe" : "Strafes");
		V_snprintf(value, sizeof(value), "%.0f%%", jump->sync * 100.0f);
		AddSegment(line, "{olive}", value, "Sync");
		V_snprintf(value, sizeof(value), "%.0f", jump->GetTakeoffSpeed());
		AddSegment(line, "{olive}", value, "Pre");
		V_snprintf(value, sizeof(value), "%.0f", jump->currentMaxSpeed);
		AddSegment(line, "{olive}", value, "Max");
		if (jump->edge >= 0.0f)
		{
			V_snprintf(value, sizeof(value), "%.2f", jump->edge);
			AddSegment(line, "{olive}", value, "Edge");
		}
		V_snprintf(value, sizeof(value), "%.1f", jump->currentMaxHeight);
		AddSegment(line, "{olive}", value, "Height");
		std::string release = ReleaseString(jump);
		if (!release.empty())
		{
			AddSegment(line, release.c_str(), "", "W");
		}
		utils::CPrintChat(target->GetController(), "%s", line.c_str());
	}

	static_function void PrintJumpToConsole(HNSPlayer *target, Jump *jump, DistanceTier tier)
	{
		if (!hns::config::settings.jsConsole)
		{
			return;
		}
		DistanceTier minTier = ClampTierSetting(hns::config::settings.jsMinTier);
		if (minTier == DistanceTier_None || tier < minTier)
		{
			return;
		}
		CCSPlayerController *controller = target->GetController();
		utils::PrintConsole(controller, "%s jumped %.4f units with a %s", jump->player->GetName(), jump->GetDistance(), jumpTypeStr[jump->jumpType]);

		char block[48] = "", edge[48] = "", landEdge[48] = "";
		if (jump->block > 0.0f)
		{
			V_snprintf(block, sizeof(block), "%.0f Block | ", jump->block);
		}
		if (jump->edge >= 0.0f)
		{
			V_snprintf(edge, sizeof(edge), "%.2f Edge | ", jump->edge);
		}
		if (jump->landingEdge > 0.0f)
		{
			V_snprintf(landEdge, sizeof(landEdge), "%.2f LandEdge | ", jump->landingEdge);
		}
		utils::PrintConsole(controller, "%s%s%s%i %s | %.1f%% Sync | %.2f Pre | %.2f Max | %.0f%% BA | %.0f%% OL | %.0f%% DA | %.2f Height", block,
							edge, landEdge, (i32)jump->strafes.size(), jump->strafes.size() == 1 ? "Strafe" : "Strafes", jump->sync * 100.0f,
							jump->GetTakeoffSpeed(), jump->currentMaxSpeed, jump->badAngles * 100.0f, jump->overlap * 100.0f,
							jump->deadAir * 100.0f, jump->currentMaxHeight);
		utils::PrintConsole(controller,
							"%.0f%% GainEff | %.3f Airpath | %.1f Deviation | %.1f Width | %.4f Airtime | %.1f Offset | %.2f/%.2f Crouched | "
							"%.1f W release (ticks)",
							jump->gainEff * 100.0f, jump->GetAirPath(), jump->GetDeviation(), jump->width, jump->airtime, jump->GetOffset(),
							jump->duckEndDuration, jump->duckDuration, jump->GetReleaseInTick());
		// Per strafe, like CS2KZ's console table (angle ratio column left out).
		utils::PrintConsole(controller, "  #.   Sync     Gain     Loss      Max  AirTime   BA   OL   DA  Width");
		for (size_t i = 0; i < jump->strafes.size(); i++)
		{
			Strafe &s = jump->strafes[i];
			utils::PrintConsole(controller, "%3i. %5.0f%% %8.2f %8.2f %8.2f %8.3f %4.1f %4.1f %4.1f %6.1f", (i32)i + 1, s.GetSync() * 100.0f,
								s.GetGain(), -fabs(s.GetLoss()), s.strafeMaxSpeed, s.duration, s.badAngles * ENGINE_FIXED_TICK_RATE,
								s.overlap * ENGINE_FIXED_TICK_RATE, s.deadAir * ENGINE_FIXED_TICK_RATE, fabsf(s.width));
		}
	}

	static_function void PlayJumpstatSound(HNSPlayer *target, DistanceTier tier)
	{
		if (!target->jumpstats->sounds)
		{
			return;
		}
		DistanceTier minTier = ClampTierSetting(hns::config::settings.jsSoundMinTier);
		if (minTier == DistanceTier_None || tier < minTier || tier <= DistanceTier_Meh || tables.sounds[tier].empty())
		{
			return;
		}
		utils::PlaySoundToClient(target->GetPlayerSlot(), tables.sounds[tier].c_str(), Clamp(hns::config::settings.jsVolume, 0.0f, 1.0f));
	}

	// True when viewer is a spectator (or dead player) watching target's pawn in first or third person. A live player is
	// never treated as a spectator, whatever their observer services say.
	static_function bool IsSpectating(HNSPlayer *viewer, CCSPlayerPawn *target)
	{
		if (!target || !viewer->IsConnectedController() || viewer->IsFakeClient())
		{
			return false;
		}
		CCSPlayerController *controller = viewer->GetController();
		if (!controller)
		{
			return false;
		}
		CCSPlayerPawn *ownPawn = controller->GetPlayerPawn();
		if (ownPawn && ownPawn->IsAlive())
		{
			return false;
		}
		CCSPlayerPawnBase *observer = controller->GetObserverPawn();
		if (!observer || !observer->m_pObserverServices())
		{
			return false;
		}
		CPlayer_ObserverServices *obs = observer->m_pObserverServices();
		ObserverMode_t mode = obs->m_iObserverMode();
		if (mode != OBS_MODE_IN_EYE && mode != OBS_MODE_CHASE)
		{
			return false;
		}
		return obs->m_hObserverTarget().Get() == target;
	}

	void AnnounceJump(Jump *jump)
	{
		if (!hns::config::settings.jumpstats || jump->jumpType == JumpType_FullInvalid)
		{
			return;
		}
		// Invalid jumps and jumps that land lower than they took off are never reported (CS2KZ without jsalways).
		if (!jump->IsValid() || jump->GetOffset() <= -JS_EPSILON)
		{
			return;
		}
		DistanceTier tier = GetDistanceTier(jump->jumpType, jump->GetDistance());
		if (tier == DistanceTier_None)
		{
			return;
		}
		// Jumpstats are private: the jumper and the spectators watching them (first or third person) get the report and
		// the sound. Nobody else sees or hears anything about this jump.
		HNSPlayer *jumper = jump->player;
		if (!jumper || !jumper->processingMovement || !jumper->IsConnectedController() || jumper->IsFakeClient())
		{
			return;
		}
		CCSPlayerPawn *jumperPawn = jumper->GetPlayerPawn();
		for (i32 i = 0; i < MAXPLAYERS; i++)
		{
			HNSPlayer *viewer = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(i));
			if (!viewer || !viewer->jumpstats->reporting)
			{
				continue;
			}
			if (viewer != jumper && !IsSpectating(viewer, jumperPawn))
			{
				continue;
			}
			PrintJumpToChat(viewer, jump, tier);
			PrintJumpToConsole(viewer, jump, tier);
			PlayJumpstatSound(viewer, tier);
		}
	}

	void OnBuildGameSessionManifest(IEntityResourceManifest *manifest)
	{
		if (!manifest || !hns::config::settings.jumpstats)
		{
			return;
		}
		for (const std::string &file : tables.precacheFiles)
		{
			HNS_LOG_INFO(LogChannel::General, "Precache %s\n", file.c_str());
			manifest->AddResource(file.c_str());
		}
	}

	void ToggleReporting(CPlayerSlot slot)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(slot);
		if (!player || !player->GetController())
		{
			return;
		}
		player->jumpstats->reporting = !player->jumpstats->reporting;
		utils::CPrintChat(player->GetController(), "%s {grey}Jumpstats %s{grey}.", JS_CHAT_PREFIX,
						  player->jumpstats->reporting ? "{green}on" : "{red}off");
	}

	void ToggleSounds(CPlayerSlot slot)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(slot);
		if (!player || !player->GetController())
		{
			return;
		}
		player->jumpstats->sounds = !player->jumpstats->sounds;
		utils::CPrintChat(player->GetController(), "%s {grey}Jumpstat sounds %s{grey}.", JS_CHAT_PREFIX,
						  player->jumpstats->sounds ? "{green}on" : "{red}off");
	}

	void PrintStatus()
	{
		META_CONPRINTF("---- hns-movement jumpstats (%s) ----\n", hns::config::settings.jumpstats ? "on" : "off");
		META_CONPRINTF("%-4s %7s %7s %7s %7s %7s %7s\n", "type", "meh", "impr", "perf", "godlike", "ownage", "wrecker");
		for (i32 i = 0; i < JS_TIERED_JUMPTYPES; i++)
		{
			const f32 *t = tables.tiers[i];
			META_CONPRINTF("%-4s %7g %7g %7g %7g %7g %7g\n", jumpTypeShortStr[i], t[0], t[1], t[2], t[3], t[4], t[5]);
		}
		META_CONPRINTF("max_distance %g\n", tables.maxDistance);
		for (i32 i = DistanceTier_Impressive; i < DISTANCETIER_COUNT; i++)
		{
			META_CONPRINTF("sound %-10s %s\n", distanceTierNames[i], tables.sounds[i].c_str());
		}
		for (const std::string &file : tables.precacheFiles)
		{
			META_CONPRINTF("precache %s\n", file.c_str());
		}
	}
} // namespace hns::jumpstats

CON_COMMAND_F(hns_js, "Turn your jumpstats on or off (chat, console and sounds).", FCVAR_CLIENT_CAN_EXECUTE)
{
	if (context.GetPlayerSlot().Get() < 0)
	{
		hns::jumpstats::PrintStatus();
		return;
	}
	hns::jumpstats::ToggleReporting(context.GetPlayerSlot());
}

CON_COMMAND_F(hns_js_sound, "Turn your jumpstat tier sounds on or off.", FCVAR_CLIENT_CAN_EXECUTE)
{
	if (context.GetPlayerSlot().Get() < 0)
	{
		return;
	}
	hns::jumpstats::ToggleSounds(context.GetPlayerSlot());
}
