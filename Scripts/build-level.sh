#!/usr/bin/env bash
# Build /Game/Maps/Beach (lighting only) and /Game/Maps/MaterialTest (terrain and water
# material test bed), headless, via editor Python (Art/unreal/build_level.py). Run
# Scripts/import-art.sh first so the materials exist. Idempotent. Exits non-zero if Python fails.
# UE_ROOT overrides the engine location. CRABSIM_TEST_CAMERA picks the MaterialTest camera
# (game, surf, shore, water, crab, top; default game).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
SCRIPT="$ROOT/Art/unreal/build_level.py"
LOG="$ROOT/Saved/Logs/build-level.log"
mkdir -p "$(dirname "$LOG")"

set +e
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$ROOT/CrabSim.uproject" \
	-run=pythonscript -script="$SCRIPT" \
	-unattended -nullrhi -nosplash -nosound -nopause -stdout -FullStdOutLogOutput \
	-log 2>&1 | tee "$LOG"
STATUS=${PIPESTATUS[0]}
set -e

if [ "$STATUS" -ne 0 ]; then
	echo "build-level: editor exited with $STATUS (log: $LOG)" >&2
	exit "$STATUS"
fi
if ! grep -q "CRABSIM_BUILD_LEVEL_OK" "$LOG"; then
	echo "build-level: build_level.py did not report success (log: $LOG)" >&2
	exit 1
fi
echo "build-level: done (log: $LOG)"
