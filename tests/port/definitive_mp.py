#!/usr/bin/env python3
"""Definitive combat layer 8 (MP): port/game/definitive_mp.c's boot patch, compiled alone against a stand-in
records_digimon filled with the rows of src/main/records.c, gives the PRD's table; and tools/convert_save_mp.py carries
the same numbers (old values = records.c's, new values = the patch's). Needs only the host gcc, no disc.
New game and level-up read these same two fields (gamestate_init_records: start_mp; stfgtrep level-up: hp_mp_gains[1])."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_mp"
NAMES = ["Kotemon", "Kumamon", "Monmon", "Agumon", "Veemon", "Guilmon", "Renamon", "Patamon"]
# PRD layer 8: rookie -> (start_mp, gain); the others keep the disc's
WANT = {0: (140, 57), 1: (140, 57), 2: (140, 57), 4: (150, 57), 5: (140, 60)}


def disc_rows():
    """start_mp and hp_mp_gains[1] of the first eight rows of records_digimon (… }, level, start_hp, start_mp, { g0, g1 }, …)."""
    src = (ROOT / "src/main/records.c").read_text()
    body = src[src.index("RecordsDigimon records_digimon[] = {"):]
    rows = re.findall(r"\}, \d+, \d+, (\d+), \{ \d+, (\d+) \}", body)[:8]
    return [(int(a), int(b)) for a, b in rows]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_mp_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"), "-I", str(ROOT / "tests/host"), "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_mp.c"), str(ROOT / "tests/port/definitive_mp_driver.c"), "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_mp: FAIL: does not compile")
        return 1
    old = disc_rows()
    run = subprocess.run([str(exe)], input="".join(f"{a} {b}\n" for a, b in old), capture_output=True, text=True)
    got = [tuple(map(int, l.split()))[1:] for l in run.stdout.splitlines()]
    bad = 0
    if run.returncode or len(got) != 8:
        print("definitive_mp: FAIL: driver", run.returncode)
        return 1
    for i in range(8):
        want = WANT.get(i, old[i])
        ok = got[i] == want
        bad += not ok
        print(f"{'ok' if ok else 'FAIL'} {NAMES[i]}: {old[i]} -> {got[i]} (want {want})")
    # the converter's table is the patch's, and its "old" side is the disc's
    sys.path.insert(0, str(ROOT / "tools"))
    import convert_save_mp
    for i, (oi, og, ni, ng) in convert_save_mp.MP_TABLE.items():
        ok = (oi, og) == old[i] and (ni, ng) == got[i]
        bad += not ok
        print(f"{'ok' if ok else 'FAIL'} convert_save_mp {NAMES[i]}: {(oi, og, ni, ng)} vs disc {old[i]} patch {got[i]}")
    missing = {i for i in range(8) if got[i] != old[i]} - set(convert_save_mp.MP_TABLE)
    if missing:
        bad += 1
        print("FAIL convert_save_mp lacks", sorted(missing))
    print("definitive_mp: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
