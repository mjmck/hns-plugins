#pragma once
#include "common.h"

// Loads csgo/cfg/hns-movement/hns-movement.cfg.
//
// The file holds two kinds of lines, both "<name> <value>", with // comments:
//   - movement cvars from hns_cvars.h (sv_airaccelerate 1000 ...): enforced every tick.
//   - plugin settings below (hns_ps_enabled 1 ...).
// Anything not set in the file keeps its built-in default. Run hns_reload after editing.
#define HNS_CONFIG_PATH   "cfg/hns-movement/hns-movement.cfg"
#define HNS_FALLBACK_PATH "cfg/hns-movement/hns_movement_fallback.cfg"

namespace hns::config
{
	// The game keeps the player this far away from what they collide with. Physical constant of the engine, not tunable.
	inline constexpr f32 RAMP_BUG_OFFSET = 0.03125f;

	struct Settings
	{
		// KZ prestrafe model. On (default) = identical to CS2KZ Classic: ground speed is capped at 250 + prestrafe (max 276),
		// which overrides sv_maxspeed on the ground, and perf speed uses KZ's log formula.
		// Off = no ground speed clamp (sv_maxspeed applies) and a perf keeps max(landing speed, takeoff speed).
		bool psEnabled = true;

		// Only apply the perf speed/height rewrite when the player actually jumped. CS2KZ also applies it when the player
		// leaves the ground without jumping (e.g. sliding off a surf ramp lip), which caps surf speed. 1 = HNS fix.
		bool perfRequiresJump = true;

		// ---- HNS round flow (replaces ChaseMod's freeze and team switch) ----
		// Seconds CTs stay frozen at round start (Ts move freely). 0 = off.
		f32 ctFreezeTime = 15.0f;
		// Fall damage off for everyone while the CTs are frozen (same as ChaseMod).
		bool freezeNoFallDamage = true;
		// Blue tint and health-shot screen effect on frozen CTs (same as ChaseMod).
		bool freezeEffects = true;

		// ---- Weapons (hns_weapons.cpp) ----
		bool noGuns = true;
		bool knifeCTOnly = true;
		bool knifeStabOnly = true;
		// CTs with the knife out: left click (+attack) is turned into a stab (+attack2).
		bool knifeAttackIsStab = true;
		// Switch to the knife when a player has nothing in hand (spawn, or their held gun was removed).
		bool equipKnife = true;
		// Ts get one smoke grenade and one flashbang when they spawn (topped up, never doubled).
		bool tSpawnGrenades = true;
		// CTs get a decoy when they spawn (topped up, never doubled).
		bool ctSpawnDecoy = true;
		// mp_autokick 0 on map start: no kicks for suiciding too often (falls), team kills or team damage.
		bool disableAutokick = true;
		// mp_team_intro_time 0: no frozen team intro when warmup ends (the CT freeze would only start after it).
		bool disableTeamIntro = true;
		// Hide the "<player> is joining the Terrorists/Counter-Terrorists" chat line on every team change.
		bool silentTeamChange = true;
		// Damage of every CT knife hit on a T. 0 = game default (slash 40, stab 65, backstab 180).
		f32 knifeDamage = 55.0f;
		// Seconds a stabbed T takes no further knife damage (red tint meanwhile). 0 = no cooldown.
		f32 knifeCooldown = 2.0f;
		// Understab: a CT stab is denied when the T stands (or last stood) more than understabHeight units above the floor
		// the CT is standing on or above. The CT is told "UNDERSTAB! Stab Denied" in chat.
		bool understab = true;
		// Players never damage their own teammates (knife, grenades, anything), whatever mp_friendlyfire is set to.
		bool noTeamDamage = true;
		f32 understabHeight = 65.0f;
		// Centre-screen speed and key HUD (hns_hud.h): speed, last takeoff speed and held keys.
		bool speedHud = true;
		// Swap CTs and Ts when the CTs win.
		bool teamSwitch = false; // ChaseMod owns the team switch (see hns_compat.h)
		// Also swap after this many T wins in a row. 0 = never.
		i32 tWinStreakSwitch = 0;

		// Rampbug fix thresholds (CS2KZ Classic constants, tuned at sv_airaccelerate 100). Exposed for retuning at 1000.
		f32 rampbugThreshold = 0.999f; // Normals with a dot product below this count as a different plane.
		f32 rampbugSamePlane = 0.99f;  // Offset trace counts as the same ramp above this dot product.
		f32 rampbugMinDistance = 1.0f; // Offset trace must go at least this much further to be accepted.

		// Log when something outside the plugin changes an owned cvar.
		bool warnOnExternalChange = true;

		// Print every takeoff (perf or not, time on ground, speeds) to the server console. Development only.
		bool debugPerf = false;
	};

	inline Settings settings;

	// Read the cfg file. Missing file = defaults plus a warning. Returns false only on a read error.
	bool Load();

	// Write a plain cvar cfg with the currently enforced values, for "exec hns-movement/hns_movement_fallback" in
	// server.cfg. If the plugin ever fails to load, the server still gets the same movement values.
	bool WriteFallback();

	// Write a commented default hns-movement.cfg if none exists yet.
	bool WriteDefaultConfigIfMissing();

	void PrintSettings();
} // namespace hns::config
