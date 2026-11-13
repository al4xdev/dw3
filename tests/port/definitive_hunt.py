#!/usr/bin/env python3
"""PRD 4 layer A (the regional hunt): port/game/definitive_hunt.c and definitive_bestiary.c compiled alone against a
driver with stand-ins for the game's state: the chance curve, the chain (won encounters only, not the Rush's, not
scripted battles), its resets (region, teleport, a Rare Target fought), the swap and the enemies' raise, the card
record (written, read back, an older record as zero) and a replay script (nothing changes, nothing is drawn). Also
that the generated table is consistent: every target an existing battle, no region twice. Needs only the host gcc."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_hunt"


def table_ok():
    gen = (ROOT / "port/game/definitive_hunt_targets.c").read_text()
    rows = [tuple(int(x, 0) for x in m) for m in re.findall(r"\{\s*(0x[0-9A-Fa-f]+),\s*(\d+),\s*(\d+)\s*\}", gen)]
    count = int(re.search(r"def_hunt_target_count = (\d+);", gen).group(1))
    n = int(re.search(r"FieldstgBattle fieldstg_battles\[(\d+)\]",
                      (ROOT / "src/fieldstg/fieldstg_80083784.c").read_text()).group(1))
    regions = [r[0] for r in rows]
    ok = (len(rows) == count and len(set(regions)) == len(regions) and all(0 < b < n for _, b, _ in rows)
          and all(0 < lv <= 99 for _, _, lv in rows))
    print(f"{'ok' if ok else 'FAIL'} the generated table: {count} regions, each once, existing battles, levels 1..99")
    return ok


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_hunt_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"),
          "-I", str(ROOT / "tests/host"),
          "-I", str(ROOT / "psxstack/include"), "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_hunt.c"), str(ROOT / "port/game/definitive_bestiary.c"),
          str(ROOT / "tests/port/definitive_hunt_driver.c"), "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_hunt: FAIL: does not compile")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    bad = run.returncode != 0
    bad |= not table_ok()
    print("definitive_hunt: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
