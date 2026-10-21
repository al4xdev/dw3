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
  if [ -f "$controlfolder/harfbuzz_run" ]; then
    $ESUDO $controlfolder/harfbuzz_run "Digimon World 3" "Please copy your Digimon World 2003 .bin/.cue into ports/dw3/game/"
  fi
  exit 1
fi

# Environment and display configuration:
# DW3_VIDEO="fbdev" selects direct /dev/fb0 presentation with ARM NEON 2x scaling.
# To force SDL2 KMSDRM instead: export DW3_VIDEO="sdl2"
export DW3_VIDEO="fbdev"
export SDL_VIDEODRIVER="kmsdrm"
export SDL_AUDIODRIVER="alsa"
# The CFW's own controller database, so the pad is the same gamepad on every device (the port reads it through SDL's
# gamecontroller API and maps it in its own menu).
[ -n "${sdl_controllerconfig:-}" ] && export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
# A 32-bit build on a 64-bit CFW needs the CFW's own 32-bit libraries first; harmless when there are none.
export LD_LIBRARY_PATH="$GAMEDIR/dw3${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# What PortMaster's platform helpers want to know about the binary before it runs (a no-op on CFWs without them).
command -v pm_platform_helper >/dev/null 2>&1 && pm_platform_helper "$GAMEDIR/dw3/dw2003" >/dev/null 2>&1

# Gamepad menu on the panel, then the game. No keyboard on this handheld.
if [ -x "$GAMEDIR/dw3.sh" ]; then
  "$GAMEDIR/dw3.sh"
else
  "$GAMEDIR/dw3/dw2003" --disc "$DISC" --window --fullscreen --watchdog 0
fi

# Back to the frontend: PortMaster's cleanup when the CFW has it, gptokeyb and the frontend's events otherwise.
if command -v pm_finish >/dev/null 2>&1; then
  pm_finish
else
  $ESUDO kill -9 $(pidof gptokeyb) 2>/dev/null || true
  $ESUDO systemctl restart oga_events &
fi
printf "\033c" > /dev/tty0
