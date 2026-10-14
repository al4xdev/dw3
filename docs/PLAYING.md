# Playing the PC port

## Supported platforms

| Platform | State |
|---|---|
| **Linux** x86_64 (glibc 2.39 or newer: Ubuntu 24.04, Fedora 40, or later) | Supported |
| **Windows** | On the way |
| macOS | Not planned yet |

## Download

1. Open the [Releases page](https://github.com/gascarcella/dw2003recomp/releases) and download the latest
   `dw2003-<version>-x86_64.AppImage` (and `SHA256SUMS`, to check it with `sha256sum -c SHA256SUMS`).
2. Make it executable and run it:
   ```sh
   chmod +x dw2003-*-x86_64.AppImage
   ./dw2003-*-x86_64.AppImage
   ```
   The AppImage opens the **launcher**, which holds the game and its mods. Nothing needs installing or compiling. If
   your system has no FUSE, run it with `--appimage-extract-and-run`.

## Provide your disc image

The game is read from **your own disc image** of *Digimon World 2003 (Europe) (En,Fr,De,Es,It)*:

- It must be the raw **`.bin` + `.cue`** dump (MODE2/2352, one track, 692,146,560 bytes), SHA-1
  `457cb233349ba841e03b33d8060f8fbcadd45cb3`. Dumping tools such as Redump-style rippers produce this format.
- A 2048-byte `.iso` conversion will **not** work: it loses the movies' audio sectors.
- Other releases (the US *Digimon World 3*, the Japanese release) are not supported.

In the launcher's **Disc** screen choose the `.cue` (or `.bin`), drag it onto the window, or type its path. The
launcher checks the SHA-1 once and remembers the disc; then press **Play**.

## Settings, controls and saves

- **Settings** (window scale, fullscreen, 50 or 60 Hz, mute, memory cards), **controls** (keyboard and gamepad
  rebinding, hotkeys) and **mods** (fast-forward, skip dialogues, disable battle animations) are all in the launcher.
- Settings and memory cards (`card1.mcd`, `card2.mcd`, the same format as PCSX-Redux's) are kept in
  `~/.local/share/dw2003/`. A `portable.txt` file beside the launcher keeps them beside it instead.

Details: [`docs/LAUNCHER.md`](LAUNCHER.md) and psxstack's `launcher/README.md`.

## Reporting an issue

Found a bug, a crash, or something that plays differently from the original? Please
[open an issue](https://github.com/gascarcella/dw2003recomp/issues/new/choose) and pick the matching template. A
useful report has:

- the **version** (the AppImage's file name, or the commit you built) and your **system** (distribution, desktop,
  GPU, gamepad);
- **what happened** and **what you expected**, ideally compared with the original game on a PS1 or an emulator;
- **steps to reproduce** from a new game or a save (attach the memory card from `~/.local/share/dw2003/` if it helps);
- the **output**: the launcher shows the game's last lines after an error and has a **Copy** button for them;
- screenshots or a short video when it is visual.

Please check the [open issues](https://github.com/gascarcella/dw2003recomp/issues) first, and never attach the game's
files or a disc image. Questions about the code are welcome as issues too. Planned work is tracked in the
[project board](https://github.com/users/gascarcella/projects/1).
