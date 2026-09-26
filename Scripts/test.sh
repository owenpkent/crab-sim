#!/usr/bin/env bash
# Run the Unreal automation tests headless: CrabSim (the game) by default. Pass
# a filter to narrow, e.g. CrabSim.Movement.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
FILTER="${1:-CrabSim}"

exec "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$ROOT/CrabSim.uproject" \
	-ExecCmds="Automation RunTests $FILTER; Quit" \
	-unattended -nullrhi -nosplash -nosound -stdout -FullStdOutLogOutput \
	-testexit="Automation Test Queue Empty"
