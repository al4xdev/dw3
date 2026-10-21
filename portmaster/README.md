## Installing it (no tools needed)

1. Install the port with **PortMaster** on the console (Ports → PortMaster → search for "Digimon World 2003"), or unzip
   `dw3.zip` into the card's `ports/` folder by hand. On ArkOS and EmuELEC that folder is `/storage/roms/ports/`.
2. Put your own disc in `ports/dw3/game/` — the **`.cue` and the `.bin` together**, straight from your own copy of the
   game (Europe / PAL, disc 1, serial `SLES-03975`; a redump-style `.cue` + `.bin` pair is what the launcher expects).
   Copy them the way you copy any ROM: card in the PC, or over the network. The `.bin` is ~660 MB.
3. Launch it from the **Ports** menu. A gamepad menu comes first (that is the port's own, driven only by the pad): the
   bottom row is **PLAY**. Settings, button remapping and the LED bar are the rows above it.

The port contains no game data: without the disc in `game/` it says `NO DISC IN game/` and does not start.

If the game does not start after an update, the two files worth deleting are `settings.json` and `pad.map` in
`ports/dw3/` (they are rebuilt on the next launch).

## Notes

A source port of **Digimon World 2003** (PAL), built from [dw2003recomp](https://github.com/gascarcella/dw2003recomp): the
game's own C, decompiled to a matching build, compiled for 32-bit ARM, with the PlayStation's hardware (CPU, GTE, GPU, SPU,
MDEC, CD) reimplemented on the host. The disc's data is the only thing it reads from the game; nothing of the game is
included here.

It needs your own copy of the PAL disc, in `ports/dw3/game/` as described above.

Two renderers ship in the binary: the software rasteriser (byte-identical to the port's reference) and a GLES one that
draws the PlayStation's VRAM on the Mali. `ports/dw3/settings.json` picks one (`"video": { "renderer": "gles" }` or
`"software"`); on this device GLES is the default and is faster in 3D, the software one is faster in menus.

## Controls

| Button | Action |
|--|--|
| D-Pad | Move / menu |
| A (east) | Confirm (Cross) |
| B (south) | Cancel (Circle) |
| X (north) | Square |
| Y (west) | Triangle |
| L1 / R1 | L1 / R1 |
| Start | Start |
| Select + Start | Quit |

All of it can be rebound in the gamepad menu that runs before the game (`BUTTONS` → `REMAP`); the menu writes the mapping
into `settings.json` and `pad.map`. `Select + Start` quits from anywhere.

## Compile

The port is built with the game repository's ARM cross build (Docker with an armhf toolchain):

```
git clone https://github.com/gascarcella/dw2003recomp --recurse-submodules
cd dw2003recomp
tools/build_arm.sh      # configures with -DPSXSTACK_SDL2=ON -DPSXSTACK_EGL=ON and packages ports/dw3
```

## Thanks

Thanks to the dw2003recomp project for the decompilation and the port, and to the PortMaster crew for the packaging
conventions this folder follows. Thanks also to the DuckStation project, whose VRAM-as-a-texture renderer is the design
the GLES backend here was measured against (`docs/gpu/01`).
