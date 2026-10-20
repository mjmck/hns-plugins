#pragma once
#include "common.h"

class HNSPlayer;
class PlayerCommand;

// Speed and key HUD (hns_speed_hud), shown in the centre of the screen:
//   Speed: 312 u/s (289)        current horizontal speed, takeoff speed of the last jump in brackets
//   W A S D C J                 keys held; unheld keys show as _
// Jump lights up for any jump input, including a mouse wheel jump, which presses and releases jump inside a single
// tick and so never shows up as "held". A live player always sees their own HUD; dead players and spectators see the
// HUD of the player they are watching in first or third person (same rule as the CS2KZ legacy HUD).
namespace hns::hud
{
	// Reads the player's inputs for this tick (called before the game processes them; nothing is changed).
	void OnProcessUsercmds(HNSPlayer *player, PlayerCommand *cmds, int numcmds);
	// The player jumped and left the ground; the takeoff speed is final at this point.
	void OnTakeoff(HNSPlayer *player);
	void OnGameFrame();
	void OnPlayerReset(HNSPlayer *player);
	// Per-player switch (console command hns_hud). Returns the new state.
	bool Toggle(HNSPlayer *player);
} // namespace hns::hud
