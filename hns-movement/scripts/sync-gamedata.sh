#!/bin/bash
# Merge CS2KZ's latest gamedata into gamedata/hns-movement.games.txt after a CS2 update (keeps hns-only entries).
# Usage: scripts/sync-gamedata.sh [branch-or-commit]   (default: master). See sync-gamedata.py.
set -euo pipefail
python3 "$(dirname "$0")/sync-gamedata.py" "$@"
git -C "$(dirname "$0")/.." diff --stat -- gamedata/ || true
