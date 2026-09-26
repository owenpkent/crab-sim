#!/usr/bin/env bash
# Plays the real game (real renderer, real input stack) with a virtual mouse and
# checks what the crab does, from the game log. Complements the headless
# automation suites (test.sh), which cannot see the window, the cursor or the
# camera.
#
# Needs: an X11 session, write access to /dev/uinput, and xwininfo and xprop
# (plus xwd and ffmpeg for screenshots). Opens a game window and drives the real
# pointer, so leave the machine alone while it runs (about a minute once the
# game has loaded).
#
# The defaults are the reference run: 1280x720 windowed, no sound, whichever GPU
# Vulkan picks. Environment overrides:
#   LIVE_RESX, LIVE_RESY   window size (default 1280x720)
#   LIVE_SOUND=1           keep the audio device (the default passes -nosound)
#   LIVE_VK_ICD=<path>     force one Vulkan driver, e.g.
#                          /usr/share/vulkan/icd.d/intel_icd.json
#   LIVE_EXEC_EXTRA=<cmds> extra console commands, appended to -ExecCmds
#   LIVE_PYTHON            python interpreter (default python3)
# Output goes to Saved/LiveTest/<time>/: game.log, run.txt, one screenshot per step.
# Exit code: 0 all passed, 1 an assertion failed, 2 could not start, 3 the run
# could not proceed (game died or never became ready).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
UE_BIN="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
PYTHON="${LIVE_PYTHON:-python3}"
export DISPLAY="${DISPLAY:-:0}"

RESX="${LIVE_RESX:-1280}"
RESY="${LIVE_RESY:-720}"
EXEC_CMDS="CrabSim.StateLog 1"
# Unreal splits -ExecCmds on commas. A pipe makes it one command with junk on the end.
if [ -n "${LIVE_EXEC_EXTRA:-}" ]; then EXEC_CMDS="$EXEC_CMDS,$LIVE_EXEC_EXTRA"; fi
ARGS=( -game -windowed -ResX="$RESX" -ResY="$RESY" -nosplash -unattended -stdout -FullStdOutLogOutput )
if [ "${LIVE_SOUND:-0}" != "1" ]; then ARGS+=( -nosound ); fi
ARGS+=( -ExecCmds="$EXEC_CMDS" )
# One driver only, so the run proves which GPU it ran on instead of asking Vulkan to choose.
if [ -n "${LIVE_VK_ICD:-}" ]; then
	[ -r "$LIVE_VK_ICD" ] || { echo "LIVE_VK_ICD is not readable: $LIVE_VK_ICD" >&2; exit 2; }
	export VK_DRIVER_FILES="$LIVE_VK_ICD" VK_ICD_FILENAMES="$LIVE_VK_ICD"
fi

# A game started by this script carries -unattended; a play.sh session or an
# editor does not. That is how the two are told apart. The [U] keeps pgrep and
# pkill from matching their own command line.
LIVE_PATTERN='[U]nrealEditor .*CrabSim\.uproject .*-unattended'
ANY_PATTERN='[U]nrealEditor .*CrabSim\.uproject'

nap() { "$PYTHON" -c "import sys, time; time.sleep(float(sys.argv[1]))" "$1"; }

# Wait up to ~10 s for every live-test game to be gone.
wait_gone() {
	for _ in $(seq 1 50); do
		pgrep -f "$LIVE_PATTERN" >/dev/null || return 0
		nap 0.2
	done
	return 1
}

[ -x "$UE_BIN" ] || { echo "UnrealEditor not found at $UE_BIN (set UE_ROOT)." >&2; exit 2; }
[ -w /dev/uinput ] || { echo "/dev/uinput is not writable, so there is no virtual mouse." >&2; exit 2; }
command -v xwininfo >/dev/null && command -v xprop >/dev/null || { echo "xwininfo and xprop are required." >&2; exit 2; }
xwininfo -root >/dev/null 2>&1 || { echo "Cannot reach the X display $DISPLAY." >&2; exit 2; }

if pgrep -f "$LIVE_PATTERN" >/dev/null; then
	echo "Stopping a game left over from an earlier live test..."
	pkill -f "$LIVE_PATTERN" || true
	wait_gone || pkill -9 -f "$LIVE_PATTERN" || true
fi
if pgrep -f "$ANY_PATTERN" >/dev/null; then
	echo "Another CrabSim UnrealEditor is running (the editor, or a play.sh session)." >&2
	echo "Close it first: the test drives the real mouse and must not click into the wrong window." >&2
	exit 2
fi

OUT="$ROOT/Saved/LiveTest/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
GAME_PID=""

cleanup() {
	trap - EXIT
	if [ -n "$GAME_PID" ]; then kill "$GAME_PID" 2>/dev/null || true; fi
	pkill -f "$LIVE_PATTERN" 2>/dev/null || true
	# Wait for it to actually go, so the next run does not find it still holding
	# the window, the log or the GPU.
	wait_gone || pkill -9 -f "$LIVE_PATTERN" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

{
	echo "resolution: ${RESX}x${RESY} windowed"
	echo "sound:      $([ "${LIVE_SOUND:-0}" = "1" ] && echo on || echo "off (-nosound)")"
	echo "vulkan icd: ${LIVE_VK_ICD:-whichever the loader picks}"
	echo "exec cmds:  $EXEC_CMDS"
} > "$OUT/run.txt"

"$UE_BIN" "$ROOT/CrabSim.uproject" "${ARGS[@]}" > "$OUT/game.log" 2>&1 &
GAME_PID=$!

echo "Started the game (pid $GAME_PID). Waiting for CRABSIM_READY, then driving the mouse..."
status=0
( cd "$ROOT/Scripts/live" && "$PYTHON" session.py "$OUT/game.log" "$OUT" "$GAME_PID" ) || status=$?
echo "Output: $OUT"
exit $status
