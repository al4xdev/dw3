#!/usr/bin/env python3
"""PRD 4 layer I (the Overlevel Rush's results on the field): port/game/definitive_rush.c, the panel's rows, texts and
clock, compiled alone with definitive_glyph.c against a driver with stand-in words (byte-for-byte checks of the rows'
texts, the material line made one panel line, the clock's replace-on-a-second-Rush and its closing). The rewards
themselves (def_rush_apply_quiet) are STFGTREP's routines and are checked in the game against the results screen (the
layer's equivalence test). Needs only the host gcc."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_rush"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_rush_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"), "-I", str(ROOT / "tests/host"),
          "-I", str(ROOT / "psxstack/include"), "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_rush.c"), str(ROOT / "port/game/definitive_glyph.c"),
          str(ROOT / "tests/port/definitive_rush_driver.c"), "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_rush: FAIL: does not compile")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    bad = run.returncode != 0
    print("definitive_rush: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
