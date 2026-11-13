#!/usr/bin/env python3
"""The Guide (port/game/definitive_quest.c): the rules compiled against a driver with stand-ins for the game's state
(the main quest row of every story step and its key-item variants, the missions, each auction's state, price and
travel rule, the notices once per change, nothing in a replay script). Then the tables checked against the game's own
data: the sixteen auctions against FIELDSTG's fieldstg_flag_events (its step, its "attended" flag 0x400 + N, its price
condition 0x7074 + the price's index), every main quest step 0..45 with a plain row after its variants, and every text
the tables name present in tools/text_override/en.txt within its window's lines. Needs only the host gcc."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_quest"
Q_LINES, WHEN_LINES = 6, 2  # definitive_quest_screen.c: GD_Q_LINES for a quest's text, two for an auction's line


def tables_ok():
    src = (ROOT / "port/game/definitive_quest.c").read_text()
    fs = (ROOT / "src/fieldstg/fieldstg_80083D08.c").read_text()
    en = {k: v for k, v in re.findall(r"^\$(\w+): (.*)$", (ROOT / "tools/text_override/en.txt").read_text(), re.M)}
    ok = True
    events = [tuple(int(x) for x in m) for m in re.findall(r"\{ (-?\d+), (\d+), (\d+), (\d+), (\d+) \}",
                                                             fs[fs.index("fieldstg_flag_events["):])][:16]
    auctions = [(int(p), int(i), int(m), w) for p, i, m, w in
                re.findall(r"\{ (\d+), (\d+), (\d+), def_str_(AucWhen\d+) \}", src)]
    a_ok = len(auctions) == 16 == len(events)
    for n, ((progress, _item, money, _when), ev) in enumerate(zip(auctions, events)):
        a_ok &= ev[0] == progress and ev[1] == 0x400 + n and ev[2] == 0x7074 + money
    print(f"{'ok' if a_ok else 'FAIL'} the auctions are FIELDSTG's: step, attended flag and price, in order")
    ok &= a_ok
    flag = r"(ANY|ITEM\(\d+\))"
    rows = re.findall(r"\{ (\d+), (\d+), " + flag + r", (\d), (0x[0-9A-F]+|0), def_str_(\w+), def_str_(\w+) \}", src)
    body = src[src.index("const DefQuest def_quests[]"):src.index("const int def_quest_count")]
    main = [r for r in rows if f"def_str_{r[5]}," in body]
    q_ok = bool(main)
    for p in range(46):
        covering = [r for r in main if int(r[0]) <= p <= int(r[1])]
        plain = [i for i, r in enumerate(covering) if r[2] == "ANY"]
        q_ok &= bool(plain) and plain[0] == len(covering) - 1  # the variants first, then one plain row last
    print(f"{'ok' if q_ok else 'FAIL'} every story step 0..45 has a plain main quest row after its variants")
    ok &= q_ok
    names = set(re.findall(r"def_str_(\w+)", src))
    missing = sorted(n for n in names if n not in en)
    print(f"{'ok' if not missing else 'FAIL'} every text the tables name is in en.txt" + (f": {missing}" if missing else ""))
    ok &= not missing
    long_ = [n for n in names if n in en and n.endswith("T") and en[n].count("<1>") + 1 > Q_LINES]
    long_ += [n for n in names if n.startswith("AucWhen") and n in en and en[n].count("<1>") + 1 > WHEN_LINES]
    print(f"{'ok' if not long_ else 'FAIL'} the texts fit their windows' lines" + (f": {sorted(long_)}" if long_ else ""))
    ok &= not long_
    return ok


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_quest_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"), "-I", str(ROOT / "tests/host"),
          "-I", str(ROOT / "psxstack/include"), "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_quest.c"), str(ROOT / "port/game/definitive_text.c"),
          str(ROOT / "tests/port/definitive_quest_driver.c"), "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_quest: FAIL: does not compile")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    bad = run.returncode != 0
    bad |= not tables_ok()
    print("definitive_quest: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
