#include "hns_player.h"
#include "hns_movement.h"
#include "hns_cvars.h"
#include "hns_config.h"
#include "hns_hud.h"
#include "hns_gameplay.h"
#include "hns_weapons.h"
#include "utils/utils.h"
#include "jumpstats/jumpstats.h"

#include "vprof.h"
#include "tier0/memdbgon.h"

static_global HNSPlayerManager g_HNSPlayerManager;
HNSPlayerManager *g_pHNSPlayerManager = &g_HNSPlayerManager;
PlayerManager *g_pPlayerManager = dynamic_cast<PlayerManager *>(&g_HNSPlayerManager);

// ============================================================
// Lifecycle
// ============================================================

HNSPlayer::~HNSPlayer()
{
	this->DestroyServices();
}

void HNSPlayer::DestroyServices()
{
	delete this->movement;
	this->movement = nullptr;
	delete this->jumpstats;
	this->jumpstats = nullptr;
}

void HNSPlayer::Init()
{
	MovementPlayer::Init();
	this->DestroyServices();
	this->movement = new HNSMovementService(this);
	this->jumpstats = new HNSJumpstatsService(this);
}

void HNSPlayer::Reset()
{
	MovementPlayer::Reset();
	this->movement->Reset();
	this->jumpstats->Reset();
	this->stats = {};
	hns::hud::OnPlayerReset(this);
}

void HNSPlayer::OnPlayerActive()
{
	// Same point CS2KZ sends mode cvars (KZPlayer::OnPlayerActive -> SwitchToMode). The client needs the server's values
	// for prediction, and the engine will not replicate them because the plugin writes cvar memory directly.
	if (!this->IsFakeClient())
	{
		hns::cvars::SendToClient(this->GetPlayerSlot());
	}
}

// ============================================================
// Movement hooks
// ============================================================

void HNSPlayer::OnPhysicsSimulate()
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer::OnPhysicsSimulate();
	// sv_subtick_movement_view_angles is checked inside SetupMove, so the cvars have to be set before that.
	hns::cvars::ApplyAll();
	this->enableWaterFix = this->movement->EnableWaterFix();
	this->movement->OnPhysicsSimulate();
	hns::gameplay::ApplyFreezeToWeapons(this);
}

void HNSPlayer::OnPhysicsSimulatePost()
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer::OnPhysicsSimulatePost();
	this->movement->OnPhysicsSimulatePost();
}

void HNSPlayer::OnSetupMove(PlayerCommand *pc)
{
	VPROF_BUDGET(__func__, "HNS");
	this->movement->OnSetupMove(pc);
}

void HNSPlayer::OnProcessUsercmds(PlayerCommand *cmds, int numcmds)
{
	hns::hud::OnProcessUsercmds(this, cmds, numcmds);
	hns::weapons::RemapKnifeAttack(this, cmds, numcmds);
}

void HNSPlayer::OnProcessMovement()
{
	VPROF_BUDGET(__func__, "HNS");
	MovementPlayer::OnProcessMovement();
	// CS2KZ applies the values a second time here (KZPlayer::OnProcessMovement). Kept for parity.
	hns::cvars::ApplyAll();
	this->enableWaterFix = this->movement->EnableWaterFix();
	this->movement->OnProcessMovement();
	hns::gameplay::ApplyFreezeToMovement(this);
	// After the mode, as in KZPlayer::OnProcessMovement.
	this->jumpstats->OnProcessMovement();
}

void HNSPlayer::OnProcessMovementPost()
{
	VPROF_BUDGET(__func__, "HNS");
	// KZPlayer::OnProcessMovementPost order: UpdateJump before the mode, OnProcessMovementPost after it.
	this->jumpstats->UpdateJump();
	this->movement->OnProcessMovementPost();
	this->jumpstats->OnProcessMovementPost();
	if (hns::gameplay::IsFrozen(this) && this->GetPlayerPawn())
	{
		// Networked: the client predicts zero speed too, so a frozen CT does not rubber-band when pressing keys.
		this->GetPlayerPawn()->m_flVelocityModifier(0.0f);
	}
	MovementPlayer::OnProcessMovementPost();
}

void HNSPlayer::OnPlayerMove()
{
	this->movement->OnPlayerMove();
}

void HNSPlayer::OnCategorizePosition(bool bStayOnGround)
{
	this->movement->OnCategorizePosition(bStayOnGround);
}

void HNSPlayer::OnCategorizePositionPost(bool bStayOnGround)
{
	this->movement->OnCategorizePositionPost(bStayOnGround);
}

void HNSPlayer::OnAirMove()
{
	this->movement->OnAirMove();
}

void HNSPlayer::OnAirMovePost()
{
	this->movement->OnAirMovePost();
}

void HNSPlayer::OnAirAccelerate(Vector &wishdir, f32 &wishspeed, f32 &accel)
{
	this->jumpstats->OnAirAccelerate();
}

void HNSPlayer::OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel)
{
	this->jumpstats->OnAirAcceleratePost(wishdir, wishspeed, accel);
}

void HNSPlayer::OnWaterMove()
{
	this->movement->OnWaterMove();
}

void HNSPlayer::OnWaterMovePost()
{
	this->movement->OnWaterMovePost();
}

void HNSPlayer::OnTryPlayerMove(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing)
{
	this->movement->OnTryPlayerMove(pFirstDest, pFirstTrace, bIsSurfing);
	this->jumpstats->OnTryPlayerMove();
}

void HNSPlayer::OnTryPlayerMovePost(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing)
{
	this->movement->OnTryPlayerMovePost(pFirstDest, pFirstTrace, bIsSurfing);
	this->jumpstats->OnTryPlayerMovePost();
}

// ============================================================
// Movement events
// ============================================================

void HNSPlayer::OnStartTouchGround()
{
	VPROF_BUDGET(__func__, "HNS");
	// Before the mode, as in KZPlayer::OnStartTouchGround.
	this->jumpstats->EndJump();
	this->movement->OnStartTouchGround();
}

void HNSPlayer::OnStopTouchGround()
{
	VPROF_BUDGET(__func__, "HNS");
	// Same as KZPlayer::OnStopTouchGround: under the modern jump model a perf is a jump inside sv_bhop_time_window.
	// Inert on this server's default (sv_legacy_jump true).
	if (!this->inPerf && !hns::cvars::GetBool(HNSCVAR_SV_LEGACY_JUMP))
	{
		f32 window = hns::cvars::GetFloat(HNSCVAR_SV_BHOP_TIME_WINDOW);
		f32 landingTick = this->GetMoveServices()->m_ModernJump().m_nLastLandedTick() + this->GetMoveServices()->m_ModernJump().m_flLastLandedFrac();
		f32 windowMin = landingTick - window * 0.5f * ENGINE_FIXED_TICK_RATE;
		f32 windowMax = landingTick + window * 0.5f * ENGINE_FIXED_TICK_RATE;
		f32 startTime = this->currentMoveData->m_flSubtickStartFraction + this->currentMoveData->m_nTickCount;

		this->inPerf = (startTime >= windowMin && startTime <= windowMax) && this->jumped;
	}

	this->movement->OnStopTouchGround();
	// After the mode, as in KZPlayer::OnStopTouchGround.
	this->jumpstats->AddJump();

	if (this->jumped)
	{
		this->stats.jumps++;
		if (this->inPerf)
		{
			this->stats.perfs++;
		}
		if (this->possibleLadderHop)
		{
			this->stats.ladderHops++;
		}
	}
	this->stats.lastTakeoffSpeed = this->takeoffVelocity.Length2D();
	if (this->jumped)
	{
		hns::hud::OnTakeoff(this);
	}
	this->stats.lastTimeOnGround = this->takeoffTime - this->landingTime;

	if (hns::config::settings.debugPerf && this->jumped)
	{
		Vector landing2D(this->landingVelocity.x, this->landingVelocity.y, 0.0f);
		HNS_LOG_INFO(LogChannel::Movement, "%s: %s ground %.4fs (%.2f ticks) landing %.2f takeoff %.2f z %.3f%s\n", this->GetName(),
					 this->inPerf ? "PERF" : "no perf", this->stats.lastTimeOnGround, this->stats.lastTimeOnGround * ENGINE_FIXED_TICK_RATE,
					 landing2D.Length(), this->stats.lastTakeoffSpeed, this->takeoffOrigin.z, this->possibleLadderHop ? " (ladder hop)" : "");
	}
}

void HNSPlayer::OnChangeMoveType(MoveType_t oldMoveType)
{
	VPROF_BUDGET(__func__, "HNS");
	this->jumpstats->OnChangeMoveType(oldMoveType);
	this->movement->OnChangeMoveType(oldMoveType);
}

void HNSPlayer::OnTeleport(const Vector *origin, const QAngle *angles, const Vector *velocity)
{
	VPROF_BUDGET(__func__, "HNS");
	// KZPlayer::OnTeleport: remember the time (JustTeleported) and end the jump as a teleport.
	this->jumpstats->lastTeleportTime = g_pKZUtils->GetServerGlobals()->curtime;
	this->jumpstats->HandleTeleport();
	this->movement->OnTeleport(origin, angles, velocity);
}

// ============================================================
// Manager
// ============================================================

HNSPlayer *HNSPlayerManager::ToPlayer(CPlayerPawnComponent *component)
{
	return static_cast<HNSPlayer *>(MovementPlayerManager::ToPlayer(component));
}

HNSPlayer *HNSPlayerManager::ToPlayer(CBasePlayerController *controller)
{
	return static_cast<HNSPlayer *>(MovementPlayerManager::ToPlayer(controller));
}

HNSPlayer *HNSPlayerManager::ToPlayer(CBasePlayerPawn *pawn)
{
	return static_cast<HNSPlayer *>(MovementPlayerManager::ToPlayer(pawn));
}

HNSPlayer *HNSPlayerManager::ToPlayer(CPlayerSlot slot)
{
	return static_cast<HNSPlayer *>(MovementPlayerManager::ToPlayer(slot));
}

HNSPlayer *HNSPlayerManager::ToPlayer(CEntityIndex entIndex)
{
	return static_cast<HNSPlayer *>(MovementPlayerManager::ToPlayer(entIndex));
}

HNSPlayer *HNSPlayerManager::ToPlayer(CPlayerUserId userID)
{
	return static_cast<HNSPlayer *>(MovementPlayerManager::ToPlayer(userID));
}

HNSPlayer *HNSPlayerManager::ToPlayer(u32 index)
{
	return static_cast<HNSPlayer *>(MovementPlayerManager::players[index]);
}
