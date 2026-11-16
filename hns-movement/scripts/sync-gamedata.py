#!/usr/bin/env python3
"""Pull CS2KZ's latest gamedata into gamedata/hns-movement.games.txt after a CS2 update.

hns-movement shares most of its signatures and offsets with CS2KZ, but also has its own entries that CS2KZ does not
(CBaseEntity_TakeDamageOld, CBasePlayerPawn_RemovePlayerItem, CCSPlayer_ItemServices_GiveNamedItem, ...). So this
merges instead of copying: every entry CS2KZ has is updated (or added), every hns-only entry is kept as it is and
listed at the end so it can be checked by hand (CounterStrikeSharp's gamedata.json has most of them).

Usage: scripts/sync-gamedata.py [branch-or-commit]   (default: master)
"""
import re
import sys
import urllib.request
from pathlib import Path

REF = sys.argv[1] if len(sys.argv) > 1 else "master"
URL = f"https://raw.githubusercontent.com/KZGlobalTeam/cs2kz-metamod/{REF}/gamedata/cs2kz-core.games.txt"
DEST = Path(__file__).resolve().parent.parent / "gamedata" / "hns-movement.games.txt"


def sections(text):
    """{section: [(name, block_text)]} for the Signatures/Offsets/Patches sections of a games.txt."""
    out = {}
    for sec in ("Signatures", "Offsets", "Patches"):
        m = re.search(r'\n[ \t]*"' + sec + r'"\s*\n\s*\{', text)
        if not m:
            continue
        i, depth, start = m.end(), 1, m.end()
        entries = []
        while depth:
            em = re.compile(r'\n([ \t]*)"([^"]+)"\s*\n\s*\{').match(text, i) if depth == 1 else None
            if em:
                # Entry block: include preceding comment lines.
                j, d = em.end(), 1
                while d:
                    d += (text[j] == "{") - (text[j] == "}")
                    j += 1
                bstart = em.start()
                while True:
                    prev = text.rfind("\n", 0, bstart)
                    if text[prev + 1:bstart].strip().startswith("//"):
                        bstart = prev
                    else:
                        break
                entries.append((em.group(2), text[bstart:j]))
                i = j
                continue
            c = text[i]
            depth += (c == "{") - (c == "}")
            i += 1
        out[sec] = (start, entries)
    return out


def main():
    upstream = urllib.request.urlopen(URL).read().decode("utf8")
    ours = DEST.read_text()
    up = sections(upstream)
    changed, added, kept = [], [], []
    for sec, (_, up_entries) in up.items():
        our_entries = dict(sections(ours).get(sec, (0, []))[1])
        for name, block in up_entries:
            if name in our_entries:
                if our_entries[name].strip() != block.strip():
                    ours = ours.replace(our_entries[name], block, 1)
                    changed.append(f"{sec}/{name}")
            else:
                start = sections(ours)[sec][0]
                ours = ours[:start] + block + ours[start:]
                added.append(f"{sec}/{name}")
        up_names = {n for n, _ in up_entries}
        kept += [f"{sec}/{n}" for n in our_entries if n not in up_names]
    DEST.write_text(ours)
    print(f"Merged cs2kz-metamod@{REF} into {DEST.name}")
    print("  updated:", ", ".join(changed) or "nothing")
    print("  added:  ", ", ".join(added) or "nothing")
    print("  hns-only entries kept as they are (check these by hand):", ", ".join(kept) or "none")


if __name__ == "__main__":
    main()
