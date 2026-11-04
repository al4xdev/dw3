# Contributing

This page covers what you need before you open a pull request. Background on the project is in the
[README](../README.md) and [`docs/STATUS.md`](STATUS.md).

## Contents

- [Ground rules](#ground-rules)
- [What you can work on](#what-you-can-work-on)
- [Getting set up](#getting-set-up)
- [Repository layout](#repository-layout)
- [Making a change](#making-a-change)
  - [Decompilation (matching C)](#decompilation-matching-c)
  - [The PC port and the launcher](#the-pc-port-and-the-launcher)
  - [Scripts and tools](#scripts-and-tools)
  - [Documentation](#documentation)
- [Checks before a pull request](#checks-before-a-pull-request)
- [Opening a pull request](#opening-a-pull-request)
- [Reporting issues](#reporting-issues)
- [AI-assisted contributions](#ai-assisted-contributions)

## Ground rules

- **Never commit game data.** Nothing from the disc: no BIN/ISO, extracted files, split assembly (`asm/`), assets,
  textures, audio, BIOS, and no Psy-Q/SDK files. The repository is code only; everyone supplies their own disc. Check
  `git status` before you commit.
- **The matching build must stay byte-identical.** Every game file (`SLES_039.36`, the overlays) must still rebuild to
  the original SHA-1. A change that breaks it can't be merged.
- **Europe only.** The target is the unpatched EU disc (SLES-03936). The US decompilation is a reference, not a
  dependency: anything you borrow from another project must be recorded in
  [`docs/THIRD_PARTY.md`](THIRD_PARTY.md) in the same pull request.
- **Don't decompile Psy-Q library functions.** They are identified by signature, named with Sony's names and stay as
  split assembly (excluded from the progress count).
- **Tests never change the game's code.** Reference tests live in `tests/` and `tools/`, never in `src/`.
- **Don't claim something works until you have run it.** Setup steps go in `scripts/` or `docs/`, not only in your
  shell history. Anything that needs `sudo` or a system-wide install is out; install into `tools/`.
- By contributing you agree that your work is released under the project's [MIT licence](../LICENSE).

## What you can work on

- **Open issues** labelled [`good first issue`](https://github.com/al4xdev/dw3/labels/good%20first%20issue)
  or [`help wanted`](https://github.com/al4xdev/dw3/labels/help%20wanted).
- **Planned work** on the [project board](https://github.com/users/gascarcella/projects/1).
- **Names and types:** many overlay functions and fields are still `func_…`/`unk_…`.
- **Matching workarounds:** every function matches ([`PROGRESS.md`](PROGRESS.md): 3606 of 3606), but some need
  tricks to get there; `tools/venv/bin/python tools/hacks.py --list` shows each one, and cleaner C that still matches
  is welcome.
- **Play-testing the PC port** against the original and reporting differences.

For anything bigger than a small fix, open an issue (or comment on an existing one) first, so we can agree on the
approach before you spend time on it.

## Getting set up

Follow [`BUILDING.md`](BUILDING.md): the requirements, `tools/local.env` pointing at your disc, the `psxstack`
submodule, git worktrees, and the extract, build and test commands.

## Repository layout

| Path | Contents |
|---|---|
| `src/`, `include/` | The decompiled C and the recovered headers and structs |
| `config/` | splat YAML per link unit, `symbol_addrs.txt` (EXE names), `<overlay>.symbols.txt` (overlay names), `*.sha1` |
| `port/` | The PC port's game side: the adapter, `game.json`, the mods ([`port/README.md`](../port/README.md)) |
| `psxstack/` | Submodule: the runtime, the Psy-Q shim and the launcher |
| `tests/` | The reference tests ([`tests/README.md`](../tests/README.md)); they never touch `src/` |
| `scripts/`, `tools/` | Setup, build and test scripts; the Python tools. `tools/` also receives the built toolchain (untracked) |
| `packaging/`, `portmaster/`, `handheld/` | Release packaging, the PortMaster package, the handheld launcher |
| `docs/` | Reference documentation |
| `iso/`, `extracted/`, `asm/`, `assets/`, `build/` | Untracked: your disc link, its contents, splat's output, build outputs |

## Making a change

### Decompilation (matching C)

The workflow, the tools (splat, objdiff, asm-differ, decomp-permuter, m2c) and the C patterns that make this compiler
match are in [`docs/MATCHING.md`](MATCHING.md). In short:

- A function is **matching** when it compiles from C and the full build is byte-identical. Work-in-progress C goes
  under `#ifdef NON_MATCHING` beside the `INCLUDE_ASM` and must still compile with `-DNON_MATCHING`.
- **Naming** ([`DECISIONS.md`](DECISIONS.md) "Naming conventions"): game symbols are `<module>_<verb>_<noun>` in
  snake_case, with the module prefix being the source file's name (`src/main/filetable.c` → `filetable_get_lba`);
  types are PascalCase, constants `MODULE_UPPER_SNAKE`, unknown fields `unk_1C`. Unnamed symbols keep splat's defaults
  (`func_8001xxxx`, `D_8001xxxx`, `jtbl_…`; in overlays `func_FIELDSTG_8008xxxx`). Name only what you understand.
- New names go in `config/symbol_addrs.txt` (the EXE, `name = 0x80012345; // type:func`) or
  `config/<overlay>.symbols.txt` (overlays).
- The C must also compile for the PC port: `tools/venv/bin/python tools/port_inventory.py probe` must pass. Use the
  hooks in `include/port.h` for anything PS1-specific (see [`docs/PORT.md`](PORT.md)).

### The PC port and the launcher

The port's game side is `port/` (C: the adapter, `game.json`, the mods); the runtime, the Psy-Q shim and the launcher
(C++17, Dear ImGui on SDL3) are the `psxstack` submodule, pinned by commit. Read [`docs/PORT.md`](PORT.md),
[`docs/LAUNCHER.md`](LAUNCHER.md), [`port/README.md`](../port/README.md) and psxstack's `docs/RUNTIME.md` and
`launcher/README.md`. A change to the runtime or the launcher is a psxstack pull request, then a pin bump here; a
change to this game's adapter or mods is a pull request here. A change in behaviour should come with a test: a
layer-1 golden, a layer-2 pad script, or the launcher's self-test ([`tests/README.md`](../tests/README.md)).

### Scripts and tools

A new tool gets a `scripts/setup.sh` step that installs it into a gitignored `tools/<dir>` (add a `.gitignore` line
without a trailing slash if the directory is not already ignored). `setup.sh` installs into the main checkout's
`tools/` and links it into worktrees, so changing a pinned version affects every worktree.

### Documentation

`docs/` holds reference documentation only: how things are and why. Plans go in the project board, problems in
issues; there is no session log. Update [`docs/STATUS.md`](STATUS.md) when the state changed, and record a decision
that changes how the project works in [`docs/DECISIONS.md`](DECISIONS.md) (a few lines: what and why).

## Checks before a pull request

CI on a pull request from a fork has **no disc**, so it can only build the toolchain, compile every unit for the host
and run the disc-free checks. Please run the rest yourself and say so in the pull request:

```sh
scripts/build.sh --check                                    # every game file byte-identical, from scratch
scripts/test.sh                                             # the reference tests (needs the emulator: scripts/setup.sh redux)
tools/venv/bin/python tools/port_inventory.py probe         # the game C still compiles for the host
```

`scripts/check_paths.sh` runs in CI and takes a second: it fails if a tracked file carries a path from a specific
machine (`/home/<someone>/`, `/Users/<someone>/`, `C:\Users\<someone>`, a mounted drive). Write `<name>`, `$HOME` or a
path relative to the checkout instead.

For port or launcher changes also build them (the `cmake` commands in
[`BUILDING.md`](BUILDING.md#the-pc-port-and-the-launcher)) and run the relevant tests (`tests/port/run.py`, the
launcher's `--self-test`). CI also cross-builds them for Windows and runs them under Wine; `scripts/build_windows.sh
--test` is the same gate (it needs `scripts/setup.sh llvm-mingw sdl3-windows` and `wine`). A change to the release
packaging has its own checks in [`RELEASE.md`](RELEASE.md). `scripts/ci_areas.sh --diff origin/main` tells you which CI
areas your change touches.

## Opening a pull request

- Branch from `main`, keep the pull request focused on one thing, and write commit messages that say what changed and
  why.
- Fill in the [pull request template](../.github/PULL_REQUEST_TEMPLATE.md): what changed, how you tested it, and the
  checklist.
- Update progress with `tools/venv/bin/python tools/progress.py --readme` if your change affects the counts.
- A maintainer reviews and merges. CI must be green.

## Reporting issues

Use the [issue templates](https://github.com/al4xdev/dw3/issues/new/choose): a **bug report** for
something that is wrong in the game, the port, the launcher or the build, a **feature request** for an idea. Search the
[open issues](https://github.com/al4xdev/dw3/issues) first. A useful bug report has:

- the **version** (the AppImage's file name, or the commit you built) and your **system** (distribution, desktop, GPU,
  gamepad);
- **what happened** and **what you expected**, compared with the original game on a PS1 or an emulator if you can;
- **steps to reproduce** from a new game or a save (the memory card in `~/.local/share/dw2003/` helps);
- the **output**: the launcher shows the game's last lines after an error and has a **Copy** button for them;
- a screenshot or a short video when it is visual.

Never attach game files or a disc image.

## AI-assisted contributions

Most of this project was written with AI coding agents, and AI-assisted pull requests are welcome on the same terms as
any other: you are responsible for the change, you have run the checks above, and you can explain it. Say in the pull
request which parts were AI-generated.