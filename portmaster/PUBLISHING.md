# Publishing on PortMaster: the checklist

State of the package on this branch, what is already done and verified, and what only the owner can do. Written to be
followed in a later session, without the console or this conversation.

## Where it goes

A **decompilation** of a commercial game is not accepted by the main catalogue: the port belongs to **PortMaster-MV-New**
(the multiverse repository, `https://github.com/PortsMaster-MV/PortMaster-MV-New`), under `ports/dw3/`. The portname is
`dw3`: lowercase, starts with a letter, and it clashed with nothing in either catalogue (checked against
`020_portmaster.source.json`, 1,396 ports, and `021_portmaster.multiverse.source.json`, 50).

## Done, and how it was checked

- [x] **The package builds itself and validates itself**: `portmaster/make_release.sh` assembles `dist/dw3.zip` in the
      layout the publishing page asks for and refuses to build when a required file is missing, when `port.json` does not
      describe the layout, or when a disc image is inside. Last run: 12 files, `b3555e67…`.
- [x] **`port.json`** — version 4, `name: dw3.zip`, `items: ["Digimon World 3.sh", "dw3"]`, `arch: ["armhf"]`, `rtr:
      false`, `exp: false`, `availability: full`, `porter: ["al4xdev"]`, and an `inst` field that names the disc (Europe /
      PAL, `SLES-03975`), says the `.cue` and the `.bin` go in together, gives the folder as `ports/dw3/game/` and as the
      ArkOS path, and warns that renaming the `.bin` without the `.cue` breaks it. Validated with `python3 -m json.tool`.
- [x] **`README.md`** — an "Installing it (no tools needed)" section for players, then Notes, Controls (with what is
      printed on the buttons, `Select + Start` to quit), Compile and Thanks.
- [x] **`gameinfo.xml`** — EmulationStation entry (path, name, description, publisher, genre, image).
- [x] **`screenshot.png`** — 640x480, 4:3, gameplay, captured from this console's own framebuffer (Kusanagi City).
- [x] **The launcher** (`handheld/Digimon World 3.sh`) follows the conventions: `PORT_32BIT="Y"`, the CFW's
      `SDL_GAMECONTROLLERCONFIG`, `LD_LIBRARY_PATH`, `pm_platform_helper`/`pm_finish` when present, and it does not
      `exec` the game so that cleanup runs before the frontend returns.
- [x] **No game data in the zip** — checked by the script, which fails on `.bin/.cue/.iso/.chd/.mcd/.img`.
- [x] **The repository is the source of truth**: `tools/build_arm.sh` reproduces the binary from a fresh clone
      (`--recurse-submodules`), `docs/XF35H.md` and `docs/gpu/*` carry the evidence, and nothing needed to rebuild lives
      outside the clone.

## Still to do — owner only

- [ ] **Licences with real text** in `dw3/licenses/`: today it holds one `README.txt` that says what the port ships and
      borrows. Add the **SDL2** zlib licence text and the project's own `LICENSE` (the decompilation's licence).
- [ ] **`cover.jpg`** (optional): the catalogue will use `screenshot.png` if there is none.
- [ ] **Open a Discord testing thread** in the PortMaster server — *required* before a pull request; the report wants the
      device(s) and CFW(s) tested, the resolution, and the known issues below.
- [ ] **Test on other CFWs**: `AmberELEC`, `dARKOS`, `muOS`, `ROCKNIX` (Libmali and Panfrost), `Knulli` optional. On this
      machine only one handheld and one CFW exist, so this is the port's largest untested surface — the binary is armhf
      only and uses EGL on GBM plus `/dev/fb0`.
- [ ] **Fork `PortMaster-MV-New`**, sparse checkout, put the contents of `dist/dw3.zip` under `ports/dw3/`, run
      `python3 tools/build_release.py --do-check`, and open the pull request citing the thread.
- [ ] **Update the published release**: the GitHub release `dw3-port-1` still carries an older zip. After any change:
      `tools/build_arm.sh`, `cd portmaster && ./make_release.sh`, then
      `gh release upload dw3-port-1 dist/dw3.zip --clobber -R al4xdev/dw3`.
- [ ] **The card's `dw3-menu.py`** must be the committed one: `ssh root@<ip> 'cat > /storage/roms/ports/dw3/dw3-menu.py' < handheld/dw3-menu.py`.

## What to declare as known issues

- **The movies' top-right artefact on the GPU renderer**: cosmetic, film only, absent on the software renderer. Full
  account and every hypothesis already spent in `docs/gpu/17-movie-artefact.md`.
- **The renderer is a trade-off**: the GPU path wins where a frame has 3D (41.9 against 37.7 fps in the same battle); a
  light 2D frame pays the same ~3.2 ms present either way. The menu has a `RENDERER  GLES/SOFTWARE` row, so a player can
  switch without touching a file.
- **The heaviest battles are over budget**: ~24 ms against 20 with `gles`.
- **GLES VRAM differs from the software renderer in 0.2–0.7% of its texels** (edges, 5-bit rounding); the picture is
  byte-identical at the frames compared with `--screenshot`.
- **`--input-test` hangs on this device** (no output, immune to `timeout`); it does not affect playing.
- **Everything was measured on one handheld** (XF35H, RK3326, Mali-G31, 640x480) at 60 Hz, and the audio work is
  reproducible byte for byte (`--wav`, md5 `39757ffd…`).
