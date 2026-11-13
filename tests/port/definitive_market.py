#!/usr/bin/env python3
"""PRD 4 layer G, the Black Market's rules: port/game/definitive_market.c and definitive_bestiary.c compiled alone
against a driver with stand-ins for the Rare Targets, their lead species, the replicas, gamestate_data and def_on: the
Exchange's rate and cap (no value made or lost outside it), the replicas' distinct bosses, a purchase and its refusals,
the resale 0 and the bit's end, the bits in the card record (written, read back, an older record as none) and a replay
script (nothing changes). Also tools/gen_replicas.py --check (the replica table is the rule's: records_items, the
shops' goods and the game's item flags, no disc). The screen itself (definitive_market_screen.c) is the in-game test's.
Needs only the host gcc."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_market"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_market_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"), "-I", str(ROOT / "tests/host"),
          "-I", str(ROOT / "psxstack/include"), "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_market.c"), str(ROOT / "port/game/definitive_bestiary.c"),
          str(ROOT / "tests/port/definitive_market_driver.c"), "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_market: FAIL: does not compile")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    bad = run.returncode != 0
    table = subprocess.run([sys.executable, str(ROOT / "tools/gen_replicas.py"), "--check"], capture_output=True, text=True)
    print(table.stdout, end="")
    bad |= table.returncode != 0
    print("definitive_market: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
