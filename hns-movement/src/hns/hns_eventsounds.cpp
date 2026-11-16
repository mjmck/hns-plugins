#include "hns_eventsounds.h"
#include "hns_config.h"
#include "hns_joinsound.h"
#include "hns_player.h"
#include "utils/utils.h"
#include "utils/ctimer.h"
#include "sdk/gamerules.h"
#include "sdk/resourcemanifest.h"
#include "igameevents.h"

#include <algorithm>
#include <string>
#include <vector>

#include "tier0/memdbgon.h"

using hns::config::settings;

namespace hns::eventsounds
{
	struct Sounds
	{
		std::string countdown[4]; // [1], [2], [3] used.
		std::string lastMan;
		std::string doubleKill;
		f32 doubleKillWindow = 4.0f;
		// Delay before last_man when the same death also fired double_kill, so the two do not talk over each other.
		f32 lastManAfterDoubleKill = 1.5f;
		f32 volume = 1.0f;
	};

	static_persist Sounds sounds;
	static_persist std::vector<std::string> precacheFiles;

	// Per-round state.
	static_global i32 roundSerial {};
	static_global bool roundLive {};
	static_global bool lastManPlayed {};
	// Per-attacker kill streak inside the double kill window.
	static_global f32 lastKillTime[MAXPLAYERS + 1] {};
	static_global i32 streakKills[MAXPLAYERS + 1] {};

	static_persist const char defaultFile[] =
		"// hns-movement event sounds. Edit, then run hns_reload (server console or rcon).\n"
		"// Lives at csgo/cfg/hns-movement/event-sounds.cfg. All of them play to everyone on the server.\n"
		"// hns_event_sounds 0 in hns-movement.cfg turns them all off; \"\" turns one off.\n"
		"//\n"
		"// Sound event names come from the HNS workshop addon (soundevents/soundevents_hns.vsndevts). The addon's soundevents\n"
		"// file must be precached: join-sounds.cfg already does that, so no precache line is needed here.\n"
		"// Any stock CS2 sound event works too. Test from the server console: hns_event_sound_test <event | sound event>\n"
		"\n"
		"// Seeker release countdown: plays as the CT start freeze reaches 3, 2 and 1 seconds left.\n"
		"countdown_3   hns.countdown.3\n"
		"countdown_2   hns.countdown.2\n"
		"countdown_1   hns.countdown.1\n"
		"\n"
		"// The death that leaves exactly one T alive (once per round).\n"
		"last_man      hns.last_man_standing\n"
		"\n"
		"// Two kills by the same player within double_kill_window seconds.\n"
		"double_kill          hns.double_kill\n"
		"double_kill_window   4\n"
		"\n"
		"// Volume for all event sounds, 0-1.\n"
		"volume  1.0\n";

	static_function bool IsWarmup()
	{
		CCSGameRules *rules = g_pKZUtils->GetGameRules();
		return rules && rules->m_bWarmupPeriod();
	}

	static_function f32 Now()
	{
		return g_pKZUtils->GetServerGlobals()->curtime;
	}

	static_function void Play(const std::string &soundEvent)
	{
		if (!settings.eventSounds || soundEvent.empty())
		{
			return;
		}
		hns::joinsound::PlayToAll(soundEvent.c_str(), sounds.volume);
	}

	static_function void ResetRound()
	{
		roundSerial++;
		lastManPlayed = false;
		for (i32 i = 0; i <= MAXPLAYERS; i++)
		{
			lastKillTime[i] = 0.0f;
			streakKills[i] = 0;
		}
	}

	bool Load()
	{
		sounds = Sounds();
		precacheFiles.clear();

		std::vector<char> buffer;
		if (!utils::ReadBufferFromFile(HNS_EVENTSOUNDS_PATH, buffer))
		{
			std::vector<char> example(defaultFile, defaultFile + sizeof(defaultFile) - 1);
			if (utils::WriteBufferToFile(HNS_EVENTSOUNDS_PATH, example))
			{
				HNS_LOG_INFO(LogChannel::General, "Created default %s.\n", HNS_EVENTSOUNDS_PATH);
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
			const char *key = tokens[0].c_str();
			// A key with no value (or "") turns that sound off.
			std::string value = tokens.size() >= 2 ? tokens[1] : std::string();

			if (KZ_STREQI(key, "precache"))
			{
				if (!value.empty())
				{
					precacheFiles.push_back(value);
				}
			}
			else if (KZ_STREQI(key, "countdown_3"))
			{
				sounds.countdown[3] = value;
			}
			else if (KZ_STREQI(key, "countdown_2"))
			{
				sounds.countdown[2] = value;
			}
			else if (KZ_STREQI(key, "countdown_1"))
			{
				sounds.countdown[1] = value;
			}
			else if (KZ_STREQI(key, "last_man"))
			{
				sounds.lastMan = value;
			}
			else if (KZ_STREQI(key, "double_kill"))
			{
				sounds.doubleKill = value;
			}
			else if (KZ_STREQI(key, "double_kill_window"))
			{
				sounds.doubleKillWindow = (std::max)((f32)atof(value.c_str()), 0.0f);
			}
			else if (KZ_STREQI(key, "last_man_after_double_kill"))
			{
				sounds.lastManAfterDoubleKill = (std::max)((f32)atof(value.c_str()), 0.0f);
			}
			else if (KZ_STREQI(key, "volume"))
			{
				sounds.volume = std::clamp((f32)atof(value.c_str()), 0.0f, 1.0f);
			}
			else
			{
				HNS_LOG_WARN(LogChannel::General, "%s:%u: unknown key '%s', ignored.\n", HNS_EVENTSOUNDS_PATH, lineNumber, key);
			}
		}
		HNS_LOG_INFO(LogChannel::General, "Loaded %s.\n", HNS_EVENTSOUNDS_PATH);
		return true;
	}

	void OnFreezeCountdown(i32 secondsLeft)
	{
		if (secondsLeft >= 1 && secondsLeft <= 3)
		{
			Play(sounds.countdown[secondsLeft]);
		}
	}

	static_function i32 SlotOf(CEntityInstance *controller)
	{
		if (!controller || !controller->m_pEntity)
		{
			return -1;
		}
		i32 slot = controller->m_pEntity->m_EHandle.GetEntryIndex() - 1;
		return (slot >= 0 && slot <= MAXPLAYERS) ? slot : -1;
	}

	static_function i32 TeamOf(i32 slot)
	{
		HNSPlayer *player = slot >= 0 ? g_pHNSPlayerManager->ToPlayer(CPlayerSlot(slot)) : nullptr;
		CCSPlayerController *controller = player ? player->GetController() : nullptr;
		return controller ? (i32)controller->m_iTeamNum() : CS_TEAM_NONE;
	}

	// Ts alive, not counting the player who just died (their pawn may not be flagged dead yet when the event fires).
	static_function i32 CountAliveTs(i32 excludeSlot)
	{
		i32 count = 0;
		for (i32 i = 0; i < MAXPLAYERS; i++)
		{
			if (i == excludeSlot)
			{
				continue;
			}
			HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(i));
			if (!player || !player->IsConnectedController())
			{
				continue;
			}
			CCSPlayerController *controller = player->GetController();
			if (controller && controller->m_iTeamNum() == CS_TEAM_T && player->IsAlive())
			{
				count++;
			}
		}
		return count;
	}

	static_function f64 PlayLastManTimer(i32 serial)
	{
		// Round already restarted during the delay.
		if (serial == roundSerial && roundLive)
		{
			Play(sounds.lastMan);
		}
		return 0.0;
	}

	static_function void OnPlayerDeath(IGameEvent *event)
	{
		if (!roundLive || IsWarmup())
		{
			return;
		}
		i32 victim = SlotOf(event->GetPlayerController("userid"));
		i32 attacker = SlotOf(event->GetPlayerController("attacker"));
		i32 victimTeam = TeamOf(victim);

		bool doubleKill = false;
		if (attacker >= 0 && attacker != victim && victim >= 0 && TeamOf(attacker) != victimTeam && !sounds.doubleKill.empty())
		{
			f32 now = Now();
			if (streakKills[attacker] > 0 && now - lastKillTime[attacker] <= sounds.doubleKillWindow)
			{
				streakKills[attacker]++;
			}
			else
			{
				streakKills[attacker] = 1;
			}
			lastKillTime[attacker] = now;
			// Only the second kill plays: a third inside the window stays quiet.
			if (streakKills[attacker] == 2)
			{
				doubleKill = true;
				Play(sounds.doubleKill);
			}
		}

		if (victimTeam == CS_TEAM_T && !lastManPlayed && !sounds.lastMan.empty() && CountAliveTs(victim) == 1)
		{
			lastManPlayed = true;
			f32 delay = doubleKill ? sounds.lastManAfterDoubleKill : 0.0f;
			if (delay > 0.0f)
			{
				StartTimer<i32>(PlayLastManTimer, roundSerial, delay, false, false);
			}
			else
			{
				Play(sounds.lastMan);
			}
		}
	}

	void OnFireEvent(IGameEvent *event)
	{
		const char *name = event->GetName();
		if (KZ_STREQI(name, "player_death"))
		{
			OnPlayerDeath(event);
		}
		else if (KZ_STREQI(name, "round_prestart"))
		{
			roundLive = false;
			ResetRound();
		}
		else if (KZ_STREQI(name, "round_freeze_end") || KZ_STREQI(name, "round_start"))
		{
			roundLive = true;
		}
		else if (KZ_STREQI(name, "round_end"))
		{
			roundLive = false;
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

	static_function const char *Show(const std::string &s)
	{
		return s.empty() ? "(off)" : s.c_str();
	}

	void PrintStatus()
	{
		META_CONPRINTF("---- hns-movement event sounds (%s) ----\n", settings.eventSounds ? "on" : "off");
		META_CONPRINTF("countdown  3 %s, 2 %s, 1 %s\n", Show(sounds.countdown[3]), Show(sounds.countdown[2]), Show(sounds.countdown[1]));
		META_CONPRINTF("last_man   %s\n", Show(sounds.lastMan));
		META_CONPRINTF("double_kill %s (window %gs)\n", Show(sounds.doubleKill), sounds.doubleKillWindow);
		META_CONPRINTF("volume     %.2f\n", sounds.volume);
	}

	const std::string *Find(const char *event)
	{
		if (KZ_STREQI(event, "countdown_3"))
		{
			return &sounds.countdown[3];
		}
		if (KZ_STREQI(event, "countdown_2"))
		{
			return &sounds.countdown[2];
		}
		if (KZ_STREQI(event, "countdown_1"))
		{
			return &sounds.countdown[1];
		}
		if (KZ_STREQI(event, "last_man"))
		{
			return &sounds.lastMan;
		}
		if (KZ_STREQI(event, "double_kill"))
		{
			return &sounds.doubleKill;
		}
		return nullptr;
	}
} // namespace hns::eventsounds

CON_COMMAND_F(hns_event_sound_test, "Play an event sound to everyone: hns_event_sound_test <countdown_3|countdown_2|countdown_1|last_man|double_kill | sound event>",
			  FCVAR_NONE)
{
	if (context.GetPlayerSlot().Get() != -1)
	{
		return;
	}
	if (args.ArgC() < 2)
	{
		hns::eventsounds::PrintStatus();
		META_CONPRINTF("Usage: hns_event_sound_test <countdown_3|countdown_2|countdown_1|last_man|double_kill | sound event>\n");
		return;
	}
	const std::string *configured = hns::eventsounds::Find(args.Arg(1));
	const char *sound = configured ? configured->c_str() : args.Arg(1);
	if (!*sound)
	{
		META_CONPRINTF("'%s' is off in %s.\n", args.Arg(1), HNS_EVENTSOUNDS_PATH);
		return;
	}
	hns::joinsound::PlayToAll(sound, 1.0f);
	META_CONPRINTF("Played '%s'.\n", sound);
}
