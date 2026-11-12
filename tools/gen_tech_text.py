#!/usr/bin/env python3
"""Write the technique descriptions (SKLINF) of tools/text_override/en.txt from what each technique does in the code.

  tools/venv/bin/python tools/gen_tech_text.py [--report FILE]    # rewrite SKLINF lines of en.txt; before/after table

Definitive combat PRD, layer 1: effect first, no flourish, at most MAX characters (the longest text the disc has: 34).
Reads src/main/records.c (RecordsTechnique) and the disc's own SKLINF/SKLNAM. A technique whose `kind` this script does
not know (enemy-only effects) keeps the disc's text, as do the unnamed placeholders ("a"). Only what is true of the code
today: a later layer that changes a technique rewrites its line (by hand in en.txt, or by teaching this script).
The other lines of en.txt (other text files) are kept.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import disc_files as df  # noqa: E402
import dump_text as dt  # noqa: E402

OUT = ROOT / "tools/text_override/en.txt"
MAX = 34
FIELDS = ("mp power element target accuracy defense_stat element_power strong_type kind effect_chance effect_power "
          "anim_stage anim_effect hit_sound anim_script hit_count").split()
ELEMENTS = {2: "Fire", 3: "Water", 4: "Ice", 5: "Wind", 6: "Thunder", 7: "Machine", 8: "Dark"}  # defense_stat
CHANCE = {2: "Poison chance.", 3: "Paralysis chance.", 4: "Confusion chance.", 5: "Sleep chance.", 6: "KO chance.",
          8: "Drains HP.", 10: "Counters.", 11: "Crit chance.", 12: "Steals an item."}
CURE = {17: "poison", 18: "paralysis", 19: "confusion", 20: "all ailments"}
STAT = {22: "attack", 23: "defense", 24: "speed"}
LOWER = {26: "attack", 27: "defense", 28: "speed"}


def records():
    src = (ROOT / "src/main/records.c").read_text()
    body = src[src.index("RecordsTechnique records_techniques[] = {"):]
    body = body[:body.index("\n};")]
    rows = []
    for line in body.splitlines()[1:]:
        nums = [int(x, 0) for x in re.findall(r"-?0x[0-9A-Fa-f]+|-?\d+", line)]
        if nums:
            rows.append(dict(zip(FIELDS, nums)))
    return rows


def text_file(name):
    d = (df.DISC / dt.find_file(name, "ENG").path).read_bytes()
    (_, offs), = dt.tables_of(d)
    tables = dt.char_tables()
    return [dt.decode(d, o, tables, latin=False)[0].replace("−", "-").replace("’", "'") for o in offs]


# The four techniques the owner remade (definitive combat PRD, layer 6), by ID: their new descriptions.
REMADE = {202: "Reveals the enemy's weaknesses.", 203: "Relay: the next swap is free.",
          207: "Lowers a resist of the enemy.", 208: "Lowers two resists of the enemy.",
          # PRD 2, layer B: attack buffs count for magic, defence ones against both kinds of hit
          198: "User's attack up: phys and magic.", 199: "Allies' attack up: phys and magic.",
          200: "User's defense up vs all damage.", 201: "Allies' defense up vs all damage.",
          205: "Enemy defense down vs all damage.", 206: "Enemy defense down vs all damage."}


def describe(t):
    """The new description of record `t`, or None to keep the disc's."""
    k, cls, tg = t["kind"], t["element"], t["target"]
    elem = ELEMENTS.get(t["defense_stat"])
    who = "all allies" if tg == 4 else "the user"
    if cls in (2, 3) and (k == 1 or k in CHANCE or k == 9):
        if cls == 2:
            base = f"{elem} attack." if elem else "Physical attack."
        else:
            base = f"{elem} magic." if elem else "Magic attack."
        if k == 9:
            return f"{base} {t['hit_count']} hits."
        return f"{base} {CHANCE[k]}" if k in CHANCE else base
    if cls == 4 and k == 15:
        return f"Heals {who}."
    if cls == 4 and k == 16:
        return "Heals the user over time."
    if cls == 4 and k in CURE:
        return f"Cures {CURE[k]} on {who}."
    if cls == 4 and k == 21:
        return "Revives all allies at max HP."
    if cls == 5 and k in STAT:
        return f"Raises {STAT[k]} of {who}."
    if cls == 5 and k in LOWER:
        return f"Lowers enemy {LOWER[k]}."
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", help="write the before/after table (Markdown) here")
    a = ap.parse_args()
    recs, old, names = records(), text_file("SKLINF"), text_file("SKLNAM")
    keep = [ln for ln in OUT.read_text().splitlines() if not ln.startswith("SKLINF ")] if OUT.exists() else []
    lines, report = [], ["| ID | Name | Before | After |", "|---|---|---|---|"]
    for tid in range(1, min(len(old), len(recs) + 1)):
        o = old[tid]
        if len(o) <= 1:  # "a" placeholders (1..52, a few unused) and empty entries stay as they are
            continue
        new = REMADE.get(tid) or describe(recs[tid - 1])
        if new is None:
            continue
        if len(new) > MAX:
            sys.exit(f"technique {tid}: {new!r} is {len(new)} characters, the limit is {MAX}")
        lines.append(f"SKLINF {tid}: {new}")
        report.append(f"| {tid} | {names[tid] if tid < len(names) else '?'} | {o} | {new} |")
    OUT.write_text("\n".join(keep + lines) + "\n")
    print(f"{OUT.relative_to(ROOT)}: {len(lines)} SKLINF lines (longest {max(len(l.split(': ', 1)[1]) for l in lines)} characters)")
    if a.report:
        Path(a.report).write_text("\n".join(report) + "\n")


if __name__ == "__main__":
    main()
