// Ported from CS2KZ src/kz/jumpstats/jump_reporting.cpp (AGPL-3.0, KZGlobalTeam). Same chat and console layout as CS2KZ;
// its translated phrases are written out in English here, and preferences come from JSPrefs instead of the option service.
#include "jumpstats.h"
#include "hns/hns_player.h"
#include "hns/hns_config.h"

#include "utils/utils.h"
#include "utils/tables.h"
#include "sdk/recipientfilters.h"
#include "sdk/services.h"

#include <cstdarg>

#include "tier0/memdbgon.h"

#define JS_CHAT_PREFIX "{lime}JS {grey}|{default}"

static_global const char *columnKeys[] = {"#.", "Sync", "Gain", "", "Loss", "", "Max", "Airtime", "BA", "OL", "DA", "Width", "AvgGain", "GainEff", "AngRatio(Avg/Med/Max)"};

// clang-format off
extern const JSFieldDef JS_FIELDS[(i32)JSField::Count] =
{
	{"block",     {"Block"},             true},
	{"strafes",   {"Strafes"},           false},
	{"sync",      {"Sync"},              false},
	{"premax",    {"Pre", "Max"},        false},
	{"edge",      {"Edge"},              false},
	{"height",    {"Height"},            true},
	{"airtime",   {"Airtime"},           true},
	{"width",     {"Width"},             true},
	{"gaineff",   {"GainEff"},           true},
	{"airpath",   {"Airpath"},           true},
	{"badangles", {"BA", "OL", "DA"},    true},
	{"deviation", {"Dev"},               true},
	{"miss",      {"Miss"},              true},
	{"release",   {"W"},                 false},
	{"offset",    {"Offset"},            true},
};
// clang-format on

// ---- Printing (CS2KZ's KZPlayer::PrintChat / PrintConsole, kz_player_print.cpp) ----

static_function bool IsInGame(HNSPlayer *player)
{
	return player && player->IsConnectedController() && !player->IsFakeClient();
}

static_function void PrintToPlayer(HNSPlayer *target, int dest, bool addPrefix, const char *format, ...)
{
	if (!IsInGame(target) && !(target && target->IsCSTV()))
	{
		return;
	}
	va_list args;
	va_start(args, format);
	char buffer[512];
	size_t prefixLen = 0;
	if (addPrefix)
	{
		snprintf(buffer, sizeof(buffer), "%s ", JS_CHAT_PREFIX);
		prefixLen = strlen(buffer);
	}
	vsnprintf(buffer + prefixLen, sizeof(buffer) - prefixLen, format, args);
	va_end(args);

	CRecipientFilter filter;
	filter.AddRecipient(target->GetPlayerSlot());
	if (dest == HUD_PRINTTALK)
	{
		char coloredBuffer[512];
		if (!utils::CFormat(coloredBuffer, sizeof(coloredBuffer), buffer))
		{
			Warning("[hns-movement] jumpstats chat line too long: %s\n", buffer);
			return;
		}
		utils::ClientPrintFilter(&filter, HUD_PRINTTALK, coloredBuffer, "", "", "", "");
	}
	else
	{
		utils::ClientPrintFilter(&filter, HUD_PRINTCONSOLE, buffer, "", "", "", "");
	}
}

#define PrintChat(target, addPrefix, ...)    PrintToPlayer(target, HUD_PRINTTALK, addPrefix, __VA_ARGS__)
#define PrintConsole(target, addPrefix, ...) PrintToPlayer(target, HUD_PRINTCONSOLE, addPrefix, __VA_ARGS__)

// The player whose pawn `spectator` is watching, or nullptr. CS2KZ: KZSpecService::GetSpectatedPlayer.
static_function HNSPlayer *GetSpectatedPlayer(HNSPlayer *spectator)
{
	CCSPlayerController *controller = spectator->GetController();
	if (!controller || spectator->GetPlayerPawn() && spectator->GetPlayerPawn()->IsAlive())
	{
		return nullptr;
	}
	CCSPlayerPawnBase *observerPawn = controller->m_hObserverPawn().Get();
	if (!observerPawn || !observerPawn->m_pObserverServices())
	{
		return nullptr;
	}
	CBaseEntity *target = observerPawn->m_pObserverServices()->m_hObserverTarget().Get();
	if (!target)
	{
		return nullptr;
	}
	return g_pHNSPlayerManager->ToPlayer((CBasePlayerPawn *)target);
}

static_function void AddFieldValue(JSFieldText &out, const char *format, ...)
{
	char buffer[64];
	va_list args;
	va_start(args, format);
	V_vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	out.values[out.count++] = buffer;
}

void Jump::FormatField(JSField field, bool chat, bool greyTier, JSFieldText &out)
{
	out = JSFieldText();
	out.present = true;
	switch (field)
	{
		case JSField::Block:
		{
			// Colored by the block's own tier, the way the distance is by the distance's.
			out.present = this->GetBlock() > 0.0f;
			out.color = JSFieldColor::Tier;
			out.tier = greyTier ? DistanceTier_Meh : hns::jumpstats::GetDistanceTier(this->GetReportJumpType(), this->GetBlock());
			AddFieldValue(out, "%.0f", this->GetBlock());
			break;
		}
		case JSField::Strafes:
		{
			AddFieldValue(out, "%i", this->GetStrafeCount());
			break;
		}
		case JSField::Sync:
		{
			AddFieldValue(out, "%.0f%%", this->GetSync() * 100.0f);
			break;
		}
		case JSField::PreMax:
		{
			AddFieldValue(out, chat ? "%.0f" : "%.1f", this->GetTakeoffSpeed());
			AddFieldValue(out, chat ? "%.0f" : "%.1f", this->GetMaxSpeed());
			break;
		}
		case JSField::Edge:
		{
			out.present = this->GetEdge(false) >= 0.0f;
			AddFieldValue(out, "%.2f", this->GetEdge(false));
			break;
		}
		case JSField::Height:
		{
			AddFieldValue(out, chat ? "%.2f" : "%.1f", this->GetMaxHeight());
			break;
		}
		case JSField::AirTime:
		{
			HNSPlayer *jumper = this->GetJumpPlayer();
			AddFieldValue(out, "%.3fs", jumper->landingTimeActual - jumper->takeoffTime);
			break;
		}
		case JSField::Width:
		{
			AddFieldValue(out, "%.1f°", this->GetWidth());
			break;
		}
		case JSField::GainEfficiency:
		{
			AddFieldValue(out, "%.0f%%", this->GetGainEfficiency() * 100.0f);
			break;
		}
		case JSField::AirPath:
		{
			AddFieldValue(out, "%.2f", this->GetAirPath());
			break;
		}
		case JSField::BadAngles:
		{
			AddFieldValue(out, "%.0f%%", this->GetBadAngles() * 100.0f);
			AddFieldValue(out, "%.0f%%", this->GetOverlap() * 100.0f);
			AddFieldValue(out, "%.0f%%", this->GetDeadAir() * 100.0f);
			break;
		}
		case JSField::Deviation:
		{
			out.present = this->GetBlock() > 0.0f;
			// Which side of the jump direction the landing is on. Nothing is added at zero.
			const f32 deviation = this->GetSignedDeviation();
			const f32 rounded = roundf(fabsf(deviation) * 10.0f) / 10.0f;
			AddFieldValue(out, "%.1f%s", rounded, rounded == 0.0f ? "" : deviation > 0.0f ? " L" : " R");
			break;
		}
		case JSField::Miss:
		{
			out.present = this->GetMiss() > 0.0f;
			AddFieldValue(out, "%.2f", this->GetMiss());
			break;
		}
		case JSField::Release:
		{
			// Same cases and colors as GetReleaseString. Where it does not apply, the chat leaves it out and the HUD greys it.
			const JumpType type = this->GetReportJumpType();
			const f32 release = this->GetReleaseInTick();
			if ((type != JumpType_LongJump && type != JumpType_LadderJump && type != JumpType_WeirdJump) || release < -20)
			{
				out.present = !chat;
				out.color = JSFieldColor::Grey;
				AddFieldValue(out, "-");
			}
			else if (release > 10)
			{
				out.color = JSFieldColor::Red;
				AddFieldValue(out, "✗");
			}
			else if (release > 0)
			{
				out.color = JSFieldColor::Red;
				AddFieldValue(out, "+%.1f", release);
			}
			else if (release == 0)
			{
				out.color = JSFieldColor::Green;
				AddFieldValue(out, "✓");
			}
			else
			{
				out.color = JSFieldColor::Blue;
				AddFieldValue(out, "%.1f", release);
			}
			break;
		}
		case JSField::Offset:
		{
			AddFieldValue(out, "%+.2f", this->GetOffset());
			break;
		}
		default:
			out.present = false;
			break;
	}
}

// Indexed by JSFieldColor. Tier takes the tier's own color instead.
static_global const char *const JS_CHAT_COLORS[] = {"{olive}", "{grey}", "{red}", "{green}", "{blue}", "{grey}"};

// Leaves room for the chat prefix and for the color codes to expand, inside the 512 bytes a chat message gets.
#define JS_CHAT_LINE_MAX 380

std::string Jump::GetInvalidationReasonString(const char *reason)
{
	if (!reason || reason[0] == '\0')
	{
		return "";
	}
	return std::string("(") + reason + ")";
}

void HNSJumpstatsService::PrintJumpToChat(HNSPlayer *target, Jump *jump, bool extended)
{
	if (!IsInGame(target))
	{
		return;
	}
	const JSPrefs &prefs = target->jumpstats->GetPrefs();
	JumpType reportType = jump->GetReportJumpType();
	DistanceTier color = hns::jumpstats::GetDistanceTier(reportType, jump->GetDistance());
	bool jsAlways = prefs.always;
	bool isFailstat = jump->IsFailstat();
	if (isFailstat && !prefs.failstats)
	{
		return;
	}
	// CS2KZ also has a HUD panel with its own minimum tier; hns-movement only reports to chat.
	const bool chatTier = jsAlways || (prefs.minTier != DistanceTier_None && color >= prefs.minTier);
	if (!chatTier)
	{
		return;
	}
	if (isFailstat || jump->GetOffset() <= -JS_EPSILON || !jump->IsValid() || jsAlways)
	{
		color = DistanceTier_Meh;
	}
	const char *jumpColor = distanceTierColors[color];

	std::string jumpTypeShort = jumpTypeShortStr[reportType];
	if (isFailstat)
	{
		jumpTypeShort += "-F";
	}

	// Basic fields go on the first line and extended ones on a second, in the player's order.
	std::vector<std::string> lines[2];
	{
		char header[128];
		// "Jumpstats Report - Chat Header": {jump_color}{jump_type_short}{grey}: {jump_color}{distance}
		V_snprintf(header, sizeof(header), "%s%s{grey}: %s%.1f", jumpColor, jumpTypeShort.c_str(), jumpColor, jump->GetDistance(true, false, 1));
		lines[0].push_back(header);
	}
	for (i32 f = 0; f < (i32)JSField::Count; f++)
	{
		const JSField field = (JSField)f;
		const JSFieldDef &def = JS_FIELDS[f];
		if (def.extended && !extended)
		{
			continue;
		}
		JSFieldText text;
		jump->FormatField(field, true, color <= DistanceTier_Meh, text);
		if (!text.present)
		{
			continue;
		}
		const char *valueColor = JS_CHAT_COLORS[(i32)text.color];
		if (text.color == JSFieldColor::Tier)
		{
			valueColor = distanceTierColors[text.tier];
		}
		for (i32 i = 0; i < text.count; i++)
		{
			const char *label = def.labels[i];
			if (field == JSField::Strafes && jump->GetStrafeCount() <= 1)
			{
				label = "Strafe";
			}
			// "Jumpstats Report - Chat Segment": {color}{value} {grey}{label}
			const std::string segment = std::string(valueColor) + text.values[i] + " {grey}" + label;
			// Chat messages have a fixed size, so a line that would outgrow one carries on in another.
			std::vector<std::string> &group = lines[def.extended ? 1 : 0];
			if (group.empty() || group.back().size() + segment.size() > JS_CHAT_LINE_MAX)
			{
				group.push_back(segment);
			}
			else
			{
				group.back() += " {grey}| " + segment;
			}
		}
	}

	// Only the first line gets the chat prefix, as the others carry on from it.
	bool first = true;
	for (const std::vector<std::string> &group : lines)
	{
		for (const std::string &line : group)
		{
			PrintChat(target, first, "%s", line.c_str());
			first = false;
		}
	}
}

void HNSJumpstatsService::PrintJumpToConsole(HNSPlayer *target, Jump *jump, bool broadcast)
{
	if (!IsInGame(target) && !(target && target->IsCSTV()))
	{
		return;
	}
	const JSPrefs &prefs = target->jumpstats->GetPrefs();
	if (jump->IsFailstat() && !prefs.failstats)
	{
		return;
	}
	if (broadcast && (jump->IsFailstat() || jump->GetOffset() <= -JS_EPSILON))
	{
		return;
	}

	JumpType reportType = jump->GetReportJumpType();
	DistanceTier color = hns::jumpstats::GetDistanceTier(reportType, jump->GetDistance());
	DistanceTier minTier = broadcast ? prefs.broadcastMinTier : prefs.minTierConsole;
	// We only print if the jump meets one of these requirements:
	// - The jump tier is equal or higher than the minimum tier set in preferences
	// - The "jsAlways" option is enabled and this is not a broadcasted jump
	// - The target is a CSTV (HLTV) client
	bool shouldPrint = minTier != DistanceTier_None && color >= minTier;
	shouldPrint |= !broadcast && prefs.always;
	shouldPrint |= target->IsCSTV();
	if (!shouldPrint)
	{
		return;
	}
	std::string jumpType = jumpTypeStr[reportType];
	// "Jumpstats Report - Console Summary" / "... Failstat Summary"
	PrintConsole(target, false, "%s %s %.4f units with a %s %s", jump->GetJumpPlayer()->GetName(), jump->IsFailstat() ? "failed" : "jumped",
				 jump->GetDistance(), jumpType.c_str(), jump->GetInvalidationReasonString(jump->invalidateReason).c_str());
	std::string modeStyleNames = "HNS";
	std::string releaseString = "";
	if (reportType == JumpType_LongJump || reportType == JumpType_LadderJump || reportType == JumpType_WeirdJump)
	{
		releaseString = jump->GetReleaseString(false);
	}
	// Build inline stats for console
	char segment[64];
	std::string edgeString = "";
	std::string landingEdgeString = "";
	std::string blockString = "";
	std::string missString = "";
	if (jump->GetEdge(false) >= 0.0f)
	{
		V_snprintf(segment, sizeof(segment), " | %.2f Edge", jump->GetEdge(false));
		edgeString = segment;
	}
	if (jump->GetEdge(true) > 0.0f)
	{
		V_snprintf(segment, sizeof(segment), " | %.2f LandEdge", jump->GetEdge(true));
		landingEdgeString = segment;
	}
	if (jump->GetBlock() > 0.0f)
	{
		V_snprintf(segment, sizeof(segment), " | %.0f Block", jump->GetBlock());
		blockString = segment;
	}
	if (jump->GetMiss() > 0.0f)
	{
		V_snprintf(segment, sizeof(segment), " | %.2f Miss", jump->GetMiss());
		missString = segment;
	}

	// clang-format off
	// "Jumpstat Report - Console Details 1"
	PrintConsole(target, false, "%s%s%s%s%s | %i %s | %.1f%% Sync | %.2f Pre | %.2f Max | %.0f%% BA | %.0f%% OL | %.0f%% DA | %.2f Height%s",
		modeStyleNames.c_str(),
		blockString.c_str(),
		edgeString.c_str(),
		landingEdgeString.c_str(),
		missString.c_str(),
		jump->GetStrafeCount(),
		jump->GetStrafeCount() > 1 ? "Strafes" : "Strafe",
		jump->GetSync() * 100.0f,
		jump->GetTakeoffSpeed(),
		jump->GetMaxSpeed(),
		jump->GetBadAngles() * 100.0f,
		jump->GetOverlap() * 100.0f,
		jump->GetDeadAir() * 100.0f,
		jump->GetMaxHeight(),
		releaseString.c_str()
	);

	// "Jumpstat Report - Console Details 2"
	PrintConsole(target, false, "%.0f%% GainEff | %.3f Airpath | %.1f Deviation | %.1f Width | %.4f Airtime | %.1f Offset | %.2f/%.2f Crouched",
		jump->GetGainEfficiency() * 100.0f,
		jump->GetAirPath(),
		jump->GetDeviation(),
		jump->GetWidth(),
		jump->GetJumpPlayer()->landingTimeActual - jump->GetJumpPlayer()->takeoffTime,
		jump->GetOffset(),
		jump->GetDuckTime(true),
		jump->GetDuckTime(false)
	);

	CUtlString headers[KZ_ARRAYSIZE(columnKeys)];
	for (u32 i = 0; i < KZ_ARRAYSIZE(columnKeys); i++)
	{
		headers[i] = columnKeys[i];
	}
	utils::Table<KZ_ARRAYSIZE(columnKeys)> table("", headers);
	
	for (size_t i = 0; i < jump->strafes.size(); i++)
	{
		char strafeNumberString[5];
		char syncString[16], gainString[16], lossString[16], externalGainString[16], externalLossString[16], maxString[16], durationString[16];
		char badAngleString[16], overlapString[16], deadAirString[16], widthString[16], avgGainString[16], gainEffString[16];
		char angRatioString[32];
		V_snprintf(strafeNumberString, sizeof(strafeNumberString), "%zu.", i + 1);
		V_snprintf(syncString, sizeof(syncString), "%.0f%%%%", jump->strafes[i].GetSync() * 100.0f);
		V_snprintf(gainString, sizeof(gainString), "%.2f", jump->strafes[i].GetGain());
		V_snprintf(externalGainString, sizeof(externalGainString), "(+%.2f)", fabs(jump->strafes[i].GetGain(true)));
		V_snprintf(lossString, sizeof(lossString), "-%.2f", fabs(jump->strafes[i].GetLoss()));
		V_snprintf(externalLossString, sizeof(externalLossString), "(-%.2f)", fabs(jump->strafes[i].GetLoss(true)));
		V_snprintf(maxString, sizeof(maxString), "%.2f", jump->strafes[i].GetStrafeMaxSpeed());
		V_snprintf(durationString, sizeof(durationString), "%.3f", jump->strafes[i].GetStrafeDuration());
		V_snprintf(badAngleString, sizeof(badAngleString), "%.1f", jump->strafes[i].GetBadAngleDuration() * ENGINE_FIXED_TICK_RATE);
		V_snprintf(overlapString, sizeof(overlapString), "%.1f", jump->strafes[i].GetOverlapDuration() * ENGINE_FIXED_TICK_RATE);
		V_snprintf(deadAirString, sizeof(deadAirString), "%.1f", jump->strafes[i].GetDeadAirDuration() * ENGINE_FIXED_TICK_RATE);
		V_snprintf(widthString, sizeof(widthString), "%.1f", fabsf(jump->strafes[i].GetWidth()));
		V_snprintf(avgGainString, sizeof(avgGainString), "%.2f", jump->strafes[i].GetGain() / jump->strafes[i].GetStrafeDuration() * ENGINE_FIXED_TICK_INTERVAL);
		V_snprintf(gainEffString, sizeof(gainEffString), "%.0f%%%%", jump->strafes[i].GetGain() / jump->strafes[i].GetMaxGain() * 100.0f);

		if (jump->strafes[i].arStats.available)
		{
			V_snprintf(angRatioString, sizeof(angRatioString),
				"%.2f/%.2f/%.2f",
				jump->strafes[i].arStats.average,
				jump->strafes[i].arStats.median,
				jump->strafes[i].arStats.max
			);
		}
		else
		{
			V_snprintf(angRatioString, sizeof(angRatioString), "N/A");
		}

		table.SetRow(i,
			strafeNumberString,
			syncString,
			gainString,
			externalGainString,
			lossString,
			externalLossString,
			maxString,
			durationString,
			badAngleString,
			overlapString,
			deadAirString,
			widthString,
			avgGainString,
			gainEffString,
			angRatioString
		);
	}
	// clang-format on

	if (!jump->strafes.empty())
	{
		PrintConsole(target, false, "%s", table.GetHeader().Get());
		for (u32 i = 0; i < table.GetNumEntries(); i++)
		{
			PrintConsole(target, false, "%s", table.GetLine(i).Get());
		}
	}

	if (!broadcast)
	{
		std::string strafeLeft, strafeRight, mouseLeft, mouseRight;
		if (jump->BuildConsoleStrafeMouseGraph(strafeLeft, strafeRight, mouseLeft, mouseRight))
		{
			PrintConsole(target, false, "Strafe keys:");
			PrintConsole(target, false, "L: %s", strafeLeft.c_str());
			PrintConsole(target, false, "R: %s", strafeRight.c_str());
			PrintConsole(target, false, "Mouse movement:");
			PrintConsole(target, false, "L: %s", mouseLeft.c_str());
			PrintConsole(target, false, "R: %s", mouseRight.c_str());
		}
	}
}

void HNSJumpstatsService::BroadcastJumpToChat(HNSPlayer *target, Jump *jump)
{
	if (!IsInGame(target))
	{
		return;
	}
	if (!jump->IsValid())
	{
		return;
	}
	DistanceTier tier = hns::jumpstats::GetDistanceTier(jump->GetReportJumpType(), jump->GetDistance());
	if (tier == DistanceTier_None)
	{
		return;
	}
	const char *jumpColor = distanceTierColors[tier];

	DistanceTier broadcastTier = target->jumpstats->GetPrefs().broadcastMinTier;
	bool broadcastEnabled = broadcastTier != DistanceTier_None;
	bool validBroadcastTier = tier >= broadcastTier;
	if (broadcastEnabled && validBroadcastTier)
	{
		// clang-format off
		// "Broadcast Jumpstat Chat Report", without the mode tag
		PrintChat(target, true, "{default}%s {grey}jumped %s%.2f {grey}units with a {lime}%s",
			jump->GetJumpPlayer()->GetName(),
			jumpColor,
			jump->GetDistance(),
			jumpTypeStr[jump->GetReportJumpType()]
		);
		// clang-format on
	}
}

void HNSJumpstatsService::AnnounceJump(Jump *jump)
{
	if (jump->GetJumpType() == JumpType_FullInvalid || !hns::config::settings.jsEnabled)
	{
		return;
	}
	bool isFailstat = jump->IsFailstat();
	for (i32 i = 1; i <= MAXPLAYERS; i++)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer((u32)i);
		if (!player || !(IsInGame(player) || player->IsCSTV()))
		{
			continue;
		}
		const JSPrefs &prefs = player->jumpstats->GetPrefs();
		// If the player is the one who did the jump or is spectating the jumper, we show more details in chat.
		if (player == jump->GetJumpPlayer() || GetSpectatedPlayer(player) == jump->GetJumpPlayer())
		{
			if ((jump->GetOffset() <= -JS_EPSILON || !jump->IsValid()) && !isFailstat && !prefs.always)
			{
				continue;
			}
			if (!prefs.reporting)
			{
				continue;
			}
			if (!isFailstat || prefs.failstats)
			{
				HNSJumpstatsService::PrintJumpToChat(player, jump, prefs.extended);
			}
			HNSJumpstatsService::PrintJumpToConsole(player, jump);
		}
		// If the player is a CSTV bot, we only display console output.
		else if (player->IsCSTV())
		{
			// CSTV bots see all jumps.
			HNSJumpstatsService::PrintJumpToConsole(player, jump);
		}
		// If the player has broadcasting enabled, we show a brief summary in chat and play a sound.
		else
		{
			if (!jump->IsValid() || jump->IsFailstat() || jump->GetOffset() <= -JS_EPSILON)
			{
				continue;
			}
			if (!prefs.reporting)
			{
				continue;
			}
			HNSJumpstatsService::BroadcastJumpToChat(player, jump);
			HNSJumpstatsService::PrintJumpToConsole(player, jump, true);
		}
	}
}
