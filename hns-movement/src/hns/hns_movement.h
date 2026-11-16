#pragma once
#include "common.h"
#include "sdk/datatypes.h"
#include "utlvector.h"

class HNSPlayer;
class CUserCmd;
class CTraceFilter;

// Ported from CS2KZ's Classic mode (src/kz/mode/kz_mode_ckz.h/.cpp). Names and structure are kept close to upstream
// so fixes can be diffed and pulled across. HNS differences are marked "HNS:".
//
// Not ported (no equivalent on an HNS server):
//   - Timer-zone half-tick gating (CanTouchTimerZone / OnTrigger*Touch): no KZ timer.
//   - Triggerfix (TouchTriggersAlongPath / UpdateTriggerTouchList): needs CS2KZ's trigger service. The TryPlayerMove path
//     is still recorded (tpmTriggerFixOrigins) so it can be added later (plan section 5.4, optional).
//   - Distance tiers (jumpstats).

#define SPEED_NORMAL 250.0f
// Prestrafe related
#define PS_SPEED_MAX        26.0f
#define PS_MIN_REWARD_RATE  2.0f  // Minimum computed turn rate for any prestrafe reward
#define PS_MAX_REWARD_RATE  15.5f // Ideal computed turn rate for maximum prestrafe reward
#define PS_MAX_PS_TIME      0.50f // Time to reach maximum prestrafe speed with optimal turning
#define PS_TURN_RATE_WINDOW 0.02f // Turn rate will be computed over this amount of time
#define PS_DECREMENT_RATIO  3.0f  // Prestrafe will lose this fast compared to gaining
// Controls the ratio between prestrafe ratio and gain.
// The lower the value, the faster the prespeed gain at the start, but the slower the gain near the max value.
// 1 means prestrafe gain is linear with the ratio.
#define PS_RATIO_TO_SPEED 0.5f
// Prestrafe ratio will be not go down after landing for this amount of time - helps with small movements after landing
// Ideally should be much higher than the perf window!
#define PS_LANDING_GRACE_PERIOD 0.25f
// Bhop related
#define BH_PERF_WINDOW                  0.02f // Any jump performed after landing will be a perf for this much time
#define BH_BASE_MULTIPLIER              51.5f // Multiplier for how much speed would a perf gain in ideal scenario
#define BH_LANDING_DECREMENT_MULTIPLIER 75.0f // How much would a non real perf impact the takeoff speed
// Magic number so that landing speed at max ground prestrafe speed would result in the same takeoff velocity
#define BH_NORMALIZE_FACTOR (BH_BASE_MULTIPLIER * log(SPEED_NORMAL + PS_SPEED_MAX) - (SPEED_NORMAL + PS_SPEED_MAX))
// Rampbug: hull shrink used by the ground trace fix.
#define GROUND_CHECK_SHRINK_SIZE 0.0625f
// Misc
#define DUCK_SPEED_NORMAL  8.0f
#define DUCK_SPEED_MINIMUM 6.0234375f // Equal to if you just ducked/unducked for the first time in a while

class HNSMovementService
{
public:
	HNSMovementService(HNSPlayer *player) : player(player) {}

	HNSPlayer *player;

	void Reset();
	void Cleanup();

	// Classic mode turns the water fix on while jump is held.
	bool EnableWaterFix();

	void OnTracePlayerBBoxPost(const Ray_t &ray, const Vector &start, const Vector &end, CTraceFilter *filter, trace_t *pm);

	void OnPhysicsSimulate();
	void OnPhysicsSimulatePost();
	void OnSetupMove(CUserCmd *pc);
	void OnProcessMovement();
	void OnPlayerMove();
	void OnProcessMovementPost();
	void OnCategorizePosition(bool bStayOnGround);
	void OnCategorizePositionPost(bool bStayOnGround);
	void OnAirMove();
	void OnAirMovePost();
	void OnWaterMove();
	void OnWaterMovePost();
	void OnStartTouchGround();
	void OnStopTouchGround();
	void OnTryPlayerMove(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing);
	void OnTryPlayerMovePost(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing);
	void OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity);
	// HNS: ChaseMod freezes with MOVETYPE_OBSOLETE and restores velocity directly. Treat any movetype change made outside
	// movement as a hard reset of the perf/prestrafe state so nothing from before the freeze leaks into the next jump.
	void OnChangeMoveType(MoveType_t oldMoveType);

	void InterpolateViewAngles();
	void RestoreInterpolatedViewAngles();

	void UpdateAngleHistory();
	void CalcPrestrafe();
	f32 GetPrestrafeGain();

	// Game's max speed for this tick before the plugin's clamp (weapon dependent). For hns_status.
	f32 GetOriginalMaxSpeed()
	{
		return this->originalMaxSpeed;
	}
	// HNS: 250 + prestrafe while hns_ps_enabled, otherwise no clamp (negative = leave the engine's max speed alone).
	f32 GetGroundMaxSpeed();

	void CheckVelocityQuantization();
	void RemoveCrouchJumpBind();
	/*
		Ported from DanZay's SimpleKZ (via CS2KZ):
		Duck speed is reduced by the game upon ducking or unducking.
		The goal here is to accept that duck speed is reduced, but
		stop it from being reduced further when spamming duck.

		This is done by enforcing a minimum duck speed equivalent to
		the value as if the player only ducked once. When not in not
		in the middle of ducking, duck speed is reset to its normal
		value in effort to reduce the number of times the minimum
		duck speed is enforced. This should reduce noticeable lag.
	*/
	void ReduceDuckSlowdown();

	void SlopeFix();

	void FixTryPlayerMoveTrace(const Ray_t &ray, const Vector &start, const Vector &end, CTraceFilter *filter, trace_t *pm);
	void FixGroundTrace(const Ray_t &ray, const Vector &start, const Vector &end, CTraceFilter *filter, trace_t *pm);

private:
	bool hasValidDesiredViewAngle {};
	QAngle lastValidDesiredViewAngle;
	f32 lastJumpReleaseTime {};
	bool oldDuckPressed {};
	bool oldJumpPressed {};
	bool forcedUnduck {};
	f32 postProcessMovementZSpeed {};

	struct AngleHistory
	{
		f32 rate;
		f32 when;
		f32 duration;
	};

	CUtlVector<AngleHistory> angleHistory;
	f32 leftPreRatio {};
	f32 rightPreRatio {};
	f32 bonusSpeed {};
	f32 originalMaxSpeed {};

	// Rampbug fix
	bool inTryPlayerMove {};
	// Last plane that TryPlayerMove hit.
	Vector lastPlane = vec3_origin;
	// Number of traces in the current TryPlayerMove that didn't move the player.
	u32 stuckTraceCount {};

	// Keep track of TryPlayerMove path for triggerfixing.
	bool airMoving {};
	CUtlVector<Vector> tpmTriggerFixOrigins;
};

namespace hns::movement
{
	// Resolve and install the TracePlayerBBox post-hook (rampbug fix). CS2KZ does this in the Classic mode plugin.
	bool InitTraceHook(char *error, size_t maxlen);
	void CleanupTraceHook();
} // namespace hns::movement
