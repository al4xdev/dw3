#!/bin/sh
# What Ports runs: detect the environment, optional framebuffer menu, then the game.
set -eu
cd "$(dirname "$0")"
# shellcheck disable=SC1091
. ./detect-env.sh
detect_env >> ./log.txt

menu_rc=0
if [ "$DW3_MENU" = "fb" ]; then
  set +e
  python3 ./dw3-menu.py > ./menu.log 2>&1
  menu_rc=$?
  set -e
  if [ "$menu_rc" -eq 3 ]; then
    echo "menu: framebuffer unavailable (exit 3); continuing with settings.json" >> ./menu.log
  elif [ "$menu_rc" -ne 0 ]; then
    echo "menu failed with status $menu_rc" >> ./menu.log
    exit "$menu_rc"
  fi
else
  echo "menu skipped: $DW3_MENU" >> ./menu.log
fi

mkdir -p ./logs
# How much the game says: settings.json's debug.logs is "off" (the default), "soft" or "heavy", and the menu's DEBUG
# group writes it. Off prints nothing and leaves the log files empty for a player who never asked for them; soft is the
# ordinary commentary; heavy adds the periodic measurements (the per-second present line). The crash report keeps its
# last lines in every case, so a report is still possible with the logs off.
DW3_LOG_LEVEL=$(python3 -c 'import json;print(json.load(open("settings.json")).get("debug",{}).get("logs","off"))' 2>/dev/null || echo off)
case "$DW3_LOG_LEVEL" in
  soft|heavy) export DW3_LOG="$DW3_LOG_LEVEL" ;;
  *) export DW3_LOG=off ;;
esac
# The separate meters are for one-off measurements, not for playing: the GPU log reads the cycle counter about ten
# times per span (6-15 cycles each on this core), 1-2 ms in a battle, and both write to the card every second.
#export DW3_PRESENT_LOG=./logs/present.log
#export DW3_GPU_LOG=./logs/gpu.log
# DW3_PHASE is not a cheap meter in a battle: it wraps every GTE operation (~111k a frame there) and costs ~23 ms of
# the 20 ms budget. Never leave it on for play.
#export DW3_PHASE=./logs/phase.log
# The SDL2 build's input (psxstack/runtime/input.c, the PSXSTACK_SDL2 branch) has a fixed button map and ignores
# settings.json's input.gamepad, so the game applies pad.map itself (dw3_pad_permute, src/main/pad.c). The SDL3
# builds must not set this: there the runtime maps the pad and the game must leave the bits alone. Losing this line
# makes the remap screen read pad.map and ignore it, and the only symptom is a mapping that does nothing.
export DW3_PAD_REMAP=1

# settings.json wins unless DW3_FORCE_RENDERER was set (detect_env already folded that into DW3_RENDERER
# and logged it). Pass --renderer only when there is no file yet, or the player forced one: an existing
# file is what the reference launcher used, with no --renderer flag.
set -- --config ./settings.json --window --fullscreen --watchdog 0
if [ -n "${DW3_FORCE_RENDERER:-}" ] || [ ! -f ./settings.json ]; then
  set -- "$@" --renderer "$DW3_RENDERER"
fi
exec ./dw2003 "$@" >> ./logs/game.log 2>&1
