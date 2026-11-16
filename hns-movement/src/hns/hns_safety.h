#pragma once
#include "common.h"

// Safety net for CS2 updates.
//
// A CS2 update can change engine interfaces under a native plugin. Signatures fail cleanly when they stop matching,
// but a moved virtual function or a changed struct does not: the plugin calls the wrong code and the server crashes or
// freezes (16 Nov 2026: every player join froze the server until the watchdog killed it). Two checks keep the server
// joinable when that happens:
//
// 1. Self-test at the first map start (the engine registers its network messages after plugins load). The layout that the 12 Nov update changed (the thing that froze the server) is
//    checked with reads that cannot crash (hns_netmsg): a message record that does not look valid switches the plugin
//    off; a message that is only missing just disables the sends that need it.
//
// 2. Crash breaker, per CS2 server version. The first time the plugin runs on a CS2 version it has not run on before,
//    it writes "<version> probing" to cfg/hns-movement/.runstate as soon as a human player is in the game. Once a player
//    has been in the game for hns_safety_confirm_seconds (default 180) without the server dying, it writes
//    "<version> good". If the server dies before that (freeze -> watchdog, crash), the next start still finds
//    "probing" for the same version and assumes the plugin caused it. A clean shutdown writes "unconfirmed" instead,
//    so restarting the server never trips it.
//
// If either check fails, the plugin starts in SAFE MODE: it stays loaded but installs no hooks and changes nothing,
// exactly as if hns-movement.vdf had been renamed to .off, and prints a warning on every map start. The movement cvars
// fall back to hns_movement_fallback.cfg. hns_safety_reset (server console) clears the breaker once a fixed build is in.
// hns_safety 0 in hns-movement.cfg turns the breaker off (the self-test always runs).
namespace hns::safety
{
#define HNS_RUNSTATE_PATH "cfg/hns-movement/.runstate"

	// Called in Load, before the hooks. Returns false when the crash breaker tripped: the plugin then stays loaded in
	// safe mode and must not install hooks. reason receives a short explanation.
	bool CheckBeforeHooks(char *reason, size_t maxlen);

	// True when the plugin is switched off (breaker at load, or the self-test at map start failed).
	bool IsSafeMode();

	// At map start, before any player can join. Returns false (and switches to safe mode) if the network message table
	// does not look like this build expects; the caller must then remove its hooks.
	bool RunSelfTest(char *reason, size_t maxlen);

	// From GameFramePre: counts time with a human in the game and marks this CS2 version good once it is long enough.
	void OnGameFrame();

	// From Unload (clean shutdown or meta unload).
	void OnUnload();

	// Printed on every map start while in safe mode.
	void PrintSafeModeWarning();

	void PrintStatus();

	// Copies n (max 512) bytes from addr without crashing if addr is not readable. Copy into plain bytes only: never
	// into an engine type with a destructor (a CUtlString copy frees the engine's string when it goes out of scope).
	bool SafeRead(const void *addr, void *out, size_t n);
} // namespace hns::safety
