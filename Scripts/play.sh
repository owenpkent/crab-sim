#!/usr/bin/env bash
# Start the game windowed for a person to play. No checks, and it leaves the
# game running: closing this terminal or pressing Ctrl-C here does not stop it,
# close the game window instead. The log goes to Saved/PlaySession/<time>/game.log.
#
#   PLAY_RESX, PLAY_RESY   window size (default 1280x720)
#   PLAY_EXEC=<cmds>       console commands run at startup, comma separated,
#                          e.g. "CrabSim.StateLog 1" (default: none)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
export DISPLAY="${DISPLAY:-:0}"

RESX="${PLAY_RESX:-1280}"
RESY="${PLAY_RESY:-720}"
OUT="$ROOT/Saved/PlaySession/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"

ARGS=( -game -windowed -ResX="$RESX" -ResY="$RESY" -nosplash -stdout -FullStdOutLogOutput )
if [ -n "${PLAY_EXEC:-}" ]; then
	ARGS+=( -ExecCmds="$PLAY_EXEC" )
fi

nohup "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$ROOT/CrabSim.uproject" "${ARGS[@]}" \
	> "$OUT/game.log" 2>&1 &
disown

echo "Started the game (${RESX}x${RESY} windowed). Close its window when you are done."
echo "Log: $OUT/game.log"
