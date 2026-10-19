#include "hns_commands.h"
#include "jumpstats/jumpstats.h"
#include "hns_config.h"
#include "hns_cvars.h"
#include "hns_player.h"
#include "hns_weapons.h"
#include "hns_gameplay.h"
#include "hns_compat.h"
#include "hns_hud.h"
#include "utils/utils.h"

#include "tier0/memdbgon.h"

// All commands are server console / rcon only, except hns_hud, which players run from their own console.
static_function bool FromServer(const CCommandContext &context)
{
	if (context.GetPlayerSlot().Get() != -1)
	{
		return false;
	}
	return true;
}

CON_COMMAND_F(hns_reload, "Reload cfg/hns-movement/hns-movement.cfg and push the movement cvars to every client.", FCVAR_NONE)
{
	if (!FromServer(context))
	{
		return;
	}
	hns::config::Load();
	hns::cvars::ApplyAll();
	hns::cvars::SendToAllClients();
	hns::gameplay::ApplyServerSettings();
	hns::compat::Check();
	HNS_LOG_INFO(LogChannel::General, "Reloaded. Run hns_write_fallback if you also want the fallback cfg updated.\n");
}

CON_COMMAND_F(hns_status, "Print the enforced movement cvars and the plugin settings.", FCVAR_NONE)
{
	if (!FromServer(context))
	{
		return;
	}
	META_CONPRINTF("---- hns-movement cvars (enforced every tick) ----\n");
	hns::cvars::PrintTable();
	META_CONPRINTF("---- hns-movement settings ----\n");
	hns::config::PrintSettings();
	hns::weapons::PrintDebug();
	hns::compat::PrintStatus();
}

CON_COMMAND_F(hns_write_fallback, "Write cfg/hns-movement/hns_movement_fallback.cfg with the enforced values.", FCVAR_NONE)
{
	if (!FromServer(context))
	{
		return;
	}
	hns::config::WriteFallback();
}

// Read-only stats, also the simplest bridge for ChaseMod or an admin plugin (plan section 6, option b).
CON_COMMAND_F(hns_perf_stats, "Print per-player jump/perf counts since connect.", FCVAR_NONE)
{
	if (!FromServer(context))
	{
		return;
	}
	META_CONPRINTF("%-4s %-24s %8s %8s %7s %10s %10s\n", "slot", "name", "jumps", "perfs", "perf%", "lastspeed", "lastgnd");
	for (i32 i = 0; i < MAXPLAYERS; i++)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(i));
		if (!player || !player->IsConnectedController())
		{
			continue;
		}
		const HNSPlayer::Stats &s = player->stats;
		f32 ratio = s.jumps ? 100.0f * (f32)s.perfs / (f32)s.jumps : 0.0f;
		META_CONPRINTF("%-4d %-24.24s %8llu %8llu %6.1f%% %10.2f %9.4fs\n", i, player->GetName(), s.jumps, s.perfs, ratio, s.lastTakeoffSpeed,
					   s.lastTimeOnGround);
	}
}

void hns::commands::Init()
{
	// The commands above register themselves through META_CONVAR_REGISTER. Nothing else to do yet; the sweep test and the
	// per-tick CSV logger (plan section 7, testing) will be added here.
}

static_function void ToggleHud(CPlayerSlot slot)
{
	HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(slot);
	if (!player || !player->GetController())
	{
		return;
	}
	bool on = hns::hud::Toggle(player);
	utils::PrintConsole(player->GetController(), "Speed HUD %s.", on ? "on" : "off");
	utils::PrintChat(player->GetController(), " Speed HUD %s.", on ? "on" : "off");
}

// Players type this in their own console (or !hud / /hud in chat) to turn their speed/key HUD off or back on.
// Lasts until they disconnect.
CON_COMMAND_F(hns_hud, "Turn your speed and key HUD on or off.", FCVAR_CLIENT_CAN_EXECUTE)
{
	CPlayerSlot slot = context.GetPlayerSlot();
	if (slot.Get() < 0)
	{
		META_CONPRINTF("hns_hud is a player command. To turn the HUD off for everyone, set hns_speed_hud 0 in the cfg.\n");
		return;
	}
	ToggleHud(slot);
}

namespace hns::commands
{
	struct ChatCommand
	{
		const char *name;
		void (*run)(CPlayerSlot slot);
	};

	static const ChatCommand chatCommands[] = {
		{"hud", ToggleHud},
	};

	bool OnSay(const CCommandContext &context, const CCommand &args)
	{
		CPlayerSlot slot = context.GetPlayerSlot();
		if (slot.Get() < 0 || args.ArgC() < 2)
		{
			return false;
		}
		if (V_stricmp(args.Arg(0), "say") != 0 && V_stricmp(args.Arg(0), "say_team") != 0)
		{
			return false;
		}
		const char *text = args.Arg(1);
		while (*text == ' ')
		{
			text++;
		}
		char prefix = text[0];
		if (prefix != '!' && prefix != '/')
		{
			return false;
		}
		// The command word: everything after the prefix up to the first space.
		char word[32];
		i32 len = 0;
		for (const char *c = text + 1; *c && *c != ' ' && len < (i32)sizeof(word) - 1; c++)
		{
			word[len++] = *c;
		}
		word[len] = '\0';
		// Jumpstats commands take an argument (!jstier 3): the text after the word, without leading spaces.
		const char *argument = text + 1 + len;
		while (*argument == ' ')
		{
			argument++;
		}
		if (hns::jumpstats::OnChatCommand(slot, word, argument))
		{
			return prefix == '/';
		}
		for (const ChatCommand &command : chatCommands)
		{
			if (V_stricmp(word, command.name) == 0)
			{
				command.run(slot);
				return prefix == '/';
			}
		}
		return false;
	}
} // namespace hns::commands
