#!/bin/bash
# PortMaster Launcher Script for Digimon World 2003 / Digimon World 3 Handheld Port
# Architecture: armhf (32-bit ARM Linux userspace) for R36S / RK3326

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

# The port is a 32-bit ARM build only (PortMaster's convention: this sets the CFW's 32-bit environment variables).
export PORT_32BIT="Y"

source $controlfolder/control.txt 2>/dev/null || true
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR="/$directory/ports/dw3"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

cd "$GAMEDIR"

# Check for required game disc (.bin / .cue)
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
  if command -v pm_show_error >/dev/null 2>&1; then
    pm_show_error "Please copy your Digimon World 2003 .bin/.cue into ports/dw3/game/" --title "Digimon World 3"
  elif [ -f "$controlfolder/harfbuzz_run" ]; then
    $ESUDO $controlfolder/harfbuzz_run "Digimon World 3" "Please copy your Digimon World 2003 .bin/.cue into ports/dw3/game/"
  fi
  exit 1
fi

# Display, audio and renderer come from detect-env.sh (framebuffer on the reference handheld,
# SDL/Wayland when that is what the CFW actually has). Overrides: DW3_FORCE_VIDEO, DW3_FORCE_RENDERER.
# shellcheck disable=SC1091
. "$GAMEDIR/detect-env.sh"
sh "$GAMEDIR/probe.sh" || true
detect_env

# The CFW's own controller database, so the pad is the same gamepad on every device (the port reads it through SDL's
# gamecontroller API and maps it in its own menu).
[ -n "${sdl_controllerconfig:-}" ] && export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
# A 32-bit build on a 64-bit CFW needs the CFW's own 32-bit libraries first; harmless when there are none.
export LD_LIBRARY_PATH="$GAMEDIR/dw3${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

BIN="$GAMEDIR/dw2003"
if command -v ldd >/dev/null 2>&1; then
  missing="$(ldd "$BIN" 2>&1 | grep 'not found' || true)"
  if [ -n "$missing" ]; then
    echo "BLOCKER: 32-bit libraries missing:"
    echo "$missing"
    if command -v pm_show_error >/dev/null 2>&1; then
      pm_show_error "This 32-bit build is missing ARM libraries. See ports/dw3/log.txt." --title "Digimon World 3"
    fi
    exit 1
  fi
else
  echo "ldd unavailable; library check skipped (TODO: validate)"
fi

# What PortMaster's platform helpers want to know about the binary before it runs (a no-op on CFWs without them).
command -v pm_platform_helper >/dev/null 2>&1 && pm_platform_helper "$BIN" >/dev/null 2>&1

pm_done() {
  if [ "${DW3_PM_DONE:-0}" = 1 ]; then
    return 0
  fi
  DW3_PM_DONE=1
  if command -v pm_finish >/dev/null 2>&1; then
    pm_finish
  fi
}
trap pm_done EXIT INT TERM

# Gamepad menu on the panel, then the game. No keyboard on this handheld.
if [ -x "$GAMEDIR/dw3.sh" ]; then
  "$GAMEDIR/dw3.sh"
else
  "$BIN" --disc "$DISC" --window --fullscreen --watchdog 0
fi
