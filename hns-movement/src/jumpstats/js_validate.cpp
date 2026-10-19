// Jump validation. Ported from CS2KZ src/kz/jumpstats/jump_validating.cpp (AGPL-3.0), plus the Redie ghost check.
#include "js_jumpstats.h"
#include "hns/hns_player.h"
#include "utils/utils.h"

#include "tier0/memdbgon.h"

void HNSJumpstatsService::CheckValidMoveType()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	// Invalidate jumpstats if movetype is invalid (this also covers ChaseMod / hns-movement's frozen CTs).
	if (!pawn || (pawn->m_MoveType() != MOVETYPE_WALK && pawn->m_MoveType() != MOVETYPE_LADDER))
	{
		this->InvalidateJumpstats("Invalid movetype");
	}
}

void HNSJumpstatsService::DetectNoclip()
{
	if (this->lastNoclipTime + JS_MAX_NOCLIP_RESET_TIME > g_pKZUtils->GetGlobals()->curtime)
	{
		this->InvalidateJumpstats("Just noclipped");
	}
}

void HNSJumpstatsService::DetectEdgebug()
{
	if (this->jumps.empty() || !this->jumps.back().IsValid())
	{
		return;
	}
	// If the player suddenly gain speed from negative speed, they probably edgebugged.
	this->possibleEdgebug = false;
	if (this->tpmVelocity.z < 0.0f && this->player->currentMoveData->m_vecVelocity.z > this->tpmVelocity.z
		&& this->player->currentMoveData->m_vecVelocity.z > -JS_EPSILON)
	{
		this->possibleEdgebug = true;
	}
}

void HNSJumpstatsService::DetectInvalidCollisions()
{
	if (this->jumps.empty() || !this->jumps.back().IsValid())
	{
		return;
	}
	if (this->player->IsCollidingWithWorld())
	{
		this->jumps.back().touchDuration += g_pKZUtils->GetGlobals()->frametime;
		// Headhit invalidates following bhops but not the current jump,
		// while other collisions do after a certain duration.
		if (this->jumps.back().touchDuration > JS_TOUCH_GRACE_PERIOD)
		{
			this->InvalidateJumpstats("Invalid collisions");
		}
		if (this->player->moveDataPre.m_vecVelocity.z > 0.0f)
		{
			this->jumps.back().hitHead = true;
		}
	}
}

void HNSJumpstatsService::DetectInvalidGains()
{
	// Ported from GOKZ: fix certain props that don't give you base velocity.
	// We check for speed reduction for abuse; while prop abuses increase speed,
	// wall collision will very likely (if not always) result in a speed reduction.
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!pawn)
	{
		return;
	}
	f32 speed = this->player->currentMoveData->m_vecVelocity.Length2D();
	f32 actualSpeed = (this->player->currentMoveData->m_vecAbsOrigin - this->player->moveDataPre.m_vecAbsOrigin).Length2D();

	if (pawn->m_vecBaseVelocity().Length() > 0.0f || pawn->m_fFlags() & FL_BASEVELOCITY)
	{
		this->InvalidateJumpstats("Base velocity detected");
	}
	if (actualSpeed - speed > JS_SPEED_MODIFICATION_TOLERANCE && actualSpeed > JS_EPSILON)
	{
		this->InvalidateJumpstats("Invalid gains");
	}
}

void HNSJumpstatsService::DetectExternalModifications()
{
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!pawn)
	{
		return;
	}
	if ((this->player->currentMoveData->m_vecAbsOrigin - this->player->moveDataPost.m_vecAbsOrigin).LengthSqr() > JS_TELEPORT_DISTANCE_SQUARED)
	{
		this->InvalidateJumpstats("Externally modified");
	}
	if (pawn->m_vecBaseVelocity().Length() > 0.0f || pawn->m_fFlags() & FL_BASEVELOCITY)
	{
		this->InvalidateJumpstats("Base velocity detected");
	}
	if (pawn->m_flGravityScale() != 1 || pawn->m_flActualGravityScale() != 1)
	{
		this->InvalidateJumpstats("Player gravity scale changed");
	}
}

void HNSJumpstatsService::DetectWater()
{
	if (this->jumps.empty() || !this->jumps.back().IsValid())
	{
		return;
	}
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (pawn && pawn->m_flWaterLevel() > 0.0f)
	{
		this->InvalidateJumpstats("Touched water");
	}
}

void HNSJumpstatsService::DetectGhost()
{
	// Redie (CounterStrikeSharp) ghosts keep moving but their pawn is LIFE_DYING. The collision group (noblock) is not
	// checked: other plugins use noblock groups for live players too.
	CCSPlayerPawn *pawn = this->player->GetPlayerPawn();
	if (!pawn || pawn->m_lifeState() != LIFE_ALIVE)
	{
		this->InvalidateJumpstats("Ghost");
	}
}
