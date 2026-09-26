#!/usr/bin/env bash
# Build the editor target. UE_ROOT overrides the engine location.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"

exec "$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
	CrabSimEditor Linux "${1:-Development}" \
	-project="$ROOT/CrabSim.uproject" -waitmutex
