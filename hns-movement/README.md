# hns-movement

Metamod:Source plugin for CS2 Hide and Seek servers. It gives HNS the movement fixes from CS2KZ's Classic mode
(consistent perfect bhops, ladder fixes, rampbug/surf fixes, duck fixes) and enforces the movement cvars.
Gameplay (CT freeze, knife, team switching) stays in ChaseMod; the two plugins run side by side.

Forked from [cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod) at commit `c796a0d` (2026-10-14).
License: AGPL-3.0 (inherited). If you run a modified version on a public server you must offer its source.

## What is in it

| Area | Files | Origin |
|---|---|---|
| Hook layer (27 movement detours + `TracePlayerBBox`) | `src/utils/hooks.cpp` | CS2KZ, trimmed to movement only |
| Player / movement model | `src/player/`, `src/movement/` | CS2KZ, unchanged |
| Classic movement logic | `src/hns/hns_movement.*` | Port of `kz_mode_ckz.cpp`; HNS changes marked `HNS:` |
| Player | `src/hns/hns_player.*` | Replaces `KZPlayer` (no timer, HUD, jumpstats, DB, anticheat) |
| Cvar ownership | `src/hns/hns_cvars.*` | New, same write method as CS2KZ's `ApplyModeSettings` |
| Config | `src/hns/hns_config.*`, `cfg/` | New |
| Commands | `src/hns/hns_commands.*` | New |
| Gamedata | `gamedata/hns-movement.games.txt` | Verbatim copy of `cs2kz-core.games.txt` |

Ported from Classic mode, unchanged in behaviour: subtick normalisation (`when` snapped to 0 / 0.5, forced subtick moves),
jump-release handling, 0.02 s perf window, KZ perf speed formula and prestrafe, perf height snap, velocity quantization fix
(ladders), crouch-jump bind removal, duck-spam slowdown fix, view-angle interpolation, rampbug fix, ground-trace fix,
slope fix, water fix, ladder takeoff/landing handling.

Not ported: timer-zone gating, distance tiers, and triggerfix (`TouchTriggersAlongPath` needs CS2KZ's trigger service;
the TryPlayerMove path is still recorded so it can be added later).

## Install

1. Metamod:Source 2.x must be installed. Copy the contents of `build/package/` into `game/csgo/`.
2. Remove the `// Movement related` block and `mp_solid_teammates 2` from your server cfg (`CVARS.txt`), except
   `sv_falldamage_scale`, which ChaseMod still owns. Add `exec hns-movement/hns_movement_fallback` in its place, so the values still apply if the plugin fails to load.
3. Start the server and run `meta list` and `hns_status`.

## Configuration

`csgo/cfg/hns-movement/hns-movement.cfg` (written with defaults on first start if missing). Two kinds of lines:

- Movement cvars (`sv_airaccelerate 1000`, ...): written into the cvar data every tick, so map cfgs, `mp_restartgame` or
  a console typo cannot change them. A change from outside prints one warning and is overwritten.
- Plugin settings:

| Setting | Default | Meaning |
|---|---|---|
| `hns_ps_enabled` | 1 | 1 = CS2KZ-identical: ground speed capped at 250 + prestrafe (max 276), which overrides `sv_maxspeed` on the ground. 0 = no clamp, perf keeps max(landing, takeoff) speed. |
| `hns_rampbug_threshold` | 0.999 | Rampbug fix plane threshold (CS2KZ constant). |
| `hns_rampbug_same_plane` | 0.99 | Rampbug fix same-ramp threshold (CS2KZ constant). |
| `hns_rampbug_min_distance` | 1.0 | Rampbug fix minimum gain (CS2KZ constant). |
| `hns_warn_external_change` | 1 | Warn when something else changes an enforced cvar. |
| `hns_debug_perf` | 0 | Print every takeoff (perf, ground time, speeds) to the console. |
| `hns_understab` | 1 | Deny a CT stab when the T stands (or last stood before jumping) more than `hns_understab_height` units above the floor the CT is on or above. Both players see "UNDERSTAB! Stab Denied" (UNDERSTAB! in red). A T standing on (or who last stood on) a CT's head can always be stabbed, and there is no height check when either player is on a ladder. |
| `hns_understab_height` | 65 | Understab height limit in units. |
| `hns_no_team_damage` | 1 | Players never damage their teammates (knife, grenades, anything), whatever `mp_friendlyfire` is set to. |
| `hns_solid_teammates` | 0 | Enforced into `mp_solid_teammates` (0 = pass through, 1 = solid, 2 = solid, no standing on heads). 0 matches CS2KZ; the rampbug fix assumes teammates don't collide. |

Not owned by the plugin, on purpose: `mp_solid_enemies` (Ts and CTs still collide), `sv_falldamage_scale` (ChaseMod),
and all game mode, round and bot cvars.

### Commands (server console / rcon)

| Command | |
|---|---|
| `hns_reload` | Reload the cfg and resend the cvars to every client. |
| `hns_status` | Enforced value, live game value and CS2KZ value for every owned cvar, plus settings. |
| `hns_write_fallback` | Regenerate `cfg/hns-movement/hns_movement_fallback.cfg` from the enforced values. |
| `hns_perf_stats` | Jumps, perfs and perf % per player. |

### Why replication is turned off on the owned cvars

The plugin writes cvar memory directly every tick (like CS2KZ). That never triggers the engine's replication, so clients
would keep stale values and mispredict. CS2KZ solves this by removing `FCVAR_REPLICATED` and sending the values to each
client itself (`SendConVarValue`) when the player becomes active. hns-movement does the same on `ClientActive` and on
`hns_reload`. This is the reasoning read from CS2KZ's source; it still needs to be confirmed on a live server (P2 spike).

## Build

Windows (local test server): install Build Tools for Visual Studio 2022 with "Desktop development with C++", Git and
Python 3, then run `build-windows.bat`. It clones this repo from `hns-movement.gitbundle`, fetches the SDK and Metamod,
builds, and copies the plugin into the server (it never overwrites an existing cfg).


Linux (what DatHost runs), same toolchain as CS2KZ:

```
git submodule add https://github.com/alliedmodders/metamod-source.git metamod-source
git submodule add -b cs2 https://github.com/alliedmodders/hl2sdk.git hl2sdk-cs2
git submodule update --init --recursive
mkdir build
docker build . -t hns-movement-builder
docker run -v ./build:/app/build -v ./.git:/app/.git hns-movement-builder
```

Pin the submodules to the commits CS2KZ uses (`hl2sdk-cs2` `aeaa10b`, `metamod-source` `9c49d4c` at fork time).
Windows: see `.github/workflows/build.yml`. The output is `build/package/`.

## After a CS2 update

Signatures break. Run `scripts/sync-gamedata.sh` to copy CS2KZ's latest gamedata, rebuild, and check the server log.
The plugin refuses to load if any movement signature fails to resolve, rather than running with missing detours.

## ChaseMod notes

- ChaseMod's freeze sets `MOVETYPE_OBSOLETE` and its unfreeze sets `MOVETYPE_WALK` plus velocity. hns-movement sees this as a
  movetype change outside movement and clears its per-player rampbug/quantization/view-angle state.
- Still to do in ChaseMod (plan section 6): remember the movetype before freezing instead of always restoring
  `MOVETYPE_WALK` (wrong on a ladder), and replace the hard-coded `64 * 60` tick counts with `Server.TickInterval`.

## Known gaps

- Not yet run on a live server. Builds cleanly against the same SDK and gamedata as CS2KZ `c796a0d`.
- CS2KZ resets `m_flVelocityModifier` to 1.0 every tick. That removes the slowdown after taking damage; kept identical
  for now, but it changes knife-hit feel in HNS and needs a decision after playtesting.
- CS2KZ's turnbind blocker was not ported (scope: movement only, no anticheat).
- Sweep test harness and per-tick CSV logger (plan section 7) are not written yet.
