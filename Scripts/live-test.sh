#!/usr/bin/env bash
# Plays the real game (real renderer, real input stack) with a virtual mouse and
# checks what the crab and the sea do, from the game log. Complements the
# headless automation suites (test.sh), which cannot see the window, the cursor
# or the camera.
#
# Needs: an X11 session, write access to /dev/uinput, and xwininfo and xprop
# (plus xwd and ffmpeg for screenshots). Opens a game window and drives the real
# pointer, so leave the machine alone while it runs (a few minutes: each
# scenario is its own game launch).
#
# SCENARIOS (run one after the other, each in a fresh game launch)
#   basic  launched with CrabSim.StateLog 1,CrabSim.TideSpeed 0 (tide frozen at
#          low tide). In order: ready; dance (click the crab: dance on, crab
#          faces the camera and stays put, click again: dance off); burrow (click
#          burrow2: walks in and digs in, then a click elsewhere brings it out
#          and it walks); walk right; walk up; dash; dash again.
#   tide   launched with CrabSim.StateLog 1,CrabSim.TideSpeed 10 (a whole tide
#          takes 18 s; time limits below are for that speed). The mouse is never touched. In order: the water rises;
#          no sweep while the water is below the crab; depth over 45 uu and a
#          surge_begin event; grip falls below 0.7; swept_out event and swept=1;
#          the crab lands within 400 uu of burrow0's beach with depth 0; the
#          water falls again. A screenshot is saved at high tide.
#   forage launched with CrabSim.StateLog 1,CrabSim.TideSpeed 0 (tide frozen at
#          low tide, so it cannot interfere). In order: click patch3 (food_begin,
#          food rising while the crab stands on it); click away (food_end with an
#          amount); on dry sand clear of every burrow and patch, click the HUD's
#          dig button (dig_begin, no walk ordered, the crab stays put, dig_done
#          after 4 s, food down by about 0.30, dug=1, a new burrow4 in the screen
#          log); click the new hole (burrow_enter, burrow=4).
#   molt   launched with CrabSim.StateLog 1,CrabSim.TideSpeed 0,CrabSim.FoodFloor 0.9
#          (tide frozen at low water; the test-only food floor keeps the crab fed,
#          so it needs no foraging). In order: click the HUD's molt button out in
#          the open (molt_refused, no walk, the crab stays put); click burrow2 (dug
#          in); click the molt button (molt_begin, no walk, still dug in, molt
#          rising); click elsewhere after 3 s (molt_cancel, burrow_exit, nothing
#          counted, no food spent); dig in again, molt to the end (molt_done after
#          10 s, the crab stays put, 0.80 food paid, molts=1, scale 1.08, grip
#          full); click elsewhere (burrow_exit, nothing cancelled). About a minute.
#
# Environment overrides:
#   LIVE_SCENARIOS="basic tide forage molt"  which scenarios to run, in this order (space or
#                          comma separated). LIVE_SCENARIOS=tide runs only one.
#   LIVE_TIDE_SPEED=<n>    CrabSim.TideSpeed for the tide scenario (default 3). Below 10
#                          the session multiplies its tide time limits by 10/n.
#   LIVE_RESX, LIVE_RESY   window size (default 1280x720)
#   LIVE_SOUND=1           keep the audio device (the default passes -nosound)
#   LIVE_VK_ICD=<path>     force one Vulkan driver, e.g.
#                          /usr/share/vulkan/icd.d/intel_icd.json
#   LIVE_EXEC_EXTRA=<cmds> extra console commands, appended to every scenario's
#                          -ExecCmds (comma separated)
#   LIVE_PYTHON            python interpreter (default python3)
#   UE_ROOT                Unreal install (default ~/UnrealEngine/UE_5.8)
#
# Output goes to Saved/LiveTest/<time>/<scenario>/: game.log, run.txt, session.txt
# (what the checks printed) and one screenshot per step.
# Exit code: the worst of the scenarios. 0 all passed, 1 an assertion failed,
# 2 could not start, 3 a run could not proceed (game died or never became ready).
#
# Safety: the script refuses to start while any other CrabSim UnrealEditor (the
# editor, a play.sh session, another agent's game) is running, because it drives
# the real pointer. It only ever stops games it launched itself, found by the
# pid it started and by the -CrabSimLiveTest marker on their command line.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
UE_BIN="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
PYTHON="${LIVE_PYTHON:-python3}"
export DISPLAY="${DISPLAY:-:0}"

RESX="${LIVE_RESX:-1280}"
RESY="${LIVE_RESY:-720}"
SCENARIOS="${LIVE_SCENARIOS:-basic tide forage molt}"
SCENARIOS="${SCENARIOS//,/ }"
TIDE_SPEED="${LIVE_TIDE_SPEED:-3}"
[[ "$TIDE_SPEED" =~ ^[0-9]+(\.[0-9]+)?$ ]] || { echo "LIVE_TIDE_SPEED must be a number: $TIDE_SPEED" >&2; exit 2; }
export LIVE_TIDE_SPEED="$TIDE_SPEED"   # the session stretches its tide time limits when this is below 10

# Console commands per scenario. Unreal splits -ExecCmds on commas. A pipe makes
# it one command with junk on the end.
exec_cmds_for() {
	case "$1" in
		basic) echo "CrabSim.StateLog 1,CrabSim.TideSpeed 0" ;;
		tide)  echo "CrabSim.StateLog 1,CrabSim.TideSpeed $TIDE_SPEED" ;;
		forage) echo "CrabSim.StateLog 1,CrabSim.TideSpeed 0" ;;
		molt)  echo "CrabSim.StateLog 1,CrabSim.TideSpeed 0,CrabSim.FoodFloor 0.9" ;;
		*)     return 1 ;;
	esac
}

BASE_ARGS=( -game -windowed -ResX="$RESX" -ResY="$RESY" -nosplash -unattended -stdout -FullStdOutLogOutput
	-CrabSimLiveTest )
if [ "${LIVE_SOUND:-0}" != "1" ]; then BASE_ARGS+=( -nosound ); fi
# One driver only, so the run proves which GPU it ran on instead of asking Vulkan to choose.
if [ -n "${LIVE_VK_ICD:-}" ]; then
	[ -r "$LIVE_VK_ICD" ] || { echo "LIVE_VK_ICD is not readable: $LIVE_VK_ICD" >&2; exit 2; }
	export VK_DRIVER_FILES="$LIVE_VK_ICD" VK_ICD_FILENAMES="$LIVE_VK_ICD"
fi

# A game started by this script carries -CrabSimLiveTest; a play.sh session, an
# editor or another agent's game does not. That is how they are told apart.
# (-unattended alone is not enough: other tools pass it too.) The [U] keeps pgrep
# and pkill from matching their own command line.
LIVE_PATTERN='[U]nrealEditor .*CrabSim\.uproject .*-CrabSimLiveTest'
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

for scn in $SCENARIOS; do
	exec_cmds_for "$scn" >/dev/null || { echo "Unknown scenario '$scn' (known: basic tide forage molt)." >&2; exit 2; }
done
[ -n "${SCENARIOS// /}" ] || { echo "LIVE_SCENARIOS is empty." >&2; exit 2; }

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
	echo "Another CrabSim UnrealEditor is running (the editor, a play.sh session, or another agent's game)." >&2
	echo "Close it first: the test drives the real mouse and must not click into the wrong window." >&2
	exit 2
fi

OUT="$ROOT/Saved/LiveTest/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
GAME_PID=""

# Stop the game this script started, and wait for it to actually go, so the next
# launch does not find it still holding the window, the log or the GPU.
stop_game() {
	if [ -n "$GAME_PID" ]; then kill "$GAME_PID" 2>/dev/null || true; fi
	pkill -f "$LIVE_PATTERN" 2>/dev/null || true
	if ! wait_gone; then
		pkill -9 -f "$LIVE_PATTERN" 2>/dev/null || true
		wait_gone || true
	fi
	GAME_PID=""
}

cleanup() {
	trap - EXIT
	stop_game
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# One scenario: launch the game, drive it, stop it. Prints nothing but progress.
# Returns the session's exit code.
run_scenario() {
	local scn="$1" dir="$OUT/$1" cmds st=0
	cmds="$(exec_cmds_for "$scn")"
	if [ -n "${LIVE_EXEC_EXTRA:-}" ]; then cmds="$cmds,$LIVE_EXEC_EXTRA"; fi
	mkdir -p "$dir"
	{
		echo "scenario:   $scn"
		echo "resolution: ${RESX}x${RESY} windowed"
		echo "sound:      $([ "${LIVE_SOUND:-0}" = "1" ] && echo on || echo "off (-nosound)")"
		echo "vulkan icd: ${LIVE_VK_ICD:-whichever the loader picks}"
		echo "exec cmds:  $cmds"
	} > "$dir/run.txt"

	"$UE_BIN" "$ROOT/CrabSim.uproject" "${BASE_ARGS[@]}" -ExecCmds="$cmds" > "$dir/game.log" 2>&1 &
	GAME_PID=$!

	echo "== scenario $scn: started the game (pid $GAME_PID). Waiting for CRABSIM_READY, then driving it..."
	( cd "$ROOT/Scripts/live" && PYTHONUNBUFFERED=1 "$PYTHON" session.py "$dir/game.log" "$dir" "$GAME_PID" "$scn" ) \
		2>&1 | tee "$dir/session.txt" || st=$?
	# Say so when the game went away by itself (a crash, or something else killed it).
	local state gs=0
	state="$(ps -o stat= -p "$GAME_PID" 2>/dev/null | tr -d ' ' || true)"
	if [ -z "$state" ] || [ "${state:0:1}" = "Z" ]; then
		wait "$GAME_PID" 2>/dev/null || gs=$?
		echo "NOTE: the game had already exited on its own (status $gs; 143 is SIGTERM, 137 is SIGKILL)."
	fi
	stop_game
	return "$st"
}

overall=0
summary=""
first=1
for scn in $SCENARIOS; do
	# A moment between launches so the previous window and GPU context are really gone.
	if [ "$first" = "0" ]; then nap 2; fi
	first=0
	rc=0
	run_scenario "$scn" || rc=$?
	summary="$summary  $scn=$rc"
	if [ "$rc" -gt "$overall" ]; then overall="$rc"; fi
done

echo
echo "Scenario exit codes:$summary"
echo "Output: $OUT"
exit "$overall"
