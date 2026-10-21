#!/bin/sh
# What Ports runs: gamepad menu on the panel, then the game.
set -eu
cd "$(dirname "$0")"
if python3 ./dw3-menu.py > ./menu.log 2>&1; then
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
  export DW3_VIDEO=fbdev
  export SDL_VIDEODRIVER=kmsdrm
  export SDL_AUDIODRIVER=alsa
  # The SDL2 build's input (psxstack/runtime/input.c, the PSXSTACK_SDL2 branch) has a fixed button map and ignores
  # settings.json's input.gamepad, so the game applies pad.map itself (dw3_pad_permute, src/main/pad.c). The SDL3
  # builds must not set this: there the runtime maps the pad and the game must leave the bits alone. Losing this line
  # makes the remap screen read pad.map and ignore it, and the only symptom is a mapping that does nothing.
  export DW3_PAD_REMAP=1
  exec ./dw2003 --config ./settings.json --window --fullscreen --watchdog 0 >> ./logs/game.log 2>&1
fi
exit 0
