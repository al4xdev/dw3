# Digimon World 2003 decompilation

A decompilation of **Digimon World 2003** (PlayStation, Europe, SLES-03936), the PAL release of *Digimon World 3*, and
a native **PC port** built from it.

The decompiled C rebuilds a byte-identical copy of the game's executable and overlays, and the same C, compiled for
your PC, runs the game natively, with a launcher and a few optional mods.

> [!IMPORTANT]
> **This repository contains no game data.** You need your own copy of the game: the port reads everything from your
> disc image. This project is not affiliated with or endorsed by Bandai, Bandai Namco or Sony.

## Documentation

| Document | Contents |
|---|---|
| [`docs/PLAYING.md`](docs/PLAYING.md) | Playing the PC port: platforms, download, your disc image, settings, reporting an issue |
| [`docs/BUILDING.md`](docs/BUILDING.md) | Building and testing: requirements, setup, the byte-identical build, the tests, the port and the launcher |
| [`docs/PROGRESS.md`](docs/PROGRESS.md) | How much of the game's code is matched, per part and per overlay |
| [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) | How to contribute |
| [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) | The working guide: repository layout, workflow, conventions, commands |
| [`docs/STATUS.md`](docs/STATUS.md) | Where the project stands |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | The key decisions and why |
| [`docs/MATCHING.md`](docs/MATCHING.md) | How to match a function: workflow, tools, C patterns |
| [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md) | What built the original game |
| [`docs/DISC_LAYOUT.md`](docs/DISC_LAYOUT.md) | Disc files, memory map, overlays |
| [`docs/FORMATS.md`](docs/FORMATS.md) | The game's file formats |
| [`docs/MECHANICS.md`](docs/MECHANICS.md) | Game mechanics mapped to functions and tests |
| [`docs/PORT.md`](docs/PORT.md) | How the PC port works |
| [`docs/LAUNCHER.md`](docs/LAUNCHER.md) | The launcher/game contract, settings and mods |
| [`docs/SOUND.md`](docs/SOUND.md) | Sound: LIBSND, the SPU, CD audio |
| [`docs/RELEASE.md`](docs/RELEASE.md) | Making a release |
| [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md) | Everything borrowed from other projects |
| [`tests/README.md`](tests/README.md) | The reference tests |

Planned work lives in the [project board](https://github.com/users/gascarcella/projects/1), problems in
[issues](https://github.com/gascarcella/dw2003recomp/issues).

## License

Our own work (source, tools, tests, documentation) is under the [MIT licence](LICENSE). The decompiled source was
written by reading the game's machine code; every borrowing from other projects is recorded in
[`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md). The game itself remains the property of its publisher.
