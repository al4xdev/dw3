# Port plan for Digimon World 3 on the R36S (ArkOS / Linux ARM)

> Written in Portuguese at the start of the project; translated as it stands, with nothing added.

## 1. Fundamental technical guidelines

1. **Do not assume the direct framebuffer (`/dev/fb0`) is the only or the best output**:
   - The R36S has a Mali-G31 MP2 GPU and a 640x480 screen. On ArkOS and similar distributions the default pipeline
     uses SDL2 with the Mali/KMSDRM drivers.
   - Two options have to be compared:
     - **Option A:** SDL2 / OpenGL ES (GLES 2.0 / 3.0 through KMSDRM/EGL), using the Mali GPU for depth, shaders and
       the standard presentation without fighting the system.
     - **Option B:** software rasteriser + direct presentation (`/dev/fb0` or SDL streaming texture), only if the
       software rasteriser is complete and we measure that the Cortex-A35 has enough headroom.
   - The rendering decision and the use of NEON are guided by real performance measurement.

2. **Validate the memory architecture separately**:
   - Keep the game's pointers and structures 32-bit, to respect the PS1 code it came from and the 24-bit ordering
     table tags.
   - Do not treat 16 MB as a blind universal limit for every host allocation; inspect `memory.c` and `fill.c` before
     pinning anything down in the linker.
   - Keep portability to the R36S's usual userspaces (armhf 32-bit and aarch64 with multilib).

3. **Postpone the SIGSEGV handler**:
   - Do not mask failures with blind recovery in the early phase.
   - Crashes must fail loudly and diagnosably, so the root cause can be instrumented and fixed in the decompiled C.

4. **Audit the renderer in detail (`gpu_draw.c` vs `gl.c`)**:
   - Confirm whether `gpu_draw.c` produces the whole final image or only feeds the game's VRAM, delegating the 3D
     rendering, the z-buffer and the effects to `gl.c` (OpenGL 3.3).
   - If the visual effects, the battle models and the transparency depend on the accelerated path, the primary route
     is to port the desktop OpenGL 3.3 backend to **OpenGL ES 2.0 / 3.0** with SDL2.

---

## 2. Incremental milestones

### Milestone 1 (M1) - clean build for Linux ARM (32-bit)
- **Goal:** establish the cross-compilation pipeline (Docker / `arm-linux-gnueabihf` toolchain) combining the
  `dw3_decomp` C with the `dw3-native-port` patches.
- **Deliverables:**
  - A Linux build script (`build_arm.sh` or similar) producing a 32-bit ELF binary.
  - Minimal stubs for SDL and the system calls, with no Windows dependency (`windows.h`).
  - Symbol and linkage verification of the executable.

### Milestone 2 (M2) - boot and data loading on the R36S
- **Goal:** the binary boots on the console, reads the filesystem and loads the first overlay/stage.
- **Deliverables:**
  - `extract.c` / `files.c` adapted to resolve paths on Linux (`data/files/`).
  - Checksum verification of the original executable and the static tables filled (`fill.c` adapted with `mprotect`).
  - Instrumented logs confirming the main loop is entered without aborting.

### Milestone 3 (M3) - the first correct picture on the screen
- **Goal:** the first correct image on the R36S's 640x480 panel.
- **Deliverables:**
  - Comparative evaluation: SDL2 with OpenGL ES against a measured software rasteriser.
  - 4:3 aspect handling for the panel's native resolution.
  - The opening screens, the title menus and the selection screens rendered.

### Milestone 4 (M4) - audio and controls
- **Goal:** clean sound and responsive controls, integrated with the R36S's layout.
- **Deliverables:**
  - The SPU mixer (44.1 kHz, 16-bit) on an SDL2 audio stream.
  - Default mapping of the physical buttons (D-Pad, A/B/X/Y, L1/L2/R1/R2, Select, Start) through SDL Gamepad / evdev.

### Milestone 5 (M5) - gameplay, battles, videos and saves
- **Goal:** stable play across several of the game's stages.
- **Deliverables:**
  - The 3D models in battle and the sprite effects validated.
  - FMV playback (MDEC video and XA audio).
  - Reliable writing and reading of memory card saves (`.mcd`).

### Milestone 6 (M6) - handheld features and measured optimisations
- **Goal:** the essential handheld features, and the real bottlenecks optimised.
- **Deliverables:**
  - A quick menu (`L3 + R3`) and a universal exit hotkey (`Select + Start`).
  - Fast-forward with a stable timer (`clock_nanosleep(CLOCK_MONOTONIC)`).
  - SIMD (NEON) or 2D fast paths only where the telemetry shows CPU contention.

### Milestone 7 (M7) - PortMaster / ArkOS packaging
- **Goal:** a final package ready to distribute and to install directly.
- **Deliverables:**
  - The launcher script (`dw3.sh`) with the environment and audio configuration.
  - A metadata file (`dw3.json`) compatible with the PortMaster ecosystem.
  - Installation documentation with clear instructions for placing the disc image.

## What failed, or did not happen as written

Added when the plan was closed. The port works; these are the places where the plan was wrong or left undone, written down
because a plan that only records its successes is useless.

- **The machine is not an R36S.** It is an XF35H with the same RK3326/Mali-G31, so every measurement holds - but every
  line in this plan about "the R36S" is technically wrong, and the panel is the handheld's own 640x480, not a device we
  chose.
- **Point 1's comparison (Option A vs Option B) was made twice and late.** The port shipped on Option B (software +
  `/dev/fb0`) and only much later did the GLES backend exist (plan 4) - the comparison the plan asked for at M3 happened
  after M7, not before it.
- **Point 4's premise was wrong.** The plan says rendering on the Mali "breaks the byte-identical picture". It does not:
  in the frames compared with `--screenshot`, the GLES renderer's picture is byte-identical to the software one, and the
  VRAM differs in 0.2-0.7% of its texels. A premise stated as fact closed a door that should have stayed open.
- **M5's movie path was never verified end to end** on the GLES renderer: the 24-bit display path was corrected by
  reading the code, not by watching an FMV run.
- **M6's NEON work only happened where the meter pointed**, which is the right discipline but not what the milestone
  promised: the "2D fast paths" for the flat span arrived, the decoded-texture cache never did, and the audio's NEON (plan
  3) stopped at 3.1 ms of a 2.5 ms target.
- **M7's real deliverable - a published port - did not happen.** The package exists and is checked
  (`portmaster/make_release.sh`, `dist/dw3.zip`), but it was never submitted: that needs a Discord testing thread and
  tests on CFWs this machine is not, and it is the owner's call to publish.
- **Nothing here was ever tested on a second device or a second CFW.** Every number in this plan and in `docs/` comes
  from one handheld.
