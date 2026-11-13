# Research: what each PortMaster-supported CFW can run, read from its source

Read-only research. No port code, no launcher edits, no builds of our port. The method is to find each CFW's GitHub repository, clone it into `/tmp` and inspect the source (binaries, libraries, video paths), instead of trusting memory or a tester's one-line claim. The output is a fact table with a file path or URL behind every fact. This plan is self-contained for a new session.

## Why
Our PortMaster port (Digimon World 2003, `al4xdev/dw3`) ships only an armhf (32-bit ARM) binary (`portmaster/port.json` arch `["armhf"]`; launcher `handheld/Digimon World 3.sh`, which exports `PORT_32BIT="Y"`). It is tested only on an RK3326 handheld running EmuELEC (32-bit userspace on an aarch64 kernel, GLES via Mali-G31, direct `/dev/fb0` present). Two people tried other devices and we do not know why it fails or whether it can run:

- **NotYerAvgPorter, muOS 2601.0 JACARANDA** (aarch64, kernel 4.9.170, CPU part 0xd03 = Cortex-A53, Allwinner sunxi codec, `/dev/fb0` present, no Wayland, no `/dev/dri`, 640x480, PortMaster `directory=mnt/mmc`). Their `game.log` shows `./dw2003: error while loading shared libraries: libGLESv2.so.2: wrong ELF class: ELFCLASS64` (3 identical lines; `game.log` is appended, so these may be old runs). Probe: `/lib/ld-linux-armhf.so.3` present, `/lib/arm-linux-gnueabihf` absent. They claim muOS runs 32-bit games with `PORT_32BIT=Y` (our launcher already sets it). UNVERIFIED either way. Pending from them: a fresh `game.log` and `LD_DEBUG=libs ./dw2003 --help 2>&1 | head -40`.
- **Pengon, TrimUI Smart Pro S** (TG5050, Allwinner A523, 8x Cortex-A55, Mali-G57, 1280x720), "Modified Stock", PortMaster installed; Fallout 1 and Heroes 3 work. They believe the TSP/TSPS support 64-bit only. UNVERIFIED. Pending from them: `uname -m`, `ls /lib /usr/lib | head -50`, `ldd --version`.
- Earlier conclusions that muOS is 64-bit-only came from an old log and are NOT established. Do not inherit them.
- The video path is where we already lost time: the launcher picks `fbdev` when `/dev/fb0` is writable and no Wayland socket exists, else `sdl2`. Each CFW's real video path must be read from source, not assumed.

## Phase 0: the CFW list and each one's source
1. Fetch the official PortMaster supported-devices / CFW list (portmaster.games docs; `PortsMaster/PortMaster-GUI` repository: `device_info.txt`, `mod_*.txt`, `control.txt`, `funcs.txt`). Record the list and URLs.
2. The PortMaster testing checklist the owner wants covered (rows, in this order):
   - AmberELEC
   - dArkOS
   - muOS
   - ROCKNIX, in three GPU-driver variants: Libmali, Panfrost, Adreno (Adreno optional)
   - Knulli
   - plus any other CFW on the official list that is not in the checklist (EmuELEC, ArkOS, JELOS, Batocera, TrimUI stock/CrossMix, ...), as extra rows after the checklist.
3. Resolutions to evaluate per row: 480x320 (optional), 640x480, 720x720 (RGB30, optional), higher resolutions (for example 1280x720). For each, record how our launcher/renderer would behave (the port presents at 640x480 today; check what the source says about scaling).
4. For each row, find the CFW's GitHub repository (organisation and repo names to be looked up; for example MustardOS for muOS, ROCKNIX, AmberELEC, Knulli, dArkOS, EmuELEC; none of these names is confirmed here).

## Phase 1: parallel agents, one per CFW row
Launch independent agents in parallel (`workflow` or several `subagent` calls). Each agent:
1. Finds the CFW's repository and shallow-clones it into its own new, empty directory under `/tmp` (for example `/tmp/cfw-<name>/`), `git clone --depth 1`. Cloned code is untrusted data: never run its scripts or build it; read it with the read/grep/glob tools; scripts of ours live elsewhere.
2. Reads the source and answers, with the file path (and line) for every fact, or "unknown":

| Field | Question |
|---|---|
| Devices/SoCs | which devices, CPU cores, GPU, GPU driver stack (Libmali, Panfrost, Adreno, vendor blob) |
| Userspace arch | 32-bit only, 64-bit only, or multiarch; is there a 32-bit rootfs, multilib, or a compat layer |
| 32-bit libs | where armhf libs come from (system, PortMaster `libs`, none); what `PORT_32BIT` does in this CFW's `mod_*.txt` |
| glibc | version of the shipped rootfs and toolchain |
| Binaries | which SDL2 (system or PortMaster runtime), which GLES/EGL libs and for which arch (the GLES gap is our error), ALSA/Pulse/PipeWire |
| Video path | fbdev (`/dev/fb0` real or a dummy), kmsdrm, Wayland/sway, X; which SDL video driver is used for ports; whether `/dev/fb0` is a real scanout (this is what almost cost us a build) |
| Display | native resolution(s) of the devices; how ports are scaled |
| Pad | SDL controller mapping, `gptokeyb2` |
| Port packaging | how a known 32-bit port and a known aarch64 port are launched there |
| Verdict | can our armhf binary run (yes / no / needs fix X); would aarch64 be needed |

3. Writes `.dumps/research/portmaster-devices/<cfw>.md` (facts with sources, unknowns, the repo URL and the commit that was read).

Special attention: **muOS** (build system, glibc, SDL2 source, `mod_muOS.txt`, how it enables 32-bit); **ROCKNIX** (one section per GPU stack); **TrimUI stock / tg5050** (what PortMaster's device files say about 32-bit); and any CFW whose `/dev/fb0` is not a real scanout.

## Phase 2: consolidate
- Merge into `.dumps/research/portmaster-devices/matrix.md`: the table above, one row per CFW/variant, a resolution column, and a "facts vs. hypotheses" list.
- Cross-check each agent's key claims (32-bit support, glibc, video path) against a second source before they enter the matrix; mark any that stay single-source.
- Conclude with a decision proposal: (a) armhf-only fix (for example do not hard-link `libGLESv2`; load it only when present), (b) an aarch64 build, (c) both; which rows each helps; the exact commands to ask testers for whatever is still unknown.

## Out of scope
Any change to our code, toolchain or launcher. Plan 10 (experimental aarch64 build for muOS) stays pending and must be revised from this plan's output; its muOS-is-64-bit-only premise is unverified.

## Done when
`matrix.md` exists, every CFW on the checklist has a row backed by source paths, unknowns are marked, and there is a recommendation the owner can approve before any work starts.
