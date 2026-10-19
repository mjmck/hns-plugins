#pragma once

// Server console commands:
//   hns_reload          reload hns-movement.cfg and resend cvars to clients
//   hns_status          print enforced cvars (enforced / live game value / CS2KZ value) and settings
//   hns_write_fallback  write the plain cvar fallback cfg
//   hns_perf_stats      per-player jump and perf counts
// Player commands (own console, or chat with ! or /):
//   hns_hud / !hud      turn your speed and key HUD on or off
//   hns_js / !js        turn your jumpstats on or off
//   hns_js_sound / !jssound  turn your jumpstat sounds on or off
//   !pb [type]  !top [type] [block]  !sr   jumpstat personal bests, top 10 and server records (needs sql_mm)
class CCommandContext;
class CCommand;

namespace hns::commands
{
	void Init();
	// Every say / say_team goes through here before the game handles it. "!cmd" runs the command and the message is
	// still shown; "/cmd" runs it silently. Returns true if the message should be hidden.
	bool OnSay(const CCommandContext &context, const CCommand &args);
} // namespace hns::commands
