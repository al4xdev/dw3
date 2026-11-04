#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
export PORT_32BIT="Y"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR=/$directory/ports/dw3
BINARY=dw2003

cd "$GAMEDIR"

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# The player's disc. This list is the game's; pm has no finder.
DISC=""
for candidate in \
  "$GAMEDIR/game/dw3.cue" \
  "$GAMEDIR/game/dw3.bin" \
  "$GAMEDIR/game/dw2003.cue" \
  "$GAMEDIR/game/dw2003.bin" \
  "$GAMEDIR/game/Digimon World 2003.cue" \
  "$GAMEDIR/game/Digimon World 2003.bin" \
  "$GAMEDIR/game/Digimon World 3.cue" \
  "$GAMEDIR/game/Digimon World 3.bin"; do
  if [ -f "$candidate" ]; then
    DISC="$candidate"
    break
  fi
done

if [ -z "$DISC" ]; then
  DISC=$(find "$GAMEDIR/game" -maxdepth 1 -name "*.cue" -o -name "*.bin" 2>/dev/null | head -n 1)
fi

if [ -z "$DISC" ]; then
  echo "Missing Digimon World 2003 disc image (.bin/.cue) in $GAMEDIR/game/"
  pm_show_error "Please copy your Digimon World 2003 .bin/.cue into ports/dw3/game/" --title "Digimon World 3"
  exit 1
fi

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

fb=0
if [ -r /dev/fb0 ] && [ -w /dev/fb0 ]; then
  fb=1
fi
if [ -n "${DW3_FORCE_VIDEO:-}" ]; then
  export DW3_VIDEO="$DW3_FORCE_VIDEO"
  if [ "$DW3_VIDEO" = "fbdev" ]; then
    menu=fb
  else
    menu=skip
  fi
elif [ -n "${WAYLAND_DISPLAY:-}" ]; then
  # A dummy fb0 still opens. sdl2 keeps the binary off that present.
  export DW3_VIDEO=sdl2
  menu=skip
elif [ "$fb" = 1 ]; then
  export DW3_VIDEO=fbdev
  menu=fb
else
  export DW3_VIDEO=sdl2
  menu=skip
fi

BIN="$GAMEDIR/$BINARY"
if command -v ldd >/dev/null 2>&1; then
  missing="$(ldd "$BIN" 2>&1 | grep 'not found' || true)"
  if [ -n "$missing" ]; then
    echo "BLOCKER: 32-bit libraries missing:"
    echo "$missing"
    pm_show_error "This 32-bit build is missing ARM libraries. See ports/dw3/log.txt." --title "Digimon World 3"
    exit 1
  fi
else
  echo "ldd unavailable; library check skipped"
fi

mkdir -p ./logs

# software is the binary default. gles only on the same chip (Cortex-A35, CPU part 0xd04).
cpu_part=""
if [ -r /proc/cpuinfo ]; then
  cpu_part=$(awk -F': ' '/CPU part/ { gsub(/^[ \t]+/, "", $2); print $2; exit }' /proc/cpuinfo 2>/dev/null || true)
fi
if [ "$cpu_part" = "0xd04" ]; then
  export DW3_DEFAULT_RENDERER=gles
  echo "renderer: gles (CPU part 0xd04)"
else
  echo "renderer: software (CPU part ${cpu_part:-unknown})"
fi

# pm_finish clears the TTY. It does not clear the pixels this port wrote to /dev/fb0.
blank_fb() {
  if [ -w /dev/fb0 ]; then
    dd if=/dev/zero of=/dev/fb0 bs=4096 count=2048 status=none 2>/dev/null || true
  fi
}

menu_rc=0
if [ "$menu" = "fb" ]; then
  python3 ./dw3-menu.py > ./menu.log 2>&1
  menu_rc=$?
  if [ "$menu_rc" -eq 3 ]; then
    echo "menu: framebuffer unavailable (exit 3); continuing with settings.json" >> ./menu.log
  elif [ "$menu_rc" -eq 2 ]; then
    blank_fb
    pm_finish
    exit 0
  elif [ "$menu_rc" -ne 0 ]; then
    echo "menu failed with status $menu_rc" >> ./menu.log
    pm_show_error "The menu failed (status $menu_rc). See ports/dw3/menu.log." --title "Digimon World 3"
    blank_fb
    pm_finish
    exit "$menu_rc"
  fi
else
  echo "menu skipped: $menu" >> ./menu.log
fi

# Unset DW3_LOG is soft, not off. The menu's debug.logs is the player's choice.
DW3_LOG_LEVEL=$(python3 -c 'import json;print(json.load(open("settings.json")).get("debug",{}).get("logs","off"))' 2>/dev/null || echo off)
case "$DW3_LOG_LEVEL" in
  soft|heavy) export DW3_LOG="$DW3_LOG_LEVEL" ;;
  *) export DW3_LOG=off ;;
esac

# Tester diagnostic. Off unless the player asked for heavy logs.
if [ "$DW3_LOG_LEVEL" = "heavy" ]; then
  sh "$GAMEDIR/probe.sh" || true
fi

# SDL2 build ignores settings.json's pad map unless this is set.
export DW3_PAD_REMAP=1

# One-off meters. Off: the GPU log costs 1-2 ms a battle frame, DW3_PHASE about 23 ms.
#export DW3_PRESENT_LOG=./logs/present.log
#export DW3_GPU_LOG=./logs/gpu.log
#export DW3_PHASE=./logs/phase.log

# Kill switch only. The game reads the pad; no .ini. Backgrounded, or this script blocks.
$GPTOKEYB2 "$BINARY" > /dev/null 2>&1 &

pm_platform_helper "$GAMEDIR/$BINARY"

set -- --window --fullscreen --watchdog 0
if [ -f ./settings.json ]; then
  set -- --config ./settings.json "$@"
fi
# Menu exit 0 is the only path that wrote disc.path. Its name order differs.
if [ "$menu" != "fb" ] || [ "$menu_rc" != 0 ]; then
  set -- --disc "$DISC" "$@"
fi
if [ -n "${DW3_FORCE_RENDERER:-}" ]; then
  set -- "$@" --renderer "$DW3_FORCE_RENDERER"
elif [ "${DW3_DEFAULT_RENDERER:-}" = "gles" ] && [ ! -f ./settings.json ]; then
  set -- "$@" --renderer gles
fi

./dw2003 "$@" >> ./logs/game.log 2>&1
blank_fb
pm_finish
