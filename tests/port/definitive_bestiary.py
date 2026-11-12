#!/usr/bin/env python3
"""Definitive combat layer 9 (bestiary): the table's card record (port/game/definitive_bestiary.c) compiled alone against a
stand-in for the save file's last 0x800 bytes: magic, version, checksum, an empty table as zeros, a missing / wrongly
versioned / badly summed record as an empty table, three independent slots. Also that the constants the game's C repeats
(src/main/memcard.c, src/stgmcard) carry the same numbers as definitive.h. Needs only the host gcc, no disc."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_bestiary"


def define(text, name):
    m = re.search(r"#define\s+" + name + r"\s+(0x[0-9A-Fa-f]+|\d+)", text)
    return int(m.group(1), 0) if m else None


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_bestiary_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"), "-I", str(ROOT / "psxstack/include"),
          "-I", str(ROOT / "port/game"), str(ROOT / "port/game/definitive_bestiary.c"),
          str(ROOT / "tests/port/definitive_bestiary_driver.c"), "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_bestiary: FAIL: does not compile")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    bad = run.returncode != 0
    h = (ROOT / "port/game/definitive.h").read_text()
    mc = (ROOT / "src/main/memcard.c").read_text()
    sc = (ROOT / "src/stgmcard/stgmcard_80082CD0.c").read_text()
    same = [
        ("memcard.c part", define(mc, "MEMCARD_PART_BESTIARY"), define(h, "DEF_BESTIARY_PART")),
        ("memcard.c offset", define(mc, "MEMCARD_PART_BESTIARY_AT"), define(h, "DEF_BESTIARY_RECORD_AT")),
        ("stgmcard part", define(sc, "DEF_BESTIARY_PART"), define(h, "DEF_BESTIARY_PART")),
        ("stgmcard size", define(sc, "DEF_BESTIARY_RECORD_SIZE"), define(h, "DEF_BESTIARY_RECORD_SIZE")),
    ]
    for name, a, b in same:
        ok = a is not None and a == b
        bad |= not ok
        print(f"{'ok' if ok else 'FAIL'} {name} = definitive.h ({a} vs {b})")
    print("definitive_bestiary: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
