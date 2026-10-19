#pragma once
// Jumpstat personal bests and server records in a local SQLite database, through sql_mm (the same database plugin and
// approach as CS2KZ's local database). sql_mm runs every query on its own thread and calls back on the game thread.
//
// File: csgo/addons/hns-movement/data/jumpstats.sqlite3 (created on first start).
// Tables: Players (SteamID64, Name, LastSeen) and Jumpstats (one row per player, jump type and block / non-block: the
// personal best, updated in place, same idea as CS2KZ's Jumpstats table).
//
// Personal bests are cached when a player connects and server records when the database connects, so checking a jump
// never waits on the database. Only valid jumps that reach a distance tier are considered.
#include "common.h"
#include "js_jumpstats.h"

class HNSPlayer;

namespace hns::jumpstats::db
{
	// Connect if hns_js_db is on and sql_mm is loaded. Safe to call again (AllPluginsLoaded, sql_mm loading late).
	void Init();
	void Cleanup();
	// sql_mm was unloaded: forget the connection without touching it.
	void OnPluginUnloaded();
	bool IsReady();

	void OnPlayerAuthorized(HNSPlayer *player);
	void OnPlayerReset(HNSPlayer *player);

	// A reported jump (valid, tiered). Saves a new personal best and announces a new server record.
	void OnJump(Jump *jump, DistanceTier tier);

	// Chat commands. args is the text after the command word.
	void PrintPersonalBests(CPlayerSlot slot, const char *args);
	void PrintTop(CPlayerSlot slot, const char *args);
	void PrintServerRecords(CPlayerSlot slot, const char *args);

	void PrintStatus();
} // namespace hns::jumpstats::db
