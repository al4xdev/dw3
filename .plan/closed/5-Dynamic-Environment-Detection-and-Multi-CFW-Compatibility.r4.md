# Dynamic Environment Detection and Multi-CFW Compatibility

## Context
The Digimon World 2003 port (32-bit armhf binary) runs on the reference R36S/XF35H console (RK3326, Cortex-A35, Mali-G31, /dev/fb0). A tester on a Retroid Pocket 5 (Snapdragon 865, ROCKNIX, Wayland) successfully ran the exact same 32-bit native binary without recompilation by manually switching the video backend and settings. We want an automated environment detection layer that probes the runtime environment (not hardcoded device brands) and sets appropriate variables, enabling out-of-the-box execution on non-reference hardware while guaranteeing zero regression on the reference R36S.

## Invariants & Rules
- Work on isolated local branch `feature/env-detect`. Do not commit to `main` or `r36s-present`.
- Zero changes to C game code or host runtime (`psxstack`, `src/`, `port/`). No binary recompilation.
- Absolute regression protection: On reference RK3326 (`/dev/fb0` writable without Wayland), behavior remains byte-for-byte identical (fbdev, framebuffer menu, GLES, alsa).
- Renderer selection by strict allowlist: Default is "software". "gles" is used only if ALL criteria are true: no Wayland socket/env, `/dev/fb0` writable, CPU part is Cortex-A35 (`0xd04`), and Mali device node exists under `/sys/class/misc/mali*` or `/dev/mali0`. Any mismatch falls back to software rendering.
- Manual overrides (`settings.json` or `DW3_FORCE_RENDERER`/`DW3_FORCE_VIDEO` env vars) always take precedence over detection.
- All unverified assumptions must be marked with `TODO: validate`.

## Work Items

1. **Passive Environment Probe (`handheld/probe.sh`)**
   - Probe system and write diagnostics to `ports/dw3/logs/probe.txt` without modifying state:
     - Kernel & OS: `uname -m`, `/etc/os-release`.
     - Framebuffer: existence and writability of `/dev/fb0`, listing of `/dev/dri/`.
     - Wayland: `$WAYLAND_DISPLAY`, `$XDG_RUNTIME_DIR`, verify real socket existence (`TODO: validate`).
     - Audio: `timeout 2 pactl info` (prevent hang) and `aplay -l`.
     - CPU & GPU topology: `/proc/cpuinfo` (CPU part), `/sys/class/misc/mali*`, `/dev/mali0`, `/sys/class/devfreq`, driver names.
     - 32-bit compatibility: if `ldd` exists, run it checking for "not found"; if `ldd` is missing, log "ldd unavailable" and do NOT fail (`TODO: validate`). Check dynamic loader (`/lib/ld-linux-armhf.so.3` or `/lib/arm-linux-gnueabihf/`).
     - PortMaster context: `$CFW_NAME`, `$DEVICE`, display dimensions.

2. **Environment Detection Logic (`handheld/detect-env.sh`)**
   - Implement `detect_env()` sourced by `dw3.sh`:
     - **Decision order for Display:**
       1. Manual override (`DW3_FORCE_VIDEO` if set).
       2. Wayland present: check `$WAYLAND_DISPLAY` AND verify socket exists. If socket exists -> `DW3_VIDEO=sdl2`, `SDL_VIDEODRIVER=wayland`, `DW3_MENU=skip`. If WAYLAND_DISPLAY is set but socket path cannot be resolved -> leave `SDL_VIDEODRIVER` unset for SDL autodetection (`TODO: validate`), `DW3_VIDEO=sdl2`, `DW3_MENU=skip`.
       3. `/dev/fb0` writable -> `DW3_VIDEO=fbdev`, `DW3_MENU=fb` (reference path).
       4. Unrecognized fallback -> `DW3_VIDEO=sdl2`, `DW3_MENU=skip`, unset `SDL_VIDEODRIVER` (`TODO: validate`).
     - **Audio:** `pulseaudio` if `timeout 2 pactl info` succeeds, otherwise `alsa`. Log the choice and rationale.
     - **Renderer:** Allowlist for `gles` only if ALL true: no Wayland, `/dev/fb0` writable, CPU part `0xd04`, Mali node present (`/sys/class/misc/mali*` or `/dev/mali0`). Any mismatch -> `software`. User override (`DW3_FORCE_RENDERER` or `settings.json`) wins.
     - Log all decisions and rationales to `log.txt`.

3. **Safe Menu Fallback & Failure Modes (`handheld/dw3-menu.py` & launchers)**
   - Protect `/dev/fb0` open in `dw3-menu.py` with `try/except`. On framebuffer failure, log to `menu.log` and exit specifically with code 3 (designated exclusively for `FB_UNAVAILABLE`). Any other Python error produces standard non-zero codes (e.g. 1 or 2).
   - In `dw3.sh`, treat exit code 3 specifically as "skip menu, run game with settings.json". Treat other non-zero codes as actual menu errors.
   - Clean up and shutdown: Ensure `trap pm_finish EXIT INT TERM` or consistent exit paths so `pm_finish` always executes without violating PortMaster standards.
   - 32-bit dependency preflight: Run only if `ldd` is available. If `ldd` reports missing libs, log blocker, display `pm_show_error`, and abort. If `ldd` is absent, log warning and proceed (`TODO: validate`).
   - Document in `README.md` that headless/Wayland platforms (`DW3_MENU=skip`) configure mods via `settings.json`.

4. **Simulation, Hardware Verification & Delivery**
   - Test detection in 4 simulated states:
     - (a) Reference state (`/dev/fb0` writable, no Wayland): verifies `fbdev`, `fb` menu, `gles`, `alsa`.
     - (b) Wayland state (verified Wayland socket): verifies `sdl2`, `skip` menu, `software`.
     - (c) Unrecognized fallback (no fb0, no Wayland): verifies `sdl2`, `skip` menu, `software`, unset SDL_VIDEODRIVER.
     - (d) Override active: verifies `DW3_FORCE_RENDERER=software` wins even in reference state.
   - Concrete regression diff: assert that final variables produced in state (a) match the legacy script 1:1.
   - Hardware gate on real XF35H/R36S: verify state (a) chooses `gles`, `fbdev`, and passes `ldd` or gracefully skips it.
   - **Delivery:** Push the branch `feature/env-detect` to `al4xdev/PortMaster-MV-New` so the tester can download the zip directly from GitHub (`https://github.com/al4xdev/PortMaster-MV-New/archive/refs/heads/feature/env-detect.zip`). No local `.dumps` zip.
   - Deliver summary of changes, touched files, and explicit list of items marked `TODO: validate`.