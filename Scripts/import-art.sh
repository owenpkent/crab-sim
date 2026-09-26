#!/usr/bin/env bash
# Build the Crab Sim materials and import Art/export (textures, SM_*.fbx, SK_FiddlerCrab.fbx,
# A_FiddlerCrab_*.fbx), headless, via editor Python (Art/unreal/build_content.py).
# Idempotent: re-run it whenever the art changes. Missing art is skipped with a warning.
# Exits non-zero if Python fails.
# UE_ROOT overrides the engine location; CRABSIM_EXPORT_DIR overrides Art/export;
# CRABSIM_IMPORT_SCALE forces the FBX import scale (default: auto-detect from mesh size).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
SCRIPT="$ROOT/Art/unreal/build_content.py"
LOG="$ROOT/Saved/Logs/import-art.log"
mkdir -p "$(dirname "$LOG")"

set +e
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$ROOT/CrabSim.uproject" \
	-run=pythonscript -script="$SCRIPT" \
	-unattended -nullrhi -nosplash -nosound -nopause -stdout -FullStdOutLogOutput \
	-log 2>&1 | tee "$LOG"
STATUS=${PIPESTATUS[0]}
set -e

if [ "$STATUS" -ne 0 ]; then
	echo "import-art: editor exited with $STATUS (log: $LOG)" >&2
	exit "$STATUS"
fi
if ! grep -q "CRABSIM_BUILD_CONTENT_OK" "$LOG"; then
	echo "import-art: build_content.py did not report success (log: $LOG)" >&2
	exit 1
fi
echo "import-art: done (log: $LOG)"
