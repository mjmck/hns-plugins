#pragma once
#include "common.h"

class HNSPlayer;
class IEntityResourceManifest;

// Server-wide join sounds: when a listed player joins, every player on the server hears a sound.
//
// Played the same way CS2KZ plays its jumpstat sounds (utils::PlaySoundToClient -> EmitSound on a single recipient),
// once for every human in the server, so each listener hears it as a 2D sound at full volume.
//
// The list lives in csgo/cfg/hns-movement/join-sounds.cfg (written with an example if missing; hns_reload re-reads it):
//   precache "soundevents/soundevents_hns.vsndevts"   soundevents file from the workshop addon, added to the map manifest
//   76561198000000000  hns.join.example  1.0           <SteamID64 | STEAM_1:x:y> <sound event name> [volume 0-1]
//
// Trigger: the player's Steam ticket has been validated (so a copied name or spoofed ID cannot play it) AND they are in
// the server (ClientActive). The sound plays hns_join_sound_delay seconds after the later of the two, once per
// connection, and not again for the same SteamID within hns_join_sound_cooldown seconds (map changes reconnect everyone).
namespace hns::joinsound
{
#define HNS_JOINSOUND_PATH "cfg/hns-movement/join-sounds.cfg"

	// Read join-sounds.cfg. Missing file = example file written, no entries.
	bool Load();

	// Player became active in the server / passed Steam auth. Either order; the sound is queued once both happened.
	void OnPlayerActive(HNSPlayer *player);
	void OnPlayerAuthorized(HNSPlayer *player);
	void OnPlayerReset(HNSPlayer *player);

	// BuildGameSessionManifest: precache the soundevents files listed in the cfg, so clients load the custom events.
	void OnBuildGameSessionManifest(IEntityResourceManifest *manifest);

	// Play a sound event to every human in the server right now (hns_join_sound_test).
	void PlayToAll(const char *soundEvent, f32 volume);

	void PrintStatus();
} // namespace hns::joinsound
