// Jump tracking. Ported from CS2KZ src/kz/jumpstats/kz_jumpstats.cpp (AGPL-3.0); see js_jumpstats.h for what changed.
#include "js_jumpstats.h"
#include "hns/hns_player.h"
#include "hns/hns_cvars.h"
#include "sdk/cinbuttonstate.h"
#include "utils/utils.h"

#include <algorithm>

#include "tier0/memdbgon.h"

// clang-format off

const char *jumpTypeStr[JUMPTYPE_COUNT] = {
	"Long Jump",
	"Bunnyhop",
	"Multi Bunnyhop",
	"Weird Jump",
	"Ladder Jump",
	"Ladderhop",
	"Jumpbug",
	"Fall",
	"Unknown Jump",
	"Invalid Jump"
};

const char *jumpTypeShortStr[JUMPTYPE_COUNT] = {
	"LJ",
	"BH",
	"MBH",
	"WJ",
	"LAJ",
	"LAH",
	"JB",
	"FL",
	"UNK",
	"INV"
};

const char *distanceTierColors[DISTANCETIER_COUNT] = {
	"{grey}",
	"{grey}",
	"{blue}",
	"{green}",
	"{darkred}",
	"{gold}",
	"{orchid}"
};

const char *distanceTierNames[DISTANCETIER_COUNT] = {
	"none",
	"meh",
	"impressive",
	"perfect",
	"godlike",
	"ownage",
	"wrecker"
};

// clang-format on

// CS2KZ reads this from the mode's cvar values. Here it is whatever hns-movement enforces (35 on this server).
static_function f64 AirMaxWishspeed()
{
	return hns::cvars::GetFloat(HNSCVAR_SV_AIR_MAX_WISHSPEED);
}

/*
 * AACall
 */

f32 AACall::CalcIdealYaw(bool useRadians)
{
	f64 accelspeed;
	if (this->wishspeed != 0)
	{
		accelspeed = this->accel * this->wishspeed * this->surfaceFriction * this->duration;
	}
	else
	{
		accelspeed = this->accel * this->maxspeed * this->surfaceFriction * this->duration;
	}
	if (accelspeed <= 0.0)
	{
		return useRadians ? M_PI : RAD2DEG(M_PI);
	}

	if (this->velocityPre.Length2D() == 0.0)
	{
		return 0.0;
	}

	const f64 wishspeedcapped = AirMaxWishspeed();
	f64 tmp = wishspeedcapped - accelspeed;
	if (tmp <= 0.0)
	{
		return useRadians ? M_PI / 2 : RAD2DEG(M_PI / 2);
	}

	f64 speed = this->velocityPre.Length2D();
	if (tmp < speed)
	{
		return useRadians ? acos(tmp / speed) : RAD2DEG(acos(tmp / speed));
	}

	return 0.0;
}

f32 AACall::CalcMinYaw(bool useRadians)
{
	// If your velocity is lower than sv_air_max_wishspeed, any direction will get you gain.
	const f64 wishspeedcapped = AirMaxWishspeed();
	if (this->velocityPre.Length2D() <= wishspeedcapped)
	{
		return 0.0;
	}
	// CS2KZ has a literal 30 here (its sv_air_max_wishspeed); use the enforced value instead.
	f64 ratio = wishspeedcapped / this->velocityPre.Length2D();
	return useRadians ? acos(ratio) : RAD2DEG(acos(ratio));
}

f32 AACall::CalcMaxYaw(bool useRadians)
{
	f32 gamma1, numer, denom;
	gamma1 = AACall::CalcAccelSpeed(true);
	f32 speed = this->velocityPre.Length2D();
	const f64 wishspeedcapped = AirMaxWishspeed();
	if (gamma1 <= 2 * wishspeedcapped)
	{
		numer = -gamma1;
		denom = 2 * speed;
	}
	else
	{
		numer = -wishspeedcapped;
		denom = speed;
	}
	if (denom < fabs(numer))
	{
		return this->CalcIdealYaw();
	}

	return useRadians ? acos(numer / denom) : RAD2DEG(acos(numer / denom));
}

f32 AACall::CalcAccelSpeed(bool tryMaxSpeed)
{
	if (tryMaxSpeed && this->wishspeed == 0)
	{
		return this->accel * this->maxspeed * this->surfaceFriction * this->duration;
	}
	return this->accel * this->wishspeed * this->surfaceFriction * this->duration;
}

f32 AACall::CalcIdealGain()
{
	// sqrt(v^2+a^2+2*v*a*cos(yaw)
	const f32 wishspeedcapped = AirMaxWishspeed();
	const f32 a = Min(this->CalcAccelSpeed(true), wishspeedcapped);
	f32 idealSpeed = sqrt(this->velocityPre.Length2DSqr() + a * a + 2 * a * this->velocityPre.Length2D() * cos(this->CalcIdealYaw(true)));
	return idealSpeed - this->velocityPre.Length2D();
}

/*
 * Strafe
 */

void Strafe::UpdateCollisionVelocityChange(f32 delta)
{
	if (delta < 0.0f)
	{
		this->collisionLoss -= delta;
	}
	else
	{
		this->collisionGain += delta;
	}
}

void Strafe::End()
{
	for (AACall &call : this->aaCalls)
	{
		this->duration += call.duration;
		// Calculate BA/DA/OL
		if (call.wishspeed == 0)
		{
			if ((CInButtonState::IsButtonPressed(call.buttons, IN_FORWARD, true) && CInButtonState::IsButtonPressed(call.buttons, IN_BACK, true))
				|| (CInButtonState::IsButtonPressed(call.buttons, IN_MOVELEFT, true)
					&& CInButtonState::IsButtonPressed(call.buttons, IN_MOVERIGHT, true)))
			{
				this->overlap += call.duration;
			}
			else
			{
				this->deadAir += call.duration;
			}
		}
		else if ((call.velocityPost - call.velocityPre).Length2D() <= JS_EPSILON)
		{
			// This gain could just be from quantized float stuff.
			this->badAngles += call.duration;
		}
		// Calculate sync.
		else if (call.velocityPost.Length2D() - call.velocityPre.Length2D() > JS_EPSILON)
		{
			this->syncDuration += call.duration;
		}

		// Gain/loss.
		this->maxGain += call.CalcIdealGain();
		f32 speedDiff = call.velocityPost.Length2D() - call.velocityPre.Length2D();
		if (speedDiff > 0)
		{
			this->airGain += speedDiff;
		}
		else
		{
			this->airLoss += speedDiff;
		}
		if (call.externalSpeedDiff > 0)
		{
			this->externalGain += call.externalSpeedDiff;
		}
		else
		{
			this->externalLoss += call.externalSpeedDiff;
		}
		this->width += fabs(utils::GetAngleDifference(call.currentYaw, call.prevYaw, 180.0f));
	}
}

/*
 * Jump
 */

void Jump::Init()
{
	this->takeoffOrigin = this->player->takeoffOrigin;
	this->adjustedTakeoffOrigin = this->player->takeoffGroundOrigin;
	this->takeoffVelocity = this->player->takeoffVelocity;
	this->jumpType = this->player->jumpstats->DetermineJumpType();

	// W release tracking.
	f32 lastPressedTime = this->player->jumpstats->lastWPressedTime;
	this->release = lastPressedTime - g_pKZUtils->GetGlobals()->curtime;
	if (this->jumpType != JumpType_Fall)
	{
		if (this->jumpType == JumpType_WeirdJump)
		{
			this->trackingRelease = false;
			this->release = this->player->jumpstats->GetLastJumpRelease();
		}
		else
		{
			this->release += g_pKZUtils->GetGlobals()->frametime;
		}
	}
	// We only track release if we are still holding W/S.
	if (this->release >= 0 && this->trackingRelease)
	{
		this->release = 0;
	}
}

void Jump::UpdateAACallPost(Vector wishdir, f32 wishspeed, f32 accel)
{
	// Use the latest parameters, just in case they changed.
	Strafe *strafe = this->GetCurrentStrafe();
	if (strafe->aaCalls.empty())
	{
		return;
	}
	AACall *call = &strafe->aaCalls.back();
	QAngle currentAngle;
	this->player->GetAngles(&currentAngle);
	call->startFraction = this->player->currentMoveData->m_flSubtickStartFraction;
	call->endFraction = this->player->currentMoveData->m_flSubtickEndFraction;
	call->maxspeed = this->player->currentMoveData->m_flMaxSpeed;
	call->currentYaw = currentAngle.y;
	call->wishdir = wishdir;
	call->wishspeed = wishspeed;
	call->accel = accel;
	call->surfaceFriction = this->player->GetMoveServices()->m_flSurfaceFriction();
	call->duration = MAX(call->endFraction - call->startFraction, 0.0f) * ENGINE_FIXED_TICK_INTERVAL;
	call->ducking = this->player->GetMoveServices()->m_bDucked;
	this->player->GetVelocity(&call->velocityPost);

	// AA only applies at most half of the acceleration to the velocity after AACall.
	// The rest is applied in TryPlayerMove, but let's just add it here for simplicity.
	call->velocityPost += this->player->currentMoveData->m_vecFrameVelocityDelta;
	strafe->UpdateStrafeMaxSpeed(call->velocityPost.Length2D());

	// Check if we are still tracking release for the jump.
	if (this->trackingRelease)
	{
		if (this->player->currentMoveData->m_flForwardMove > 0)
		{
			this->release += g_pKZUtils->GetGlobals()->frametime;
		}
		else
		{
			this->trackingRelease = false;
		}
	}
}

void Jump::Update()
{
	// Teleports and movetype changes can end a jump outside movement processing, where the move data is not current.
	if (this->ended || !this->player->processingMovement || !this->player->currentMoveData)
	{
		return;
	}
	this->totalDistance += (this->player->currentMoveData->m_vecAbsOrigin - this->player->moveDataPre.m_vecAbsOrigin).Length2D();
	this->currentMaxSpeed = MAX(this->player->currentMoveData->m_vecVelocity.Length2D(), this->currentMaxSpeed);
	this->currentMaxHeight = MAX(this->player->currentMoveData->m_vecAbsOrigin.z - this->adjustedTakeoffOrigin.z, this->currentMaxHeight);
}

void Jump::End()
{
	this->Update();
	if (!this->strafes.empty())
	{
		this->strafes.back().End();
	}
	this->landingOrigin = this->player->landingOrigin;
	this->adjustedLandingOrigin = this->player->landingOriginActual;
	// This is not the real jump duration, it's just here to calculate sync.
	f32 jumpDuration = 0.0f;

	f32 gain = 0.0f;
	f32 maxGain = 0.0f;
	for (Strafe &strafe : this->strafes)
	{
		for (AACall &call : strafe.aaCalls)
		{
			if (call.ducking)
			{
				this->duckDuration += call.duration;
				this->duckEndDuration += call.duration;
			}
			else
			{
				this->duckEndDuration = 0.0f;
			}
		}
		this->width += strafe.width;
		this->overlap += strafe.overlap;
		this->deadAir += strafe.deadAir;
		this->badAngles += strafe.badAngles;
		this->sync += strafe.syncDuration;
		jumpDuration += strafe.duration;
		gain += strafe.GetGain();
		maxGain += strafe.maxGain;
	}
	if (!this->strafes.empty())
	{
		this->width /= this->strafes.size();
	}
	if (jumpDuration > 0.0f)
	{
		this->overlap /= jumpDuration;
		this->deadAir /= jumpDuration;
		this->badAngles /= jumpDuration;
		this->sync /= jumpDuration;
	}
	this->gainEff = maxGain != 0.0f ? gain / maxGain : 0.0f;
	this->ended = true;

	// Block/edge traces only for a real landing inside movement processing. A jump ended by a teleport, a team or
	// spectator change or a movetype change outside movement may have a pawn that is being removed.
	if (this->valid && this->player->processingMovement && this->player->GetPlayerPawn())
	{
		this->EndBlockDistance();
	}

	// If there's no air time at all then that was definitely not a jump.
	// Happens when player touches the ground from a ladder.
	if (jumpDuration == 0.0f)
	{
		this->jumpType = JumpType_FullInvalid;
	}
	else
	{
		bool exceededAirtimeCap = false;
		switch (this->jumpType)
		{
			case JumpType_LadderJump:
				exceededAirtimeCap = jumpDuration > 1.04;
				break;
			case JumpType_LongJump:
			case JumpType_Bhop:
			case JumpType_MultiBhop:
			case JumpType_WeirdJump:
			case JumpType_Ladderhop:
			case JumpType_Jumpbug:
				exceededAirtimeCap = jumpDuration > 0.8;
				break;
			default:
				break;
		}
		if (exceededAirtimeCap)
		{
			this->valid = false;
			this->jumpType = JumpType_Invalid;
		}
	}
	this->airtime = this->player->landingTimeActual - this->player->takeoffTime;
}

Strafe *Jump::GetCurrentStrafe()
{
	TurnState turning = this->player->GetTurning();
	// Always start with 1 strafe.
	if (this->strafes.empty())
	{
		this->strafes.emplace_back();
		this->strafes.back().turnstate = turning;
	}
	// If the player isn't turning, update the turn state until it changes.
	else if (!this->strafes.back().turnstate)
	{
		this->strafes.back().turnstate = turning;
	}
	// Otherwise, if the strafe is in opposite direction, we add a new strafe.
	else if (this->strafes.back().turnstate == -turning)
	{
		// Finish the previous strafe before adding a new strafe.
		this->strafes.back().End();
		this->strafes.emplace_back();
		this->strafes.back().turnstate = turning;
	}
	return &this->strafes.back();
}

f32 Jump::GetDistance(bool useDistbugFix, bool disableAddDist)
{
	f32 dist = 32.0f;
	if (this->jumpType == JumpType_LadderJump || disableAddDist)
	{
		dist = 0.0f;
	}
	if (useDistbugFix)
	{
		dist += (this->adjustedLandingOrigin - this->adjustedTakeoffOrigin).Length2D();
	}
	else
	{
		dist += (this->landingOrigin - this->takeoffOrigin).Length2D();
	}
	return dist;
}

f32 Jump::GetAirPath()
{
	f32 distance = this->GetDistance(false, true);
	if (this->totalDistance <= 0.0f || distance <= 0.0f)
	{
		return 0.0f;
	}
	return this->totalDistance / distance;
}

f32 Jump::GetDeviation()
{
	f32 distanceX = fabs(adjustedLandingOrigin.x - adjustedTakeoffOrigin.x);
	f32 distanceY = fabs(adjustedLandingOrigin.y - adjustedTakeoffOrigin.y);
	return distanceX > distanceY ? distanceY : distanceX;
}

/*
 * HNSJumpstatsService
 */

JumpType HNSJumpstatsService::DetermineJumpType()
{
	if (this->jumps.size() <= 1 || this->JustTeleported())
	{
		return JumpType_Invalid;
	}
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!pawn)
	{
		return JumpType_Invalid;
	}
	if (this->player->takeoffFromLadder)
	{
		f32 ignoreLadderJumpTime = pawn->m_ignoreLadderJumpTime();

		bool ignoringLadder = ignoreLadderJumpTime > g_pKZUtils->GetGlobals()->curtime - ENGINE_FIXED_TICK_INTERVAL;
		bool holdingJumpDuringIgnoreLadderPeriod = this->lastJumpButtonTime > ignoreLadderJumpTime - IGNORE_JUMP_TIME
												   && this->lastJumpButtonTime < ignoreLadderJumpTime + ENGINE_FIXED_TICK_INTERVAL;

		if (ignoringLadder && holdingJumpDuringIgnoreLadderPeriod)
		{
			return JumpType_Invalid;
		}
		if (this->player->jumped)
		{
			if (this->player->GetMoveServices()->m_vecLadderNormal().z > EPSILON || !this->player->possibleLadderHop
				|| this->player->takeoffOrigin.z - this->player->takeoffGroundOrigin.z > JS_EPSILON * 2)
			{
				return JumpType_Invalid;
			}
			// Mark this as a ladder hop, so if the player jumps again, we don't add a new jump.
			this->ladderHopThisMove = true;
			return JumpType_Ladderhop;
		}
		if (fabs(this->player->GetMoveServices()->m_vecLadderNormal().z) > EPSILON)
		{
			return JumpType_Invalid;
		}
		return JumpType_LadderJump;
	}
	if (!this->player->jumped)
	{
		return JumpType_Fall;
	}
	if (this->jumps.size() >= 2)
	{
		Jump &previousJump = this->jumps[this->jumps.size() - 2];
		if (this->player->duckBugged)
		{
			if (previousJump.IsValid() && previousJump.GetOffset() < JS_EPSILON && previousJump.jumpType == JumpType_LongJump)
			{
				return JumpType_Jumpbug;
			}
			return JumpType_Invalid;
		}
		if (this->HitBhop() && !this->HitDuckbugRecently())
		{
			// Check for no offset
			if (previousJump.hitHead || !previousJump.IsValid())
			{
				return JumpType_Invalid;
			}
			if (fabs(previousJump.GetOffset()) < JS_EPSILON)
			{
				switch (previousJump.jumpType)
				{
					case JumpType_LongJump:
						return JumpType_Bhop;
					case JumpType_Bhop:
					case JumpType_MultiBhop:
						return JumpType_MultiBhop;
					default:
						return JumpType_Other;
				}
			}
			// Check for weird jump
			if (previousJump.jumpType == JumpType_Fall && this->ValidWeirdJumpDropDistance())
			{
				return JumpType_WeirdJump;
			}
			return JumpType_Other;
		}
	}
	if (this->HitDuckbugRecently() || !this->GroundSpeedCappedRecently())
	{
		return JumpType_Invalid;
	}
	return JumpType_LongJump;
}

f32 HNSJumpstatsService::GetLastJumpRelease()
{
	if (this->jumps.size() <= 2)
	{
		return 0.0f;
	}
	return this->jumps[this->jumps.size() - 2].release;
}

void HNSJumpstatsService::Reset()
{
	this->jumps.clear();
	this->lastJumpButtonTime = {};
	this->lastNoclipTime = {};
	this->lastDuckbugTime = {};
	this->lastGroundSpeedCappedTime = {};
	this->lastMovementProcessedTime = {};
	this->lastTeleportTime = {};
	this->tpmVelocity = Vector(0, 0, 0);
	this->possibleEdgebug = {};
	this->lastWPressedTime = {};
	this->ladderHopThisMove = false;
	this->reporting = true;
	this->sounds = true;
}

void HNSJumpstatsService::OnProcessMovement()
{
	// Always ensure that the player has at least an ongoing jump.
	// This is mostly to prevent crash, it's not a valid jump.
	if (this->jumps.empty())
	{
		this->AddJump();
		this->InvalidateJumpstats("First jump");
		return;
	}
	this->CheckValidMoveType();
	this->DetectExternalModifications();
	this->DetectGhost();

	if (this->player->currentMoveData->m_flForwardMove > 0)
	{
		this->lastWPressedTime = g_pKZUtils->GetGlobals()->curtime;
	}
}

void HNSJumpstatsService::OnChangeMoveType(MoveType_t oldMoveType)
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!pawn)
	{
		return;
	}
	if (oldMoveType == MOVETYPE_LADDER && pawn->m_MoveType() == MOVETYPE_WALK)
	{
		this->AddJump();
	}
	else if (oldMoveType == MOVETYPE_WALK && pawn->m_MoveType() == MOVETYPE_LADDER)
	{
		// Not really a valid jump for jumpstats purposes.
		this->InvalidateJumpstats("Invalid movetype change");
		this->EndJump();
	}
	else if (pawn->m_MoveType() != MOVETYPE_WALK && pawn->m_MoveType() != MOVETYPE_LADDER)
	{
		// Died, went spectator, frozen, noclip...: close the current jump without computing anything.
		if (!this->jumps.empty() && !this->jumps.back().ended)
		{
			this->jumps.back().Invalidate("Invalid movetype");
			this->jumps.back().ended = true;
		}
	}
}

bool HNSJumpstatsService::HitBhop()
{
	return this->player->takeoffTime - this->player->landingTime < JS_MAX_BHOP_GROUND_TIME;
}

bool HNSJumpstatsService::HitDuckbugRecently()
{
	return g_pKZUtils->GetGlobals()->curtime - this->lastDuckbugTime <= JS_MAX_DUCKBUG_RESET_TIME;
}

bool HNSJumpstatsService::ValidWeirdJumpDropDistance()
{
	if (this->jumps.size() <= 2)
	{
		return false;
	}
	return this->jumps[this->jumps.size() - 2].GetOffset() > -1 * JS_MAX_WEIRDJUMP_FALL_OFFSET;
}

bool HNSJumpstatsService::GroundSpeedCappedRecently()
{
	return this->lastGroundSpeedCappedTime == this->lastMovementProcessedTime;
}

bool HNSJumpstatsService::JustTeleported()
{
	return g_pKZUtils->GetServerGlobals()->curtime - this->lastTeleportTime < JS_RECENT_TELEPORT_THRESHOLD;
}

void HNSJumpstatsService::OnAirAccelerate()
{
	if (g_pKZUtils->GetGlobals()->frametime == 0.0f || this->jumps.empty())
	{
		return;
	}
	AACall call;
	this->player->GetMoveServices()->m_nButtons().GetButtons(call.buttons);
	this->player->GetVelocity(&call.velocityPre);

	// moveDataPost is still the movedata from last tick.
	call.externalSpeedDiff = call.velocityPre.Length2D() - this->player->moveDataPost.m_vecVelocity.Length2D();
	call.prevYaw = this->player->oldAngles.y;
	Strafe *strafe = this->jumps.back().GetCurrentStrafe();
	strafe->aaCalls.push_back(call);
}

void HNSJumpstatsService::OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel)
{
	if (g_pKZUtils->GetGlobals()->frametime == 0.0f || this->jumps.empty())
	{
		return;
	}
	this->jumps.back().UpdateAACallPost(wishdir, wishspeed, accel);
}

void HNSJumpstatsService::AddJump()
{
	if (this->player->IsFakeClient() || this->ladderHopThisMove)
	{
		return;
	}
	// Only the previous jump is ever looked at (and the one before it for weird jumps), so keep three at most.
	while (this->jumps.size() > 2)
	{
		this->jumps.erase(this->jumps.begin());
	}
	this->jumps.emplace_back(this->player);
	this->jumps.back().Init();
}

void HNSJumpstatsService::UpdateJump()
{
	if (this->jumps.empty())
	{
		return;
	}
	this->jumps.back().Update();
	this->DetectInvalidCollisions();
	this->DetectInvalidGains();
	this->DetectNoclip();
}

void HNSJumpstatsService::EndJump()
{
	if (this->jumps.empty())
	{
		return;
	}
	Jump *jump = &this->jumps.back();
	// Prevent stats being calculated twice.
	if (jump->ended)
	{
		return;
	}
	jump->End();
	if (jump->jumpType == JumpType_FullInvalid)
	{
		return;
	}
	hns::jumpstats::AnnounceJump(jump);
}

void HNSJumpstatsService::OnTeleport()
{
	this->HandleTeleport();
	this->lastTeleportTime = g_pKZUtils->GetServerGlobals()->curtime;
}

void HNSJumpstatsService::HandleTeleport()
{
	if (this->jumps.empty())
	{
		return;
	}
	Jump *jump = &this->jumps.back();
	if (jump->ended)
	{
		return;
	}

	// Outside movement processing (spectator / team change, respawn, plugins, disconnect) nothing is computed or
	// reported: the jump is simply closed as invalid.
	if (!this->player->processingMovement || !this->player->currentMoveData || !this->player->GetPlayerPawn())
	{
		jump->Invalidate("Teleported");
		jump->ended = true;
		return;
	}

	Vector preTeleportOrigin;
	this->player->GetOrigin(&preTeleportOrigin);

	// Teleports are not real landings. Use the pre-teleport origin/time as a synthetic landing snapshot.
	this->player->landingOrigin = preTeleportOrigin;
	this->player->landingOriginActual = preTeleportOrigin;
	this->player->landingTime = this->player->GetCurtime();
	this->player->landingTimeServer = g_pKZUtils->GetServerGlobals()->curtime;
	this->player->landingTimeActual = this->player->landingTime;
	if (this->player->currentMoveData)
	{
		this->player->landingVelocity = this->player->currentMoveData->m_vecVelocity;
	}

	jump->Invalidate("Teleported");
	this->EndJump();
}

void HNSJumpstatsService::InvalidateJumpstats(const char *reason)
{
	if (!this->jumps.empty() && !this->jumps.back().ended)
	{
		this->jumps.back().Invalidate(reason);
	}
}

void HNSJumpstatsService::TrackJumpstatsVariables()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (this->player->IsButtonPressed(IN_JUMP))
	{
		this->lastJumpButtonTime = g_pKZUtils->GetGlobals()->curtime;
	}
	if (pawn && (pawn->m_MoveType == MOVETYPE_NOCLIP || pawn->m_nActualMoveType == MOVETYPE_NOCLIP))
	{
		this->lastNoclipTime = g_pKZUtils->GetGlobals()->curtime;
	}
	if (this->player->duckBugged)
	{
		this->lastDuckbugTime = g_pKZUtils->GetGlobals()->curtime;
	}
	if (this->player->walkMoved)
	{
		this->lastGroundSpeedCappedTime = g_pKZUtils->GetGlobals()->curtime;
	}
	this->lastMovementProcessedTime = g_pKZUtils->GetGlobals()->curtime;
}

void HNSJumpstatsService::OnTryPlayerMove()
{
	this->tpmVelocity = this->player->currentMoveData->m_vecVelocity;
}

void HNSJumpstatsService::OnTryPlayerMovePost()
{
	if (this->jumps.empty() || this->jumps.back().strafes.empty())
	{
		return;
	}
	f32 velocity = this->player->currentMoveData->m_vecVelocity.Length2D() - this->tpmVelocity.Length2D();
	this->jumps.back().strafes.back().UpdateCollisionVelocityChange(velocity);
	this->DetectEdgebug();

	FOR_EACH_VEC(this->player->currentMoveData->m_TouchList, i)
	{
		if (fabs(this->player->currentMoveData->m_TouchList[i].trace.m_vHitNormal.x) > JS_EPSILON
			|| fabs(this->player->currentMoveData->m_TouchList[i].trace.m_vHitNormal.y) > JS_EPSILON)
		{
			this->InvalidateJumpstats("Invalid collision");
		}
	}
}

void HNSJumpstatsService::OnProcessMovementPost()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (this->possibleEdgebug && pawn && !(pawn->m_fFlags() & FL_ONGROUND))
	{
		this->InvalidateJumpstats("Edgebugged");
	}
	this->possibleEdgebug = false;
	this->ladderHopThisMove = false;
	this->TrackJumpstatsVariables();
	this->DetectWater();
}
