#pragma once
// Jumpstats, ported from CS2KZ (src/kz/jumpstats, AGPL-3.0) at the same commit as the movement layer (c796a0d).
//
// Kept from CS2KZ: jump type detection (LJ, BH, MBH, WJ, LAJ, LAH, JB), distance with the distbug fix, per-strafe
// sync / gain / bad angles / overlap / dead air / width, W release, takeoff edge and block size for landed jumps,
// every validity check (collisions, base velocity, water, noclip, teleports, edgebugs, gravity, ...), the chat and
// console report, the broadcast to other players and the kz.* tier sounds.
// Left out: failstats and jsalways, the HUD panel, strafe graphs, option menus, translations, styles, replays.
// HNS additions: tier tables, sounds and limits in cfg/hns-movement/jumpstats.cfg; Redie ghosts never report.

#include "common.h"
#include "movement/movement.h"
#include "sdk/datatypes.h"

#include <string>
#include <vector>

class HNSPlayer;
class IEntityResourceManifest;

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

// Jump types that have a tier table (LJ .. JB).
#define JS_TIERED_JUMPTYPES (JumpType_Jumpbug + 1)

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

#define IGNORE_JUMP_TIME                (0.2f + 0.001f) // To take floating errors into account
#define JS_EPSILON                      0.03125f
#define JS_MAX_LADDERJUMP_OFFSET        2.0f
#define JS_MAX_BHOP_GROUND_TIME         0.05f
#define JS_MAX_DUCKBUG_RESET_TIME       0.05f
#define JS_MAX_NOCLIP_RESET_TIME        0.4f
#define JS_MAX_WEIRDJUMP_FALL_OFFSET    (64.0f + JS_EPSILON)
#define JS_TOUCH_GRACE_PERIOD           0.04f
#define JS_SPEED_MODIFICATION_TOLERANCE 0.1f
#define JS_RECENT_TELEPORT_THRESHOLD    0.05f
// clang-format off
#define JS_TELEPORT_DISTANCE_SQUARED    4096.0f * 4096.0f * ENGINE_FIXED_TICK_INTERVAL * ENGINE_FIXED_TICK_INTERVAL
// clang-format on

#define JS_MIN_BLOCK_DISTANCE     186
#define JS_MIN_LAJ_BLOCK_DISTANCE 50
#define JS_OFFSET_EPSILON         0.04f

extern const char *jumpTypeStr[JUMPTYPE_COUNT];
extern const char *jumpTypeShortStr[JUMPTYPE_COUNT];
extern const char *distanceTierColors[DISTANCETIER_COUNT];
extern const char *distanceTierNames[DISTANCETIER_COUNT];

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
	std::vector<AACall> aaCalls;
	TurnState turnstate {};

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
	void UpdateCollisionVelocityChange(f32 delta);

	f32 GetGain(bool external = false)
	{
		return external ? this->externalGain : this->airGain + this->collisionGain;
	}

	f32 GetLoss(bool external = false)
	{
		return external ? this->externalLoss : this->airLoss + this->collisionLoss;
	}

	f32 GetSync()
	{
		return this->duration > 0.0f ? this->syncDuration / this->duration : 0.0f;
	}

	void UpdateStrafeMaxSpeed(f32 speed)
	{
		this->strafeMaxSpeed = MAX(this->strafeMaxSpeed, speed);
	}
};

class Jump
{
public:
	HNSPlayer *player {};
	Vector takeoffOrigin;
	Vector adjustedTakeoffOrigin;
	Vector takeoffVelocity;
	Vector landingOrigin;
	Vector adjustedLandingOrigin;

	JumpType jumpType {};

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

	f32 release {};
	bool trackingRelease = true;

	std::vector<Strafe> strafes;
	f32 touchDuration {};
	char invalidateReason[128] {};

	// Block / edge (landed jumps only)
	f32 block {};
	f32 edge = -1.0f;
	f32 landingEdge = -1.0f;

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
		V_strncpy(this->invalidateReason, reason ? reason : "", sizeof(this->invalidateReason));
	}

	Strafe *GetCurrentStrafe();

	bool IsValid()
	{
		return this->valid && this->jumpType >= JumpType_LongJump && this->jumpType <= JumpType_Fall;
	}

	f32 GetTakeoffSpeed()
	{
		return this->takeoffVelocity.Length2D();
	}

	f32 GetOffset()
	{
		return adjustedLandingOrigin.z - adjustedTakeoffOrigin.z;
	}

	f32 GetDistance(bool useDistbugFix = true, bool disableAddDist = false);
	f32 GetAirPath();
	f32 GetDeviation();

	f32 GetReleaseInTick()
	{
		return this->release * ENGINE_FIXED_TICK_RATE;
	}

	// Block / edge
	void EndBlockDistance();
	void CalcBlockStats(Vector landingOrigin);
	void CalcLadderBlockStats(Vector landingOrigin);
};

class HNSJumpstatsService
{
public:
	HNSJumpstatsService(HNSPlayer *player) : player(player) {}

	HNSPlayer *player;

	// The current jump is back(); the one before it is [size - 2]. Older ones are dropped.
	std::vector<Jump> jumps;
	f32 lastJumpButtonTime {};
	f32 lastNoclipTime {};
	f32 lastDuckbugTime {};
	f32 lastGroundSpeedCappedTime {};
	f32 lastMovementProcessedTime {};
	f32 lastTeleportTime {};
	Vector tpmVelocity;
	bool possibleEdgebug {};
	f32 lastWPressedTime {};
	bool ladderHopThisMove {};

	// Per-player switches (!js, !jssound). Last until disconnect.
	bool reporting = true;
	bool sounds = true;

public:
	void Reset();
	void OnProcessMovement();
	void OnProcessMovementPost();
	void OnChangeMoveType(MoveType_t oldMoveType);
	void OnTryPlayerMove();
	void OnTryPlayerMovePost();
	void OnTeleport();

	JumpType DetermineJumpType();
	f32 GetLastJumpRelease();
	bool HitBhop();
	bool HitDuckbugRecently();
	bool ValidWeirdJumpDropDistance();
	bool GroundSpeedCappedRecently();
	bool JustTeleported();

	void TrackJumpstatsVariables();

	void AddJump();
	void UpdateJump();
	void EndJump();
	void HandleTeleport();
	void InvalidateJumpstats(const char *reason = NULL);
	void OnAirAccelerate();
	void OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel);

	// Jump validation (js_validate.cpp)
	void CheckValidMoveType();
	void DetectNoclip();
	void DetectEdgebug();
	void DetectInvalidCollisions();
	void DetectInvalidGains();
	void DetectExternalModifications();
	void DetectWater();
	void DetectGhost();
};

// Server-wide settings, tiers and reporting (js_report.cpp).
namespace hns::jumpstats
{
#define HNS_JUMPSTATS_PATH "cfg/hns-movement/jumpstats.cfg"

	// Read jumpstats.cfg (tier tables, max distance, tier sounds, precache files). Missing file = KZ Classic defaults,
	// and a commented default file is written.
	bool Load();

	DistanceTier GetDistanceTier(JumpType jumpType, f32 distance);

	// record_sound from jumpstats.cfg, to every human with jumpstat sounds on (a server record was just broken).
	void PlayRecordSound();

	// Chat, console and sounds for the jumper, their spectators and everyone else (broadcast).
	void AnnounceJump(Jump *jump);

	void OnBuildGameSessionManifest(IEntityResourceManifest *manifest);

	// Player chat commands !js and !jssound.
	void ToggleReporting(CPlayerSlot slot);
	void ToggleSounds(CPlayerSlot slot);

	void PrintStatus();
} // namespace hns::jumpstats
