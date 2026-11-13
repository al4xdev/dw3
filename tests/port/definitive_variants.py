#!/usr/bin/env python3
"""PRD 4 layer F, the equipment variants: port/game/definitive_variants.c compiled with the generated table
(definitive_equipment.c) against stand-in items loaded with src/main/records.c's data, every variant and a few items
that are not one. Checked: in a replay script (def_on 0) every item keeps records.c's data and every variant's name and
description override is held back (the disc's text shows); with def_on the variants have the table's main stat, hit
and bonus stats, everything else of theirs (charisma, slot, group, wearers, status, strong type) is records.c's, the
other items are untouched and no text is held back; a second hook changes nothing (written again, never compounded); a
row whose item is not of its category writes nothing. Then tools/gen_equipment.py --check
(the table is the rule's: IDs, budget, no straight upgrade, texts). Needs only the host gcc, no disc."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_variants"
sys.path.insert(0, str(ROOT / "tools"))
import gen_equipment as ge  # noqa: E402

CAT = {"RECORDS_ITEM_WEAPON": 3, "RECORDS_ITEM_ARMOR": 4}
ITMNAM, ITMINF, SKLNAM = 0x6A, 0x63, 0xA2
OTHERS = (92, 97, 236, 262, 98, 151)  # plain items of the same tiers, a set piece and an item with an ID effect


def line(it):
    head = f"{it['id']} {CAT[it['cat']]} {it['type']}"
    if it["weapon"]:
        return (f"{head} W {it['charisma']} {it['slot']} {it['group']} {it['members']} {it['values'][0]} {it['values'][1]} "
                f"{it['main']} {it['stats'][0]} {it['stats'][1]} {it['hit']} {it['status'][0]} {it['status'][1]} {it['strong']}")
    return (f"{head} A {it['charisma']} {it['slot']} {it['group']} {it['members']} {it['values'][0]} {it['values'][1]} "
            f"{it['stats'][0]} {it['stats'][1]} {it['main']} {it['hit']}")


def table():
    """{id: (weapon, main, hit, (stat0, stat1), (value0, value1))} parsed from the generated C (what the game links)."""
    s = (ROOT / "port/game/definitive_equipment.c").read_text()
    enum = {k: v for k, v in ge.BONUS.items()}
    out = {}
    for m in re.finditer(r"\{ (\d+), DEF_EQUIP_\w+, (\d), (\d+), (\d+), \{ (\w+), (\w+) \}, \{ (-?\d+), (-?\d+) \} \}", s):
        out[int(m.group(1))] = (int(m.group(2)), int(m.group(3)), int(m.group(4)), (enum[m.group(5)], enum[m.group(6)]),
                                (int(m.group(7)), int(m.group(8))))
    return out


def passes(text):
    out, cur = {}, None
    for ln in text.splitlines():
        if ln.startswith("pass "):
            cur = ln.split()[1]
            out[cur] = []
        elif cur:
            out[cur].append(ln)
    return out


def patched(it, row):
    weapon, main, hit, stats, values = row
    return dict(it, main=main, hit=hit, stats=list(stats), values=list(values))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_variants_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"), "-I", str(ROOT / "tests/host"),
          "-I", str(ROOT / "port/game"), str(ROOT / "port/game/definitive_variants.c"),
          str(ROOT / "port/game/definitive_equipment.c"), str(ROOT / "tests/port/definitive_variants_driver.c"),
          "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_variants: FAIL: does not compile")
        return 1
    its = ge.items()
    rows = table()
    ids = sorted(rows) + list(OTHERS)
    stdin = "".join(line(its[i]) + "\n" for i in ids)
    bad = 0

    def check(name, ok):
        nonlocal bad
        bad += not ok
        print(f"{'ok' if ok else 'FAIL'} {name}")

    run = subprocess.run([str(exe)], input=stdin, capture_output=True, text=True)
    if run.returncode != 0:
        print("definitive_variants: FAIL: driver", run.returncode)
        return 1
    p = passes(run.stdout)
    orig = [line(its[i]) for i in ids]
    want = [line(patched(its[i], rows[i])) if i in rows else line(its[i]) for i in ids]
    check(f"a replay script: all {len(ids)} items keep records.c's data", p["replay"] == orig)
    check(f"def_on: the {len(rows)} variants have the table's stats, the rest of theirs and the other items are records.c's",
          p["first"] == want)
    check("a second hook writes the same numbers (patched once, never compounded)", p["second"] == p["first"])
    check("the table's rows: one per variant, every ID a shop weapon or armour of its row's kind",
          len(rows) == len(set(rows)) and all(its[i]["cat"] == ("RECORDS_ITEM_WEAPON" if r[0] else "RECORDS_ITEM_ARMOR")
                                              for i, r in rows.items()))
    check("the icon (type), slot, group, wearers and charisma never change",
          all(a.split()[:3] == b.split()[:3] and a.split()[4:8] == b.split()[4:8] for a, b in zip(p["first"], orig)))
    hid_off = {tuple(map(int, ln.split()[1:])) for ln in p["text-replay"]}
    hid_on = {tuple(map(int, ln.split()[1:])) for ln in p["text-on"]}
    check("a replay script: the variants' names and descriptions are hidden (the disc's show), nothing else is",
          all((f, i, int(i in rows and f in (ITMNAM, ITMINF))) in hid_off for i in ids for f in (ITMNAM, ITMINF, SKLNAM)))
    check("def_on: no text is hidden", all(h == 0 for _f, _i, h in hid_on))
    mm = subprocess.run([str(exe), "mismatch"], input=stdin, capture_output=True, text=True)
    mp = passes(mm.stdout).get("mismatch", [])
    flipped = [ln.split() for ln in orig]
    for f in flipped:
        f[1] = "4" if f[1] == "3" else "3"
    check("a row whose item is not of its category writes nothing", mm.returncode == 0 and
          [ln.split() for ln in mp] == flipped)
    # The text gate's file IDs are the ones gen_text_override.py resolved on the disc (definitive_text.c is made on the lab)
    h = (ROOT / "port/game/definitive.h").read_text()
    gate = {n: int(v, 16) for n, v in re.findall(r"#define DEF_TEXT_(ITMNAM|ITMINF) (0x[0-9A-Fa-f]+)", h)}
    made = {n: {int(f, 16) for f in re.findall(r"\{ (0x[0-9A-F]+), \d+, def_text_\d+ \}, /\* " + n + r" ",
                                               (ROOT / "port/game/definitive_text.c").read_text())} for n in gate}
    if all(made.values()):
        check(f"definitive_text.c's item text files are the gate's {gate}", all(made[n] == {gate[n]} for n in gate))
    else:
        print("skip definitive_text.c has no ITMNAM/ITMINF line yet (run tools/gen_text_override.py with the disc)")
    gen = subprocess.run([sys.executable, str(ROOT / "tools/gen_equipment.py"), "--check"], capture_output=True, text=True)
    print(gen.stdout, end="")
    bad += gen.returncode != 0
    print("definitive_variants: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
