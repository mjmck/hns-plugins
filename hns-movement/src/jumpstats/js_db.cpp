// Jumpstat records in SQLite through sql_mm. See js_db.h.
#include "js_db.h"
#include "hns/hns_player.h"
#include "hns/hns_config.h"
#include "utils/utils.h"

#include <ctime>
#include <string>
#include <vector>

#include "vendor/sql_mm/sql_mm.h"
#include "vendor/sql_mm/sqlite_mm.h"

#include "tier0/memdbgon.h"

#define JS_DB_PATH   "addons/hns-movement/data/jumpstats.sqlite3"
#define JS_DB_PREFIX " {lime}JS {grey}|"
#define JS_TOP_COUNT 10

namespace hns::jumpstats::db
{
	// clang-format off
	static_persist const char *createPlayers =
		"CREATE TABLE IF NOT EXISTS Players ("
		" SteamID64 INTEGER NOT NULL PRIMARY KEY,"
		" Name TEXT NOT NULL,"
		" LastSeen INTEGER NOT NULL)";
	// One row per player, jump type (0 LJ .. 6 JB) and IsBlockJump: their personal best.
	static_persist const char *createJumpstats =
		"CREATE TABLE IF NOT EXISTS Jumpstats ("
		" SteamID64 INTEGER NOT NULL,"
		" JumpType INTEGER NOT NULL,"
		" IsBlockJump INTEGER NOT NULL,"
		" Distance REAL NOT NULL,"
		" Block INTEGER NOT NULL,"
		" Strafes INTEGER NOT NULL,"
		" Sync REAL NOT NULL,"
		" Pre REAL NOT NULL,"
		" Max REAL NOT NULL,"
		" Airtime REAL NOT NULL,"
		" Created INTEGER NOT NULL,"
		" PRIMARY KEY (SteamID64, JumpType, IsBlockJump))";
	static_persist const char *createIndex =
		"CREATE INDEX IF NOT EXISTS IX_Jumpstats_Top ON Jumpstats (JumpType, IsBlockJump, Block DESC, Distance DESC)";
	// clang-format on

	struct Record
	{
		bool set {};
		f32 distance {};
		i32 block {};
		i32 strafes {};
		f32 sync {};
		f32 pre {};
		f32 max {};
		f32 airtime {};
		u64 steamID {};
		std::string name;
	};

	struct PlayerCache
	{
		u64 steamID {};
		bool loaded {};
		Record pb[JS_TIERED_JUMPTYPES][2];
	};

	static_persist ISQLConnection *connection;
	static_persist bool ready;
	static_persist PlayerCache players[MAXPLAYERS + 1];
	static_persist Record serverRecords[JS_TIERED_JUMPTYPES][2];

	// Block jumps rank by block, then distance (as CS2KZ). Everything else by distance.
	static_function bool Beats(const Record &candidate, const Record &current, bool isBlock)
	{
		if (!current.set)
		{
			return true;
		}
		if (isBlock && candidate.block != current.block)
		{
			return candidate.block > current.block;
		}
		return candidate.distance > current.distance;
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

	static_function std::vector<std::string> SplitArgs(const char *args)
	{
		std::vector<std::string> out;
		std::string current;
		for (const char *c = args ? args : ""; *c; c++)
		{
			if (*c == ' ' || *c == '\t')
			{
				if (!current.empty())
				{
					out.push_back(current);
					current.clear();
				}
				continue;
			}
			current += *c;
		}
		if (!current.empty())
		{
			out.push_back(current);
		}
		return out;
	}

	static_function HNSPlayer *PlayerForSlot(i32 slot, u64 steamID)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(slot));
		if (!player || !player->IsConnectedController() || player->GetSteamId64(true) != steamID)
		{
			return nullptr;
		}
		return player;
	}

	// Columns: JumpType, IsBlockJump, Distance, Block, Strafes, Sync, Pre, Max, Airtime[, SteamID64, Name]
	static_function Record ReadRecord(ISQLResult *result, bool withOwner)
	{
		Record r;
		r.set = true;
		r.distance = result->GetFloat(2);
		r.block = result->GetInt(3);
		r.strafes = result->GetInt(4);
		r.sync = result->GetFloat(5);
		r.pre = result->GetFloat(6);
		r.max = result->GetFloat(7);
		r.airtime = result->GetFloat(8);
		if (withOwner)
		{
			r.steamID = (u64)result->GetInt64(9);
			r.name = result->IsNull(10) ? "unknown" : result->GetString(10);
		}
		return r;
	}

	static_function void LoadServerRecords()
	{
		if (!ready)
		{
			return;
		}
		connection->Query(
			"SELECT j.JumpType, j.IsBlockJump, j.Distance, j.Block, j.Strafes, j.Sync, j.Pre, j.Max, j.Airtime, j.SteamID64, p.Name "
			"FROM Jumpstats j LEFT JOIN Players p ON p.SteamID64 = j.SteamID64",
			[](ISQLQuery *query)
			{
				if (!query)
				{
					return;
				}
				for (auto &row : serverRecords)
				{
					row[0] = {};
					row[1] = {};
				}
				ISQLResult *result = query->GetResultSet();
				u32 rows = 0;
				while (result && result->FetchRow())
				{
					i32 type = result->GetInt(0);
					i32 isBlock = result->GetInt(1) ? 1 : 0;
					if (type < 0 || type >= JS_TIERED_JUMPTYPES)
					{
						continue;
					}
					Record r = ReadRecord(result, true);
					if (Beats(r, serverRecords[type][isBlock], isBlock))
					{
						serverRecords[type][isBlock] = r;
					}
					rows++;
				}
				HNS_LOG_INFO(LogChannel::General, "Jumpstat records: %u personal bests in %s.\n", rows, JS_DB_PATH);
			});
	}

	static_function void LoadPlayer(HNSPlayer *player)
	{
		if (!ready || !player || player->IsFakeClient())
		{
			return;
		}
		i32 slot = player->GetPlayerSlot().Get();
		u64 steamID = player->GetSteamId64(true);
		if (slot < 0 || slot > MAXPLAYERS || steamID == 0)
		{
			return;
		}
		players[slot] = {};
		players[slot].steamID = steamID;

		std::string name = connection->Escape(player->GetName());
		connection->Query("INSERT INTO Players (SteamID64, Name, LastSeen) VALUES (%llu, '%s', %lld) "
						  "ON CONFLICT(SteamID64) DO UPDATE SET Name = excluded.Name, LastSeen = excluded.LastSeen",
						  [](ISQLQuery *) {}, steamID, name.c_str(), (long long)time(nullptr));

		connection->Query(
			"SELECT JumpType, IsBlockJump, Distance, Block, Strafes, Sync, Pre, Max, Airtime FROM Jumpstats WHERE SteamID64 = %llu",
			[slot, steamID](ISQLQuery *query)
			{
				// The player may have left, or someone else may have the slot now.
				if (players[slot].steamID != steamID)
				{
					return;
				}
				ISQLResult *result = query ? query->GetResultSet() : nullptr;
				while (result && result->FetchRow())
				{
					i32 type = result->GetInt(0);
					i32 isBlock = result->GetInt(1) ? 1 : 0;
					if (type >= 0 && type < JS_TIERED_JUMPTYPES)
					{
						players[slot].pb[type][isBlock] = ReadRecord(result, false);
					}
				}
				players[slot].loaded = query != nullptr;
			},
			steamID);
	}

	static_function void OnConnected(bool success)
	{
		if (!success)
		{
			HNS_LOG_WARN(LogChannel::General, "Could not open %s: jumpstat records are off.\n", JS_DB_PATH);
			if (connection)
			{
				connection->Destroy();
				connection = nullptr;
			}
			return;
		}
		Transaction txn;
		txn.queries = {createPlayers, createJumpstats, createIndex};
		connection->ExecuteTransaction(
			txn,
			[](std::vector<ISQLQuery *>)
			{
				ready = true;
				HNS_LOG_INFO(LogChannel::General, "Jumpstat records database ready (%s).\n", JS_DB_PATH);
				LoadServerRecords();
				// Players who were already validated before the database was up (late load, map start).
				for (i32 i = 0; i < MAXPLAYERS; i++)
				{
					HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(i));
					if (player && player->IsConnectedController() && player->authAuthorized)
					{
						LoadPlayer(player);
					}
				}
			},
			[](std::string error, int index) { HNS_LOG_WARN(LogChannel::General, "Jumpstat records setup failed (query %d): %s\n", index, error.c_str()); });
	}

	void Init()
	{
		if (connection || !hns::config::settings.jsDb)
		{
			return;
		}
		ISQLInterface *sql = (ISQLInterface *)g_SMAPI->MetaFactory(SQLMM_INTERFACE, nullptr, nullptr);
		if (!sql || !sql->GetSQLiteClient())
		{
			HNS_LOG_WARN(LogChannel::General, "sql_mm is not loaded: jumpstat records (personal bests, server records) are off.\n");
			return;
		}
		SQLiteConnectionInfo info;
		info.database = JS_DB_PATH;
		connection = sql->GetSQLiteClient()->CreateSQLiteConnection(info);
		if (!connection)
		{
			return;
		}
		connection->Connect(OnConnected);
	}

	void Cleanup()
	{
		ready = false;
		if (connection)
		{
			connection->Destroy();
			connection = nullptr;
		}
	}

	void OnPluginUnloaded()
	{
		if (connection && !g_SMAPI->MetaFactory(SQLMM_INTERFACE, nullptr, nullptr))
		{
			// sql_mm destroyed its connections when it unloaded.
			connection = nullptr;
			ready = false;
			HNS_LOG_WARN(LogChannel::General, "sql_mm unloaded: jumpstat records are off.\n");
		}
	}

	bool IsReady()
	{
		return ready && connection;
	}

	void OnPlayerAuthorized(HNSPlayer *player)
	{
		LoadPlayer(player);
	}

	void OnPlayerReset(HNSPlayer *player)
	{
		i32 slot = player->GetPlayerSlot().Get();
		if (slot >= 0 && slot <= MAXPLAYERS)
		{
			players[slot] = {};
		}
	}

	static_function void SaveRecord(u64 steamID, i32 type, i32 isBlock, const Record &r)
	{
		connection->Query("INSERT INTO Jumpstats (SteamID64, JumpType, IsBlockJump, Distance, Block, Strafes, Sync, Pre, Max, Airtime, Created) "
						  "VALUES (%llu, %d, %d, %.4f, %d, %d, %.4f, %.2f, %.2f, %.4f, %lld) "
						  "ON CONFLICT(SteamID64, JumpType, IsBlockJump) DO UPDATE SET Distance = excluded.Distance, Block = excluded.Block, "
						  "Strafes = excluded.Strafes, Sync = excluded.Sync, Pre = excluded.Pre, Max = excluded.Max, "
						  "Airtime = excluded.Airtime, Created = excluded.Created",
						  [](ISQLQuery *query)
						  {
							  if (!query)
							  {
								  HNS_LOG_WARN(LogChannel::General, "Saving a jumpstat record failed.\n");
							  }
						  },
						  steamID, type, isBlock, r.distance, r.block, r.strafes, r.sync, r.pre, r.max, r.airtime, (long long)time(nullptr));
	}

	static_function std::string Describe(const Record &r, bool isBlock)
	{
		char out[64];
		if (isBlock)
		{
			V_snprintf(out, sizeof(out), "%d block (%.1f)", r.block, r.distance);
		}
		else
		{
			V_snprintf(out, sizeof(out), "%.1f", r.distance);
		}
		return out;
	}

	// After a new personal best: if it places the player in the server's top 10 for that jump type, tell everyone.
	// The rank query runs after the save on sql_mm's single database thread, so it already sees the new row.
	static_function void AnnounceTopPlacement(const Record &r, i32 type, i32 isBlock)
	{
		std::string name = r.name;
		f32 distance = r.distance;
		i32 block = r.block;
		auto callback = [name, distance, block, type, isBlock](ISQLQuery *query)
		{
			ISQLResult *result = query ? query->GetResultSet() : nullptr;
			if (!result || !result->FetchRow())
			{
				return;
			}
			i32 rank = result->GetInt(0) + 1;
			if (rank > JS_TOP_COUNT)
			{
				return;
			}
			char length[64];
			if (isBlock)
			{
				V_snprintf(length, sizeof(length), "%d block, %.1f", block, distance);
			}
			else
			{
				V_snprintf(length, sizeof(length), "%.1f", distance);
			}
			if (rank == 1)
			{
				hns::jumpstats::PlayRecordSound();
			}
			utils::CPrintChatAll("%s {default}%s {grey}is now {gold}#%d {grey}in the {lime}%s%s {grey}JumpTop! ({gold}%s{grey})", JS_DB_PREFIX,
								 name.c_str(), rank, jumpTypeStr[type], isBlock ? " Block" : "", length);
		};
		if (isBlock)
		{
			connection->Query("SELECT COUNT(*) FROM Jumpstats WHERE JumpType = %d AND IsBlockJump = 1 AND "
							  "(Block > %d OR (Block = %d AND Distance > %.4f))",
							  callback, type, block, block, distance);
		}
		else
		{
			connection->Query("SELECT COUNT(*) FROM Jumpstats WHERE JumpType = %d AND IsBlockJump = 0 AND Distance > %.4f", callback, type,
							  distance);
		}
	}

	static_function void TryRecord(HNSPlayer *player, PlayerCache &cache, Record candidate, i32 type, i32 isBlock)
	{
		Record &pb = cache.pb[type][isBlock];
		if (!Beats(candidate, pb, isBlock))
		{
			return;
		}
		Record previous = pb;
		pb = candidate;
		SaveRecord(cache.steamID, type, isBlock, candidate);

		CCSPlayerController *controller = player->GetController();
		if (controller && player->jumpstats->reporting)
		{
			if (previous.set)
			{
				utils::CPrintChat(controller, "%s {lime}New personal best{grey}: %s {gold}%s{grey} (was %s)", JS_DB_PREFIX, jumpTypeShortStr[type],
								  Describe(candidate, isBlock).c_str(), Describe(previous, isBlock).c_str());
			}
			else
			{
				utils::CPrintChat(controller, "%s {lime}First personal best{grey}: %s {gold}%s", JS_DB_PREFIX, jumpTypeShortStr[type],
								  Describe(candidate, isBlock).c_str());
			}
		}

		// Keep the cached server record (for !sr) current.
		Record &sr = serverRecords[type][isBlock];
		if (Beats(candidate, sr, isBlock))
		{
			sr = candidate;
		}
		if (hns::config::settings.jsDbAnnounceRecords)
		{
			AnnounceTopPlacement(candidate, type, isBlock);
		}
	}

	void OnJump(Jump *jump, DistanceTier tier)
	{
		if (!IsReady() || !hns::config::settings.jsDb || tier < DistanceTier_Meh)
		{
			return;
		}
		i32 type = jump->jumpType;
		if (type < 0 || type >= JS_TIERED_JUMPTYPES)
		{
			return;
		}
		HNSPlayer *player = jump->player;
		i32 slot = player->GetPlayerSlot().Get();
		if (slot < 0 || slot > MAXPLAYERS)
		{
			return;
		}
		PlayerCache &cache = players[slot];
		// Personal bests not loaded yet (just joined, or Steam has not validated them): nothing is saved.
		if (!cache.loaded || cache.steamID == 0 || cache.steamID != player->GetSteamId64(true))
		{
			return;
		}
		Record candidate;
		candidate.set = true;
		candidate.distance = jump->GetDistance();
		candidate.block = 0;
		candidate.strafes = (i32)jump->strafes.size();
		candidate.sync = jump->sync * 100.0f;
		candidate.pre = jump->GetTakeoffSpeed();
		candidate.max = jump->currentMaxSpeed;
		candidate.airtime = jump->airtime;
		candidate.steamID = cache.steamID;
		candidate.name = player->GetName();
		TryRecord(player, cache, candidate, type, 0);
		if (jump->block > 0.0f)
		{
			candidate.block = (i32)jump->block;
			TryRecord(player, cache, candidate, type, 1);
		}
	}

	static_function bool CheckReady(CCSPlayerController *controller)
	{
		if (IsReady())
		{
			return true;
		}
		utils::CPrintChat(controller, "%s {grey}Jumpstat records are off on this server.", JS_DB_PREFIX);
		return false;
	}

	void PrintPersonalBests(CPlayerSlot slot, const char *args)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(slot);
		CCSPlayerController *controller = player ? player->GetController() : nullptr;
		if (!controller || !CheckReady(controller))
		{
			return;
		}
		PlayerCache &cache = players[slot.Get()];
		if (!cache.loaded)
		{
			utils::CPrintChat(controller, "%s {grey}Your records are still loading.", JS_DB_PREFIX);
			return;
		}
		std::vector<std::string> words = SplitArgs(args);
		i32 onlyType = words.empty() ? -1 : ParseJumpType(words[0].c_str());
		if (!words.empty() && onlyType < 0)
		{
			utils::CPrintChat(controller, "%s {grey}Unknown jump type. Use LJ, BH, MBH, WJ, LAJ, LAH or JB.", JS_DB_PREFIX);
			return;
		}
		if (onlyType >= 0)
		{
			for (i32 isBlock = 0; isBlock < 2; isBlock++)
			{
				const Record &r = cache.pb[onlyType][isBlock];
				if (!r.set)
				{
					continue;
				}
				utils::CPrintChat(controller, "%s {grey}%s%s: {gold}%s {grey}| %d Strafes | %.0f%% Sync | %.0f Pre | %.0f Max | %.3fs", JS_DB_PREFIX,
								  jumpTypeShortStr[onlyType], isBlock ? " block" : "", Describe(r, isBlock).c_str(), r.strafes, r.sync, r.pre,
								  r.max, r.airtime);
			}
			if (!cache.pb[onlyType][0].set)
			{
				utils::CPrintChat(controller, "%s {grey}No %s personal best yet.", JS_DB_PREFIX, jumpTypeShortStr[onlyType]);
			}
			return;
		}
		for (i32 isBlock = 0; isBlock < 2; isBlock++)
		{
			std::string line = std::string(JS_DB_PREFIX) + (isBlock ? " {grey}Block bests:" : " {grey}Your bests:");
			bool any = false;
			for (i32 type = 0; type < JS_TIERED_JUMPTYPES; type++)
			{
				const Record &r = cache.pb[type][isBlock];
				if (!r.set)
				{
					continue;
				}
				line += std::string(any ? " {grey}|" : "") + " {grey}" + jumpTypeShortStr[type] + " {gold}" + Describe(r, isBlock);
				any = true;
			}
			if (any)
			{
				utils::CPrintChat(controller, "%s", line.c_str());
			}
			else if (!isBlock)
			{
				utils::CPrintChat(controller, "%s {grey}No personal bests yet.", JS_DB_PREFIX);
			}
		}
	}

	void PrintTop(CPlayerSlot slot, const char *args)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(slot);
		CCSPlayerController *controller = player ? player->GetController() : nullptr;
		if (!controller || !CheckReady(controller))
		{
			return;
		}
		std::vector<std::string> words = SplitArgs(args);
		i32 type = words.empty() ? JumpType_LongJump : ParseJumpType(words[0].c_str());
		if (type < 0)
		{
			utils::CPrintChat(controller, "%s {grey}Usage: !top <LJ|BH|MBH|WJ|LAJ|LAH|JB> [block]", JS_DB_PREFIX);
			return;
		}
		i32 isBlock = (words.size() >= 2 && KZ_STREQI(words[1].c_str(), "block")) ? 1 : 0;
		i32 slotIndex = slot.Get();
		u64 steamID = player->GetSteamId64(false);
		connection->Query(
			"SELECT j.JumpType, j.IsBlockJump, j.Distance, j.Block, j.Strafes, j.Sync, j.Pre, j.Max, j.Airtime, j.SteamID64, p.Name "
			"FROM Jumpstats j LEFT JOIN Players p ON p.SteamID64 = j.SteamID64 WHERE j.JumpType = %d AND j.IsBlockJump = %d "
			"ORDER BY %s j.Distance DESC LIMIT %d",
			[slotIndex, steamID, type, isBlock](ISQLQuery *query)
			{
				HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(slotIndex));
				if (!player || !player->GetController() || player->GetSteamId64(false) != steamID || !query)
				{
					return;
				}
				CCSPlayerController *controller = player->GetController();
				utils::CPrintChat(controller, "%s {gold}Top %d %s%s", JS_DB_PREFIX, JS_TOP_COUNT, jumpTypeStr[type], isBlock ? " (block)" : "");
				ISQLResult *result = query->GetResultSet();
				i32 rank = 0;
				while (result && result->FetchRow())
				{
					Record r = ReadRecord(result, true);
					rank++;
					utils::CPrintChat(controller, " {grey}#%d {default}%s {gold}%s {grey}| %.0f Pre | %.0f%% Sync", rank, r.name.c_str(),
									  Describe(r, isBlock).c_str(), r.pre, r.sync);
				}
				if (rank == 0)
				{
					utils::CPrintChat(controller, " {grey}No records yet.");
				}
			},
			type, isBlock, isBlock ? "j.Block DESC," : "", JS_TOP_COUNT);
	}

	void PrintServerRecords(CPlayerSlot slot, const char *args)
	{
		HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(slot);
		CCSPlayerController *controller = player ? player->GetController() : nullptr;
		if (!controller || !CheckReady(controller))
		{
			return;
		}
		utils::CPrintChat(controller, "%s {gold}Server records", JS_DB_PREFIX);
		bool any = false;
		for (i32 type = 0; type < JS_TIERED_JUMPTYPES; type++)
		{
			const Record &r = serverRecords[type][0];
			if (!r.set)
			{
				continue;
			}
			any = true;
			std::string block;
			const Record &b = serverRecords[type][1];
			if (b.set)
			{
				block = " {grey}| block {gold}" + Describe(b, true) + " {default}" + b.name;
			}
			utils::CPrintChat(controller, " {grey}%s {gold}%s {default}%s%s", jumpTypeShortStr[type], Describe(r, false).c_str(), r.name.c_str(),
							  block.c_str());
		}
		if (!any)
		{
			utils::CPrintChat(controller, " {grey}No records yet.");
		}
	}

	void PrintStatus()
	{
		META_CONPRINTF("---- jumpstat records: %s ----\n", IsReady() ? "connected (" JS_DB_PATH ")" : (hns::config::settings.jsDb ? "not connected" : "off"));
		for (i32 type = 0; type < JS_TIERED_JUMPTYPES; type++)
		{
			for (i32 isBlock = 0; isBlock < 2; isBlock++)
			{
				const Record &r = serverRecords[type][isBlock];
				if (r.set)
				{
					META_CONPRINTF("SR %-4s%s %s  %s (%llu)\n", jumpTypeShortStr[type], isBlock ? " block" : "", Describe(r, isBlock).c_str(),
								   r.name.c_str(), r.steamID);
				}
			}
		}
	}

	static_function void ReloadAll()
	{
		LoadServerRecords();
		for (i32 i = 0; i < MAXPLAYERS; i++)
		{
			HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(CPlayerSlot(i));
			if (player && player->IsConnectedController() && player->authAuthorized)
			{
				LoadPlayer(player);
			}
		}
	}

	static_function void ClearCaches()
	{
		for (auto &row : serverRecords)
		{
			row[0] = {};
			row[1] = {};
		}
		for (auto &p : players)
		{
			for (auto &row : p.pb)
			{
				row[0] = {};
				row[1] = {};
			}
		}
	}

	void ResetAll()
	{
		if (!IsReady())
		{
			META_CONPRINTF("Jumpstat records database is not connected.\n");
			return;
		}
		ClearCaches();
		connection->Query("DELETE FROM Jumpstats",
						  [](ISQLQuery *query)
						  {
							  META_CONPRINTF(query ? "All jumpstat records deleted.\n" : "Deleting jumpstat records failed.\n");
							  ReloadAll();
						  });
	}

	void DeletePlayer(u64 steamID, i32 type)
	{
		if (!IsReady())
		{
			META_CONPRINTF("Jumpstat records database is not connected.\n");
			return;
		}
		auto done = [](ISQLQuery *query)
		{
			META_CONPRINTF(query ? "Deleted %u record(s).\n" : "Deleting failed.\n", query ? query->GetAffectedRows() : 0);
			ClearCaches();
			ReloadAll();
		};
		if (type >= 0)
		{
			connection->Query("DELETE FROM Jumpstats WHERE SteamID64 = %llu AND JumpType = %d", done, steamID, type);
		}
		else
		{
			connection->Query("DELETE FROM Jumpstats WHERE SteamID64 = %llu", done, steamID);
		}
	}
} // namespace hns::jumpstats::db

CON_COMMAND_F(hns_js_db_reset, "Delete every jumpstat record: hns_js_db_reset confirm", FCVAR_NONE)
{
	if (context.GetPlayerSlot().Get() != -1)
	{
		return;
	}
	if (args.ArgC() < 2 || !KZ_STREQI(args.Arg(1), "confirm"))
	{
		META_CONPRINTF("This deletes every personal best and server record. Run: hns_js_db_reset confirm\n");
		return;
	}
	hns::jumpstats::db::ResetAll();
}

CON_COMMAND_F(hns_js_db_delete, "Delete a player's jumpstat records: hns_js_db_delete <SteamID64> [LJ|BH|MBH|WJ|LAJ|LAH|JB]", FCVAR_NONE)
{
	if (context.GetPlayerSlot().Get() != -1)
	{
		return;
	}
	if (args.ArgC() < 2)
	{
		META_CONPRINTF("Usage: hns_js_db_delete <SteamID64> [jump type]\n");
		return;
	}
	u64 steamID = 0;
	for (const char *c = args.Arg(1); *c >= '0' && *c <= '9'; c++)
	{
		steamID = steamID * 10 + (u64)(*c - '0');
	}
	i32 type = -1;
	if (args.ArgC() >= 3)
	{
		type = hns::jumpstats::db::ParseJumpType(args.Arg(2));
		if (type < 0)
		{
			META_CONPRINTF("Unknown jump type '%s'.\n", args.Arg(2));
			return;
		}
	}
	hns::jumpstats::db::DeletePlayer(steamID, type);
}
