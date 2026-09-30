#!/usr/bin/env bash
# Record a scripted tour of the real game as video: launch the game windowed at
# 1280x720, wait until it is ready and focused, capture its client area with
# ffmpeg (x11grab, 30 fps, pointer drawn, libx264 crf 23, yuv420p, no audio),
# play the tour through the real virtual pointer (Scripts/live/tour.py), stop
# ffmpeg and close the game.
#
#   Scripts/record.sh [OUT.mp4]         default videos/crab-sim-tour.mp4
#   RECORD_TOUR=molt Scripts/record.sh  the molt clip instead, default videos/crab-sim-molt.mp4
#   RECORD_TOUR=playtest SPEEDUP=4 CRF=30 Scripts/record.sh   a whole round played by the bot, videos/crab-sim-playtest.mp4
#   RECORD_TOUR=onestick Scripts/record.sh   one-stick mode, videos/crab-sim-onestick.mp4
#   RECORD_TOUR=orbit Scripts/record.sh   the camera orbit, videos/crab-sim-orbit.mp4
#   LIMIT=150 CRF=28 Scripts/record.sh videos/small.mp4
#   SPEEDUP=2 Scripts/record.sh videos/fast.mp4       the tour at 2x, after the capture
#   RECORD_TIDE_SPEED=1.5 Scripts/record.sh           a faster tide (see below)
#
# The tour, in order: the beach at low tide; forage (click a food patch, the crab
# walks there and feeds, the food bar fills); dig (click away, click the HUD's dig
# button, four seconds of standing still, a new hole); that burrow (walk off it,
# click it, the crab digs in, click elsewhere to come out); scuttle (hold left, the
# crab follows the cursor round a loop); dance (click the crab, click it again);
# dash (right click); then the crab waves at the incoming tide, the surge takes its
# grip and the sea sweeps it out to the dunes, and a few seconds of hold. About
# 105 s at real speed. The pointer glides between targets so a viewer can follow it.
# RECORD_TOUR=molt records the molt tour (Scripts/live/tour_molt.py, about 70 s) instead: the molt button
# refuses in the open, three molts in a burrow each ten seconds with the progress ring, the crab climbing out
# bigger after the first two, the third ending the round, the results panel and NEW ROUND. It runs with
# CrabSim.FoodFloor 0.9 (test only: the crab stays fed, so there is no foraging) and a slow tide (0.3 unless
# RECORD_TIDE_SPEED says otherwise), so the burrow stays dry.
# RECORD_TOUR=playtest records Scripts/live/playtest.py instead: a reactive bot plays one full round (three molts, up
# to 20 minutes) at the default tide with no cheats, left clicks only, and the run folder gets its clicks.csv. LIMIT
# defaults to 1320 s for it; the clip is the whole round, so SPEEDUP=4 CRF=30 keeps it short.
# RECORD_TOUR=onestick records Scripts/live/tour_onestick.py instead (about 40 s, LIMIT defaults to 90 s): one-stick
# mode (CrabSim.OneStick 1), driven by a virtual gamepad (Scripts/live/gamepad.py) alongside the mouse. The MENU
# column with its cursor box; a tap moving the cursor to FOOD, to BURROW, and back; a right tap selecting FOOD (the
# crab walks to the best patch); a triple tap into STEER; the stick held over at full deflection, then at partial
# deflection (visibly slower: CrabStick::SteerSpeedMultiplier); a triple tap back to the menu, the crab stopping; a
# mouse click on the ONE STICK button turning it off. It runs with the tide frozen (CrabSim.TideSpeed 0, unless
# RECORD_TIDE_SPEED says otherwise) so nothing about the water can interrupt it.
# RECORD_TOUR=orbit records Scripts/live/tour_orbit.py instead (about 60 s, LIMIT defaults to 90 s): the
# free camera orbit (a right drag turns yaw sideways and pitch up/down, CrabSim.OrbitDegreesPerPixel and
# OrbitPitchDegreesPerPixel; the pointer is locked inside the view while held, so a turn
# bigger than one drag's room is a release and a fresh press from the other side). In order: the default
# view; a slow drag right across most of the view (a bit under a full turn round the crab); a drag down to
# look almost straight down on the crab; a drag up, low across the sand; a slow wide circle with the
# pointer while held, swinging yaw and pitch together round and over the crab; a click on the crab starts
# the dance and a smaller circle orbits round it while it dances (the crab keeps turning to face the
# camera); a second click stops the dance; a plain right click (no drag) still dashes, on release, not on
# press; a final drag brings the camera back near the default view. It runs with the tide frozen
# (CrabSim.TideSpeed 0, unless RECORD_TIDE_SPEED says otherwise) and CrabSim.Gulls 0.
# Next to OUT.mp4 goes OUT.events.txt: what the tour did and what the game
# logged, each with its time in the video (video=SECONDS, good to about half a
# second, and divided by SPEEDUP).
#
# The game runs with CrabSim.StateLog 1 (the tour reads its log; it draws
# nothing) and CrabSim.TideSpeed set by RECORD_TIDE_SPEED. The tide clock starts
# with the game, so the forage, dig and burrow beats have to finish before the water
# reaches the flats where they happen: at the default speed 0.75 that is about 50 s into the
# clip, and faster tides leave less time. Full HUD (tide gauge, grip bar, hints) stays on.
# The tour and molt clips run with CrabSim.Gulls 0 so no gull interrupts them; the playtest runs with gulls on;
# the onestick and orbit clips also run with CrabSim.Gulls 0 (and the tide frozen, above).
#
# Needs: an X11 session, write access to /dev/uinput, xwininfo, xprop, and ffmpeg
# with x11grab and libx264. Opens a game window and drives the real pointer, so
# leave the machine alone while it runs (about 2 minutes, most of it the game starting).
# Do not let another window cover the game: the capture is of the screen.
#
# Environment overrides:
#   RECORD_TOUR=tour|molt|playtest|onestick|orbit  which tour to play (default tour)
#   RECORD_TIDE_SPEED=<n>  CrabSim.TideSpeed (default 0.75: a whole tide takes 240 s; 0.3 for the molt tour)
#   LIMIT=<s>              stop recording after this many seconds (default 240, 1320 for the playtest)
#   FPS=30  CRF=23         capture frame rate and x264 quality (lower is better)
#   SPEEDUP=1              play the video this many times faster, after the capture
#   TOUR_HOLD=<s>          seconds kept after the sweep (default 3.5)
#   RECORD_RESX, RECORD_RESY  window size (default 1280x720)
#   RECORD_CURSOR_SIZE=<px>  size of the pointer arrow the game shows, so it reads
#                          in the video (default 48; XCURSOR_SIZE, for the game only)
#   RECORD_SOUND=1         keep the audio device (the default passes -nosound;
#                          the video has no audio track either way)
#   RECORD_VK_ICD=<path>   force one Vulkan driver, e.g.
#                          /usr/share/vulkan/icd.d/nvidia_icd.json
#   RECORD_EXEC_EXTRA=<cmds>  extra console commands, appended to -ExecCmds
#   RECORD_PYTHON          python interpreter (default python3)
#   UE_ROOT                Unreal install (default ~/UnrealEngine/UE_5.8)
#
# Output, besides the mp4 and events file: Saved/Record/<time>/ with game.log,
# tour.txt (what the tour printed), ffmpeg.log and run.txt.
# Exit code: 0 recorded and every beat happened, 1 recorded but a beat did not,
# 2 could not start, 3 the run could not proceed (game died or never ready).
#
# Safety: the script refuses to start while any other CrabSim UnrealEditor (the
# editor, a play.sh session, a live test, another agent's game) is running,
# because it drives the real pointer. It only ever stops the processes it
# started (the game, the tour and ffmpeg), by the pids it got when it launched
# them. It never matches processes by name.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
UE_BIN="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
PYTHON="${RECORD_PYTHON:-python3}"
export DISPLAY="${DISPLAY:-:0}"

case "${1:-}" in -h|--help) awk 'NR > 1 && /^#/ {sub(/^# ?/, ""); print; next} NR > 1 {exit}' "$0"; exit 0 ;; esac
TOUR="${RECORD_TOUR:-tour}"
case "$TOUR" in
	tour) TOUR_SCRIPT=tour.py; TOUR_TIDE=0.75; TOUR_EXEC=",CrabSim.Gulls 0" ;;
	molt) TOUR_SCRIPT=tour_molt.py; TOUR_TIDE=0.3; TOUR_EXEC=",CrabSim.FoodFloor 0.9,CrabSim.Gulls 0" ;;
	playtest) TOUR_SCRIPT=playtest.py; TOUR_TIDE=1; TOUR_EXEC="" ;;
	onestick) TOUR_SCRIPT=tour_onestick.py; TOUR_TIDE=0; TOUR_EXEC=",CrabSim.OneStick 1,CrabSim.Gulls 0" ;;
	orbit) TOUR_SCRIPT=tour_orbit.py; TOUR_TIDE=0; TOUR_EXEC=",CrabSim.Gulls 0" ;;
	*) echo "RECORD_TOUR must be tour, molt, playtest, onestick or orbit: $TOUR" >&2; exit 2 ;;
esac
DEFAULT_LIMIT=240
if [ "$TOUR" = "playtest" ]; then DEFAULT_LIMIT=1320; fi
if [ "$TOUR" = "onestick" ]; then DEFAULT_LIMIT=90; fi
if [ "$TOUR" = "orbit" ]; then DEFAULT_LIMIT=90; fi
OUT="${1:-$ROOT/videos/crab-sim-$TOUR.mp4}"
RESX="${RECORD_RESX:-1280}"
RESY="${RECORD_RESY:-720}"
TIDE_SPEED="${RECORD_TIDE_SPEED:-$TOUR_TIDE}"
LIMIT="${LIMIT:-$DEFAULT_LIMIT}"
FPS="${FPS:-30}"
CRF="${CRF:-23}"
SPEEDUP="${SPEEDUP:-1}"
for v in TIDE_SPEED LIMIT FPS CRF SPEEDUP; do
	[[ "${!v}" =~ ^[0-9]+(\.[0-9]+)?$ ]] || { echo "$v must be a number: ${!v}" >&2; exit 2; }
done

[ -x "$UE_BIN" ] || { echo "UnrealEditor not found at $UE_BIN (set UE_ROOT)." >&2; exit 2; }
[ -w /dev/uinput ] || { echo "/dev/uinput is not writable, so there is no virtual mouse." >&2; exit 2; }
command -v xwininfo >/dev/null && command -v xprop >/dev/null || { echo "xwininfo and xprop are required." >&2; exit 2; }
command -v ffmpeg >/dev/null && command -v ffprobe >/dev/null || { echo "ffmpeg and ffprobe are required." >&2; exit 2; }
xwininfo -root >/dev/null 2>&1 || { echo "Cannot reach the X display $DISPLAY." >&2; exit 2; }

# Any CrabSim game or editor at all. The [U] keeps pgrep from matching its own command line.
if pgrep -f '[U]nrealEditor .*CrabSim\.uproject' >/dev/null; then
	echo "Another CrabSim UnrealEditor is running (the editor, a play.sh session, a live test, or another agent's game)." >&2
	echo "Close it first: the tour drives the real mouse and must not click into the wrong window." >&2
	exit 2
fi

mkdir -p "$(dirname "$OUT")"
OUT="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
EVENTS="${OUT%.mp4}.events.txt"
RUN="$ROOT/Saved/Record/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN"
GAME_PID=""
TOUR_PID=""
FF_PID=""

# Stop only what this script started, and wait for it, so the next launch does
# not find the window, the log or the GPU still held.
stop_pid() {
	local pid="$1" name="$2" sig="${3:-TERM}"
	[ -n "$pid" ] || return 0
	kill -0 "$pid" 2>/dev/null || { wait "$pid" 2>/dev/null || true; return 0; }
	kill "-$sig" "$pid" 2>/dev/null || true
	for _ in $(seq 1 100); do
		kill -0 "$pid" 2>/dev/null || { wait "$pid" 2>/dev/null || true; return 0; }
		sleep 0.1
	done
	echo "$name (pid $pid) did not stop on SIG$sig; killing that pid" >&2
	kill -KILL "$pid" 2>/dev/null || true
	wait "$pid" 2>/dev/null || true
}

cleanup() {
	trap - EXIT
	stop_pid "$TOUR_PID" "the tour" INT
	stop_pid "$FF_PID" "ffmpeg" INT
	stop_pid "$GAME_PID" "the game" TERM
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

EXEC_CMDS="CrabSim.StateLog 1,CrabSim.TideSpeed $TIDE_SPEED$TOUR_EXEC"
if [ -n "${RECORD_EXEC_EXTRA:-}" ]; then EXEC_CMDS="$EXEC_CMDS,$RECORD_EXEC_EXTRA"; fi
# -CrabSimRecord marks a game started here. It is only a label: nothing is ever matched by it.
GAME_ARGS=( -game -windowed -ResX="$RESX" -ResY="$RESY" -nosplash -unattended -stdout -FullStdOutLogOutput
	-CrabSimRecord )
if [ "${RECORD_SOUND:-0}" != "1" ]; then GAME_ARGS+=( -nosound ); fi
if [ -n "${RECORD_VK_ICD:-}" ]; then
	[ -r "$RECORD_VK_ICD" ] || { echo "RECORD_VK_ICD is not readable: $RECORD_VK_ICD" >&2; exit 2; }
	export VK_DRIVER_FILES="$RECORD_VK_ICD" VK_ICD_FILENAMES="$RECORD_VK_ICD"
fi
{
	echo "output:     $OUT"
	echo "resolution: ${RESX}x${RESY} windowed"
	echo "exec cmds:  $EXEC_CMDS"
	echo "capture:    ${FPS} fps, crf $CRF, speedup $SPEEDUP, limit ${LIMIT} s"
} > "$RUN/run.txt"

XCURSOR_SIZE="${RECORD_CURSOR_SIZE:-48}" "$UE_BIN" "$ROOT/CrabSim.uproject" "${GAME_ARGS[@]}" -ExecCmds="$EXEC_CMDS" \
	> "$RUN/game.log" 2>&1 &
GAME_PID=$!
echo "== started the game (pid $GAME_PID). Waiting for it to be ready, then recording to $OUT"

# The tour waits for CRABSIM_READY, focuses the window, parks the pointer, then publishes
# the window's place in window.txt and waits for go (the unix time the video starts).
( cd "$ROOT/Scripts/live" && PYTHONUNBUFFERED=1 exec "$PYTHON" "$TOUR_SCRIPT" "$RUN/game.log" "$RUN" "$GAME_PID" --sync ) \
	> "$RUN/tour.txt" 2>&1 &
TOUR_PID=$!

# The tour is gone when its pid no longer answers; finish_tour then collects its exit code.
tour_gone() { ! kill -0 "$TOUR_PID" 2>/dev/null; }
finish_tour() { local rc=0; wait "$TOUR_PID" 2>/dev/null || rc=$?; TOUR_PID=""; return "$rc"; }

for _ in $(seq 1 1200); do
	[ -s "$RUN/window.txt" ] && break
	if tour_gone; then
		rc=0; finish_tour || rc=$?
		echo "the tour stopped before the game was ready (exit $rc); its output:" >&2
		cat "$RUN/tour.txt" >&2
		exit 3
	fi
	sleep 0.5
done
[ -s "$RUN/window.txt" ] || { echo "the game never became ready" >&2; cat "$RUN/tour.txt" >&2; exit 3; }

read -r WIN X Y W H < "$RUN/window.txt"
W=$((W / 2 * 2)); H=$((H / 2 * 2))   # libx264 wants even sizes
echo "window $WIN at ${X},${Y} ${W}x${H}"

ffmpeg -hide_banner -loglevel error -y -f x11grab -draw_mouse 1 -framerate "$FPS" -video_size "${W}x${H}" \
	-i "${DISPLAY}+${X},${Y}" -an -c:v libx264 -preset veryfast -crf "$CRF" -pix_fmt yuv420p \
	-movflags +faststart "$OUT" < /dev/null > "$RUN/ffmpeg.log" 2>&1 &
FF_PID=$!
START=$(date +%s.%N)
sleep 1.0   # let ffmpeg open the display and start grabbing before anything moves
kill -0 "$FF_PID" 2>/dev/null || { echo "ffmpeg stopped at once; log:" >&2; cat "$RUN/ffmpeg.log" >&2; exit 3; }
echo "$START" > "$RUN/go.tmp" && mv "$RUN/go.tmp" "$RUN/go"
echo "recording; the tour is running"

REASON="tour finished"
while :; do
	NOW=$(date +%s.%N)
	if tour_gone; then break; fi
	if ! kill -0 "$FF_PID" 2>/dev/null; then echo "ffmpeg stopped; log:" >&2; cat "$RUN/ffmpeg.log" >&2; exit 3; fi
	if ! kill -0 "$GAME_PID" 2>/dev/null; then REASON="game exited"; break; fi
	if awk -v n="$NOW" -v s="$START" -v l="$LIMIT" 'BEGIN{exit !(n - s >= l)}'; then REASON="time limit"; break; fi
	sleep 0.5
done
TOUR_RC=0
if tour_gone; then finish_tour || TOUR_RC=$?; else stop_pid "$TOUR_PID" "the tour" INT; TOUR_PID=""; TOUR_RC=3; fi
sleep 0.5
stop_pid "$FF_PID" "ffmpeg" INT
FF_PID=""
echo "stopped: $REASON (tour exit $TOUR_RC)"

if [ "$SPEEDUP" != "1" ]; then
	# Played faster after the capture: every frame's time divided, then resampled to FPS.
	ffmpeg -hide_banner -loglevel error -y -i "$OUT" -filter:v "setpts=PTS/$SPEEDUP" -r "$FPS" -an -c:v libx264 \
		-preset veryfast -crf "$CRF" -pix_fmt yuv420p -movflags +faststart "$RUN/fast.mp4" < /dev/null
	mv "$RUN/fast.mp4" "$OUT"
fi

# What happened, with times in the video (divided by SPEEDUP like the frames).
if [ -s "$RUN/events.txt" ]; then
	awk -v k="$SPEEDUP" '{v=$1; sub("video=", "", v); $1=""; printf "video=%.1f%s\n", v / k, $0}' "$RUN/events.txt" > "$EVENTS"
fi

stop_pid "$GAME_PID" "the game" TERM
GAME_PID=""
rg -n "^(PASS|FAIL|BEAT)" "$RUN/tour.txt" || true
ffprobe -v error -show_entries format=duration,size -of default=nw=1 "$OUT"
echo "Output: $OUT"
echo "Events: $EVENTS"
echo "Logs:   $RUN"
exit "$TOUR_RC"
