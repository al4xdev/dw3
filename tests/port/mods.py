#!/usr/bin/env python3
"""The port's mods that change the game's behaviour, run with the mod on (docs/LAUNCHER.md "Mod runtime").

Usage: tests/port/mods.py [check] [--out DIR] [-j N] [--only MOD ...]
       tests/port/mods.py record [--out DIR]     # rewrite tests/port/mods/expected/ from this build (after a review)
The mods run N at a time (-j, default $DW3_JOBS, else every mod at once): each in its own process (this script with
--only MOD, its output printed whole, in the order below) and its own directory under --out; the port is built once
first. DW3_JOBS=1 (scripts/test.sh's default) runs them one after another.

A run with a mod on has its own expected results (the random generator steps once a frame: any skipping moves later
rolls), and the emulator has no mods, so they are the port's, committed in tests/port/mods/expected/ and checked here
together with what the mod must keep from the emulator's run. The scripts are tests/port/mods/scripts/*.json, run
with a settings file that enables the mod and --script-mods.

skip_dialogues (scripts/skip_dialogues.json; on from the start: DW3_PORT_SKIP_DIALOGUES=1): first_battle_save's route
with no press through the scenes and one press per NPC talk and per choice, each choice after 300 idle frames:
  - two runs give the same log and record, whose cross-core view equals expected/skip_dialogues.json (the choice
    checkpoints `tamer_choice` and `inn_choice` are still on their maps: the choices waited);
  - every checkpoint it shares with first_battle_save has the emulator's stable gamestate hash
    (tests/replay/expected/first_battle_save.json): skipping the text changes no game logic on this route;
  - with the mod off the same script stops at its first scene (the intro's first dialogue waits for a press);
  - the first battle with one CROSS every 100 frames needs fewer presses with the mod on than off (the battle's
    message waits go on by themselves; first_battle_save's route to the battle for the mod-off run);
  - `fast_forward_waits` on: the same log and record (only the pace changes), and fast-forward is asked for during
    the scenes' events.
battle_animations: first_battle_save itself with the mod on, `hit_reaction` on and off: the emulator's
cross-core view (the battle's rules run before its animations), and the first battle's three actions cut, the last
one keeping its KO reaction. tests/port/battle.py checks the scripts on the disc that the cut relies on.
global_save (scripts/global_save.json): first_battle_save's route to the lab after the first battle (map 0x206, not
an inn's map), the field menu's SAVE entry, STGMCARD's save, Back to the field, a reset and Continue:
  - the slot (gamestate_data's first 0x26C4 bytes, the volatile ranges aside) is the same at `saved_anywhere`,
    `back_after_save` and `loaded_anywhere`, on map 0x206; the load resumes the map as a return to it (map_is_new 0,
    prev_map 0xC00) and the card's slot tail carries the mod's record ("GSAV" at slot offset 0x26C4);
  - with `restore_map_state` off the load is the game's own (map_is_new 1);
  - with the mod off the menu has no SAVE entry: the script's SAVE press opens STATUS instead and its wait for
    STGMCARD times out.
tests/saves/run.py loads the card the mod writes in the emulator (the PS1 game ignores the record).
preset_language (no script of its own):
  - each of the seven languages, booted through the debug channel (tools/mcp/game.py), reaches the opening's map
    (0xE02; 0xE01 for Japanese) with its records_language, without the language select screen; with the mod off the
    game is still on that screen (map 0x1600) 600 frames in;
  - French through new_game.json's route from the opening (its CNTY_SEL steps replaced by a wait for map 0xE02) to
    the first field map: the opening well before the emulator's (which goes through the screen), and the new game's
    deck names (gamestate_init_records, at the title's New Game) are the French text file's.
definitive_xp (the party_xp and xp_boost mods, now the game's own: PRD 3): first_battle_save's route to
back_on_field (party member 0 fights alone, 4 experience). With DW3_DEFINITIVE=1 the members who sat out gain the
fighter's whole share (4, 4, 4) and the money stays 50 (no bits boost); without it, the original's (4, 0, 0) with the
emulator's stable hashes up to battle_won. The 14x form experience is not reached by a scripted run (member 0 fights
as its own form).
widescreen (no script of its own): first_battle_save to back_on_field with the mod on, in the headless build
(no GPU renderer): the frame log equals the run without the mod (the mod changes nothing the game does; the renderer
draws its wide canvas, tests/port/render_gpu.py), and the run logs once that the picture stays 4:3.
battle_menu: from the first battle's start (a state of the build), at the command menu: each default button chooses
its own action, the buttons not on the menu choose nothing, the mod off reads the original's menu, a pad.map that
swaps cross and square moves the actions with the buttons, two actions on one button fall back to the defaults.
evo_buff: the registry (a builtin with no options), the flag the setting sets, and the shield's condition: it is the
member as a *target* only (`first == 0`), because fightstg_rules_scale_damage divides by stats[1] and the buffed
Digimon's own attacks must keep their numbers on that turn. No scripted route digivolves (the story's first battle has
no digivolution to choose), so the arithmetic is checked where it lives; the play-test is the device.
Exit codes: 0 pass, 1 fail, 2 something missing.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/port"))
sys.path.insert(0, str(ROOT / "tests/replay"))
from run import DISC, SCRIPTS, Missing, build, tool_env  # noqa: E402  (the port test's build)
from replay import compare, cross_core_view  # noqa: E402

MODS = ROOT / "tests/port/mods"
SD_SCRIPT = MODS / "scripts/skip_dialogues.json"
SD_EXPECTED = MODS / "expected/skip_dialogues.json"
FBS_EXPECTED = ROOT / "tests/replay/expected/first_battle_save.json"
BATTLE_PRESS = {"type": "press", "buttons": ["CROSS"], "frames": 2, "release": 100,
                "until": {"type": "wait_stage", "stage": 20}, "timeout": 30000,
                "comment": "one CROSS every 102 frames until the battle report (STFGTREP)"}

FAILURES = []


def check(cond, what):
    print(f"  {'ok  ' if cond else 'FAIL'} {what}")
    if not cond:
        FAILURES.append(what)


def settings(out, name, mods):
    path = out / f"{name}.settings.json"
    path.write_text(json.dumps({"schema": 1, "disc": {"path": str(DISC)}, "video": {"window": False},
                                "memcard1": None, "memcard2": None, "mods": mods}, indent=1))
    return path


def run_port(binary, out, label, script, config=None, mods_on=False, env=None):
    """One run; returns (exit code, log bytes, record dict or None, stderr)."""
    log, record = out / f"{label}.log", out / f"{label}.json"
    cmd = [str(binary), "--script", str(script), "--log", str(log), "--record", str(record)]
    cmd += ["--config", str(config)] if config else ["--disc", str(DISC)]
    if mods_on:
        cmd.append("--script-mods")
    proc = subprocess.run(cmd, cwd=ROOT, env=dict(os.environ, **(env or {})), capture_output=True, text=True,
                          timeout=600)
    (out / f"{label}.stderr").write_text(proc.stderr)
    rec = json.loads(record.read_text()) if record.exists() and record.stat().st_size else None
    return proc.returncode, log.read_bytes() if log.exists() else b"", rec, proc.stderr


def derived(out, name, steps):
    path = out / f"{name}.script.json"
    path.write_text(json.dumps({"name": name, "max_frames": 120000, "steps": steps}, indent=1))
    return path


def battle_presses(log):
    """The CROSS presses (I lines) from the checkpoint battle_start to the end of the log."""
    n, on = 0, False
    for line in log.decode().splitlines():
        p = line.split()
        if p and p[0] == "C" and p[2] == "battle_start":
            on = True
        elif on and p and p[0] == "I" and int(p[3], 16) & 0x4000:
            n += 1
    return n


def skip_dialogues(binary, out, record):
    print("mods: skip_dialogues (tests/port/mods/scripts/skip_dialogues.json, on from the start)")
    on = settings(out, "sd_on", {"skip_dialogues": {"enabled": True}})
    env = {"DW3_PORT_SKIP_DIALOGUES": "1"}
    rc1, log1, rec1, err1 = run_port(binary, out, "sd_1", SD_SCRIPT, on, True, env)
    rc2, log2, rec2, _ = run_port(binary, out, "sd_2", SD_SCRIPT, on, True, env)
    check(rc1 == 0 and rc2 == 0 and rec1 is not None, f"two runs complete (exit {rc1}, {rc2})")
    if rec1 is None:
        print("    " + "\n    ".join(err1.splitlines()[-5:]))
        return
    check(log1 == log2 and rec1 == rec2, "the two runs' logs and records are identical")
    if record:
        SD_EXPECTED.parent.mkdir(parents=True, exist_ok=True)
        SD_EXPECTED.write_text(json.dumps(rec1, indent=1) + "\n")
        print(f"  recorded {SD_EXPECTED.relative_to(ROOT)}")
    expected = json.loads(SD_EXPECTED.read_text())
    diffs = compare(cross_core_view(expected), cross_core_view(rec1))
    check(not diffs, f"the cross-core view equals {SD_EXPECTED.relative_to(ROOT)}" + ("".join(f"\n      {d}" for d in diffs)))
    cps = {c["name"]: c for c in rec1["checkpoints"]}
    check(cps.get("tamer_choice", {}).get("map") == 0x203 and cps.get("inn_choice", {}).get("map") == 0x20A,
          "after 300 idle frames the choices are still open (tamer_choice on map 0x203, inn_choice on 0x20A)")
    emu = {c["name"]: c for c in json.loads(FBS_EXPECTED.read_text())["checkpoints"]}
    shared = [n for n in cps if n in emu]
    same = [n for n in shared if cps[n]["gamestate_sha1_stable"] == emu[n]["gamestate_sha1_stable"]]
    check(len(shared) >= 6 and same == shared,
          f"the {len(shared)} checkpoints shared with first_battle_save have the emulator's stable hashes "
          f"({', '.join(n + ('' if n in same else ' DIFFERS') for n in shared)}): no game logic changed")
    fewer = [f"{n} {emu[n]['frame'] - cps[n]['frame']:+d}" for n in shared]
    print(f"    frames saved against the emulator's mod-off run: {', '.join(fewer)}")

    rc, _, rec, err = run_port(binary, out, "sd_off", SD_SCRIPT)
    check(rc == 5 and rec is not None and not rec["checkpoints"] and "(wait_map) timed out" in err
          and rec["map_sequence"][-1]["map"] == 0x2D7,
          f"with the mod off the script stops at the intro's first dialogue (exit {rc})")

    steps = json.loads(SD_SCRIPT.read_text())["steps"]
    i = next(k for k, s in enumerate(steps) if s.get("type") == "checkpoint" and s.get("name") == "battle_start")
    rc_on, log_on, _, _ = run_port(binary, out, "battle_on", derived(out, "battle_on", steps[:i + 1] + [BATTLE_PRESS]),
                                   on, True, env)
    fbs = json.loads((SCRIPTS / "first_battle_save.json").read_text())["steps"]
    j = next(k for k, s in enumerate(fbs) if s.get("type") == "checkpoint" and s.get("name") == "battle_start")
    rc_off, log_off, _, _ = run_port(binary, out, "battle_off", derived(out, "battle_off", fbs[:j + 1] + [BATTLE_PRESS]))
    p_on, p_off = battle_presses(log_on), battle_presses(log_off)
    check(rc_on == 0 and rc_off == 0 and 0 < p_on < p_off,
          f"the first battle, one CROSS every 102 frames: {p_on} presses with the mod on, {p_off} off "
          f"(the battle's messages go on by themselves)")

    ffw = settings(out, "sd_ffw", {"skip_dialogues": {"enabled": True, "fast_forward_waits": True}})
    rc, log, rec, err = run_port(binary, out, "sd_ffw", SD_SCRIPT, ffw, True, env)
    asked = err.count("(a cutscene's waits)")
    check(rc == 0 and log == log1 and rec == rec1 and asked > 0,
          f"fast_forward_waits: the same log and record, fast-forward asked for {asked} time(s) during the events")


def battle_animations(binary, out):
    """battle_animations: first_battle_save itself with the mod on (both settings of hit_reaction) must give the
    emulator's cross-core view (the battle's rules ran before the animations: the outcome, the save, the reload, the
    shops are the same), with the first battle's three actions cut (the last one a knock-out, which keeps its KO
    reaction in both settings)."""
    print("mods: battle_animations (first_battle_save, the mod on)")
    emu = json.loads(FBS_EXPECTED.read_text())
    for hit in (True, False):
        cfg = out / f"ba_{hit}.settings.json"
        cfg.write_text(json.dumps({"schema": 1, "disc": {"path": str(DISC)}, "video": {"window": False},
                                   "memcard1": f"ba_{hit}_1.mcd", "memcard2": f"ba_{hit}_2.mcd",
                                   "mods": {"battle_animations": {"enabled": True, "hit_reaction": hit}}}))
        for card in (out / f"ba_{hit}_1.mcd", out / f"ba_{hit}_2.mcd"):
            card.unlink(missing_ok=True)
        rc, _, rec, err = run_port(binary, out, f"ba_{hit}", SCRIPTS / "first_battle_save.json", cfg, True,
                                   {"DW3_PORT_RESET_CHECK": "1"})
        diffs = compare(cross_core_view(emu), cross_core_view(rec)) if rec else ["no record"]
        cuts = [l.split("cut", 1)[1] for l in err.splitlines() if "battle animations: frame" in l]
        battle = [c for c in cuts]
        ko = bool(battle) and "reaction 3" in battle[-1]
        reactions = sum("the target's reaction" in c for c in battle)
        check(rc == 0 and not diffs, f"hit_reaction {str(hit).lower()}: the emulator's cross-core view (all "
              f"{len(emu['checkpoints'])} checkpoints, sequences){'' if not diffs else ': ' + '; '.join(diffs[:2])}")
        want = len(battle) if hit else 1
        check(len(battle) == 3 and ko and reactions == want,
              f"hit_reaction {str(hit).lower()}: the first battle's {len(battle)} actions cut, {reactions} kept as the "
              f"target's reaction, the knock-out's KO reaction kept")
        if rec:
            cps = {c["name"]: c for c in rec["checkpoints"]}
            e = {c["name"]: c for c in emu["checkpoints"]}
            print(f"    the battle took {cps['battle_won']['frame'] - cps['battle_start']['frame']} frames "
                  f"(emulator, the mod off: {e['battle_won']['frame'] - e['battle_start']['frame']})")


def battle_text(binary, out):
    """battle_text: first_battle_save's battle with each mode on. The message box is presentation (the rules ran before
    it), so the battle's outcome (the game state at battle_won and battle_end, `gamestate_sha1_stable`) must be the
    same in every mode, and the battle must take fewer frames than with the mode off (the messages no longer wait for
    a press). Only the checkpoints up to battle_end are compared: the route's later steps are the disc replay's."""
    print("mods: battle_text (first_battle_save to battle_end, the three modes)")
    # the port keeps the pad's raw bits while the route was written for the rotated ones (see bm_state): CROSS and
    # CIRCLE swapped
    swap = {"CROSS": "CIRCLE", "CIRCLE": "CROSS"}
    steps = json.loads((SCRIPTS / "first_battle_save.json").read_text())["steps"]
    route = derived(out, "bt_route", [dict(s, buttons=[swap.get(b, b) for b in s["buttons"]]) if "buttons" in s else s
                                      for s in steps])
    seen = {}
    for mode in ("off", "skip", "async"):
        cfg = out / f"bt_{mode}.settings.json"
        cfg.write_text(json.dumps({"schema": 1, "disc": {"path": str(DISC)}, "video": {"window": False},
                                   "memcard1": f"bt_{mode}_1.mcd", "memcard2": f"bt_{mode}_2.mcd",
                                   "mods": {"battle_text": {"enabled": True, "mode": mode}}}))
        for card in (out / f"bt_{mode}_1.mcd", out / f"bt_{mode}_2.mcd"):
            card.unlink(missing_ok=True)
        _, _, rec, _ = run_port(binary, out, f"bt_{mode}", route, cfg, True)   # the route fails after battle_end
        cps = {c["name"]: c for c in rec["checkpoints"]} if rec else {}
        check(all(n in cps for n in ("battle_start", "battle_won", "battle_end")),
              f"mode {mode}: the battle is won and ends")
        if "battle_end" in cps:
            seen[mode] = (cps["battle_won"]["gamestate_sha1_stable"], cps["battle_end"]["gamestate_sha1_stable"],
                          cps["battle_won"]["frame"] - cps["battle_start"]["frame"])
            print(f"    the battle took {seen[mode][2]} frames")
    if len(seen) == 3:
        check(seen["skip"][:2] == seen["off"][:2] == seen["async"][:2],
              "the game state at battle_won and battle_end is the same in all three modes")
        check(seen["skip"][2] < seen["off"][2] and seen["async"][2] < seen["off"][2],
              f"skip ({seen['skip'][2]}) and async ({seen['async'][2]}) take fewer frames than off ({seen['off'][2]})")


def widescreen(binary, out):
    """widescreen: the battle with the mod on in the headless build: the game's frame log unchanged, the software
    picture's 4:3 logged once."""
    print("mods: widescreen (first_battle_save to back_on_field, the headless build: no GPU renderer)")
    fbs = json.loads((SCRIPTS / "first_battle_save.json").read_text())["steps"]
    j = next(k for k, s in enumerate(fbs) if s.get("type") == "checkpoint" and s.get("name") == "back_on_field")
    script = derived(out, "ws", fbs[:j + 1])
    rc_on, log_on, _, err = run_port(binary, out, "ws_on", script, settings(out, "ws", {"widescreen": {"enabled": True}}),
                                     True)
    rc_off, log_off, _, _ = run_port(binary, out, "ws_off", script)
    said = err.count("widescreen: needs the GPU renderer")
    check(rc_on == rc_off == 0 and log_on == log_off and len(log_on) > 0,
          f"the frame log with the mod equals the one without (exit {rc_on}, {rc_off})")
    check("mods: widescreen on" in err and said == 1, f"the 4:3 picture logged once ({said} times)")


def led_bar(binary, out):
    """led_bar: a machine with no LED bar (this one) - the mod changes nothing and says so once.

    The bar is an MCU on the handheld's /dev/ttyS2 with no sysfs node; the mod talks to the package's daemon over
    /tmp/ledd.sock and gives up when neither the daemon nor ./ledd.py is there. That is the whole of its behaviour off
    the handheld, so that is what is testable here: the frame log equals the run without it, and one line says why the
    bar is being left alone. The palette and the daemon's lock are device tests (docs/XF35H.md).
    """
    print("mods: led_bar (first_battle_save to back_on_field, this machine has no LED bar)")
    fbs = json.loads((SCRIPTS / "first_battle_save.json").read_text())["steps"]
    j = next(k for k, s in enumerate(fbs) if s.get("type") == "checkpoint" and s.get("name") == "back_on_field")
    script = derived(out, "led", fbs[:j + 1])
    rc_on, log_on, _, err_on = run_port(binary, out, "led_on", script,
                                        settings(out, "led", {"led_bar": {"enabled": True}}), True)
    rc_off, log_off, _, err_off = run_port(binary, out, "led_off", script)
    said = err_on.count("led bar:")
    check(rc_on == rc_off == 0 and log_on == log_off and len(log_on) > 0,
          f"the frame log with the mod equals the one without (exit {rc_on}, {rc_off})")
    check(said == 1 and "led bar:" not in err_off,
          f"the missing bar logged once ({said} time(s)), and nothing without the mod")


GS_SCRIPT = MODS / "scripts/global_save.json"
GS_FIELDS = {"map": 0x26C4, "prev_map": 0x26CC, "map_is_new": 0x26D8, "field_last_map": 0x26DC}


def gs_field(dump, name):
    return int.from_bytes(dump[GS_FIELDS[name]:GS_FIELDS[name] + 4], "little", signed=True)


def gs_stable_slot(dump):
    """gamestate_data's slot bytes (4..0x26C4) with layer 2's volatile ranges zeroed."""
    from replay import VOLATILE_RANGES
    stable = bytearray(dump)
    for lo, hi in VOLATILE_RANGES:
        stable[lo:hi] = bytes(hi - lo)
    return bytes(stable[4:0x26C4])


def global_save(binary, out):
    """global_save: a save from the field menu on a map that is not an inn's, Back, a reset and its load (the docstring)."""
    print("mods: global_save (tests/port/mods/scripts/global_save.json, a save from the lab and its load)")
    runs = {}
    for label, restore in (("gs_on", True), ("gs_norestore", False)):
        cfg = out / f"{label}.settings.json"
        cfg.write_text(json.dumps({"schema": 1, "disc": {"path": str(DISC)}, "video": {"window": False},
                                   "memcard1": f"{label}_1.mcd", "memcard2": None,
                                   "mods": {"global_save": {"enabled": True, "restore_map_state": restore}}}))
        (out / f"{label}_1.mcd").unlink(missing_ok=True)
        dumps = out / f"{label}_dumps"
        dumps.mkdir()
        rc, _, rec, err = run_port(binary, out, label, GS_SCRIPT, cfg, True, {"DW3_PORT_CHECKPOINT_DIR": str(dumps)})
        if rc != 0 or rec is None:
            print("    " + "\n    ".join(err.splitlines()[-5:]))
        d = {f.name.split("_", 1)[1][:-4]: f.read_bytes() for f in dumps.glob("cp*.bin")}
        runs[label] = (rc, rec, d, err)
    rc, rec, d, err = runs["gs_on"]
    cps = {c["name"]: c for c in rec["checkpoints"]} if rec else {}
    names = ("menu_open", "saved_anywhere", "back_after_save", "loaded_anywhere", "loaded_settled")
    check(rc == 0 and all(n in d for n in names), f"the run completes with its checkpoints (exit {rc})")
    if not all(n in d for n in names):
        return
    check(cps["saved_anywhere"]["stage"] == 12 and cps["saved_anywhere"]["map"] == 0xC00 and
          all(cps[n]["map"] == 0x206 and cps[n]["stage"] == 2 for n in ("menu_open", "back_after_save", "loaded_anywhere")),
          "SAVE on the lab (map 0x206) opens STGMCARD as map 0xC00; Back and the load return to the lab")
    check(gs_stable_slot(d["saved_anywhere"]) == gs_stable_slot(d["loaded_anywhere"]) ==
          gs_stable_slot(d["back_after_save"]) == gs_stable_slot(d["menu_open"]),
          "the slot's bytes are the same when the menu opened, saved, back on the field and loaded")
    # loaded_anywhere is taken as FIELDSTG loads, before its fieldstg_update_main sets map_is_new; loaded_settled after
    check(gs_field(d["loaded_settled"], "map_is_new") == 0 and gs_field(d["loaded_settled"], "prev_map") == 0xC00 and
          gs_field(d["loaded_settled"], "field_last_map") == 0x206 and
          "the map's state restored (map 0x206" in err and "the map's state saved with the slot (map 0x206" in err,
          "the load resumes the lab as a return to it (map_is_new 0 once FIELDSTG runs; the record saved and restored)")
    card = (out / "gs_on_1.mcd").read_bytes()
    check(card[0x2000 + 0x300 + 0x26C4:][:5] == b"GSAV\x01",
          "the card's slot 1 tail (block 1, file offset 0x300 + 0x26C4) carries the record, version 1")
    rc2, rec2, d2, err2 = runs["gs_norestore"]
    check(rc2 == 0 and "loaded_settled" in d2 and gs_field(d2["loaded_settled"], "map_is_new") == 1 and
          gs_stable_slot(d2["loaded_anywhere"]) == gs_stable_slot(d["saved_anywhere"]) and
          "not restored (restore_map_state off)" in err2,
          f"restore_map_state off: the same slot, the map entered fresh (map_is_new 1) (exit {rc2})")
    rc3, _, rec3, err3 = run_port(binary, out, "gs_off", GS_SCRIPT)
    check(rc3 == 5 and rec3 is not None and "wait_stage) timed out" in err3 and
          all(c["name"] != "saved_anywhere" for c in rec3["checkpoints"]) and rec3["map_sequence"][-1]["map"] == 0x1000,
          f"with the mod off the menu has no SAVE: the press opens STATUS (map 0x1000) and the wait for STGMCARD times out (exit {rc3})")


PL_CODES = {"english": 2, "french": 3, "german": 5, "italian": 4, "spanish": 6, "usa": 1, "japanese": 0}
PL_DECKS = 0x628  # gamestate_data.decks[3]: GamestateDeck (0x66 bytes), its name first
NEW_GAME = SCRIPTS / "new_game.json"
NG_EXPECTED = ROOT / "tests/replay/expected/new_game.json"


def pl_boot(binary, out, lang):
    """One boot through the debug channel with preset_language on (lang) or off (None): (map, records_language,
    frame) once the opening's map is reached, or after 600 frames on the language select screen."""
    sys.path.insert(0, str(ROOT / "tools"))
    import mcp_game  # noqa: E402
    from mcp_game import Game  # the debug channel's client (stdlib only)
    mods = {"preset_language": {"enabled": True, "language": lang}} if lang else {}
    cfg = settings(out, f"pl_{lang or 'off'}", mods)
    rl = mcp_game.symbols(binary).host("records_language").addr
    g = Game.spawn([str(binary), "--config", str(cfg), "--cd-speed", "instant"], cwd=str(ROOT), hold=True)
    try:
        r = g.wait(ps1_map=0xE01 if lang == "japanese" else 0xE02, timeout=600)
        st = g.status()
        return st["map"], int.from_bytes(g.peek(rl, 4), "little", signed=True), r["frame"]
    finally:
        g.stop()


def preset_language(binary, out):
    """preset_language: every language boots past the language select screen with its records_language, and a new
    game in French starts with the French text (the docstring)."""
    print("mods: preset_language (boots through the debug channel; new_game's route from the opening in French)")
    for lang, code in PL_CODES.items():
        m, rl, frame = pl_boot(binary, out, lang)
        want = 0xE01 if code == 0 else 0xE02
        check(m == want and rl == code, f"{lang}: the opening's map {m:#x} (want {want:#x}) at frame {frame}, "
              f"records_language {rl} (want {code}), no language select screen")
    m, rl, frame = pl_boot(binary, out, None)
    check(m == 0x1600 and rl == 2, f"the mod off: still on the language select screen (map {m:#x}) after {frame} "
          f"frames, records_language {rl}")
    # new_game.json from the opening on: its first three steps (CNTY_SEL, its checkpoint, the START that confirms
    # English) become a wait for the opening's map, and the route stops at the first field map's checkpoint
    steps = json.loads(NEW_GAME.read_text())["steps"]
    names = [s.get("name") for s in steps]
    script = derived(out, "preset_language", [{"type": "wait_map", "map": "0xE02", "timeout": 3000}] +
                     steps[3:names.index("new_game_field") + 1])
    cfg = settings(out, "pl_new_game", {"preset_language": {"enabled": True, "language": "french"}})
    dumps = out / "pl_dumps"
    dumps.mkdir()
    rc, _, rec, err = run_port(binary, out, "pl_new_game", script, cfg, True, {"DW3_PORT_CHECKPOINT_DIR": str(dumps)})
    if rc != 0 or rec is None:
        print("    " + "\n    ".join(err.splitlines()[-5:]))
    cps = {c["name"]: c for c in rec["checkpoints"]} if rec else {}
    emu = {c["name"]: c for c in json.loads(NG_EXPECTED.read_text())["checkpoints"]}
    check(rc == 0 and "opening_movie" in cps and "new_game_field" in cps and
          cps["opening_movie"]["frame"] + 100 < emu["opening_movie"]["frame"],
          f"French: new_game's route from the opening to the first field map (exit {rc}); the opening at frame "
          f"{cps.get('opening_movie', {}).get('frame')} (the emulator's, through the screen: {emu['opening_movie']['frame']})")
    dump = next(dumps.glob("cp*_new_game_field.bin"), None)
    if dump is None:
        check(False, "French: the new_game_field checkpoint's dump")
        return
    sys.path.insert(0, str(ROOT / "tools"))
    import disc_files
    import dump_text
    text = disc_files.read(0x32 + PL_CODES["french"])
    offs = dump_text.table_offsets(text)
    want = [text[offs[0x16 + j]:].split(b"\0")[0] for j in range(3)]
    d = dump.read_bytes()
    got = [d[PL_DECKS + 0x66 * j:][:0x16].split(b"\0")[0] for j in range(3)]
    check(got == want, f"French: the new game's deck names are the French text file's (0x35, entries 0x16-0x18): {got}")


FIRST_BATTLE_MONEY = 50  # stfgtrep_rewards[records_battle_results.battle].money of the first battle


def px_party_exp(dump):
    """The party's (Digimon, exp) from a gamestate_data dump: party[3] at 0x70, digimon[i].record.exp at
    0x75C + i * 0x3DC + 0xC + 0x18."""
    party = [int.from_bytes(dump[0x70 + 4 * k:0x74 + 4 * k], "little", signed=True) for k in range(3)]
    return [(d, int.from_bytes(dump[0x780 + d * 0x3DC:0x784 + d * 0x3DC], "little", signed=True)) for d in party]


def gs_money(dump):
    return int.from_bytes(dump[0x6C:0x70], "little", signed=True)


def first_battle_runs(binary, out, runs, env=None, same_battle=True):
    """first_battle_save's route to back_on_field once per (label, mods) of `runs` (mods None: off). Yields (label,
    mods, the party's exp gains and the money gain from battle_won to battle_end, stderr) for the runs that reach
    back_on_field; with `same_battle`, battle_start and battle_won must be at the emulator's stable hashes (the mods act
    in the report only). `env` is added to each run's environment."""
    fbs = json.loads((SCRIPTS / "first_battle_save.json").read_text())["steps"]
    j = next(k for k, s in enumerate(fbs) if s.get("type") == "checkpoint" and s.get("name") == "back_on_field")
    script = derived(out, "first_battle", fbs[:j + 1])
    emu = {c["name"]: c for c in json.loads(FBS_EXPECTED.read_text())["checkpoints"]}
    for label, mods in runs:
        dumps = out / f"{label}_dumps"
        dumps.mkdir()
        cfg = settings(out, label, mods) if mods else None
        rc, _, rec, err = run_port(binary, out, label, script, cfg, mods is not None,
                                   dict(env or {}, DW3_PORT_CHECKPOINT_DIR=str(dumps)))
        d = {f.name.split("_", 1)[1][:-4]: f.read_bytes() for f in dumps.glob("cp*.bin")}
        if rc != 0 or "battle_end" not in d:
            check(False, f"{label}: the run reaches back_on_field (exit {rc})")
            print("    " + "\n    ".join(err.splitlines()[-5:]))
            continue
        cps = {c["name"]: c for c in rec["checkpoints"]}
        before = ("battle_start", "battle_won")
        if same_battle:
            check(all(cps[n]["gamestate_sha1_stable"] == emu[n]["gamestate_sha1_stable"] for n in before),
                  f"{label}: battle_start and battle_won have the emulator's stable hashes")
        won, end = px_party_exp(d["battle_won"]), px_party_exp(d["battle_end"])
        gains = tuple(e[1] - w[1] for w, e in zip(won, end))
        yield label, mods, won, gains, gs_money(d["battle_end"]) - gs_money(d["battle_won"]), err


def definitive_xp(binary, out):
    """The definitive edition's experience (PRD 3, A: the party_xp and xp_boost mods made the game's own): with
    DW3_DEFINITIVE=1 and no mod, the first battle's report gives the two members who sat out the fighter's whole share
    (DEF_XP_PARTY_SHARE 100) and the money is the battle's own (no bits boost); without it, the original's report."""
    print("mods: definitive_xp (first_battle_save to back_on_field: one member of three fights, 4 experience)")
    want = {"xp_definitive": (4, 4, 4), "xp_original": (4, 0, 0)}
    for label, env in (("xp_definitive", {"DW3_DEFINITIVE": "1"}), ("xp_original", None)):
        for _, _, won, gains, money, err in first_battle_runs(binary, out, ((label, None),), env, env is None):
            shares = err.count("party xp: frame")
            check(gains == want[label] and shares == (3 if env else 0),
                  f"{label}: the party (Digimon {', '.join(str(w[0]) for w in won)}) gains {gains}, want {want[label]}; "
                  f"{shares} share(s) logged")
            check(money == FIRST_BATTLE_MONEY, f"{label}: the money gain {money}, want {FIRST_BATTLE_MONEY}")


def evo_buff(binary, out):
    """evo_buff: the manifest is the game's registry, the flag follows the setting, and - the review's bug - the
    one-turn shield is the member as a *target* only, never as an attacker.

    The first battle of the story has no digivolution to choose, so no scripted route reaches wfightmn_digivolve; the
    arithmetic is therefore checked where it lives: the shield writes `out->stats[1]`, the statistic
    `fightstg_rules_scale_damage` divides by, and must not write it on the attacker's own read of the same member, or
    the buffed Digimon's attacks on that enemy turn would each be divided by 9999. The play-test is the device.
    """
    print("mods: evo_buff (the manifest, the flag, and the shield's condition)")
    reg = json.loads(subprocess.run([str(binary), "--print-mods"], cwd=ROOT, capture_output=True, text=True).stdout)
    entry = next((m for m in reg if m["id"] == "evo_buff"), None)
    check(entry is not None and entry["kind"] == "builtin" and entry["options"] == [],
          f"the registry has evo_buff as a builtin without options ({entry})")
    cfg = settings(out, "evo_flags", {"evo_buff": {"enabled": True}})
    rc, _, _, err = run_port(binary, out, "evo_flags", derived(out, "evo_flags", []), cfg, True)
    check("evo_buff on" in err and "evo_buff off" not in err,
          f"the mod starts when the setting enables it ({[l for l in err.splitlines() if 'evo_buff' in l]})")
    src = (ROOT / "src/fightstg/fightstg_8008D3B4.c").read_text()
    line = next((l.strip() for l in src.splitlines() if "dw3_evo_shield_member) {" in l), "")
    check(line.startswith("if (first == 0 && member == dw3_evo_shield_member)"),
          f"the shield applies to the target's read only ({line!r})")


def bm_state(binary, out):
    """The first battle's start (first_battle_save's checkpoint battle_start) saved as a state of this binary, or None.

    The port keeps the pad's raw bits (pad.c, PC_PORT) while the scripts of the replays were written for the rotated
    ones (CROSS confirms): when the route does not get there as it is, it is run again with CROSS and CIRCLE swapped."""
    steps = json.loads((SCRIPTS / "first_battle_save.json").read_text())["steps"]
    steps = steps[:next(i for i, s in enumerate(steps) if s.get("name") == "battle_start") + 1]
    # the state is written at the end of the checkpoint's frame: a script that ends on the checkpoint exits first
    steps.append({"type": "wait_frames", "frames": 5})
    state = out / "battle_start.state"
    swap = {"CROSS": "CIRCLE", "CIRCLE": "CROSS"}
    for swapped in (False, True):
        route = [dict(s, buttons=[swap.get(b, b) for b in s["buttons"]]) if swapped and "buttons" in s else s
                 for s in steps]
        script = derived(out, f"bm_route_{swapped}", route)
        proc = subprocess.run([str(binary), "--disc", str(DISC), "--script", str(script), "--save-state",
                               f"battle_start:{state}", "--save-state-exit"], cwd=ROOT, capture_output=True,
                              text=True, timeout=600)
        if proc.returncode == 0 and state.exists():
            return state
    return None


def bm_press(binary, out, state, label, button, mods, remap=None):
    """One press of `button` at the first battle's command menu with the mod's settings `mods` (None: the mod off);
    returns the menu's log lines. `remap`: the text of a pad.map, applied (DW3_PAD_REMAP=1) from a directory of its own."""
    cfg = settings(out, f"bm_{label}", {} if mods is None else {"battle_menu": dict(mods, enabled=True)})
    steps = [{"type": "wait_frames", "frames": 700}]
    if button:
        steps.append({"type": "press", "buttons": [button], "frames": 2, "release": 2})
    steps += [{"type": "wait_frames", "frames": 40}, {"type": "checkpoint", "name": "end"}]
    script = derived(out, f"bm_{label}", steps)
    cwd, env = ROOT, dict(os.environ)
    if remap is not None:
        cwd = out / f"bm_{label}_dir"
        cwd.mkdir()
        (cwd / "pad.map").write_text(remap)
        env["DW3_PAD_REMAP"] = "1"
    proc = subprocess.run([str(binary), "--config", str(cfg), "--script", str(script), "--script-mods",
                           "--load-state", str(state)], cwd=cwd, env=env, capture_output=True, text=True, timeout=600)
    (out / f"bm_{label}.stderr").write_text(proc.stderr)
    return proc.returncode, [l.split("port: ", 1)[1] for l in proc.stderr.splitlines() if "battle menu" in l]


def bm_chosen(lines):
    """[(word, action, bit)] of the menu's press lines."""
    got = []
    for l in lines:
        p = l.replace("(", "").replace(")", "").split()
        if "frame" in p and p[-4] in ("action", "refused"):   # battle menu: frame N: action A bit B
            got.append((p[-4], int(p[-3]), int(p[-1])))
    return got


def battle_menu(binary, out):
    """battle_menu (docs/LAUNCHER.md "Battle menu"): from the first battle's start, at its command menu, with the mod's
    default buttons each of the six presses chooses its own action; a button that is not on the menu chooses nothing;
    the mod off draws and reads the original's menu (no press line); a pad.map that swaps cross and square moves the
    actions with the buttons (the menu follows the player's mapping); two actions on one button fall back to the
    defaults. What the submenus do with the choice is the game's own: the tests read the menu's result only."""
    print("mods: battle_menu (the first battle's command menu)")
    state = bm_state(binary, out)
    check(state is not None, "the first battle's start saved as a state")
    if state is None:
        return
    # the default places: east Fight, l1 Tech, north Digivolve, south Tag, r1 Item, west Run (script names are the runtime's)
    for button, action, bit in (("CIRCLE", 0, 13), ("L1", 1, 10), ("TRIANGLE", 2, 12), ("CROSS", 3, 14),
                                ("R1", 4, 11), ("SQUARE", 5, 15)):
        rc, lines = bm_press(binary, out, state, f"{button}", button, {})
        got = bm_chosen(lines)
        check(rc == 0 and len(got) == 1 and got[0][1:] == (action, bit),
              f"{button}: action {action} from bit {bit} ({got})")
    for button in (None, "L2", "R2", "UP", "DOWN", "START"):
        rc, lines = bm_press(binary, out, state, f"none_{button}", button, {})
        check(rc == 0 and not bm_chosen(lines), f"{button or 'no press'}: no action chosen ({bm_chosen(lines)})")
    rc, lines = bm_press(binary, out, state, "off", "CROSS", None)
    check(rc == 0 and not lines, f"the mod off: the original's menu, no press line ({lines})")
    # the player's pad: logical cross is delivered by the west button, logical square by the south one
    remap = "cross west B\nsquare south X\n"
    for button, action, bit in (("CROSS", 3, 15), ("SQUARE", 5, 14)):
        rc, lines = bm_press(binary, out, state, f"remap_{button}", button, {}, remap)
        got = bm_chosen(lines)
        check(rc == 0 and len(got) == 1 and got[0][1:] == (action, bit),
              f"pad.map swapping cross and square: {button} chooses action {action} through bit {bit} ({got})")
    rc, lines = bm_press(binary, out, state, "dup", "SQUARE", {"fight": "west", "tech": "west"})
    got = bm_chosen(lines)
    check(rc == 0 and any("two actions on one button" in l for l in lines) and got == [("action", 5, 15)],
          f"fight and tech on one button: the defaults are used and logged ({got})")
    rc, lines = bm_press(binary, out, state, "moved", "R2", {"run": "r2", "tag": "l2"})
    got = bm_chosen(lines)
    check(rc == 0 and got == [("action", 5, 9)], f"run moved to R2: R2 chooses action 5 ({got})")
    battle_menu_flow(binary, out, state)


def bm_run(binary, out, state, label, steps, mods):
    """The first battle from its start state with a script of `steps`; battle_menu with `mods`; returns the stderr lines
    of the menu's mod ("battle menu")."""
    cfg = settings(out, f"bm_{label}", {"battle_menu": dict(mods, enabled=True),
                                        "battle_text": {"enabled": True, "mode": "skip"}})
    script = derived(out, f"bm_{label}", steps + [{"type": "checkpoint", "name": "end"}])
    proc = subprocess.run([str(binary), "--config", str(cfg), "--script", str(script), "--script-mods",
                           "--load-state", str(state)], cwd=ROOT, capture_output=True, text=True, timeout=600)
    (out / f"bm_{label}.stderr").write_text(proc.stderr)
    return proc.returncode, [l.split("port: ", 1)[1] for l in proc.stderr.splitlines() if "battle menu" in l]


def battle_menu_flow(binary, out, state):
    """remember_cursor and buffer_input, from the first battle's start."""
    tap = lambda b: {"type": "press", "buttons": [b], "frames": 2, "release": 6}
    wait = lambda n: {"type": "wait_frames", "frames": n}
    # remember_cursor: action 5 (Run) on turn 1, then the enemy's turn; turn 2 opens with last 5 when remembered, 0 when off
    run_act = [wait(700), tap("SQUARE"), wait(1500), wait(60)]
    for remember in (True, False):
        rc, lines = bm_run(binary, out, state, f"remember_{remember}", run_act,
                           {"remember_cursor": remember, "buffer_input": "off"})
        lasts = [int(l.split("last ")[1]) for l in lines if "command menu created with last" in l]
        want = [0, 5] if remember else [0, 0]
        check(rc == 0 and lasts[:2] == want,
              f"remember_cursor {str(remember).lower()}: the command menu keeps the last choice ({lasts[:2]}, want {want})")
    # buffer_input: Fight, then Fight pressed every 8 frames through the animation and the enemy's turn: with the buffer
    # one press falls in the window before each next menu and is given to it; without it none is
    mash = [wait(700), tap("CIRCLE")] + [s for _ in range(150) for s in (wait(8), tap("CIRCLE"))] + [wait(60)]
    results = {}
    for buf in ("off", "short", "long"):
        rc, lines = bm_run(binary, out, state, f"buffer_{buf}", mash, {"remember_cursor": False, "buffer_input": buf})
        results[buf] = (rc, sum("buffered press" in l for l in lines), len(bm_chosen(lines)))
    check(results["off"][0] == 0 and results["off"][1] == 0, f"buffer_input off: nothing is given ({results['off']})")
    for buf in ("short", "long"):
        rc, given, chosen = results[buf]
        check(rc == 0 and given >= 1 and given <= chosen,
              f"buffer_input {buf}: a press before the menu is given to it, never more than the menus ({results[buf]})")


# Every mod's test, the longest first (the parallel run's critical path): name -> function(binary, out, record).
MODS_TESTS = {
    "preset_language": lambda binary, out, record: preset_language(binary, out),
    "skip_dialogues": skip_dialogues,
    "definitive_xp": lambda binary, out, record: definitive_xp(binary, out),
    "evo_buff": lambda binary, out, record: evo_buff(binary, out),
    "global_save": lambda binary, out, record: global_save(binary, out),
    "battle_animations": lambda binary, out, record: battle_animations(binary, out),
    "battle_text": lambda binary, out, record: battle_text(binary, out),
    "widescreen": lambda binary, out, record: widescreen(binary, out),
    "battle_menu": lambda binary, out, record: battle_menu(binary, out),
    "led_bar": lambda binary, out, record: led_bar(binary, out),
}


def run_child(name, mode, out, binary):
    """One mod's test in its own process (--only); returns (name, exit code, its output)."""
    cmd = [sys.executable, str(Path(__file__).resolve()), mode, "--only", name, "--out", str(out / name),
           "--binary", str(binary)]
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return name, proc.returncode, proc.stdout + (proc.stderr if proc.returncode not in (0, 1) else "")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("mode", nargs="?", default="check", choices=("check", "record"))
    ap.add_argument("--out", help="scratch directory (default build/port-test/mods)")
    ap.add_argument("-j", "--jobs", type=int, default=int(os.environ.get("DW3_JOBS", "0")) or None,
                    help="mods run at once, and the build's jobs (default: $DW3_JOBS, else every mod at once)")
    ap.add_argument("--only", action="append", choices=sorted(MODS_TESTS), metavar="MOD",
                    help="only these mods' tests, in this process one after another (repeatable)")
    ap.add_argument("--binary", help=argparse.SUPPRESS)   # the parallel run's children: the port built by the parent
    args = ap.parse_args()
    try:
        if not DISC.exists():
            raise Missing(f"no disc image ({DISC.relative_to(ROOT)})")
        env = tool_env()
    except Missing as e:
        print(f"mods: {e}", file=sys.stderr)
        return 2
    out = (Path(args.out) if args.out else ROOT / "build/port-test/mods").resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    binary = Path(args.binary) if args.binary else build("build/port", [], args.jobs, env)
    if args.only:
        for name in args.only:
            MODS_TESTS[name](binary, out, args.mode == "record")
    else:
        names = list(MODS_TESTS)
        with ThreadPoolExecutor(max_workers=min(args.jobs or len(names), len(names))) as pool:
            for name, rc, text in pool.map(lambda n: run_child(n, args.mode, out, binary), names):
                print(text, end="", flush=True)
                if rc != 0:
                    FAILURES.append(f"{name}: exit {rc}")
                    if rc != 1:
                        print(f"  FAIL {name}: exit {rc}")
    if not args.binary:   # a child's summary is its parent's
        print(f"mods test: {'FAIL (' + str(len(FAILURES)) + (' check(s))' if args.only else ' mod(s))') if FAILURES else 'pass'}")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
