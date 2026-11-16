// Port of CS2KZ's Classic mode (src/kz/mode/kz_mode_ckz.cpp, AGPL-3.0) to a single HNS ruleset.
// Upstream names are kept so changes can be diffed. Every HNS deviation is marked "HNS:".

#include "cs_usercmd.pb.h"
#include "hns_movement.h"
#include "hns_player.h"
#include "hns_cvars.h"
#include "hns_config.h"
#include "utils/utils.h"
#include "utils/addresses.h"
#include "utils/gameconfig.h"
#include "sdk/usercmd.h"
#include "sdk/tracefilter.h"
#include "sdk/navphysicsinterface.h"

#include "tier0/memdbgon.h"

extern CGameConfig *g_pGameConfig;

using hns::config::RAMP_BUG_OFFSET;
using hns::config::settings;

// The player that is currently running TryPlayerMove or CategorizePosition.
static_global HNSMovementService *tracingMovementService {};

// Every hull trace done by the game's movement code goes through this function.
static KHook::Return<void> TracePlayerBBoxPost(void *traceCache, trace_t *pm, const Vector *start, const Vector *end, const bbox_t *bounds,
											   CTraceFilter *filter)
{
	if (tracingMovementService)
	{
		tracingMovementService->OnTracePlayerBBoxPost(Ray_t(bounds->mins, bounds->maxs), *start, *end, filter, pm);
	}
	return {KHook::Action::Ignore};
}

static KHook::Function<void, void *, trace_t *, const Vector *, const Vector *, const bbox_t *, CTraceFilter *> TracePlayerBBox(nullptr,
																																TracePlayerBBoxPost);

bool hns::movement::InitTraceHook(char *error, size_t maxlen)
{
	void *tracePlayerBBoxAddress = g_pGameConfig->ResolveSignature("TracePlayerBBox");
	if (!tracePlayerBBoxAddress)
	{
		V_snprintf(error, maxlen, "Failed to resolve signature: TracePlayerBBox");
		return false;
	}
	TracePlayerBBox.Configure(tracePlayerBBoxAddress);
	return true;
}

void hns::movement::CleanupTraceHook()
{
	tracingMovementService = nullptr;
}

/*
	Actual movement stuff.
*/

void HNSMovementService::Reset()
{
	this->hasValidDesiredViewAngle = {};
	this->lastValidDesiredViewAngle = vec3_angle;
	this->lastJumpReleaseTime = {};
	this->oldDuckPressed = {};
	this->oldJumpPressed = {};
	this->forcedUnduck = {};
	this->postProcessMovementZSpeed = {};

	this->angleHistory.RemoveAll();
	this->leftPreRatio = {};
	this->rightPreRatio = {};
	this->bonusSpeed = {};
	this->originalMaxSpeed = {};

	this->inTryPlayerMove = {};
	this->lastPlane = vec3_origin;
	this->stuckTraceCount = {};

	this->airMoving = {};
	this->tpmTriggerFixOrigins.RemoveAll();
}

void HNSMovementService::Cleanup()
{
	if (tracingMovementService == this)
	{
		tracingMovementService = nullptr;
	}
	auto pawn = this->player->GetPlayerPawn();
	if (pawn)
	{
		pawn->m_flVelocityModifier(1.0f);
	}
}

bool HNSMovementService::EnableWaterFix()
{
	return this->player->IsButtonPressed(IN_JUMP);
}

f32 HNSMovementService::GetGroundMaxSpeed()
{
	return settings.psEnabled ? SPEED_NORMAL + this->GetPrestrafeGain() : -1.0f;
}

void HNSMovementService::OnStopTouchGround()
{
	if (this->player->GetMoveType() != MOVETYPE_WALK)
	{
		return;
	}
	Vector velocity;
	this->player->GetVelocity(&velocity);

	f32 timeOnGround = this->player->takeoffTime - this->player->landingTime;
	// HNS: CS2KZ treats ANY ground contact shorter than the perf window as a perf, including sliding off a surf ramp lip
	// or skimming a flat section for one tick without jumping. The perf then rewrites horizontal speed with the KZ curve,
	// which cuts 1000 u/s down to ~334 u/s. Harmless at KZ speeds, but it kills surf speed on an airaccel-1000 server.
	// With hns_perf_requires_jump (default 1) only real jumps get the perf treatment.
	if (settings.perfRequiresJump && !this->player->jumped)
	{
		return;
	}
	// Perf
	if (timeOnGround <= BH_PERF_WINDOW && !this->player->possibleLadderHop)
	{
		this->player->inPerf = true;
		// Perf speed
		Vector2D landingVelocity2D(this->player->landingVelocity.x, this->player->landingVelocity.y);
		landingVelocity2D.NormalizeInPlace();
		float newSpeed = MAX(this->player->landingVelocity.Length2D(), this->player->takeoffVelocity.Length2D());
		// HNS: with hns_ps_enabled 0 the perf keeps max(landing, takeoff) speed and skips KZ's log curve.
		if (settings.psEnabled && newSpeed > SPEED_NORMAL + this->GetPrestrafeGain())
		{
			newSpeed = MIN(newSpeed, (BH_BASE_MULTIPLIER - timeOnGround * BH_LANDING_DECREMENT_MULTIPLIER) * log(newSpeed) - BH_NORMALIZE_FACTOR);
			// Make sure it doesn't go lower than the ground speed.
			newSpeed = MAX(newSpeed, SPEED_NORMAL + this->GetPrestrafeGain());
		}
		velocity.x = newSpeed * landingVelocity2D.x;
		velocity.y = newSpeed * landingVelocity2D.y;
		this->player->SetVelocity(velocity);
		this->player->takeoffVelocity = velocity;

		// Perf height
		Vector origin;
		this->player->GetOrigin(&origin);
		origin.z = this->player->GetGroundPosition();
		this->player->SetOrigin(origin);
		this->player->takeoffOrigin = origin;
	}
}

void HNSMovementService::OnStartTouchGround()
{
	this->SlopeFix();
	// HNS: CS2KZ touches triggers between the landing origin and the ground here (triggerfix). Not ported yet.
}

void HNSMovementService::OnPhysicsSimulate()
{
	CCSPlayer_MovementServices *moveServices = this->player->GetMoveServices();
	if (!moveServices)
	{
		return;
	}
	u32 tickCount = g_pKZUtils->GetServerGlobals()->tickcount;

	f32 subtickMoveTime = (tickCount - 0.5) * ENGINE_FIXED_TICK_INTERVAL;
	for (u32 i = 0; i < 4; i++)
	{
		if (fabs(subtickMoveTime - moveServices->m_arrForceSubtickMoveWhen[i]) < 0.001)
		{
			return;
		}
		if (subtickMoveTime > moveServices->m_arrForceSubtickMoveWhen[i])
		{
			moveServices->SetForcedSubtickMove(i, subtickMoveTime, false);
			return;
		}
	}
}

void HNSMovementService::OnPhysicsSimulatePost()
{
	CCSPlayer_MovementServices *moveServices = this->player->GetMoveServices();
	if (!moveServices)
	{
		return;
	}
	u32 tickCount = g_pKZUtils->GetServerGlobals()->tickcount;

	f32 subtickMoveTime = (tickCount + 0.5) * ENGINE_FIXED_TICK_INTERVAL;
	for (u32 i = 0; i < 4; i++)
	{
		if (fabs(subtickMoveTime - moveServices->m_arrForceSubtickMoveWhen[i]) < 0.001)
		{
			subtickMoveTime += ENGINE_FIXED_TICK_INTERVAL;
			continue;
		}
		if (subtickMoveTime > moveServices->m_arrForceSubtickMoveWhen[i])
		{
			moveServices->SetForcedSubtickMove(i, subtickMoveTime);
			subtickMoveTime += ENGINE_FIXED_TICK_INTERVAL;
		}
	}
}

void HNSMovementService::OnSetupMove(CUserCmd *pc)
{
	for (i32 j = 0; j < pc->mutable_base()->subtick_moves_size(); j++)
	{
		CSubtickMoveStep *subtickMove = pc->mutable_base()->mutable_subtick_moves(j);
		if (subtickMove->button() == IN_ATTACK || subtickMove->button() == IN_ATTACK2 || subtickMove->button() == IN_RELOAD)
		{
			continue;
		}
		float when = subtickMove->when();
		if (subtickMove->button() == IN_JUMP)
		{
			f32 inputTime = (g_pKZUtils->GetGlobals()->tickcount + when - 1) * ENGINE_FIXED_TICK_INTERVAL;
			if (when != 0)
			{
				if (subtickMove->pressed() && inputTime - this->lastJumpReleaseTime > 0.5 * ENGINE_FIXED_TICK_INTERVAL)
				{
					this->player->GetMoveServices()->m_LegacyJump().m_bOldJumpPressed = false;
				}
				if (!subtickMove->pressed())
				{
					this->lastJumpReleaseTime = (g_pKZUtils->GetGlobals()->tickcount + when - 1) * ENGINE_FIXED_TICK_INTERVAL;
				}
			}
		}
		subtickMove->set_when(when >= 0.5 ? 0.5 : 0);
	}
}

void HNSMovementService::OnProcessMovement()
{
	if (this->player->GetPlayerPawn()->m_flVelocityModifier() != 1.0f)
	{
		this->player->GetPlayerPawn()->m_flVelocityModifier(1.0f);
	}
	this->CheckVelocityQuantization();
	this->RemoveCrouchJumpBind();
	this->ReduceDuckSlowdown();
	this->InterpolateViewAngles();
	this->UpdateAngleHistory();
	this->CalcPrestrafe();
}

void HNSMovementService::OnPlayerMove()
{
	this->originalMaxSpeed = this->player->currentMoveData->m_flMaxSpeed;
	// HNS: only clamp ground speed to 250 + prestrafe when the KZ prestrafe model is on.
	if (settings.psEnabled)
	{
		this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL + this->GetPrestrafeGain();
	}
}

void HNSMovementService::OnProcessMovementPost()
{
	// HNS: CS2KZ calls UpdateTriggerTouchList here (triggerfix). Not ported.
	this->RestoreInterpolatedViewAngles();
	this->oldDuckPressed = this->forcedUnduck || this->player->IsButtonPressed(IN_DUCK, true);
	this->oldJumpPressed = this->player->IsButtonPressed(IN_JUMP);
	Vector velocity;
	this->player->GetVelocity(&velocity);
	this->postProcessMovementZSpeed = velocity.z;
	// The velocity modifier tells the client about the server's max speed clamp so prediction matches.
	// HNS: without the clamp (hns_ps_enabled 0) it stays at the 1.0 set in OnProcessMovement.
	if (settings.psEnabled)
	{
		f32 velMod = this->originalMaxSpeed >= 0 ? (SPEED_NORMAL + this->GetPrestrafeGain()) / this->originalMaxSpeed : 1.0f;
		if (this->player->GetPlayerPawn()->m_flVelocityModifier() != velMod)
		{
			this->player->GetPlayerPawn()->m_flVelocityModifier(velMod);
		}
	}
}

void HNSMovementService::InterpolateViewAngles()
{
	// Second half of the movement, no change.
	CGlobalVars *globals = g_pKZUtils->GetGlobals();
	f64 subtickFraction, whole;
	subtickFraction = modf((f64)globals->curtime * ENGINE_FIXED_TICK_RATE, &whole);
	if (subtickFraction < 0.001)
	{
		return;
	}

	// First half of the movement, tweak the angle to be the middle of the desired angle and the last angle
	QAngle newAngles = player->currentMoveData->m_vecViewAngles;
	QAngle oldAngles = this->hasValidDesiredViewAngle ? this->lastValidDesiredViewAngle : this->player->moveDataPost.m_vecViewAngles;
	if (newAngles[YAW] - oldAngles[YAW] > 180)
	{
		newAngles[YAW] -= 360.0f;
	}
	else if (newAngles[YAW] - oldAngles[YAW] < -180)
	{
		newAngles[YAW] += 360.0f;
	}

	for (u32 i = 0; i < 3; i++)
	{
		newAngles[i] += oldAngles[i];
		newAngles[i] *= 0.5f;
	}
	player->currentMoveData->m_vecViewAngles = newAngles;
}

void HNSMovementService::RestoreInterpolatedViewAngles()
{
	player->currentMoveData->m_vecViewAngles = player->moveDataPre.m_vecViewAngles;
	if (g_pKZUtils->GetGlobals()->frametime > 0.0f)
	{
		this->hasValidDesiredViewAngle = true;
		this->lastValidDesiredViewAngle = player->currentMoveData->m_vecViewAngles;
	}
}

void HNSMovementService::RemoveCrouchJumpBind()
{
	this->forcedUnduck = false;

	bool onGround = this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND;
	bool justJumped = !this->oldJumpPressed && this->player->IsButtonPressed(IN_JUMP);

	if (onGround && !this->oldDuckPressed && justJumped)
	{
		this->player->GetMoveServices()->m_nButtons().m_pButtonStates[0] &= ~IN_DUCK;
		this->forcedUnduck = true;
	}
}

void HNSMovementService::ReduceDuckSlowdown()
{
	if (!this->player->GetMoveServices()->m_bDucking && this->player->GetMoveServices()->m_flDuckSpeed < DUCK_SPEED_NORMAL - EPSILON)
	{
		this->player->GetMoveServices()->m_flDuckSpeed = DUCK_SPEED_NORMAL;
	}
	else if (this->player->GetMoveServices()->m_flDuckSpeed < DUCK_SPEED_MINIMUM - EPSILON)
	{
		this->player->GetMoveServices()->m_flDuckSpeed = DUCK_SPEED_MINIMUM;
	}
}

void HNSMovementService::UpdateAngleHistory()
{
	CMoveData *mv = this->player->currentMoveData;
	u32 oldEntries = 0;
	FOR_EACH_VEC(this->angleHistory, i)
	{
		if (this->angleHistory[i].when + PS_TURN_RATE_WINDOW < g_pKZUtils->GetGlobals()->curtime)
		{
			oldEntries++;
			continue;
		}
		break;
	}
	this->angleHistory.RemoveMultipleFromHead(oldEntries);
	if ((this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) == 0)
	{
		return;
	}

	AngleHistory *angHist = this->angleHistory.AddToTailGetPtr();
	angHist->when = g_pKZUtils->GetGlobals()->curtime;
	angHist->duration = g_pKZUtils->GetGlobals()->frametime;

	// Not turning if velocity is null.
	if (mv->m_vecVelocity.Length2D() == 0)
	{
		angHist->rate = 0;
		return;
	}

	// Copying from WalkMove
	Vector forward, right, up;
	AngleVectors(mv->m_vecViewAngles, &forward, &right, &up);

	f32 fmove = mv->m_flForwardMove;
	f32 smove = -mv->m_flSideMove;

	if (forward[2] != 0)
	{
		forward[2] = 0;
		forward = g_pKZUtils->NormalizeVector(forward);
	}

	if (right[2] != 0)
	{
		right[2] = 0;
		right = g_pKZUtils->NormalizeVector(right);
	}

	Vector wishdir;
	for (int i = 0; i < 2; i++)
	{
		wishdir[i] = forward[i] * fmove + right[i] * smove;
	}
	wishdir[2] = 0;

	wishdir = g_pKZUtils->NormalizeVector(wishdir);

	if (wishdir.Length() == 0)
	{
		angHist->rate = 0;
		return;
	}

	Vector velocity = mv->m_vecVelocity;
	velocity[2] = 0;
	velocity = g_pKZUtils->NormalizeVector(velocity);
	QAngle accelAngle;
	QAngle velAngle;
	VectorAngles(wishdir, accelAngle);
	VectorAngles(velocity, velAngle);
	accelAngle.y = g_pKZUtils->NormalizeDeg(accelAngle.y);
	velAngle.y = g_pKZUtils->NormalizeDeg(velAngle.y);
	angHist->rate = g_pKZUtils->GetAngleDifference(velAngle.y, accelAngle.y, 180.0, true);
}

void HNSMovementService::CalcPrestrafe()
{
	f32 totalDuration = 0;
	f32 sumWeightedAngles = 0;
	FOR_EACH_VEC(this->angleHistory, i)
	{
		sumWeightedAngles += this->angleHistory[i].rate * this->angleHistory[i].duration;
		totalDuration += this->angleHistory[i].duration;
	}
	f32 averageRate;
	if (totalDuration == 0)
	{
		averageRate = 0;
	}
	else
	{
		averageRate = sumWeightedAngles / totalDuration;
	}

	f32 rewardRate = Clamp(fabs(averageRate) / PS_MAX_REWARD_RATE, 0.0f, 1.0f) * g_pKZUtils->GetGlobals()->frametime;
	f32 punishRate = 0.0f;
	if (this->player->landingTime + PS_LANDING_GRACE_PERIOD < g_pKZUtils->GetGlobals()->curtime)
	{
		punishRate = g_pKZUtils->GetGlobals()->frametime * PS_DECREMENT_RATIO;
	}

	bool perfEligible = g_pKZUtils->GetGlobals()->curtime - g_pKZUtils->GetGlobals()->frametime - this->player->landingTime <= BH_PERF_WINDOW
						&& !this->player->possibleLadderHop;

	if ((this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND) && !perfEligible)
	{
		// Prevent instant full pre from crouched prestrafe.
		Vector velocity;
		this->player->GetVelocity(&velocity);
		f32 speed = Clamp(velocity.Length2D(), 0.0f, SPEED_NORMAL);

		f32 currentPreRatio;
		if (speed <= 0.0f)
		{
			currentPreRatio = 0.0f;
		}
		else
		{
			currentPreRatio = pow(this->bonusSpeed / PS_SPEED_MAX * SPEED_NORMAL / speed, 1 / PS_RATIO_TO_SPEED) * PS_MAX_PS_TIME;
		}

		this->leftPreRatio = MIN(this->leftPreRatio, currentPreRatio);
		this->rightPreRatio = MIN(this->rightPreRatio, currentPreRatio);

		this->leftPreRatio += averageRate > PS_MIN_REWARD_RATE ? rewardRate : -punishRate;
		this->rightPreRatio += averageRate < -PS_MIN_REWARD_RATE ? rewardRate : -punishRate;
		this->leftPreRatio = Clamp(leftPreRatio, 0.0f, PS_MAX_PS_TIME);
		this->rightPreRatio = Clamp(rightPreRatio, 0.0f, PS_MAX_PS_TIME);
		this->bonusSpeed = this->GetPrestrafeGain() / SPEED_NORMAL * speed;
	}
	else
	{
		rewardRate = g_pKZUtils->GetGlobals()->frametime;
		// Raise both left and right pre to the same value as the player is in the air.
		if (this->leftPreRatio < this->rightPreRatio)
		{
			this->leftPreRatio = Clamp(this->leftPreRatio + rewardRate, 0.0f, rightPreRatio);
		}
		else
		{
			this->rightPreRatio = Clamp(this->rightPreRatio + rewardRate, 0.0f, leftPreRatio);
		}
	}
}

f32 HNSMovementService::GetPrestrafeGain()
{
	return PS_SPEED_MAX * pow(MAX(this->leftPreRatio, this->rightPreRatio) / PS_MAX_PS_TIME, PS_RATIO_TO_SPEED);
}

void HNSMovementService::CheckVelocityQuantization()
{
	if (this->postProcessMovementZSpeed > this->player->currentMoveData->m_vecVelocity.z
		&& this->postProcessMovementZSpeed - this->player->currentMoveData->m_vecVelocity.z < 0.03125f
		// Colliding with a flat floor can result in a velocity of +0.0078125u/s, and this breaks ladders.
		// The quantization accidentally fixed this bug...
		&& fabs(this->player->currentMoveData->m_vecVelocity.z) > 0.03125f)
	{
		this->player->currentMoveData->m_vecVelocity.z = this->postProcessMovementZSpeed;
	}
}

// ORIGINAL AUTHORS : Mev & Blacky
// URL: https://forums.alliedmods.net/showthread.php?p=2322788
void HNSMovementService::SlopeFix()
{
	CTraceFilterPlayerMovementCS filter(this->player->GetPlayerPawn());

	Vector ground = this->player->currentMoveData->m_vecAbsOrigin;
	ground.z -= 2;

	// HNS: read the enforced value instead of the live cvar (same number, no cvar lookup).
	f32 standableZ = hns::cvars::GetFloat(HNSCVAR_SV_STANDABLE_NORMAL);

	bbox_t bounds;
	this->player->GetBBoxBounds(&bounds);
	trace_t trace;

	INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), this->player->currentMoveData->m_vecAbsOrigin, ground, &filter, &trace);

	// Doesn't hit anything, fall back to the original ground
	if (trace.m_bStartInSolid || trace.m_flFraction == 1.0f)
	{
		return;
	}

	if (standableZ <= trace.m_vHitNormal.z && trace.m_vHitNormal.z < 1.0f)
	{
		// Copy the ClipVelocity function from sdk2013
		float backoff;
		float change;
		Vector newVelocity;

		backoff = DotProduct(this->player->landingVelocity, trace.m_vHitNormal) * 1;

		for (u32 i = 0; i < 3; i++)
		{
			change = trace.m_vHitNormal[i] * backoff;
			newVelocity[i] = this->player->landingVelocity[i] - change;
		}

		f32 adjust = DotProduct(newVelocity, trace.m_vHitNormal);
		if (adjust < 0.0f)
		{
			newVelocity -= (trace.m_vHitNormal * adjust);
		}
		// Make sure the player is going down a ramp by checking if they actually will gain speed from the boost.
		if (newVelocity.Length2D() >= this->player->landingVelocity.Length2D())
		{
			this->player->currentMoveData->m_vecVelocity.x = newVelocity.x;
			this->player->currentMoveData->m_vecVelocity.y = newVelocity.y;
			this->player->landingVelocity.x = newVelocity.x;
			this->player->landingVelocity.y = newVelocity.y;
		}
	}
}

void HNSMovementService::OnTryPlayerMove(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing)
{
	tracingMovementService = this;
	this->inTryPlayerMove = true;
	this->stuckTraceCount = 0;
	// The rest of the path is added by FixTryPlayerMoveTrace.
	Vector origin;
	this->player->GetOrigin(&origin);
	this->tpmTriggerFixOrigins.RemoveAll();
	this->tpmTriggerFixOrigins.AddToTail(origin);

	// WalkMove does the first trace before calling TryPlayerMove, so we need to fix that one here.
	// TryPlayerMove only uses it if it ends where its own first trace would end.
	if (pFirstDest && pFirstTrace && (this->player->GetPlayerPawn()->m_fFlags & FL_ONGROUND))
	{
		Vector velocity, end;
		this->player->GetVelocity(&velocity);
		VectorMA(origin, g_pKZUtils->GetGlobals()->frametime, velocity, end);
		if (end == *pFirstDest)
		{
			bbox_t bounds;
			this->player->GetBBoxBounds(&bounds);
			CTraceFilterPlayerMovementCS filter(this->player->GetPlayerPawn());
			this->FixTryPlayerMoveTrace(Ray_t(bounds.mins, bounds.maxs), origin, end, &filter, pFirstTrace);
		}
	}
}

void HNSMovementService::OnTryPlayerMovePost(Vector *pFirstDest, trace_t *pFirstTrace, bool *bIsSurfing)
{
	tracingMovementService = nullptr;
	this->inTryPlayerMove = false;
	// HNS: CS2KZ touches triggers along tpmTriggerFixOrigins here while air moving (triggerfix). Not ported yet.
}

void HNSMovementService::OnCategorizePosition(bool bStayOnGround)
{
	tracingMovementService = this;
}

void HNSMovementService::OnCategorizePositionPost(bool bStayOnGround)
{
	tracingMovementService = nullptr;
}

void HNSMovementService::OnTracePlayerBBoxPost(const Ray_t &ray, const Vector &start, const Vector &end, CTraceFilter *filter, trace_t *pm)
{
	if (this->inTryPlayerMove)
	{
		this->FixTryPlayerMoveTrace(ray, start, end, filter, pm);
	}
	else
	{
		this->FixGroundTrace(ray, start, end, filter, pm);
	}
}

/*
	Rampbug fix:
	The game keeps the player 1/32 unit away from whatever they collide with.
	While sliding on a ramp, the player can end up closer to the ramp than that, or collide with the edge between two triangles of the ramp
	instead of the ramp itself, which gives a wrong normal. TryPlayerMove then either stops the player or sends them in a random direction.

	To fix this, we redo the trace a bit further away from the ramp. We only use that trace if it actually goes further than the original one.
	If it just ends up on another plane right away, the player is really running into a corner and the original trace is kept.

	HNS: RAMP_BUG_THRESHOLD, RAMP_BUG_SAME_PLANE and RAMP_BUG_MIN_DISTANCE come from hns-movement.cfg so they can be retuned
	for sv_airaccelerate 1000. Defaults are the CS2KZ values.
*/
void HNSMovementService::FixTryPlayerMoveTrace(const Ray_t &ray, const Vector &start, const Vector &end, CTraceFilter *filter, trace_t *pm)
{
	const f32 RAMP_BUG_THRESHOLD = settings.rampbugThreshold;
	const f32 RAMP_BUG_SAME_PLANE = settings.rampbugSamePlane;
	const f32 RAMP_BUG_MIN_DISTANCE = settings.rampbugMinDistance;

	Vector direction = end - start;
	f32 length = direction.NormalizeInPlace();
	// We hit a plane that will change our velocity, or we barely moved at all. This is either a real obstacle or a rampbug.
	bool shouldConsiderRampbug = pm->m_flFraction < 1.0f && !pm->m_bStartInSolid && this->lastPlane.LengthSqr() > 0.0f && length > 0.0f
								 && (pm->m_vHitNormal.Dot(this->lastPlane) < RAMP_BUG_THRESHOLD || pm->m_flFraction * length < RAMP_BUG_OFFSET);
	if (shouldConsiderRampbug)
	{
		bool success {};
		// We already failed to move twice, so TryPlayerMove is about to stop the player. Use any trace that goes further.
		bool stuck = this->stuckTraceCount >= 2;
		// This can only be a rampbug if the player is still right next to the last plane they hit.
		trace_t test;
		INavPhysicsInterface::TraceShape(ray, pm->m_vEndPos + this->lastPlane * RAMP_BUG_OFFSET,
										 pm->m_vEndPos - this->lastPlane * (RAMP_BUG_OFFSET * 2.0f), filter, &test);
		if (test.m_flFraction < 1.0f && !test.m_bStartInSolid && test.m_vHitNormal.Dot(this->lastPlane) >= RAMP_BUG_THRESHOLD)
		{
			// Redo the trace a bit further away from the plane.
			Vector offset = this->lastPlane * RAMP_BUG_OFFSET;
			trace_t offsetTrace;
			INavPhysicsInterface::TraceShape(ray, start + offset, end + offset, filter, &offsetTrace);
			// If this trace only goes a bit further and then hits a different plane, the original trace hit something real,
			// like a corner or two ramps meeting each other. Keep the original so TryPlayerMove can slide along both planes.
			f32 distance = (offsetTrace.m_vEndPos - pm->m_vEndPos).Dot(direction);
			bool samePlane = offsetTrace.m_vHitNormal.Dot(this->lastPlane) >= RAMP_BUG_SAME_PLANE;
			success = !offsetTrace.m_bStartInSolid && distance > RAMP_BUG_OFFSET
					  && (stuck || offsetTrace.m_flFraction == 1.0f || distance > RAMP_BUG_MIN_DISTANCE || samePlane);
			if (success)
			{
				*pm = offsetTrace;
			}
		}
		if (!success && stuck && pm->m_flFraction * length < RAMP_BUG_OFFSET)
		{
			// The player is stuck between two planes, moving away from one of them will just push them into the other one.
			// A trace only gives us one of the two planes, so we find the other one by tracing away from the first.
			Vector normal = pm->m_vHitNormal;
			Vector otherNormal = vec3_origin;
			if (normal.Dot(this->lastPlane) < RAMP_BUG_THRESHOLD)
			{
				otherNormal = this->lastPlane;
			}
			else
			{
				INavPhysicsInterface::TraceShape(ray, start, start + normal * (RAMP_BUG_OFFSET * 2.0f), filter, &test);
				if (test.m_flFraction < 1.0f && !test.m_bStartInSolid && test.m_vHitNormal.Dot(normal) < RAMP_BUG_THRESHOLD)
				{
					otherNormal = test.m_vHitNormal;
				}
			}
			if (otherNormal.LengthSqr() > 0.0f && normal.Dot(otherNormal) > -0.99f)
			{
				// Move away from both planes at once.
				Vector offset = normal + otherNormal;
				offset.NormalizeInPlace();
				offset *= MIN(RAMP_BUG_OFFSET / normal.Dot(offset), 1.0f);
				trace_t offsetTrace;
				INavPhysicsInterface::TraceShape(ray, start + offset, end + offset, filter, &offsetTrace);
				if (!offsetTrace.m_bStartInSolid && (offsetTrace.m_vEndPos - pm->m_vEndPos).Dot(direction) > RAMP_BUG_OFFSET)
				{
					*pm = offsetTrace;
				}
			}
		}
	}
	this->FixGroundTrace(ray, start, end, filter, pm);
	if (pm->m_flFraction < 1.0f && !pm->m_bStartInSolid)
	{
		if (pm->m_vHitNormal.LengthSqr() > 0.98f)
		{
			this->lastPlane = pm->m_vHitNormal;
		}
		if (pm->m_flFraction * length < RAMP_BUG_OFFSET)
		{
			this->stuckTraceCount++;
		}
	}
	// Triggerfix related
	this->tpmTriggerFixOrigins.AddToTail(pm->m_vEndPos);
}

// Sometimes the game thinks the player is on standable ground while they are actually on a steep slope right below a wall.
// A slightly smaller hull doesn't have this problem and will hit the slope instead.
// It will also hit a slope if the player is standing over a gap between two floors though, so we make sure there's a wall next to the player.
void HNSMovementService::FixGroundTrace(const Ray_t &ray, const Vector &start, const Vector &end, CTraceFilter *filter, trace_t *pm)
{
	f32 standableZ = hns::cvars::GetFloat(HNSCVAR_SV_STANDABLE_NORMAL);
	if (pm->m_flFraction == 1.0f || pm->m_bStartInSolid || pm->m_vHitNormal.z < standableZ)
	{
		return;
	}
	Vector shrink(GROUND_CHECK_SHRINK_SIZE, GROUND_CHECK_SHRINK_SIZE, 0.0f);
	trace_t smallTrace;
	INavPhysicsInterface::TraceShape(Ray_t(ray.m_Hull.m_vMins + shrink, ray.m_Hull.m_vMaxs - shrink), start, end, filter, &smallTrace);
	if (smallTrace.m_flFraction == 1.0f || smallTrace.m_bStartInSolid || smallTrace.m_vHitNormal.z >= standableZ)
	{
		return;
	}
	// Is there a wall on the side that the slope goes up towards?
	Vector wallDirection(-smallTrace.m_vHitNormal.x, -smallTrace.m_vHitNormal.y, 0.0f);
	wallDirection.NormalizeInPlace();
	trace_t wallTrace;
	INavPhysicsInterface::TraceShape(ray, pm->m_vEndPos, pm->m_vEndPos + wallDirection * GROUND_CHECK_SHRINK_SIZE, filter, &wallTrace);
	if (wallTrace.m_flFraction < 0.5f && !wallTrace.m_bStartInSolid)
	{
		pm->m_vHitNormal = smallTrace.m_vHitNormal;
	}
}

void HNSMovementService::OnAirMove()
{
	this->airMoving = true;
	if (settings.psEnabled)
	{
		this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
	}
}

void HNSMovementService::OnAirMovePost()
{
	this->airMoving = false;
	if (settings.psEnabled)
	{
		this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL + this->GetPrestrafeGain();
	}
}

void HNSMovementService::OnWaterMove()
{
	if (settings.psEnabled)
	{
		this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL;
	}
}

void HNSMovementService::OnWaterMovePost()
{
	if (settings.psEnabled)
	{
		this->player->currentMoveData->m_flMaxSpeed = SPEED_NORMAL + this->GetPrestrafeGain();
	}
}

void HNSMovementService::OnTeleport(const Vector *newPosition, const QAngle *newAngles, const Vector *newVelocity)
{
	// Prevent prekeep from teleporting the player to a lower speed.
	if (newVelocity)
	{
		f32 speed = Clamp(newVelocity->Length2D(), 0.0f, SPEED_NORMAL);

		f32 maxPreRatio;
		if (speed <= 0.0f)
		{
			maxPreRatio = 0.0f;
		}
		else
		{
			maxPreRatio = pow(this->bonusSpeed / PS_SPEED_MAX * SPEED_NORMAL / speed, 1 / PS_RATIO_TO_SPEED) * PS_MAX_PS_TIME;
		}

		this->leftPreRatio = MIN(this->leftPreRatio, maxPreRatio);
		this->rightPreRatio = MIN(this->rightPreRatio, maxPreRatio);
		this->leftPreRatio = Clamp(leftPreRatio, 0.0f, PS_MAX_PS_TIME);
		this->rightPreRatio = Clamp(rightPreRatio, 0.0f, PS_MAX_PS_TIME);
		this->bonusSpeed = this->GetPrestrafeGain() / SPEED_NORMAL * speed;
	}
	if (!this->player->processingMovement)
	{
		return;
	}
	// Only happens when triggerfix happens.
	if (newPosition)
	{
		this->player->currentMoveData->m_vecAbsOrigin = *newPosition;
	}
	if (newVelocity)
	{
		this->player->currentMoveData->m_vecVelocity = *newVelocity;
	}
}

void HNSMovementService::OnChangeMoveType(MoveType_t oldMoveType)
{
	// Ladder enter/exit is reported from inside LadderMove; those must behave exactly like CS2KZ, so leave them alone.
	if (this->player->processingMovement)
	{
		return;
	}
	// HNS: the movetype was changed outside movement (ChaseMod freeze/unfreeze, admin plugins, respawn).
	// Drop the state that only makes sense for continuous movement, so the first tick afterwards starts clean.
	Vector velocity;
	this->player->GetVelocity(&velocity);
	this->postProcessMovementZSpeed = velocity.z;
	this->lastPlane = vec3_origin;
	this->stuckTraceCount = 0;
	this->hasValidDesiredViewAngle = false;
	this->angleHistory.RemoveAll();
}
