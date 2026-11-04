# Publishing on PortMaster

## Where it goes

A decompilation of a commercial game is not accepted by the main PortMaster catalogue. The port belongs to
**PortMaster-MV-New** (the multiverse repository, `https://github.com/PortsMaster-MV/PortMaster-MV-New`), under
`ports/dw3/`. The portname is `dw3` (lowercase, starts with a letter).

`portmaster/make_release.sh` builds `dist/dw3.zip` in the layout the PortMaster packaging page asks for. It refuses to
build when a required file is missing, when `port.json` does not describe the layout, when the binary is not a 32-bit ARM
ELF, or when a disc image is inside.

## Build

```
tools/build_arm.sh               # build/armhf-sdl2/dw2003
cd portmaster && ./make_release.sh   # dist/dw3.zip
```

## Open steps

- [ ] **Open a testing thread** in the PortMaster Discord. It is required before a pull request. Give the device and CFW
      tested, the resolution and the known issues (see "Known issues" in `README.md`).
- [ ] **Fork `PortMaster-MV-New`**, sparse-checkout it, put the contents of `dist/dw3.zip` under `ports/dw3/`, run the
      repository's check (`python3 tools/build_release.py --do-check`; this script is not in this repository and its
      name has not been confirmed against the current PortMaster-MV-New), and open the pull request citing the thread.
- [ ] **Test other firmware.** Only the XF35H on EmuELEC has been tested; the other rows of "Tested on" in `README.md`
      are open.

## After any change

1. Fix the source here (`handheld/`, `portmaster/`), not the copy in the zip.
2. Rebuild: the two commands above.
3. Update the published release: `gh release upload dw3-port-1 dist/dw3.zip --clobber -R al4xdev/dw3`.
4. Copy the new zip's contents over `ports/dw3/` in the PortMaster-MV-New fork.

The menu on a test device must be the committed one. To push it without a full install:
see `handheld/README.md`, "Deploying the menu".