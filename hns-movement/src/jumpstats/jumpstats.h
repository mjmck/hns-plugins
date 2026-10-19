// Jumpstats, ported from CS2KZ (cs2kz-metamod src/kz/jumpstats/kz_jumpstats.h, AGPL-3.0, KZGlobalTeam).
// The maths (jump types, distance, strafes, sync, gain, block/edge/failstats) is unchanged. What differs from CS2KZ:
//   - KZPlayer -> HNSPlayer, and the service is a plain member (player->jumpstats) instead of a KZBaseService.
//   - Distance tiers come from hns-movement.cfg (hns_js_tiers_*, CS2KZ Classic values by default) instead of a mode service.
//   - No styles, HUD panel, menus, anticheat, replays, recording, map triggers or translations.
//   - Player preferences are per-connection (JSPrefs below) and set with chat commands (!js, !jstier, ...).
#pragma once

#include "common.h"
#include "movement/movement.h"
#include "sdk/datatypes.h"
#include "utils/circularfifobuffer.h"

#include <string>
#include <vector>

class HNSPlayer;

enum JumpType
{
	JumpType_FullInvalid = -1,
	JumpType_LongJump,
	JumpType_Bhop,
	JumpType_MultiBhop,
	JumpType_WeirdJump,
	JumpType_LadderJump,
	JumpType_Ladderhop,
	JumpType_Jumpbug,
	JumpType_Fall,
	JumpType_Other,
	JumpType_Invalid,
	JUMPTYPE_COUNT
};

// Jump types with distance tiers: LJ, BH, MBH, WJ, LAJ, LAH, JB (CS2KZ's distanceTiers[JUMPTYPE_COUNT - 3]).
#define JS_TIERED_JUMPTYPE_COUNT (JUMPTYPE_COUNT - 3)

enum DistanceTier : u8
{
	DistanceTier_None = 0,
	DistanceTier_Meh,
	DistanceTier_Impressive,
	DistanceTier_Perfect,
	DistanceTier_Godlike,
	DistanceTier_Ownage,
	DistanceTier_Wrecker,
	DISTANCETIER_COUNT
};

// where to show js
enum JSReportType : u8
{
	JSReportType_Hud = 0,
	JSReportType_Chat,
	JSReportType_Both,
	JSREPORTTYPE_COUNT
};

// The stats listed under a jump's distance, in their default order, shared by the chat report and the HUD panel.
enum class JSField : u8
{
	Block,
	Strafes,
	Sync,
	PreMax,
	Edge,
	Height,
	AirTime,
	Width,
	GainEfficiency,
	AirPath,
	BadAngles,
	Deviation,
	Miss,
	Release,
	Offset,
	Count,
};

#define JS_FIELD_MAX_VALUES 3

struct JSFieldDef
{
	const char *slug; // short name of the field
	// One per value, as the chat prints them. Pre and max, say, are one field with two values.
	const char *labels[JS_FIELD_MAX_VALUES];
	bool extended; // printed on the chat's second line, which only shows with extended chat stats on
};

// In CS2KZ's default chat order.
extern const JSFieldDef JS_FIELDS[(i32)JSField::Count];

// Per-player jumpstats preferences. CS2KZ keeps these in its option service (saved to its DB); here they last until the
// player disconnects, and start from the server defaults in hns-movement.cfg.
struct JSPrefs
{
	bool reporting = true;     // master switch (!js)
	bool extended = false;     // second chat line with the extended stats (!jsextend)
	bool always = false;       // report every jump, invalid ones included, greyed (!jsalways)
	bool failstats = true;     // report failed jumps (!jsfailstats)
	DistanceTier minTier {};   // lowest tier printed to chat (!jstier)
	DistanceTier minTierConsole {};
	DistanceTier broadcastMinTier {}; // lowest tier of other players' jumps announced to you (!jsbroadcast)
	bool initialized = false;
};

enum class JSFieldColor : u8
{
	Olive, // what the chat prints every stat value in
	Grey,
	Red,
	Green,
	Blue,
	Tier, // the distance tier's color
};

// One field of one jump, formatted for the chat or the HUD.
struct JSFieldText
{
	bool present {}; // false for a stat the jump does not have, such as the block of a jump without one
	JSFieldColor color {};
	DistanceTier tier {};
	std::string values[JS_FIELD_MAX_VALUES];
	i32 count {};
};

#define IGNORE_JUMP_TIME                (0.2f + 0.001f) // To take floating errors into account
#define JS_EPSILON                      0.03125f
#define JS_MAX_LADDERJUMP_OFFSET        2.0f
#define JS_MAX_BHOP_GROUND_TIME         0.05f
#define JS_MAX_DUCKBUG_RESET_TIME       0.05f
#define JS_MAX_NOCLIP_RESET_TIME        0.4f
#define JS_MAX_WEIRDJUMP_FALL_OFFSET    (64.0f + JS_EPSILON)
#define JS_TOUCH_GRACE_PERIOD           0.04f
#define JS_SPEED_MODIFICATION_TOLERANCE 0.1f
// clang-format off
#define JS_TELEPORT_DISTANCE_SQUARED    4096.0f * 4096.0f * ENGINE_FIXED_TICK_INTERVAL * ENGINE_FIXED_TICK_INTERVAL
// clang-format on

#define JS_MIN_BLOCK_DISTANCE          186
#define JS_MIN_LAJ_BLOCK_DISTANCE      50
#define JS_MAX_LAJ_FAILSTAT_DISTANCE   250
#define JS_FAILSTATS_MAX_TRACKED_TICKS 512
#define JS_OFFSET_EPSILON              0.04f
// HNS: velocity change between movement calls (u/s) that counts as an outside push. See DetectExternalModifications.
#define JS_EXTERNAL_SPEED_TOLERANCE    2.0f

extern const char *jumpTypeStr[JUMPTYPE_COUNT];
extern const char *jumpTypeShortStr[JUMPTYPE_COUNT];
extern const char *distanceTierColors[DISTANCETIER_COUNT];
extern const char *distanceTierNames[DISTANCETIER_COUNT];

namespace hns::jumpstats
{
	// Distance tier of a jump. Same rule as CS2KZ's KZClassicModeService::GetDistanceTier, with the tier table from
	// hns-movement.cfg (hns_js_tiers_*).
	DistanceTier GetDistanceTier(JumpType jumpType, f32 distance);
	// Air speed cap used by the ideal-gain maths (sv_air_max_wishspeed, enforced by hns-movement).
	f32 GetAirMaxWishspeed();
} // namespace hns::jumpstats

struct JumpPose
{
	Vector position;
	// These fields are used for jsalways's backtracking.
	f32 airtime;
	f32 badAngles;
	f32 overlap;
	f32 deadAir;
	f32 sync;
};

class Jump;

class AACall
{
public:
	f32 externalSpeedDiff {};
	f32 prevYaw {};
	f32 currentYaw {};
	f32 startFraction {};
	f32 endFraction {1.0f};
	Vector wishdir;
	f32 maxspeed {};
	f32 wishspeed {};
	f32 accel {};

	f32 surfaceFriction {};
	f32 duration {};
	u64 buttons[3] {};
	Vector velocityPre;
	Vector velocityPost;
	bool ducking {};

public:
	f32 CalcIdealYaw(bool useRadians = false);
	f32 CalcMinYaw(bool useRadians = false);
	f32 CalcMaxYaw(bool useRadians = false);
	f32 CalcAccelSpeed(bool tryMaxSpeed = false);
	f32 CalcIdealGain();
};

class Strafe
{
public:
	Strafe() {}

	Strafe(Jump *jump) : jump(jump) {}

	Jump *jump;
	std::vector<AACall> aaCalls;
	TurnState turnstate;

	f32 duration {};

	f32 badAngles {};
	f32 overlap {};
	f32 deadAir {};

	f32 syncDuration {};
	f32 width {};

	// Gain/loss from airstrafing
	f32 airGain {};
	f32 maxGain {};
	f32 airLoss {};

	// Gain/loss from collisions
	f32 collisionGain {};
	f32 collisionLoss {};

	// Gain/loss from triggers/networking/etc..
	f32 externalGain {};
	f32 externalLoss {};
	f32 strafeMaxSpeed {};

public:
	void End();

	f32 GetStrafeDuration()
	{
		return this->duration;
	}

	void UpdateCollisionVelocityChange(f32 delta);

	f32 GetMaxGain()
	{
		return this->maxGain;
	}

	f32 GetGain(bool external = false)
	{
		return external ? this->externalGain : this->airGain + this->collisionGain;
	}

	f32 GetLoss(bool external = false)
	{
		return external ? this->externalLoss : this->airLoss + this->collisionLoss;
	}

	f32 GetWidth()
	{
		return this->width;
	}

	// BA/OL/DA
	f32 GetBadAngleDuration()
	{
		return this->badAngles;
	}

	f32 GetOverlapDuration()
	{
		return this->overlap;
	}

	f32 GetDeadAirDuration()
	{
		return this->deadAir;
	}

	f32 GetSync()
	{
		return this->syncDuration / this->duration;
	}

	f32 GetSyncDuration()
	{
		return this->syncDuration;
	}

	f32 GetStrafeMaxSpeed()
	{
		return this->strafeMaxSpeed;
	}

	// Calculate the ratio for each strafe.
	// The ratio is 0 if the angle is perfect, closer to -1 if it's too slow
	// Closer to 1 if it passes the optimal value.
	// Note: if the player jumps in place, no velocity and no attempt to move at all, any angle will be "perfect".
	// Returns false if there is no available stats.
	struct AngleRatioStats
	{
		bool available;
		f32 max;
		f32 median;
		f32 average;
	};

	AngleRatioStats arStats;

	bool CalcAngleRatioStats();

	void UpdateStrafeMaxSpeed(f32 speed)
	{
		this->strafeMaxSpeed = MAX(this->strafeMaxSpeed, speed);
	}
};

class Jump
{
public:
	HNSPlayer *player;
	u32 serverTick {}; // When the jump ended
	Vector takeoffOrigin;
	Vector adjustedTakeoffOrigin;
	Vector takeoffVelocity;
	Vector landingOrigin;
	Vector adjustedLandingOrigin;

	JumpType jumpType;
	JumpType originalJumpType;

	// Required for airpath stat.
	f32 totalDistance {};
	f32 currentMaxSpeed {};
	f32 currentMaxHeight = -16384.0f;
	f32 airtime {};

	f32 deadAir {};
	f32 overlap {};
	f32 badAngles {};
	f32 sync {};
	f32 duckDuration {};
	f32 duckEndDuration {};
	f32 width {};
	f32 gainEff {};

	bool hitHead {};
	bool valid = true;
	bool ended {};

	f32 release;

	std::vector<Strafe> strafes;
	f32 touchDuration {};
	char invalidateReason[256] {};
	bool trackingRelease = true;

	// Block / failstat / edge tracking
	f32 block {};
	f32 edge = -1.0f;
	f32 landingEdge = -1.0f;
	f32 miss {};
	f32 failstatDistance {};
	f32 failstatOffset {};
	f32 failstatSync {};
	f32 failstatBadAngles {};
	i32 failstatStrafeCount {};
	f32 failstatTotalDistance {};
	i32 failstatGraphCallCount {};
	bool failstatBlockDetected {};
	bool failstatFailed {};
	bool failstatValid {};
	f32 failstatBlockHeight {};

	// Running totals for ended strafes, used by RecordPose snapshots.
	f32 poseEndedDuration {};
	f32 poseEndedSyncDuration {};
	f32 poseEndedBadAnglesDuration {};
	f32 poseEndedOverlapDuration {};
	f32 poseEndedDeadAirDuration {};

public:
	Jump() = default;

	Jump(HNSPlayer *player) : player(player) {}

	void Init();
	void UpdateAACallPost(Vector wishdir, f32 wishspeed, f32 accel);
	void Update();
	void End();

	void Invalidate(const char *reason)
	{
		this->valid = false;
		V_strncpy(this->invalidateReason, reason, sizeof(this->invalidateReason));
	}

	void MarkHitHead()
	{
		this->hitHead = true;
	}

	Strafe *GetCurrentStrafe();

	JumpType GetJumpType()
	{
		return this->jumpType;
	}

	JumpType GetReportJumpType();

	HNSPlayer *GetJumpPlayer()
	{
		return this->player;
	}

	bool IsValid()
	{
		return this->valid && this->jumpType >= JumpType_LongJump && this->jumpType <= JumpType_Fall;
	}

	bool AlreadyEnded()
	{
		return this->ended;
	}

	bool DidHitHead()
	{
		return this->hitHead;
	}

	f32 GetTakeoffSpeed()
	{
		return this->takeoffVelocity.Length2D();
	}

	f32 GetOffset()
	{
		if (this->IsFailstat())
		{
			return this->failstatOffset;
		}
		return adjustedLandingOrigin.z - adjustedTakeoffOrigin.z;
	}

	f32 GetDistance(bool useDistbugFix = true, bool disableAddDist = false, i32 floorLevel = -1);

	f32 GetMaxSpeed()
	{
		return this->currentMaxSpeed;
	}

	i32 GetStrafeCount()
	{
		if (this->IsFailstat())
		{
			return this->failstatStrafeCount;
		}
		return this->strafes.size();
	}

	f32 GetSync()
	{
		if (this->IsFailstat())
		{
			return this->failstatSync;
		}
		return this->sync;
	}

	f32 GetBadAngles()
	{
		if (this->IsFailstat())
		{
			return this->failstatBadAngles;
		}
		return this->badAngles;
	}

	f32 GetOverlap()
	{
		return this->overlap;
	}

	f32 GetDeadAir()
	{
		return this->deadAir;
	}

	f32 GetMaxHeight()
	{
		return this->currentMaxHeight;
	}

	f32 GetWidth()
	{
		return this->width;
	}

	f32 GetEdge(bool landing);

	f32 GetBlock()
	{
		return this->block;
	}

	f32 GetMiss()
	{
		return this->miss;
	}

	bool IsFailstat() const
	{
		return this->failstatValid;
	}

	f32 GetGainEfficiency()
	{
		return this->gainEff;
	}

	f32 GetAirPath();

	f32 GetDuckTime(bool endOnly = true)
	{
		return endOnly ? this->duckEndDuration : this->duckDuration;
	}

	f32 GetDeviation();

	// Same distance as GetDeviation, positive when the landing is to the left of the jump direction and negative to the right.
	f32 GetSignedDeviation();

	f32 GetReleaseInTick()
	{
		return this->release * ENGINE_FIXED_TICK_RATE;
	}

	std::string GetReleaseString(bool colored = true)
	{
		char releaseString[64];
		// Don't show anything for large negative values (W released long ago)
		if (this->GetReleaseInTick() < -20)
		{
			return "";
		}
		if (this->GetReleaseInTick() > 10)
		{
			return colored ? " | {red}✗{grey} W" : " | ✗ W";
		}
		else if (this->GetReleaseInTick() > 0)
		{
			V_snprintf(releaseString, sizeof(releaseString), " %s| %s+%.1f%s W", colored ? "{grey}" : "", colored ? "{red}" : "",
					   this->GetReleaseInTick(), colored ? "{grey}" : "");
		}
		else if (this->GetReleaseInTick() == 0)
		{
			return colored ? " | {green}✓{grey} W" : " | ✓ W";
		}
		else
		{
			V_snprintf(releaseString, sizeof(releaseString), " %s| %s%.1f%s W", colored ? "{grey}" : "", colored ? "{blue}" : "",
					   this->GetReleaseInTick(), colored ? "{grey}" : "");
		}
		return releaseString;
	}

	// greyTier greys the block out like the distance, for a jump that is invalid or reported through jsalways.
	void FormatField(JSField field, bool chat, bool greyTier, JSFieldText &out);

	std::string GetInvalidationReasonString(const char *reason);
	bool BuildConsoleStrafeMouseGraph(std::string &strafeLeft, std::string &strafeRight, std::string &mouseLeft, std::string &mouseRight) const;

	// Block / failstat / edge tracking
	void RecordPose();
	void CacheEndedStrafePoseTotals(Strafe &strafe);
	void UpdateFailstat();
	void EndBlockDistance();
	void AlwaysFailstat();
	void CalcBlockStats(Vector landingOrigin, bool checkOffset = false);
	void CalcLadderBlockStats(Vector landingOrigin, bool checkOffset = false);
	void CalcAlwaysEdge();
	bool GetFailOrigin(f32 planeHeight, Vector &result, i32 offset);
};

class HNSJumpstatsService
{
public:
	HNSJumpstatsService(HNSPlayer *player) : player(player)
	{
		this->tpmVelocity = Vector(0, 0, 0);
	}

	HNSPlayer *player;
	JSPrefs prefs;
	// Server time of the last teleport, for JustTeleported (CS2KZ keeps this on KZPlayer).
	f32 lastTeleportTime {};

	bool JustTeleported(f32 threshold = 0.05f);

	// Jumpstats
	std::vector<Jump> jumps;
	// One pose per subtick movement of the current jump, consumed by the tail jump's failstat tracking.
	CFIFOCircularBuffer<JumpPose, JS_FAILSTATS_MAX_TRACKED_TICKS> poseHistory;
	f32 lastJumpButtonTime {};
	f32 lastNoclipTime {};
	f32 lastDuckbugTime {};
	f32 lastGroundSpeedCappedTime {};
	f32 lastMovementProcessedTime {};
	Vector tpmVelocity;
	bool possibleEdgebug {};
	f32 lastWPressedTime {};
	bool ladderHopThisMove {};

public:
	static bool GetDistTierFromString(const char *tierString, DistanceTier &outTier);

	void Reset();
	void OnProcessMovement();
	void OnProcessMovementPost();
	void OnChangeMoveType(MoveType_t oldMoveType);
	void OnTryPlayerMove();
	void OnTryPlayerMovePost();

	JumpType DetermineJumpType();
	f32 GetLastJumpRelease();

	f32 GetLastWPressedTime()
	{
		return lastWPressedTime;
	}

	bool HitBhop();
	bool HitDuckbugRecently();
	bool ValidWeirdJumpDropDistance();
	bool GroundSpeedCappedRecently();

	void TrackJumpstatsVariables();

	void AddJump();
	void UpdateJump();
	void EndJump();
	void HandleTeleport();
	void InvalidateJumpstats(const char *reason = NULL);
	void OnAirAccelerate();
	void OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel);

	// Jump validation
	void CheckValidMoveType();
	void DetectNoclip();
	void DetectEdgebug();
	void DetectInvalidCollisions();
	void DetectInvalidGains();
	void DetectExternalModifications();
	void DetectWater();

	// Jump reporting
	static void AnnounceJump(Jump *jump);
	static void BroadcastJumpToChat(HNSPlayer *target, Jump *jump);
	static void PrintJumpToChat(HNSPlayer *target, Jump *jump, bool extended = false);
	static void PrintJumpToConsole(HNSPlayer *target, Jump *jump, bool broadcast = false);

	// Preferences start from the cfg defaults the first time they are read after a connect.
	JSPrefs &GetPrefs();
};

namespace hns::jumpstats
{
	// Chat commands (!js, !jstier, ...). Returns true if the word was a jumpstats command.
	bool OnChatCommand(CPlayerSlot slot, const char *word, const char *argument);
	void PrintHelp(CPlayerSlot slot);
} // namespace hns::jumpstats
