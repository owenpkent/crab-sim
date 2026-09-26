#!/usr/bin/env bash
# Save a screenshot of the running CrabSim game window to the given PNG path.
# Usage: Scripts/shot.sh out.png
set -euo pipefail
OUT="${1:?usage: shot.sh out.png}"
export DISPLAY="${DISPLAY:-:0}"
# The inner UnrealEditor-class window line ends with its absolute position: "WxH+rx+ry  +ax+ay".
LINE="$(xwininfo -root -tree | rg '"UnrealEditor" "UnrealEditor"' | head -1 || true)"
[ -n "$LINE" ] || { echo "no CrabSim game window found" >&2; exit 1; }
GEOM="$(echo "$LINE" | rg -o '[0-9]+x[0-9]+\+-?[0-9]+\+-?[0-9]+' | head -1)"
ABS="$(echo "$LINE" | rg -o '\+-?[0-9]+\+-?[0-9]+\s*$' | tr -d ' ')"
W="${GEOM%%x*}"; REST="${GEOM#*x}"; H="${REST%%+*}"
AX="$(echo "$ABS" | cut -d+ -f2)"; AY="$(echo "$ABS" | cut -d+ -f3)"
ffmpeg -loglevel error -y -f x11grab -video_size "${W}x${H}" -i "${DISPLAY}+${AX},${AY}" -frames:v 1 "$OUT"
