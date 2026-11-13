#!/usr/bin/env python3
"""PRD 4's economy (layer D on): port/game/definitive_economy.c compiled alone against a driver with stand-in items, and
the generated price table checked against its rule (tools/gen_item_prices.py --check: records_items and the shops'
goods, no disc). Needs only the host gcc."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_economy"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_economy_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"), "-I", str(ROOT / "tests/host"),
          "-I", str(ROOT / "psxstack/include"), "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_economy.c"), str(ROOT / "tests/port/definitive_economy_driver.c"), "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_economy: FAIL: does not compile")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    bad = run.returncode != 0
    table = subprocess.run([sys.executable, str(ROOT / "tools/gen_item_prices.py"), "--check"], capture_output=True, text=True)
    print(table.stdout, end="")
    bad |= table.returncode != 0
    print("definitive_economy: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
