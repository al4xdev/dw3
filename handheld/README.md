# The handheld port's launcher

What goes on the console, kept here so it is versioned with the port it starts. The package on the device is
`/storage/roms/ports/dw3/`; the build (`tools/build_arm.sh`) copies the files
below into it and adds the binary.

| file | what it is |
|---|---|
| `Digimon World 3.sh` | what EmulationStation runs (`ports_scripts/`): finds the disc, sets the video and audio drivers, and hands over to `dw3.sh`. PortMaster's launcher conventions. |
| `dw3.sh` | the game's own script: draws the gamepad menu, then `exec`s `dw2003` with `settings.json`, logging to `logs/game.log`. |
| `dw3-menu.py` | the framebuffer menu: no keyboard, pad only, 640x480, its own 5x7 glyphs. Writes `settings.json` (which is how the mods and the pad mapping reach the game) and `pad.map`. |
| `dw3-tui.sh` | the same settings from a terminal, for a desktop or a serial console. |
| `ledd.py` | the LED bar's daemon: the only process that writes `/dev/ttyS2`, started by the game's `led_bar` mod (`docs/LAUNCHER.md`, "LED bar"; `docs/XF35H.md`, "What the bar actually does"). Holds an `flock` so there is only ever one, and turns the bar off when the pid that asked for the animation goes away. |

PortMaster's metadata is **not** here: it is `../portmaster/port.json` (version 4, with the screenshot and
`gameinfo.xml` beside it), and `portmaster/make_release.sh` is what assembles the zip. An earlier `dw3.json` lived in this
directory and was a version-2 draft; it was deleted so nobody publishes the wrong one.

Not here, because they are the player's: `settings.json`, `pad.map`, `card1.mcd`/`card2.mcd`, `game/` (the disc) and
`logs/`.

## Publishing it

`portmaster/` (next to this directory) has the metadata the release needs - `port.json`, `README.md`, `gameinfo.xml`, a
640x480 screenshot of gameplay - and `make_release.sh`, which assembles `dist/dw3.zip` in the layout
<https://portmaster.games/packaging.html> asks for and checks it before it is offered. A port made from a decompilation
goes to **PortMaster-MV-New** (the multiverse repository), not PortMaster-New.

## What the menu owns

- `settings.json`: the mods and `input.gamepad`. `video.renderer` is `gles` on this machine (`"software"` reverts).
- `pad.map`: one line per PS1 button, `<button> <gamepad input> <letter>`. The gamepad input is SDL's button name
  (`south`, `east`, `west`, `north`, `leftshoulder`, `back`, `start`, ...), which is what `runtime/input.c` reads; the
  letter is only what the text shows. **A button belongs to one action**: binding one that another action holds swaps
  the two, and a file that has two actions on one button is healed when it is read (a `select` sharing `circle` would
  otherwise make CIRCLE+START the exit chord).
- The loading screen runs six frames of two dot lines before the game starts; the game's own boot follows.
