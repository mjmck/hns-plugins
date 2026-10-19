# HNS sound addon

Custom audio in CS2 can only reach players through a Workshop addon that their client downloads. This folder is the
source of that addon (same approach as CS2KZ, whose `kz.*` jumpstat sounds live in Workshop addon 3469155349 and are
mounted with MultiAddonManager).

```
addon/
  soundevents/soundevents_hns.vsndevts   sound event definitions (hns.join.vip -> sounds/hns/join/vip.vsnd)
  sounds/hns/join/vip.wav                the audio file (add it yourself)
```

## Build and publish

1. Install the CS2 Workshop Tools (Steam > Library > Counter-Strike 2 > Properties > DLC > CS2 Workshop Tools).
2. Launch them, create a new addon called `hns_sounds`.
3. Copy `soundevents/` and `sounds/` from this folder into
   `<CS2>/content/csgo_addons/hns_sounds/` (the tools compile them into `game/csgo_addons/hns_sounds/` on save/load:
   open the Asset Browser, find `vip.wav` and the vsndevts, and make sure both compile without errors).
4. Test locally: start your local server with the addon (`-addon hns_sounds` or via MultiAddonManager on the local
   server), join, and run `hns_join_sound_test hns.join.vip` in the server console.
5. Publish it from the Workshop Tools (Tools > Workshop Upload). Note the Workshop item ID.

## Server

1. Install [MultiAddonManager](https://github.com/Source2ZE/MultiAddonManager) (Metamod plugin) on the server.
2. In `cfg/multiaddonmanager/multiaddonmanager.cfg`: `mm_extra_addons "<workshop id>"` (comma-separate if there are more).
3. `cfg/hns-movement/join-sounds.cfg` already precaches `soundevents/soundevents_hns.vsndevts`; add the player's line:
   `76561198xxxxxxxxx  hns.join.vip  1.0`, then `hns_reload`.

To add another player, add another event to the vsndevts (e.g. `hns.join.<name>` -> `sounds/hns/join/<name>.vsnd`),
republish the addon, and add their line to join-sounds.cfg.
