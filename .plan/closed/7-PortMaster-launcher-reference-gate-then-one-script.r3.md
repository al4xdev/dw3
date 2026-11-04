# PortMaster launcher: reference audit and single-script consolidation

## Background & Objectives

PortMaster packaging guidelines emphasize using the built-in control functions provided by `control.txt` rather than maintaining bespoke device and environment detection scripts. Previously, `handheld/Digimon World 3.sh` and `handheld/dw3.sh` handled launching in two separate stages, including custom detection layers that duplicated logic already handled by PortMaster.

The goal is to consolidate the launch logic into a single idiomatic PortMaster script (`Digimon World 3.sh`) that sources `control.txt` and delegates environment setup, controls, and error dialogs to PortMaster standards, retaining only the game-specific logic (disc detection, framebuffer settings menu, and targeted GLES enablement).

## Where the edits go

Work stays on the current branch, `feature/env-detect` (tracking `public/feature/env-detect`). The owner said this is the launcher branch and it may be touched. Do not open a new branch for the cut.

The detection layer is already committed here (`00c4082`, "launcher: detect framebuffer vs Wayland without a new binary"): `handheld/detect-env.sh`, `handheld/probe.sh`, and whatever `dw3.sh` sources from them. That commit is evidence for the gate, not a protected result. If the reference already covers a behavior, the cut deletes it.

Plan 5 (`Dynamic Environment Detection and Multi-CFW Compatibility`) is closed. Plan 6, the duplicate, is closed. Neither is reopened, extended, or treated as a constraint. Do not revive their allowlists, `TODO: validate` list, or "push the branch so a tester can download a zip" delivery.

## What is not in scope

- No new environment-detection framework, and no second script whose job is to detect the CFW.
- No C, renderer, audio, or binary changes.
- `portmaster/make_release.sh` and the zip layout change only if the single launcher changes what the package must contain. Packaging is not the critic's target.
- Do not invent a PortMaster helper. If the local reference or `packaging.html` does not show it, it is not added.
- The console being reachable is not permission to SSH during the gate or the critic stages.
- Do not push, and do not open a PortMaster PR, unless the owner says so after the cut.

## Sources the gate and the critics actually read

Local copies only. Do not fetch the site again, and do not read `/home/alex/Downloads/` for this work. Provenance is `.dumps/tasks/portmaster-ref/SOURCE.txt`.

1. `.dumps/tasks/portmaster-ref/portmaster-ai-complete-reference.md` — copy of `portmaster-ai-complete-reference(1).md`, the file the reviewer named. Primary. 5058 lines.
2. `.dumps/tasks/portmaster-ref/packaging.html` — official packaging page, including the launch-script templates (`control.txt`, `pm_message`). This is the page that shows how a port script is supposed to look.
3. `.dumps/tasks/portmaster-ref/porting.html` — official porting page. Use it for the engine and runtime constraints, not as a second launcher template.
4. The current launchers on this branch and what they source: `handheld/Digimon World 3.sh`, `handheld/dw3.sh`, and helpers they actually call (`detect-env.sh`, `probe.sh`, `dw3-menu.py`). Evidence of reinvention, not a style guide.

The installed `portmaster` skill describes device install paths. It is not a guide to writing a port. Do not treat it as one.

If a copy is missing, stop. Do not reconstruct it from memory.

## Target device note

SSH to the test console (XF35H running EmuELEC) is available on the local network via standard dropbear ssh with default credentials. Stage G and Stage C do not connect. An on-device verification, if requested after the cut, uses that connection without scanning the network.

## Stage G — reference gate

Read-only. No edits. No SSH.

Write `.dumps/tasks/portmaster-launcher-gate.md` (gitignored). It must contain, each item tied to a file and a line or a heading in the local copies above:

- What `pm` / the PortMaster control script already does (messages, errors, architecture, libraries, finish, bind-mounts — only what the reference or `packaging.html` actually shows).
- Each block in our two launchers, including the detection commit, that duplicates that.
- Each block that is game-specific (disc lookup, mods, renderer or audio choice, the framebuffer menu) and has no `pm` equivalent, so it has to stay.
- The single-script shape the reference implies, including which file PortMaster actually launches.
- An explicit list of behaviors we cannot find in the reference. Those stay listed. They are not deleted in the gate.

Stage C does not start without this file. Stage R does not start without this file and both critic replies.

## Stage C — two critics, separate and parallel

After Stage G only. Two subagents, no shared transcript, same packet: the gate note, the three local copies under `.dumps/tasks/portmaster-ref/`, and the current scripts. They do not edit the repo, do not fetch the web, and do not propose a new framework.

| Critic | Route | Job |
| --- | --- | --- |
| Flash | `google-antigravity` / `gemini-3.8-flash-low` | Attack the gate note |
| Grok | `xai` / `grok-4.7` | Attack the gate note, independently |

Question for each, and only this: which claims in the gate note are wrong or unsupported by the local reference, which reinventions were missed, and which game-specific behavior would the proposed single script drop that `pm` does not cover?

These routes are not in the advertised catalog. They are the ids this harness has already selected (`gemini-3.8-flash-low`, `xai` / `grok-4.7`). If an adapter rejects one, stop and report. Do not substitute `deepseek-flash` or `gemini-4-argon-high`.

## Stage R — reconcile, then cut

Only after both critics have replied. Edits land on `feature/env-detect`.

- A finding is acted on only if the gate note or a critic points at a line in the local reference or in our scripts.
- Where the two critics disagree, keep the behavior and say so. Do not pick a side by preference.
- Then replace the two launchers with one script that calls `pm` the way `packaging.html` and the reference show, and keeps only the game-specific remainder. Helpers that only exist to reimplement `pm` go away with them.
- The size target is the reviewer's "about 75% gone", checked by line count before and after. It is a target, not a reason to delete a behavior the gate marked as game-specific.
- `make_release.sh` is updated only if the zip must ship a different launcher name.

## Stop, do not improvise

- A local copy missing, or `packaging.html` and the reference disagree on the `pm` contract: stop and ask.
- A behavior has no reference equivalent and no measured need on the XF35H: list it, do not delete it in the same turn as the collapse.
- Do not push unless the owner asks after the cut.
