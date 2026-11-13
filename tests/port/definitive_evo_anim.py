#!/usr/bin/env python3
"""Definitive progression layer G (short repeat and skippable digivolution animations): pure RAM seen-bitmask
and skip-hold logic compiled from port/game/definitive_progression.c with tests/port/definitive_evo_anim_driver.c."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_evo_anim"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_evo_anim_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_progression.c"),
          str(ROOT / "tests/port/definitive_evo_anim_driver.c"), "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_evo_anim: FAIL: does not compile")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    bad = [l for l in run.stdout.splitlines() if l.startswith("FAIL")]
    print(run.stdout, end="")
    print("definitive_evo_anim: " + ("FAIL" if bad or run.returncode else "pass"))
    return 1 if bad or run.returncode else 0


if __name__ == "__main__":
    sys.exit(main())
