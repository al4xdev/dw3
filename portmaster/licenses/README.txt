What this port ships, and what each licence here covers.

  dw2003                      the port binary: the game's own decompiled C with the PlayStation's hardware
  dw3.sh, dw3-menu.py,        reimplemented for the host, built from github.com/al4xdev/dw3 (a fork of the
  dw3-tui.sh, ledd.py         dw2003recomp decompilation). LICENSE - MIT.
                              It contains no game data: it reads the player's own disc.

  SDL2.txt                    SDL2, linked from the device's firmware (or from PortMaster's own copy when
                              the firmware has none). zlib licence.

  EGL, GLESv2, libgbm         the handheld's vendor graphics libraries, loaded at run time from the
                              firmware. Not redistributed here; their licences ship with the device.

  Psy-Q                      not included. The port declares the API it needs and implements every call
                              itself; no Sony file is present in the binary or in this package.

The game, Digimon World 2003, is the property of its publisher. This package carries nothing of it.
