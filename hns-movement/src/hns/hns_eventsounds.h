#pragma once
#include "common.h"

#include <string>

class IGameEvent;
class IEntityResourceManifest;

// Server-wide announcer sounds for round events. Every sound plays to every human in the server (2D, full volume),
// through the same path as join sounds (hns::joinsound::PlayToAll).
//
//   countdown   3, 2, 1 during the last seconds of the CT start freeze ("Seekers released in 3...").
//   last_man    the death that leaves exactly one T alive (once per round, not in warmup).
//   double_kill a player kills two enemies within double_kill_window seconds (once per streak).
//
// Sound names live in csgo/cfg/hns-movement/event-sounds.cfg (written with defaults if missing; hns_reload re-reads it).
// An empty name ("") turns that event off. hns_event_sounds 0 in hns-movement.cfg turns them all off.
namespace hns::eventsounds
{
#define HNS_EVENTSOUNDS_PATH "cfg/hns-movement/event-sounds.cfg"

	bool Load();

	// Freeze countdown tick: called whenever the whole seconds left change (3, 2, 1...).
	void OnFreezeCountdown(i32 secondsLeft);

	// player_death / round events, from hns::gameplay::OnFireEvent.
	void OnFireEvent(IGameEvent *event);

	void OnBuildGameSessionManifest(IEntityResourceManifest *manifest);

	void PrintStatus();

	// Sound configured for an event key (countdown_3 ... double_kill), nullptr if the key is unknown.
	const std::string *Find(const char *event);
} // namespace hns::eventsounds
