## Notes

A source port of **Digimon World 2003** (PAL), built from [dw2003recomp](https://github.com/gascarcella/dw2003recomp): the
game's own C, decompiled to a matching build, compiled for 32-bit ARM, with the PlayStation's hardware (CPU, GTE, GPU, SPU,
MDEC, CD) reimplemented on the host. The disc's data is the only thing it reads from the game; nothing of the game is
included here.

It needs your own copy of the PAL disc. Copy `dw2003.cue` and `dw2003.bin` into `ports/dw3/game/` — the launcher finds
any `.cue`/`.bin` pair there.

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

All of it can be rebound in the gamepad menu that runs before the game (BOTOES → REMAP); the menu writes the mapping into
`settings.json` and `pad.map`.

## Compile

The port is built with the game repository's ARM cross build (Docker with an armhf toolchain):

```
git clone https://github.com/gascarcella/dw2003recomp --recurse-submodules
cd dw2003recomp
./build_arm.sh          # configures with -DPSXSTACK_SDL2=ON -DPSXSTACK_EGL=ON and packages ports/dw3
```

## Thanks

Thanks to the dw2003recomp project for the decompilation and the port, and to the PortMaster crew for the packaging
conventions this folder follows. Thanks also to the DuckStation project, whose VRAM-as-a-texture renderer is the design
the GLES backend here was measured against (`docs/gpu/01`).
