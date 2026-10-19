// hns-movement glue for the jumpstats port: distance tiers, per-player preferences and chat commands.
// Replaces CS2KZ's mode-service tiers (kz_mode_ckz.cpp), option service and prefs.cpp / jumpstats_prefs.cpp.
#include "jumpstats.h"
#include "hns/hns_player.h"
#include "hns/hns_config.h"
#include "hns/hns_cvars.h"
#include "utils/utils.h"

#include "tier0/memdbgon.h"

#define JS_PREFIX "{lime}JS {grey}|{default} "

namespace hns::jumpstats
{
	DistanceTier GetDistanceTier(JumpType jumpType, f32 distance)
	{
		// Same rule as CS2KZ's KZClassicModeService::GetDistanceTier: no tiers for invalid, falls and unknown jumps.
		if (jumpType == JumpType_Invalid || jumpType == JumpType_FullInvalid || jumpType == JumpType_Fall || jumpType == JumpType_Other
			|| distance > 500.0f)
		{
			return DistanceTier_None;
		}
		if (jumpType < 0 || jumpType >= JS_TIERED_JUMPTYPE_COUNT)
		{
			return DistanceTier_None;
		}
		const f32 *tiers = hns::config::settings.jsTiers[jumpType];
		// Get highest tier distance that the jump beats
		DistanceTier tier = DistanceTier_None;
		while (tier + 1 < DISTANCETIER_COUNT && distance >= tiers[tier])
		{
			tier = (DistanceTier)(tier + 1);
		}
		return tier;
	}

	f32 GetAirMaxWishspeed()
	{
		return hns::cvars::GetFloat(HNSCVAR_SV_AIR_MAX_WISHSPEED);
	}

	static_function DistanceTier ClampTier(i32 value)
	{
		if (value < DistanceTier_None)
		{
			return DistanceTier_None;
		}
		if (value >= DISTANCETIER_COUNT)
		{
			return (DistanceTier)(DISTANCETIER_COUNT - 1);
		}
		return (DistanceTier)value;
	}
} // namespace hns::jumpstats

JSPrefs &HNSJumpstatsService::GetPrefs()
{
	if (!this->prefs.initialized)
	{
		const auto &settings = hns::config::settings;
		this->prefs.reporting = true;
		this->prefs.extended = settings.jsExtendedChat;
		this->prefs.always = false;
		this->prefs.failstats = settings.jsFailstats;
		this->prefs.minTier = hns::jumpstats::ClampTier(settings.jsMinTier);
		this->prefs.minTierConsole = hns::jumpstats::ClampTier(settings.jsMinTierConsole);
		this->prefs.broadcastMinTier = hns::jumpstats::ClampTier(settings.jsBroadcastMinTier);
		this->prefs.initialized = true;
	}
	return this->prefs;
}

// CS2KZ prefs.cpp, without the chat hint (the caller prints it).
bool HNSJumpstatsService::GetDistTierFromString(const char *tierString, DistanceTier &outTier)
{
	if (!tierString || tierString[0] == '\0')
	{
		return false;
	}
	if (utils::IsNumeric(tierString))
	{
		i32 tierValue = V_StringToInt32(tierString, -1);
		if (tierValue < DistanceTier_None || tierValue >= DISTANCETIER_COUNT)
		{
			return false;
		}
		outTier = (DistanceTier)tierValue;
		return true;
	}
	for (i32 i = 0; i < DISTANCETIER_COUNT; i++)
	{
		if (KZ_STREQI(distanceTierNames[i], tierString))
		{
			outTier = (DistanceTier)i;
			return true;
		}
	}
	return false;
}

namespace hns::jumpstats
{
	static_function void Reply(CPlayerSlot slot, const char *format, ...)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(slot);
		if (!player || !player->GetController())
		{
			return;
		}
		char buffer[400];
		va_list args;
		va_start(args, format);
		V_vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		utils::CPrintChat(player->GetController(), " " JS_PREFIX "%s", buffer);
	}

	static_function void TierHint(CPlayerSlot slot)
	{
		Reply(slot, "Tiers: {grey}0 None, 1 Meh, {blue}2 Impressive{grey}, {green}3 Perfect{grey}, {darkred}4 Godlike{grey}, {gold}5 Ownage{grey}, "
					"{orchid}6 Wrecker{grey}. Number or name.");
	}

	static_function void ToggleMessage(CPlayerSlot slot, const char *what, bool on)
	{
		Reply(slot, "%s %s{default}.", what, on ? "{green}on" : "{red}off");
	}

	// Sets *tier from the argument and confirms; prints the hint when the argument is missing or not a tier.
	static_function void SetTier(CPlayerSlot slot, const char *argument, DistanceTier *tier, const char *what, const char *command)
	{
		DistanceTier value;
		if (!HNSJumpstatsService::GetDistTierFromString(argument, value))
		{
			Reply(slot, "%s is %s%s{default}. Usage: {lime}!%s <tier>", what, distanceTierColors[*tier], distanceTierNames[*tier], command);
			TierHint(slot);
			return;
		}
		*tier = value;
		if (value == DistanceTier_None)
		{
			Reply(slot, "%s: {red}off{default}.", what);
		}
		else
		{
			Reply(slot, "%s: %s%s{default} and above.", what, distanceTierColors[value], distanceTierNames[value]);
		}
	}

	void PrintHelp(CPlayerSlot slot)
	{
		Reply(slot, "{lime}!js{default} on/off, {lime}!jstier{default} <tier>, {lime}!jsbroadcast{default} <tier>, {lime}!jsextend{default}, "
					"{lime}!jsalways{default}, {lime}!jsfailstats{default}, {lime}!jstierconsole{default} <tier>");
	}

	bool OnChatCommand(CPlayerSlot slot, const char *word, const char *argument)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(slot);
		if (!player || !player->jumpstats)
		{
			return false;
		}
		JSPrefs &prefs = player->jumpstats->GetPrefs();
		if (KZ_STREQI(word, "js") || KZ_STREQI(word, "jumpstats"))
		{
			prefs.reporting = !prefs.reporting;
			ToggleMessage(slot, "Jumpstats", prefs.reporting);
			if (!hns::config::settings.jsEnabled)
			{
				Reply(slot, "{grey}Jumpstats are turned off on this server (hns_js_enabled 0).");
			}
			return true;
		}
		if (KZ_STREQI(word, "jstier"))
		{
			SetTier(slot, argument, &prefs.minTier, "Chat minimum tier", "jstier");
			return true;
		}
		if (KZ_STREQI(word, "jstierconsole"))
		{
			SetTier(slot, argument, &prefs.minTierConsole, "Console minimum tier", "jstierconsole");
			return true;
		}
		if (KZ_STREQI(word, "jsbroadcast"))
		{
			SetTier(slot, argument, &prefs.broadcastMinTier, "Broadcast minimum tier", "jsbroadcast");
			return true;
		}
		if (KZ_STREQI(word, "jsextend"))
		{
			prefs.extended = !prefs.extended;
			ToggleMessage(slot, "Extended chat stats", prefs.extended);
			return true;
		}
		if (KZ_STREQI(word, "jsalways"))
		{
			prefs.always = !prefs.always;
			ToggleMessage(slot, "Report every jump (jsalways)", prefs.always);
			return true;
		}
		if (KZ_STREQI(word, "jsfailstats"))
		{
			prefs.failstats = !prefs.failstats;
			ToggleMessage(slot, "Failstats", prefs.failstats);
			return true;
		}
		if (KZ_STREQI(word, "jshelp"))
		{
			PrintHelp(slot);
			return true;
		}
		return false;
	}
} // namespace hns::jumpstats
