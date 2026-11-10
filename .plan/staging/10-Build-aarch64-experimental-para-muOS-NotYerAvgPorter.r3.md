# Experimental aarch64 build for muOS

This plan is self-contained: a new session should be able to start from it without the earlier chat. Section "Data from the chat" holds the logs and facts collected on 2026-11-10.

## Context
- Tester: NotYerAvgPorter (Discord, timezone MST). Device: muOS 2601.0 (JACARANDA), aarch64, kernel 4.9.170, CPU part 0xd03 (Cortex-A53), Allwinner (sunxi codec), `/dev/fb0` present, no Wayland, no `/dev/dri`. Exact device model unknown: ask.
- Current failure: `./dw2003: libGLESv2.so.2: wrong ELF class: ELFCLASS64`. The binary is armhf (`port.json` arch `["armhf"]`) and their system has no 32-bit libs (`/lib/arm-linux-gnueabihf` is absent).
- `DW3_FORCE_VIDEO=sdl2` does not help: the failure is the binary failing to load, not the video path.
- A different person owns a Trimui Smart Pro S (A523, also aarch64). They asked whether the port can run there and have sent no logs. Do not mix them up: the muOS log below is NOT from the Smart Pro S owner. The same aarch64 build may serve both, unconfirmed.
- Plan for the tester: tell them (message already sent/copied on Discord) that an experimental aarch64 build is coming, software renderer + SDL2 first, and that more system logs will be needed if it fails.

## Data from the chat

### Tester's error log (`/home/alex/Downloads/game.log`, from an early version)
```
./dw2003: error while loading shared libraries: libGLESv2.so.2: wrong ELF class: ELFCLASS64
./dw2003: error while loading shared libraries: libGLESv2.so.2: wrong ELF class: ELFCLASS64
./dw2003: error while loading shared libraries: libGLESv2.so.2: wrong ELF class: ELFCLASS64
```
The tester said this log is old (early versions); use it only to see their system. A newer log.txt showed they are on the right version, but the freeze after PLAY was not covered by log.txt (launcher only).

### Tester's launcher output and probe (pasted by the user)
```
probe: wrote /mnt/mmc/ports/dw3/logs/probe.txt
detect: DW3_VIDEO=fbdev (/dev/fb0 writable and no Wayland socket)
detect: DW3_MENU=fb (framebuffer menu)
detect: DW3_RENDERER=software (software: CPU part 0xd03 != 0xd04)
detect: SDL_AUDIODRIVER=alsa (pactl unavailable or failed; alsa)
detect: SDL_VIDEODRIVER=kmsdrm
========== dw3 probe ==========
date: Thu Nov  5 09:52:45 MST 2026
uname -m: aarch64
uname -a: Linux muos-812412 4.9.170 #13 SMP PREEMPT Thu Dec 25 12:15:52 CST 2025 aarch64 muOS/2 NeXT
--- /etc/os-release ---
NAME=MustardOS
VERSION="2601.0 (JACARANDA)"
ID=muos
VERSION_ID=2601.0
PRETTY_NAME="MustardOS 2601.0 (JACARANDA)"
HOME_URL="https://muos.dev/"
SUPPORT_URL="https://community.muos.dev/"
BUILD_ID=bc38efa0
--- framebuffer ---
/dev/fb0: exists
/dev/fb0: readable and writable
crw-rw----    1 root     video      29,   0 Nov  5 06:01 /dev/fb0
--- /dev/dri ---
absent
--- wayland ---
WAYLAND_DISPLAY=
XDG_RUNTIME_DIR=/run
SDL_VIDEODRIVER=
no wayland socket in XDG_RUNTIME_DIR
--- audio ---
timeout or pactl unavailable
card 0: audiocodec [audiocodec], device 0: soc@03000000:codec_plat-sunxi-snd-codec sunxi-snd-codec-0 []
card 2: ahubhdmi [ahubhdmi], device 0: ahub_plat-soc@03000000:hdmi_codec soc@03000000:hdmi_codec-0 []
--- cpu / gpu ---
CPU part        : 0xd03   (x4)
/sys/class/misc/mali0
/dev/mali0: exists
gpu
--- armhf loader / ldd ---
/lib/ld-linux-armhf.so.3: present
/lib/arm-linux-gnueabihf: absent
        not a dynamic executable
--- PortMaster context ---
CFW_NAME=muOS
DEVICE=
DISPLAY_WIDTH=640
DISPLAY_HEIGHT=480
directory=mnt/mmc
```
Reading: the armhf loader exists but no 32-bit libs, so the armhf binary cannot run. Port path on the device is `/mnt/mmc/ports/dw3/`. Display is 640x480.

### Tester's Discord messages (summary)
- Asked for: `logs/game.log`, `menu.log` from `ports/dw3/`, the device model; with Debug logs set to heavy, press PLAY, wait a couple of minutes, then grab `logs/game.log`, `menu.log`, `logs/probe.txt`.
- Suggested `export DW3_FORCE_VIDEO=sdl2` near the top of `Digimon World 3.sh`; the exact place is line 2, right under `#!/bin/bash`. A ready zip exists at `.dumps/dw3_sdl2_test.zip` (not useful for the muOS case, see Context).
- The developer offered a live back-and-forth session on Tuesday Nov 10, 8:00 PM BRT (4:00 PM MST / 23:00 UTC) only has an RK3326 to test on.

### Trimui Smart Pro S owner's request
They want the port on the Trimui Smart Pro S (TG5050, Allwinner A523, 8x Cortex-A55, Mali-G57, 1280x720, 1 GB RAM) and offered to test. A research agent returned a long OS-porting briefing (mainline Linux, U-Boot, device tree, repo `MidG971/trimui_smart_pro_s`). That briefing is irrelevant to the task: we only need a port that runs on the existing firmware. Open questions for them: which firmware (stock, CrossMix, Knulli, muOS?), PortMaster installed?, and the same `probe.txt`.

### The developer's own device (reference, working)
`.dumps/device-20261104/logs/probe.txt` and `game.log`: EmuELEC 4.7 on an RK3326 handheld (aarch64 kernel with 32-bit userspace, `DEVICE_HAS_ARMHF=Y`), Mali-G31 GLES 3.2, `/dev/fb0` 640x480, CPU part 0xd04. This is the only tested target; the game runs there with direct fbdev present and the GLES renderer.

## Repository facts (verified 2026-11-10)
- Working repo: `/home/alex/git/my/dw3/dw2003recomp`, branch `main` at `a6f1d0f` (feature/battle-menu merged and pushed to remote `public` = `https://github.com/al4xdev/dw3.git`). `origin` = upstream `https://github.com/gascarcella/dw2003recomp`. Local branch `upstream-pc-base` mirrors upstream; upstream is 56 commits ahead by count but only 3 commits (tests) newer than our cherry-picked base 61d06ba; we are 155 commits ahead.
- Release: `gh release upload dw3-port-1 dist/dw3.zip --clobber -R al4xdev/dw3` already done with `dev-a6f1d0f`-era build (the zip showed `dev-47cdc74` before the merge, rebuilt after).
- Second repo: `/home/alex/git/my/PortMaster-MV-New` (remote `al4xdev/PortMaster-MV-New`), `ports/dw3/` updated and pushed (commit 940aa63).
- Untracked: `.plan/tasks/` in the working repo.
- Build: `tools/build_arm.sh` (Docker image `dw3-armhf-builder:latest` from `tools/Dockerfile.armhf`; cmake `-S port -B build/armhf-sdl2 -G Ninja -DCMAKE_TOOLCHAIN_FILE=/work/cmake/armhf-linux.cmake -DPSXSTACK_PYTHON=/usr/bin/python3 -DPSXSTACK_SDL2=ON -DPSXSTACK_EGL=ON`; output `build/armhf-sdl2/dw2003`; package default `../portmaster`).
- Toolchain: `cmake/armhf-linux.cmake` uses `arm-linux-gnueabihf-gcc` with `-marm -mcpu=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard`.
- Packaging: `portmaster/make_release.sh` builds `dist/dw3.zip`; it requires `file "$BINARY"` to say `ELF 32-bit LSB.*ARM` and uses `BINARY="$REPO/build/armhf-sdl2/dw2003"`. It must learn a second binary. `portmaster/port.json` has `"arch": ["armhf"]`, `"min_glibc": ""`.
- Launcher: `handheld/Digimon World 3.sh` (175 lines). Relevant lines: 16 `export PORT_32BIT="Y"`; 20-21 `GAMEDIR`, `BINARY=dw2003`; 56-77 video choice (`DW3_FORCE_VIDEO`, Wayland, fb0 -> fbdev, else sdl2); 79-90 `ldd` check with the "32-bit libraries missing" blocker; 94-104 renderer (gles only when CPU part is 0xd04); 143 `probe.sh` when logs=heavy; 155 `$GPTOKEYB2`; 157 `pm_platform_helper "$GAMEDIR/$BINARY"`; 159-173 argument assembly and `./dw2003 "$@" >> ./logs/game.log`.
- Source scan: `port/` has no ARM-specific code. In `psxstack/`: `runtime/fiber.c` (`__aarch64__` at line ~200, `__arm__` at ~236), `runtime/savestate.c` (`__aarch64__` ~578, `__arm__` ~580), `runtime/crash.c` (`__aarch64__` ~235), NEON guarded by `__ARM_NEON` in `psyq/gpu.c`, `runtime/spu.c`, `runtime/fbdev_present.c`, and two `#if defined(__arm__)` blocks in `psyq/gpu.c` at ~1386 and ~2309 still to be read.
- Known risk: `port/CMakeLists.txt` says `-O2` on the game's C crashes at boot (`sound_init` -> `sound_update_loading` -> `cdload_get_subfile`, SIGSEGV), so game objects stay at `-O0`. Must be retested on aarch64.
- `port/game/state.c` already handles 8-byte pointers for `-m64` with `_Static_assert`s.
- `psxstack/` is a submodule; the fork is `al4xdev/dw3-psxstack`. Changes there need a commit in the fork and a pin update here.
- muOS-specific launcher needs: the muOS mod file is `mod_muOS.txt` under the PortMaster control folder (to be confirmed); `$directory` is `mnt/mmc`.

## Phase 0: investigation (no port code yet)
Goal: learn what muOS really ships before building the toolchain. Everything below is still to be verified; nothing is confirmed.
1. MustardOS GitHub organisation (`MustardOS`): repositories, how the image is built (Buildroot? Debian?), the glibc version, where SDL2, video libs and ALSA come from.
2. Whether the muOS image (or its rootfs) can be downloaded and extracted. If so, read `ldd --version` and `/usr/lib` from the rootfs.
3. How muOS runs PortMaster: `mod_muOS.txt`, its variables (`DEVICE_ARCH`, `PORT_32BIT`), where SDL2 comes from (PortMaster runtime or system), video driver (`kmsdrm` vs fbdev).
4. Which exact device the tester has, and whether muOS has a build for that chip (A53 + Mali + sunxi suggests an Allwinner H700/A133-class device; unconfirmed).
5. Ask the tester for: `ldd --version`, `ls /usr/lib`, and the PortMaster runtimes folder listing (path to be confirmed).
Output: a note at `.dumps/tasks/muos-research.md` with the target glibc, the SDL2 source and the video driver.

## Phase 1: aarch64 toolchain
- Branch `feature/aarch64` on the fork; do not touch `main` or the release.
- `cmake/aarch64-linux.cmake` copied from the armhf one, without `-mfpu`/`-mfloat-abi`/`-marm`, with `-mcpu=cortex-a53`.
- An aarch64 Docker image with a low glibc (an older Debian, per Phase 0) and `tools/build_arm64.sh`.
- First build: `-DPSXSTACK_SDL2=ON -DPSXSTACK_EGL=OFF`, software renderer.
- Read the two `__arm__` blocks in `psxstack/psyq/gpu.c` and decide whether aarch64 loses performance or needs a change.
- Keep `-O0` for the game C in the first build.

## Phase 2: test on the PC
- Run the aarch64 binary under qemu-user (Docker `--platform linux/arm64` or binfmt) with `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, `--renderer software`, `--max-frames 600`, using our own disc.
- First check that the aarch64 qemu binfmt is enabled on the PC.
- If the glibc is still in doubt: mount the muOS rootfs as a sysroot and run against it.
- Limits: this does not cover kmsdrm, real ALSA or A53 performance.

## Phase 3: launcher and package
- The launcher picks the binary by architecture (`DEVICE_ARCH` or `uname -m`), with no `PORT_32BIT` on the aarch64 path, and skips the 32-bit `ldd` blocker accordingly.
- `port.json` arch `["armhf","aarch64"]`, and `make_release.sh` accepting both binaries (its ELF check currently only allows 32-bit ARM).
- Video: choose sdl2/kmsdrm vs fbdev according to Phase 0. The launcher currently picks fbdev when `/dev/fb0` is writable and there is no Wayland, which is what the tester's machine did.

## Phase 4: test on the device
- A test zip on Discord, labelled experimental.
- Ask for `logs/game.log`, `menu.log` and `logs/probe.txt` (Debug logs set to heavy; press PLAY and wait a couple of minutes).
- If it fails, the next round depends on the tester's system logs.
- Separately, ask the Trimui Smart Pro S owner for firmware name and a `probe.txt`, and offer the same build.

## Done when
The game gets past the menu and shows frames on the tester's device with the software renderer. GLES and performance are a second round.
