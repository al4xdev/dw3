Digimon World 2003 - native port for the RK3326 handhelds (ArkOS / PortMaster)
=============================================================================

INSTALLING
1. Copy "Digimon World 3.sh" into the ports folder of the card (/roms/ports/, which is
   /storage/roms/ports/ on ArkOS).
2. Copy the whole "dw3" folder into /roms/ports/dw3/.
3. Put your own copy of the game (Digimon World 2003, Europe, .bin + .cue) into
   /roms/ports/dw3/game/. Nothing of the game ships with this port.

CONTROLS
- D-pad or left stick: movement and menus
- A (south): confirm / talk (Cross)
- B (east): cancel / menu (Circle)
- X (west): secondary action (Square)
- Y (north): status (Triangle)
- L1 / R1: switch menu tabs
- L2 / R2: the game's own functions
- Select: map / selection
- Start: pause / start

All of it can be rebound in the gamepad menu that runs before the game (BUTTONS -> REMAP); that menu
writes pad.map and settings.json, which is how the mapping reaches the game.

HOTKEYS
- Select + Start: quit cleanly (the log, the WAV and the memory card are closed)
- L3 + R3 (press both sticks): toggle fast-forward

VIDEO
The picture goes out through /dev/fb0, scaled 2x with NEAREST by ARM NEON, on the 640x480 panel.
settings.json picks the renderer:

  "renderer": "gles"      the 3D is drawn on the Mali-G31 (the default on this device; faster in battle)
  "renderer": "software"  the CPU rasteriser, byte-identical to the PS1's (faster in menus)

To leave fb0 and use the SDL2/KMSDRM path instead, edit the launcher "Digimon World 3.sh" and set
export DW3_VIDEO="sdl2" - it is slower here, because every frame pays a 17 ms window swap.

LOGS
logs/game.log holds the run's log. The meters (DW3_GPU_LOG, DW3_PRESENT_LOG, DW3_GLES_LOG) are off by
default: they cost real frames when on.
