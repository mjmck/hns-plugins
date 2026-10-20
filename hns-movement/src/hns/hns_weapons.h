#pragma once
#include "common.h"

class HNSPlayer;
class CBaseEntity;
class CTakeDamageInfo;
class PlayerCommand;

// HNS weapon rules (hns_no_guns, hns_knife_ct_only, hns_knife_stab_only, hns_knife_damage):
//   - No guns for anyone: any firearm or taser a player holds (bought, picked up, given by the map) is removed.
//   - Everyone keeps a knife, but only CTs can attack with it (Ts' knives can't slash or stab). Ts keep their grenades.
//   - Knife: primary attack (slash) is disabled, only the right-click stab works.
//   - Every CT knife hit on a T does a fixed amount of damage (default 55), backstabs included.
//   - Understab (hns_understab, hns_understab_height): a stab is denied when the T stands, or last stood before jumping,
//     more than 65 units above the floor the CT is standing on or above. Both get "UNDERSTAB! Stab Denied" in chat.
namespace hns::weapons
{
	// Resolves the optional signatures. Missing ones only disable the matching rule.
	void Init();
	void SetDamageHooked(bool hooked);
	// hns_status: rule state, signature state and every player's weapons.
	void PrintDebug();
	// Once per server frame, before players are simulated.
	void OnGameFrame();
	// Before the game runs a CT's commands: with the knife out, left click (+attack) becomes a stab (+attack2).
	void RemapKnifeAttack(HNSPlayer *player, PlayerCommand *cmds, int numcmds);
	// player_spawn: queue the T spawn grenades (given a few ticks later, after the game's own loadout).
	void OnPlayerSpawn(i32 slot);
	// TakeDamageOld pre-hook. Returns true to block the damage entirely.
	bool OnTakeDamage(CBaseEntity *victim, CTakeDamageInfo *info);
	// Round start: nobody starts a round knife-immune.
	void ResetStabCooldowns();
} // namespace hns::weapons
