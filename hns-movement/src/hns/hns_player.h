#pragma once
#include "movement/movement.h"

class HNSMovementService;
class HNSJumpstatsService;

// Replaces CS2KZ's KZPlayer. Keeps only what movement needs: no timer, HUD, jumpstats, anticheat, DB or chat services.
// Every movement callback is forwarded to a single HNSMovementService (the ported Classic mode).
class HNSPlayer : public MovementPlayer
{
public:
	HNSPlayer(int i) : MovementPlayer(i)
	{
		this->Init();
	}

	virtual ~HNSPlayer() override;

	virtual void Init() override;
	virtual void Reset() override;

	HNSMovementService *movement {};
	HNSJumpstatsService *jumpstats {};

	// Connected and in the server, judged from the controller entity. Used instead of Player::IsInGame, which reads the
	// engine's client list through a gamedata offset (ClientOffset) that is not reliable on every game build.
	bool IsConnectedController()
	{
		CCSPlayerController *controller = this->GetController();
		return controller && controller->m_iConnected() == PlayerConnectedState::PlayerConnected;
	}

	// Per-player stats, readable by hns_perf_stats.
	struct Stats
	{
		u64 jumps {};
		u64 perfs {};
		u64 ladderHops {};
		f32 lastTakeoffSpeed {};
		f32 lastTimeOnGround {};
	} stats;

	virtual void OnPlayerActive() override;
	virtual void OnAuthorized() override;

	// Movement hooks
	virtual void OnPhysicsSimulate() override;
	virtual void OnPhysicsSimulatePost() override;
	virtual void OnSetupMove(PlayerCommand *pc) override;
	virtual void OnProcessUsercmds(PlayerCommand *cmds, int numcmds) override;
	virtual void OnProcessMovement() override;
	virtual void OnProcessMovementPost() override;
	virtual void OnPlayerMove() override;
	virtual void OnCategorizePosition(bool bStayOnGround) override;
	virtual void OnCategorizePositionPost(bool bStayOnGround) override;
	virtual void OnAirMove() override;
	virtual void OnAirMovePost() override;
	virtual void OnAirAccelerate(Vector &wishdir, f32 &wishspeed, f32 &accel) override;
	virtual void OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel) override;
	virtual void OnWaterMove() override;
	virtual void OnWaterMovePost() override;
	virtual void OnTryPlayerMove(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing) override;
	virtual void OnTryPlayerMovePost(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing) override;

	// Movement events
	virtual void OnStartTouchGround() override;
	virtual void OnStopTouchGround() override;
	virtual void OnChangeMoveType(MoveType_t oldMoveType) override;

	// Other events
	virtual void OnTeleport(const Vector *origin, const QAngle *angles, const Vector *velocity) override;

private:
	void DestroyServices();
};

class HNSPlayerManager : public MovementPlayerManager
{
public:
	HNSPlayerManager()
	{
		for (int i = 0; i < MAXPLAYERS + 1; i++)
		{
			delete players[i];
			players[i] = new HNSPlayer(i);
		}
	}

public:
	HNSPlayer *ToPlayer(CPlayerPawnComponent *component);
	HNSPlayer *ToPlayer(CBasePlayerController *controller);
	HNSPlayer *ToPlayer(CBasePlayerPawn *pawn);
	HNSPlayer *ToPlayer(CPlayerSlot slot);
	HNSPlayer *ToPlayer(CEntityIndex entIndex);
	HNSPlayer *ToPlayer(CPlayerUserId userID);
	HNSPlayer *ToPlayer(u32 index);
};

extern HNSPlayerManager *g_pHNSPlayerManager;
