#!/bin/sh
# What Ports runs: gamepad menu on the panel, then the game.
set -eu
cd "$(dirname "$0")"
if python3 ./dw3-menu.py > ./menu.log 2>&1; then
  mkdir -p ./logs
  # The meters cost real frames: the GPU log reads the cycle counter about ten times per span (6-15 cycles each on
  # this core), 1-2 ms in a battle, and both write to the card every second. Off to play; uncomment to measure a run.
  #export DW3_PRESENT_LOG=./logs/present.log
  #export DW3_GPU_LOG=./logs/gpu.log
  # DW3_PHASE is not a cheap meter in a battle: it wraps every GTE operation (~111k a frame there) and costs ~23 ms of
  # the 20 ms budget. Never leave it on for play.
  #export DW3_PHASE=./logs/phase.log
  export DW3_VIDEO=fbdev
  export SDL_VIDEODRIVER=kmsdrm
  export SDL_AUDIODRIVER=alsa
  exec ./dw2003 --config ./settings.json --window --fullscreen --watchdog 0 >> ./logs/game.log 2>&1
fi
exit 0
