// Block size and takeoff/landing edge for landed jumps. Ported from CS2KZ src/kz/jumpstats/block_tracking.cpp
// (AGPL-3.0, originally GOKZ's jump_tracking.sp). The failstat and jsalways parts are not ported.
#include "js_jumpstats.h"
#include "hns/hns_player.h"
#include "sdk/tracefilter.h"
#include "sdk/navphysicsinterface.h"
#include "utils/utils.h"

#include "tier0/memdbgon.h"

// Hull sweep from start to end with the given bounding box. Returns true if hit.
static_function bool TraceHullPosition(HNSPlayer *player, const Vector &start, const Vector &end, const Vector &mins, const Vector &maxs,
									   Vector &position)
{
	CCSPlayerPawn *pawn = player->GetPlayerPawn();
	if (!pawn)
	{
		return false;
	}
	trace_t tr;
	bbox_t bounds = {mins, maxs};
	CTraceFilterPlayerMovementCS filter(pawn);
	INavPhysicsInterface::TraceShape(Ray_t(bounds.mins, bounds.maxs), start, end, &filter, &tr);
	if (tr.DidHit())
	{
		position = tr.m_vEndPos;
		return true;
	}
	return false;
}

// Point trace returning both hit position and surface normal.
static_function bool TraceRayPositionNormal(HNSPlayer *player, const Vector &start, const Vector &end, Vector &position, Vector &normal)
{
	CCSPlayerPawn *pawn = player->GetPlayerPawn();
	if (!pawn)
	{
		return false;
	}
	trace_t tr;
	CTraceFilterPlayerMovementCS filter(pawn);
	INavPhysicsInterface::TraceLine(start, end, &filter, &tr);
	if (tr.DidHit())
	{
		position = tr.m_vEndPos;
		normal = tr.m_vHitNormal;
		return true;
	}
	return false;
}

// coordDist: 0 = X-axis dominant, 1 = Y-axis dominant. distSign: +1 (landing > takeoff along that axis) or -1.
static_function void GetCoordOrientation(const Vector &landingPos, const Vector &takeoffPos, i32 &coordDist, i32 &distSign)
{
	coordDist = fabsf(landingPos.x - takeoffPos.x) < fabsf(landingPos.y - takeoffPos.y) ? 1 : 0;
	distSign = landingPos[coordDist] > takeoffPos[coordDist] ? 1 : -1;
}

// Short point trace; returns true if it hits and the hit normal is aligned with coordDist.
static_function bool BlockTraceAligned(HNSPlayer *player, const Vector &start, const Vector &end, i32 coordDist)
{
	Vector pos, normal;
	if (!TraceRayPositionNormal(player, start, end, pos, normal))
	{
		return false;
	}
	return fabsf(fabsf(normal[coordDist]) - 1.0f) <= JS_EPSILON;
}

// Verifies that the faces of the takeoff and landing blocks are parallel.
static_function bool BlockAreEdgesParallel(HNSPlayer *player, const Vector &startBlock, const Vector &endBlock, f32 deviation, i32 coordDist,
										   i32 coordDev)
{
	f32 offset = startBlock[coordDist] > endBlock[coordDist] ? 0.1f : -0.1f;

	Vector start, end;
	start[coordDist] = startBlock[coordDist] - offset;
	start[coordDev] = startBlock[coordDev] - deviation;
	start.z = startBlock.z;
	end[coordDist] = startBlock[coordDist] + offset;
	end[coordDev] = startBlock[coordDev] - deviation;
	end.z = startBlock.z;

	if (BlockTraceAligned(player, start, end, coordDist))
	{
		start[coordDist] = endBlock[coordDist] + offset;
		end[coordDist] = endBlock[coordDist] - offset;
		if (BlockTraceAligned(player, start, end, coordDist))
		{
			return true;
		}
		start[coordDist] = startBlock[coordDist] - offset;
		end[coordDist] = startBlock[coordDist] + offset;
	}

	start[coordDev] = startBlock[coordDev] + deviation;
	end[coordDev] = startBlock[coordDev] + deviation;

	if (BlockTraceAligned(player, start, end, coordDist))
	{
		start[coordDist] = endBlock[coordDist] + offset;
		end[coordDist] = endBlock[coordDist] - offset;
		if (BlockTraceAligned(player, start, end, coordDist))
		{
			return true;
		}
	}
	return false;
}

void Jump::EndBlockDistance()
{
	f32 dist = this->GetDistance();
	if (this->jumpType != JumpType_LadderJump && dist >= (f32)JS_MIN_BLOCK_DISTANCE)
	{
		this->CalcBlockStats(this->adjustedLandingOrigin);
	}
	else if (this->jumpType == JumpType_LadderJump && dist >= (f32)JS_MIN_LAJ_BLOCK_DISTANCE)
	{
		this->CalcLadderBlockStats(this->adjustedLandingOrigin);
	}
}

void Jump::CalcBlockStats(Vector landingOrigin)
{
	i32 coordDist, distSign;
	GetCoordOrientation(landingOrigin, this->adjustedTakeoffOrigin, coordDist, distSign);
	i32 coordDev = 1 - coordDist;

	f32 deviation = fabsf(landingOrigin[coordDev] - this->adjustedTakeoffOrigin[coordDev]);

	// Midpoint of the jump gap - assumed to be over open air, not inside a block
	Vector middle;
	middle[coordDist] = (this->adjustedTakeoffOrigin[coordDist] + landingOrigin[coordDist]) / 2.0f;
	middle[coordDev] = (this->adjustedTakeoffOrigin[coordDev] + landingOrigin[coordDev]) / 2.0f;
	middle.z = this->adjustedTakeoffOrigin.z - 1.0f;

	// Sweep line perpendicular to jump direction, wide enough to catch the blocks despite deviation
	Vector sweepMin = vec3_origin;
	Vector sweepMax = vec3_origin;
	sweepMin[coordDev] = -(deviation + 16.0f);
	sweepMax[coordDev] = deviation + 16.0f;

	// Start of the gap (takeoff side): player bounding box places the wall 16 units back
	Vector startBlock = middle;
	startBlock[coordDist] = this->adjustedTakeoffOrigin[coordDist] - distSign * 16.0f;

	// End of the gap (landing side): extend slightly past block face to catch the JS_OFFSET_EPSILON case
	Vector endBlock = middle;
	endBlock[coordDist] = landingOrigin[coordDist] + distSign * (16.0f + JS_OFFSET_EPSILON);

	if (!TraceHullPosition(this->player, middle, startBlock, sweepMin, sweepMax, startBlock)
		|| !TraceHullPosition(this->player, middle, endBlock, sweepMin, sweepMax, endBlock))
	{
		return;
	}

	if (!BlockAreEdgesParallel(this->player, startBlock, endBlock, deviation + 32.0f, coordDist, coordDev))
	{
		this->block = 0.0f;
		this->edge = -1.0f;
		this->landingEdge = -1.0f;
		return;
	}

	f32 rawBlock = fabsf(endBlock[coordDist] - startBlock[coordDist]);
	this->block = (f32)RoundFloatToInt(rawBlock);
	// Trace stops JS_OFFSET_EPSILON units in front of the actual block face; compensate
	this->edge = fabsf(startBlock[coordDist] - this->adjustedTakeoffOrigin[coordDist] + (16.0f - JS_OFFSET_EPSILON) * distSign);
	this->landingEdge = (this->adjustedLandingOrigin[coordDist] - endBlock[coordDist]) * distSign + 16.0f;

	if (this->block < (f32)JS_MIN_BLOCK_DISTANCE)
	{
		this->block = 0.0f;
		this->edge = -1.0f;
		this->landingEdge = -1.0f;
	}
}

void Jump::CalcLadderBlockStats(Vector landingOrigin)
{
	i32 coordDist, distSign;
	GetCoordOrientation(landingOrigin, this->adjustedTakeoffOrigin, coordDist, distSign);
	i32 coordDev = 1 - coordDist;

	Vector ladderNormal = this->player->GetMoveServices()->m_vecLadderNormal();
	if (fabsf(fabsf(ladderNormal[coordDist]) - 1.0f) > JS_EPSILON)
	{
		return;
	}

	Vector ladderPosition = this->adjustedTakeoffOrigin;
	Vector endBlock = landingOrigin;
	endBlock.z -= 1.0f;
	ladderPosition.z = endBlock.z;

	Vector sweepMin = vec3_origin;
	Vector sweepMax = vec3_origin;
	sweepMin[coordDev] = -20.0f;
	sweepMax[coordDev] = 20.0f;

	Vector middle;
	middle[coordDist] = ladderPosition[coordDist] + (f32)distSign * (f32)JS_MIN_LAJ_BLOCK_DISTANCE;
	middle[coordDev] = endBlock[coordDev];
	middle.z = ladderPosition.z;

	if (!TraceHullPosition(this->player, ladderPosition, middle, sweepMin, sweepMax, ladderPosition))
	{
		return;
	}

	Vector blockPosition, blockNormal;
	endBlock[coordDist] += (f32)distSign * 16.0f;
	if (!TraceRayPositionNormal(this->player, middle, endBlock, blockPosition, blockNormal)
		|| fabsf(fabsf(blockNormal[coordDist]) - 1.0f) > JS_EPSILON)
	{
		return;
	}

	this->block = (f32)RoundFloatToInt(fabsf(blockPosition[coordDist] - ladderPosition[coordDist]));
	this->edge = fabsf(this->adjustedTakeoffOrigin[coordDist] - ladderPosition[coordDist]) - 16.0f;
	this->landingEdge = (this->adjustedLandingOrigin[coordDist] - blockPosition[coordDist]) * distSign + 16.0f;

	if (this->block < (f32)JS_MIN_LAJ_BLOCK_DISTANCE)
	{
		this->block = 0.0f;
		this->edge = -1.0f;
		this->landingEdge = -1.0f;
	}
}
