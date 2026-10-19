// Hook layer for hns-movement. Trimmed from CS2KZ's src/utils/hooks.cpp (AGPL-3.0): only the movement detours and the
// few engine callbacks the movement layer needs are kept. Everything KZ-specific (triggers, chat, HUD, replays, DB,
// cvar queries, quiet mode, map API, ...) is gone.

#include "utils/utils.h"
#include "utils/hooks.h"
#include "utils/addresses.h"
#include "utils/gameconfig.h"
#include "utils/ctimer.h"

#include "entityclass.h"
#include "igameevents.h"
#include "igamesystem.h"
#include "iserver.h"
#include "bufferstring.h"

#include "hns_plugin.h"
#include "hns/hns_player.h"
#include "hns/hns_movement.h"
#include "hns/hns_gameplay.h"
#include "hns/hns_hud.h"
#include "hns/hns_commands.h"
#include "hns/hns_weapons.h"
#include "hns/hns_joinsound.h"
#include "sdk/takedamageinfo.h"
#include "sdk/usercmd.h"

#include "vprof.h"
#include "memdbgon.h"

extern CGameConfig *g_pGameConfig;

class GameSessionConfiguration_t
{
};

class EntListener : public IEntityListener
{
	virtual void OnEntityCreated(CEntityInstance *pEntity) {}

	virtual void OnEntitySpawned(CEntityInstance *pEntity);
	virtual void OnEntityDeleted(CEntityInstance *pEntity);
} entityListener;

static_global bool entityListenerAdded {};

static MovementPlayerManager *playerManager;

// ============================================================
// Entity hooks
// ============================================================

static KHook::Return<void> TeleportPre(CBaseEntity *pThis, const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity)
{
	VPROF_BUDGET(__func__, "HNS");
	if (pThis->IsPawn())
	{
		MovementPlayer *player = playerManager->ToPlayer(static_cast<CBasePlayerPawn *>(pThis));
		if (player)
		{
			player->OnTeleport(newPosition, newAngles, newVelocity);
		}
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<CBaseEntity, void, const Vector *, const QAngle *, const Vector *> teleportHook(TeleportPre, nullptr);

// ============================================================
// ISource2Server hooks
// ============================================================

static KHook::Return<void> GameFramePre(ISource2Server *pThis, bool simulating, bool bFirstTick, bool bLastTick)
{
	VPROF_BUDGET(__func__, "HNS");
	g_HNSPlugin.serverGlobals = *(g_pKZUtils->GetGlobals());
	hns::gameplay::OnGameFrame();
	hns::weapons::OnGameFrame();
	hns::hud::OnGameFrame();
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2Server, void, bool, bool, bool> gameFrameHook(GameFramePre, nullptr);

// ============================================================
// ISource2GameClients hooks
// ============================================================

static KHook::Return<void> ClientActivePost(ISource2GameClients *pThis, CPlayerSlot slot, bool bLoadGame, const char *pszName, uint64 xuid)
{
	VPROF_BUDGET(__func__, "HNS");
	g_pPlayerManager->OnClientActive(slot, bLoadGame, pszName, xuid);
	HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(slot);
	if (player->GetPlayerPawn())
	{
		hooks::AddEntityHooks(player->GetPlayerPawn());
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameClients, void, CPlayerSlot, bool, const char *, uint64> clientActiveHook(nullptr, ClientActivePost);

static KHook::Return<void> ClientDisconnectPost(ISource2GameClients *pThis, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName,
												uint64 xuid, const char *pszNetworkID)
{
	VPROF_BUDGET(__func__, "HNS");
	HNSPlayer *player = g_pHNSPlayerManager->ToPlayer(slot);
	if (player->GetPlayerPawn())
	{
		hooks::RemoveEntityHooks(player->GetPlayerPawn());
	}
	g_pPlayerManager->OnClientDisconnect(slot, reason, pszName, xuid, pszNetworkID);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ISource2GameClients, void, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64, const char *>
	clientDisconnectHook(nullptr, ClientDisconnectPost);

// ============================================================
// IGameEventManager2 hooks
// ============================================================

static KHook::Return<bool> FireEventPre(IGameEventManager2 *pThis, IGameEvent *event, bool bDontBroadcast)
{
	VPROF_BUDGET(__func__, "HNS");
	if (event && KZ_STREQI(event->GetName(), "round_prestart"))
	{
		// Same point CS2KZ re-hooks entities: the entity list is rebuilt on round restart.
		hooks::HookEntities();
	}
	if (event)
	{
		hns::gameplay::OnFireEvent(event);
	}
	return {KHook::Action::Ignore, true};
}

static KHook::Virtual<IGameEventManager2, bool, IGameEvent *, bool> fireEventHook(FireEventPre, nullptr);

// Chat commands (!hud, /hud). say and say_team are dispatched through ICvar like every other command.
static KHook::Return<void> DispatchConCommandPre(ICvar *pThis, ConCommandRef cmd, const CCommandContext &context, const CCommand &args)
{
	if (hns::commands::OnSay(context, args))
	{
		return {KHook::Action::Supersede};
	}
	return {KHook::Action::Ignore};
}

static KHook::Virtual<ICvar, void, ConCommandRef, const CCommandContext &, const CCommand &> dispatchConCommandHook(DispatchConCommandPre,
																												nullptr);

// ============================================================
// IGameSystem hooks (CEntityDebugGameSystem vtable)
// ============================================================

static KHook::Return<void> ServerGamePostSimulatePost(IGameSystem *pThis, const EventServerGamePostSimulate_t *)
{
	VPROF_BUDGET(__func__, "HNS");
	ProcessTimers();
	return {KHook::Action::Ignore};
}

static KHook::Virtual<IGameSystem, void, const EventServerGamePostSimulate_t *> serverGamePostSimulateHook(nullptr, ServerGamePostSimulatePost);

// Same hook CS2KZ uses to precache its workshop addon's soundevents (hooks.cpp BuildGameSessionManifestPost).
static KHook::Return<void> BuildGameSessionManifestPost(IGameSystem *pThis, const EventBuildGameSessionManifest_t *msg)
{
	hns::joinsound::OnBuildGameSessionManifest(msg->m_pResourceManifest);
	return {KHook::Action::Ignore};
}

static KHook::Virtual<IGameSystem, void, const EventBuildGameSessionManifest_t *> buildGameSessionManifestHook(nullptr,
																											  BuildGameSessionManifestPost);

// ============================================================
// Movement hooks (signature based)
// ============================================================

static KHook::Return<void> PhysicsSimulatePre(CCSPlayerController *controller)
{
	VPROF_BUDGET(__func__, "HNS");
	if (controller->m_bIsHLTV)
	{
		return {KHook::Action::Supersede};
	}
	g_HNSPlugin.simulatingPhysics = true;
	playerManager->ToPlayer(controller)->OnPhysicsSimulate();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> PhysicsSimulatePost(CCSPlayerController *controller)
{
	VPROF_BUDGET(__func__, "HNS");
	if (controller->m_bIsHLTV)
	{
		return {KHook::Action::Ignore};
	}
	MovementPlayer *player = playerManager->ToPlayer(controller);
	player->OnPhysicsSimulatePost();
	g_HNSPlugin.simulatingPhysics = false;
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayerController, void> PhysicsSimulate(PhysicsSimulatePre, PhysicsSimulatePost);

static KHook::Return<i32> ProcessUsercmdsPre(CCSPlayerController *controller, PlayerCommand *cmds, int numcmds, bool paused, float margin);

static KHook::Member<CCSPlayerController, i32, PlayerCommand *, int, bool, float> ProcessUsercmds(ProcessUsercmdsPre, nullptr);

static KHook::Return<i32> ProcessUsercmdsPre(CCSPlayerController *controller, PlayerCommand *cmds, int numcmds, bool paused, float margin)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(controller);
	player->OnProcessUsercmds(cmds, numcmds);
	auto retValue = ProcessUsercmds.CallOriginal(controller, cmds, numcmds, paused, margin);
	player->OnProcessUsercmdsPost(cmds, numcmds);
	return {KHook::Action::Supersede, retValue};
}

static KHook::Return<void> SetupMovePre(CCSPlayer_MovementServices *ms, PlayerCommand *pc, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->currentMoveData = mv;
	player->moveDataPre = CMoveData(*mv);
	player->OnSetupMove(pc);
	return {KHook::Action::Ignore};
}

static KHook::Return<void> SetupMovePost(CCSPlayer_MovementServices *ms, PlayerCommand *pc, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnSetupMovePost(pc);
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, PlayerCommand *, CMoveData *> SetupMove(SetupMovePre, SetupMovePost);

static KHook::Return<void> ProcessMovementPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->currentMoveData = mv;
	player->moveDataPre = CMoveData(*mv);
	player->OnProcessMovement();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> ProcessMovementPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->moveDataPost = CMoveData(*mv);
	player->OnProcessMovementPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> ProcessMovement(ProcessMovementPre, ProcessMovementPost);

static KHook::Return<bool> PlayerMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnPlayerMove();
	return {KHook::Action::Ignore, false};
}

static KHook::Return<bool> PlayerMovePost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnPlayerMovePost();
	return {KHook::Action::Ignore, false};
}

static KHook::Member<CCSPlayer_MovementServices, bool, CMoveData *> PlayerMove(PlayerMovePre, PlayerMovePost);

static KHook::Return<void> CheckParametersPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnCheckParameters();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> CheckParametersPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnCheckParametersPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> CheckParameters(CheckParametersPre, CheckParametersPost);

static KHook::Return<bool> CanMovePre(CCSPlayerPawnBase *pawn)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(pawn)->OnCanMove();
	return {KHook::Action::Ignore, false};
}

static KHook::Return<bool> CanMovePost(CCSPlayerPawnBase *pawn)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(pawn)->OnCanMovePost();
	return {KHook::Action::Ignore, false};
}

static KHook::Member<CCSPlayerPawnBase, bool> CanMove(CanMovePre, CanMovePost);

static KHook::Return<void> FullWalkMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv, bool ground)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnFullWalkMove(ground);
	return {KHook::Action::Ignore};
}

static KHook::Return<void> FullWalkMovePost(CCSPlayer_MovementServices *ms, CMoveData *mv, bool ground)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnFullWalkMovePost(ground);
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *, bool> FullWalkMove(FullWalkMovePre, FullWalkMovePost);

static KHook::Return<bool> MoveInitPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnMoveInit();
	return {KHook::Action::Ignore, false};
}

static KHook::Return<bool> MoveInitPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnMoveInitPost();
	return {KHook::Action::Ignore, false};
}

static KHook::Member<CCSPlayer_MovementServices, bool, CMoveData *> MoveInit(MoveInitPre, MoveInitPost);

static KHook::Return<bool> CheckWaterPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnCheckWater();
	return {KHook::Action::Ignore, false};
}

static KHook::Return<bool> CheckWaterPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnCheckWaterPost();
	return {KHook::Action::Ignore, false};
}

static KHook::Member<CCSPlayer_MovementServices, bool, CMoveData *> CheckWater(CheckWaterPre, CheckWaterPost);

static KHook::Return<void> WaterMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->OnWaterMove();
#ifdef WATER_FIX
	if (player->enableWaterFix)
	{
		player->ignoreNextCategorizePosition = true;
	}
#endif
	return {KHook::Action::Ignore};
}

static KHook::Return<void> WaterMovePost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnWaterMovePost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> WaterMove(WaterMovePre, WaterMovePost);

static KHook::Return<void> CheckVelocityPre(CCSPlayer_MovementServices *ms, CMoveData *mv, const char *a3)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnCheckVelocity(a3);
	return {KHook::Action::Ignore};
}

static KHook::Return<void> CheckVelocityPost(CCSPlayer_MovementServices *ms, CMoveData *mv, const char *a3)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnCheckVelocityPost(a3);
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *, const char *> CheckVelocity(CheckVelocityPre, CheckVelocityPost);

static KHook::Return<void> DuckPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->OnDuck();
	player->processingDuck = true;
	return {KHook::Action::Ignore};
}

static KHook::Return<void> DuckPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->processingDuck = false;
	player->OnDuckPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> Duck(DuckPre, DuckPost);

static KHook::Return<bool> CanUnduckPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnCanUnduck();
	return {KHook::Action::Ignore, false};
}

static KHook::Return<bool> CanUnduckPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	bool canUnduck = *(bool *)KHook::GetOriginalValuePtr();
	player->OnCanUnduckPost(canUnduck);
	return {KHook::Action::Ignore, canUnduck};
}

static KHook::Member<CCSPlayer_MovementServices, bool, CMoveData *> CanUnduck(CanUnduckPre, CanUnduckPost);

static KHook::Return<bool> LadderMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv);

static KHook::Member<CCSPlayer_MovementServices, bool, CMoveData *> LadderMove(LadderMovePre, nullptr);

static KHook::Return<bool> LadderMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->OnLadderMove();
	Vector oldVelocity = mv->m_vecVelocity;
	MoveType_t oldMoveType = player->GetPlayerPawn()->m_MoveType();
	bool result = LadderMove.CallOriginal(ms, mv);
	if (player->GetPlayerPawn()->m_lifeState() != LIFE_DEAD && !result && oldMoveType == MOVETYPE_LADDER)
	{
		player->SetMoveType(MOVETYPE_WALK, false);
	}
	if (!result && oldMoveType == MOVETYPE_LADDER)
	{
		player->RegisterTakeoff(false, true, &player->lastValidLadderOrigin);
		player->OnChangeMoveType(MOVETYPE_LADDER);
	}
	else if (result && oldMoveType != MOVETYPE_LADDER && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_LADDER
			 && !(player->GetPlayerPawn()->m_fFlags & FL_ONGROUND))
	{
		player->RegisterLanding(oldVelocity, false);
		player->OnChangeMoveType(MOVETYPE_WALK);
	}
	else if (result && oldMoveType == MOVETYPE_LADDER && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_WALK)
	{
		player->RegisterTakeoff(player->IsButtonPressed(IN_JUMP), true);
		player->possibleLadderHop = true;
		player->OnChangeMoveType(MOVETYPE_LADDER);
	}
	if (result && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_LADDER)
	{
		player->GetOrigin(&player->lastValidLadderOrigin);
	}
	player->OnLadderMovePost();
	return {KHook::Action::Supersede, result};
}

static KHook::Return<void> CheckJumpButtonLegacyPre(CCSPlayerLegacyJump *legacy, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	CCSPlayer_MovementServices *ms = legacy->m_pMovementServices;
	MovementPlayer *player = playerManager->ToPlayer(ms);
#ifdef WATER_FIX
	if (player->enableWaterFix && ms->pawn->m_MoveType() == MOVETYPE_WALK && ms->pawn->m_flWaterLevel() > 0.5f && ms->pawn->m_fFlags & FL_ONGROUND)
	{
		if (ms->m_nButtons().m_pButtonStates[0] & IN_JUMP)
		{
			ms->m_nButtons().m_pButtonStates[1] |= IN_JUMP;
		}
	}
#endif
	player->OnCheckJumpButtonLegacy();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> CheckJumpButtonLegacyPost(CCSPlayerLegacyJump *legacy, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(legacy->m_pMovementServices)->OnCheckJumpButtonLegacyPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayerLegacyJump, void, CMoveData *> CheckJumpButtonLegacy(CheckJumpButtonLegacyPre, CheckJumpButtonLegacyPost);

static KHook::Return<void> CheckJumpButtonModernPre(CCSPlayerModernJump *modern, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(modern->m_pMovementServices)->OnCheckJumpButtonModern();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> CheckJumpButtonModernPost(CCSPlayerModernJump *modern, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(modern->m_pMovementServices)->OnCheckJumpButtonModernPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayerModernJump, void, CMoveData *> CheckJumpButtonModern(CheckJumpButtonModernPre, CheckJumpButtonModernPost);

static KHook::Return<void> OnJumpLegacyPre(CCSPlayerLegacyJump *legacy, CMoveData *mv);

static KHook::Member<CCSPlayerLegacyJump, void, CMoveData *> OnJumpLegacy(OnJumpLegacyPre, nullptr);

static KHook::Return<void> OnJumpLegacyPre(CCSPlayerLegacyJump *legacy, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	CCSPlayer_MovementServices *ms = legacy->m_pMovementServices;
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->OnJumpLegacy();
	Vector oldOutWishVel = mv->m_outWishVel;
	OnJumpLegacy.CallOriginal(legacy, mv);
	if (mv->m_outWishVel != oldOutWishVel)
	{
		player->inPerf = (!player->takeoffFromLadder && !player->oldWalkMoved);
		player->RegisterTakeoff(true, player->possibleLadderHop);
		player->OnStopTouchGround();
	}
	player->OnJumpLegacyPost();
	return {KHook::Action::Supersede};
}

static KHook::Return<void> OnJumpModernPre(CCSPlayerModernJump *modern, CMoveData *mv);

static KHook::Member<CCSPlayerModernJump, void, CMoveData *> OnJumpModern(OnJumpModernPre, nullptr);

static KHook::Return<void> OnJumpModernPre(CCSPlayerModernJump *modern, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	CCSPlayer_MovementServices *ms = modern->m_pMovementServices;
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->OnJumpModern();
	Vector oldOutWishVel = mv->m_outWishVel;
	OnJumpModern.CallOriginal(modern, mv);
	if (mv->m_outWishVel != oldOutWishVel)
	{
		player->inPerf = (!player->takeoffFromLadder && !player->oldWalkMoved);
		player->RegisterTakeoff(true, player->possibleLadderHop);
		player->OnStopTouchGround();
	}
	player->OnJumpModernPost();
	return {KHook::Action::Supersede};
}

static KHook::Return<void> AirMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnAirMove();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> AirMovePost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnAirMovePost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> AirMove(AirMovePre, AirMovePost);

static KHook::Return<void> AirAcceleratePre(CCSPlayer_MovementServices *ms, CMoveData *mv, Vector &wishdir, f32 wishspeed, f32 accel)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnAirAccelerate(wishdir, wishspeed, accel);
	return {KHook::Action::Ignore};
}

static KHook::Return<void> AirAcceleratePost(CCSPlayer_MovementServices *ms, CMoveData *mv, Vector &wishdir, f32 wishspeed, f32 accel)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnAirAcceleratePost(wishdir, wishspeed, accel);
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *, Vector &, f32, f32> AirAccelerate(AirAcceleratePre, AirAcceleratePost);

static KHook::Return<void> FrictionPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnFriction();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> FrictionPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnFrictionPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> Friction(FrictionPre, FrictionPost);

static KHook::Return<void> WalkMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnWalkMove();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> WalkMovePost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->walkMoved = true;
	player->OnWalkMovePost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> WalkMove(WalkMovePre, WalkMovePost);

static KHook::Return<void> TryPlayerMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv, Vector *pFirstDest, trace_t *pFirstTrace,
											bool *bIsSurfing);

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *, Vector *, trace_t *, bool *> TryPlayerMove(TryPlayerMovePre, nullptr);

static KHook::Return<void> TryPlayerMovePre(CCSPlayer_MovementServices *ms, CMoveData *mv, Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
	player->OnTryPlayerMove(pFirstDest, pFirstTrace, bIsSurfing);
	Vector oldVelocity = mv->m_vecVelocity;
	TryPlayerMove.CallOriginal(ms, mv, pFirstDest, pFirstTrace, bIsSurfing);
	if (mv->m_vecVelocity != oldVelocity)
	{
		player->SetCollidingWithWorld();
	}
	player->OnTryPlayerMovePost(pFirstDest, pFirstTrace, bIsSurfing);
	return {KHook::Action::Supersede};
}

static KHook::Return<void> CategorizePositionPre(CCSPlayer_MovementServices *ms, CMoveData *mv, bool bStayOnGround);

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *, bool> CategorizePosition(CategorizePositionPre, nullptr);

static KHook::Return<void> CategorizePositionPre(CCSPlayer_MovementServices *ms, CMoveData *mv, bool bStayOnGround)
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer *player = playerManager->ToPlayer(ms);
#ifdef WATER_FIX
	if (player->enableWaterFix && player->ignoreNextCategorizePosition)
	{
		player->ignoreNextCategorizePosition = false;
		return {KHook::Action::Supersede};
	}
#endif
	player->OnCategorizePosition(bStayOnGround);
	Vector oldVelocity = mv->m_vecVelocity;
	bool oldOnGround = !!(player->GetPlayerPawn()->m_fFlags() & FL_ONGROUND);

	CategorizePosition.CallOriginal(ms, mv, bStayOnGround);

	bool ground = !!(player->GetPlayerPawn()->m_fFlags() & FL_ONGROUND);
	if (oldOnGround != ground)
	{
		if (ground)
		{
			player->RegisterLanding(oldVelocity);
			player->OnStartTouchGround();
		}
		else
		{
			player->RegisterTakeoff(false);
			player->OnStopTouchGround();
		}
	}
	player->OnCategorizePositionPost(bStayOnGround);
	return {KHook::Action::Supersede};
}

static KHook::Return<void> CheckFallingPre(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnCheckFalling();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> CheckFallingPost(CCSPlayer_MovementServices *ms, CMoveData *mv)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(ms)->OnCheckFallingPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayer_MovementServices, void, CMoveData *> CheckFalling(CheckFallingPre, CheckFallingPost);

static KHook::Return<void> PostThinkPre(CCSPlayerPawnBase *pawn)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(pawn)->OnPostThink();
	return {KHook::Action::Ignore};
}

static KHook::Return<void> PostThinkPost(CCSPlayerPawnBase *pawn)
{
	VPROF_BUDGET(__func__, "HNS");
	playerManager->ToPlayer(pawn)->OnPostThinkPost();
	return {KHook::Action::Ignore};
}

static KHook::Member<CCSPlayerPawnBase, void> PostThink(PostThinkPre, PostThinkPost);

// ============================================================
// Damage hook (optional, knife damage override)
// ============================================================

static KHook::Return<void> TakeDamageOldPre(CBaseEntity *victim, CTakeDamageInfo *info, void *result)
{
	if (hns::weapons::OnTakeDamage(victim, info))
	{
		return {KHook::Action::Supersede};
	}
	return {KHook::Action::Ignore};
}

static KHook::Member<CBaseEntity, void, CTakeDamageInfo *, void *> TakeDamageOld(TakeDamageOldPre, nullptr);

struct SignatureHook
{
	const char *name;
	void (*configure)(void *address);
};

#define SIGNATURE_HOOK(hook) {#hook, [](void *address) { hook.Configure(address); }}

static_global const SignatureHook SIGNATURE_HOOKS[] = {
	SIGNATURE_HOOK(PhysicsSimulate),
	SIGNATURE_HOOK(ProcessUsercmds),
	SIGNATURE_HOOK(SetupMove),
	SIGNATURE_HOOK(ProcessMovement),
	SIGNATURE_HOOK(PlayerMove),
	SIGNATURE_HOOK(CheckParameters),
	SIGNATURE_HOOK(CanMove),
	SIGNATURE_HOOK(FullWalkMove),
	SIGNATURE_HOOK(MoveInit),
	SIGNATURE_HOOK(CheckWater),
	SIGNATURE_HOOK(WaterMove),
	SIGNATURE_HOOK(CheckVelocity),
	SIGNATURE_HOOK(Duck),
	SIGNATURE_HOOK(CanUnduck),
	SIGNATURE_HOOK(LadderMove),
	SIGNATURE_HOOK(CheckJumpButtonLegacy),
	SIGNATURE_HOOK(CheckJumpButtonModern),
	SIGNATURE_HOOK(OnJumpLegacy),
	SIGNATURE_HOOK(OnJumpModern),
	SIGNATURE_HOOK(AirMove),
	SIGNATURE_HOOK(AirAccelerate),
	SIGNATURE_HOOK(Friction),
	SIGNATURE_HOOK(WalkMove),
	SIGNATURE_HOOK(TryPlayerMove),
	SIGNATURE_HOOK(CategorizePosition),
	SIGNATURE_HOOK(CheckFalling),
	SIGNATURE_HOOK(PostThink),
};

#undef SIGNATURE_HOOK

// KHook reads a virtual's vtable index from the compiler's vcall thunk. On Windows it only recognises MSVC's thunk
// (jmp [rax+disp]); clang-cl emits "mov rax,[rcx]; mov rax,[rax+disp]; jmp rax", which KHook reports as -1 and the hook is
// silently never installed. Decode both forms here and configure the hooks by index.
template<typename FUNC>
static_function std::int32_t VtableIndex(FUNC function)
{
	std::int32_t index = KHook::GetVtableIndex(function);
#ifdef _WIN32
	if (index == -1)
	{
		const u8 *p = reinterpret_cast<const u8 *>(KHook::ExtractMFP(function));
		if (p[0] == 0xE9) // jmp rel32 (incremental linking stub)
		{
			p = p + *reinterpret_cast<const i32 *>(p + 1) + 5;
		}
		if (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x01) // mov rax, [rcx]
		{
			p += 3;
			if (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x40 && p[4] == 0x48 && p[5] == 0xFF && p[6] == 0xE0) // mov rax,[rax+disp8]; jmp rax
			{
				index = *reinterpret_cast<const i8 *>(p + 3) / (i32)sizeof(void *);
			}
			else if (p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x80 && p[7] == 0x48 && p[8] == 0xFF && p[9] == 0xE0) // disp32
			{
				index = *reinterpret_cast<const i32 *>(p + 3) / (i32)sizeof(void *);
			}
		}
	}
#endif
	if (index == -1)
	{
		HNS_LOG_WARN(LogChannel::General, "Could not determine a vtable index; a hook will not be installed.\n");
	}
	return index;
}

// ============================================================
// hooks::Initialize
// ============================================================
bool hooks::Initialize(char *error, size_t maxlen)
{
	// Resolve every signature before anything is hooked, so outdated gamedata aborts the load instead of running with missing detours.
	void *signatureAddresses[KZ_ARRAYSIZE(SIGNATURE_HOOKS)];
	std::string missingSignatures;
	for (u32 i = 0; i < KZ_ARRAYSIZE(SIGNATURE_HOOKS); i++)
	{
		signatureAddresses[i] = g_pGameConfig->ResolveSignature(SIGNATURE_HOOKS[i].name);
		if (!signatureAddresses[i])
		{
			missingSignatures += missingSignatures.empty() ? "" : ", ";
			missingSignatures += SIGNATURE_HOOKS[i].name;
		}
	}
	if (!missingSignatures.empty())
	{
		snprintf(error, maxlen, "Failed to resolve signatures: %s", missingSignatures.c_str());
		HNS_LOG_WARN(LogChannel::General, "%s\n", error);
		return false;
	}
	// The rampbug fix hook. CS2KZ installs it from the Classic mode plugin; it is part of the core here.
	if (!hns::movement::InitTraceHook(error, maxlen))
	{
		HNS_LOG_WARN(LogChannel::General, "%s\n", error);
		return false;
	}

	playerManager = static_cast<MovementPlayerManager *>(g_pPlayerManager);

	// Entity hooks
	teleportHook.Configure(g_pGameConfig->GetOffset("Teleport"));

	// Interface hooks
	gameFrameHook.Configure(VtableIndex(&ISource2Server::GameFrame));
	gameFrameHook.Add(interfaces::pServer);

	clientActiveHook.Configure(VtableIndex(&ISource2GameClients::ClientActive));
	clientActiveHook.Add(g_pSource2GameClients);

	clientDisconnectHook.Configure(VtableIndex(&ISource2GameClients::ClientDisconnect));
	clientDisconnectHook.Add(g_pSource2GameClients);

	fireEventHook.Configure(VtableIndex(&IGameEventManager2::FireEvent));
	fireEventHook.Add(interfaces::pGameEventManager);

	dispatchConCommandHook.Configure(VtableIndex(&ICvar::DispatchConCommand));
	dispatchConCommandHook.Add(g_pCVar);

	{
		void *vtable = modules::server->FindVirtualTable("CEntityDebugGameSystem");
		serverGamePostSimulateHook.Configure(VtableIndex(&IGameSystem::OnServerGamePostSimulate));
		serverGamePostSimulateHook.AddGlobal((IGameSystem *)&vtable);
		buildGameSessionManifestHook.Configure(VtableIndex(&IGameSystem::OnBuildGameSessionManifest));
		buildGameSessionManifestHook.AddGlobal((IGameSystem *)&vtable);
	}

	// Signature-based hooks
	for (u32 i = 0; i < KZ_ARRAYSIZE(SIGNATURE_HOOKS); i++)
	{
		SIGNATURE_HOOKS[i].configure(signatureAddresses[i]);
	}

	// Optional: a missing signature only disables the knife damage override.
	if (void *address = g_pGameConfig->ResolveSignature("CBaseEntity_TakeDamageOld"))
	{
		TakeDamageOld.Configure(address);
		hns::weapons::SetDamageHooked(true);
	}
	else
	{
		HNS_LOG_WARN(LogChannel::General, "CBaseEntity_TakeDamageOld did not resolve: hns_knife_damage has no effect.\n");
	}
	hns::weapons::Init();

	// Late load: hook the pawns that already exist.
	if (GameEntitySystem())
	{
		hooks::HookEntities();
	}
	return true;
}

void hooks::Cleanup()
{
	teleportHook.ClearHooks();
	gameFrameHook.ClearHooks();
	clientActiveHook.ClearHooks();
	clientDisconnectHook.ClearHooks();
	fireEventHook.ClearHooks();
	dispatchConCommandHook.ClearHooks();
	serverGamePostSimulateHook.ClearHooks();
	buildGameSessionManifestHook.ClearHooks();

	hns::movement::CleanupTraceHook();

	if (GameEntitySystem() && entityListenerAdded)
	{
		GameEntitySystem()->RemoveListenerEntity(&entityListener);
		entityListenerAdded = false;
	}
}

// ============================================================
// Entity hook management
// ============================================================
static_function bool IsPlayerPawn(CBaseEntity *entity)
{
	return entity && !V_stricmp(entity->GetClassname(), "player");
}

void hooks::AddEntityHooks(CBaseEntity *entity)
{
	if (IsPlayerPawn(entity) && playerManager->ToPlayer(static_cast<CCSPlayerPawn *>(entity)))
	{
		teleportHook.Add(entity);
	}
}

void hooks::RemoveEntityHooks(CBaseEntity *entity)
{
	if (IsPlayerPawn(entity))
	{
		teleportHook.Remove(entity);
	}
}

void EntListener::OnEntitySpawned(CEntityInstance *pEntity)
{
	hooks::AddEntityHooks(static_cast<CBaseEntity *>(pEntity));
}

void EntListener::OnEntityDeleted(CEntityInstance *pEntity)
{
	hooks::RemoveEntityHooks(static_cast<CBaseEntity *>(pEntity));
}

void hooks::HookEntities()
{
	teleportHook.ClearHooks();

	if (entityListenerAdded)
	{
		GameEntitySystem()->RemoveListenerEntity(&entityListener);
	}
	for (CEntityIdentity *entID = GameEntitySystem()->m_EntityList.m_pFirstActiveEntity; entID != NULL; entID = entID->m_pNext)
	{
		hooks::AddEntityHooks(static_cast<CBaseEntity *>(entID->m_pInstance));
	}
	GameEntitySystem()->AddListenerEntity(&entityListener);
	entityListenerAdded = true;
}
