#!/usr/bin/env python3
"""The definitive combat's pure rules (PRD layers 3 and 5), compiled alone and checked case by case: port/game/definitive_mask.c
with tests/port/definitive_driver.c. Needs only the host gcc, no disc."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_mask.c"), str(ROOT / "tests/port/definitive_driver.c"), "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive: FAIL: the pure rules do not compile")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    bad = [l for l in run.stdout.splitlines() if l.startswith("FAIL")]
    print(run.stdout, end="")
    print("definitive: " + ("FAIL" if bad or run.returncode else "pass"))
    return 1 if bad or run.returncode else 0


if __name__ == "__main__":
    sys.exit(main())
