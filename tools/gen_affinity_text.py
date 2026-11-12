#!/usr/bin/env python3
"""Rewrite the status and training texts that call the elemental resists "Tolerance" or "Defense" (definitive combat PRD,
layers 5 and 7: they are the player's affinity now, the element's damage bonus, not a defence).

  tools/venv/bin/python tools/gen_affinity_text.py      # rewrites the STATUS and TRAINI lines of tools/text_override/en.txt

The disc's own entries are the source (STATUS 92-98, TRAINI 29-42 the training names, 85-91 the stat labels, 99-112 the
results); only the words change, the control codes and the rest stay. Run tools/gen_text_override.py after it.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import disc_files as df  # noqa: E402
import dump_text as dt  # noqa: E402

OUT = ROOT / "tools/text_override/en.txt"
ELEM = "Fire|Water|Ice|Wind|Thunder|Machine|Dark"
# The training names (TRAINI 29-42): what they trained as a defence, now an affinity. 17 characters at most (the longest
# the disc has is 16).
TRAIN_NAMES = ["Fire Affinity", "Water Affinity", "Ice Affinity", "Wind Affinity", "Thunder Aff.", "Machine Aff.",
               "Dark Affinity", "S Fire Affinity", "S Water Affinity", "S Ice Affinity", "S Wind Affinity",
               "S Thunder Aff.", "S Machine Aff.", "S Dark Affinity"]


def entries(name):
    tables = dt.char_tables()
    d = (df.DISC / dt.find_file(name, "ENG").path).read_bytes()
    (_, offs), = dt.tables_of(d)
    return [dt.decode(d, o, tables, latin=False)[0].replace("\n    ", "").replace("−", "-").replace("’", "'")
            for o in offs]


def main():
    lines = []
    status = entries("STATUS")
    for i in range(92, 99):
        lines.append(f"STATUS {i}: " + status[i].replace("Tolerance", "Affinity"))
    train = entries("TRAINI")
    for k, i in enumerate(range(29, 43)):
        lines.append(f"TRAINI {i}: {TRAIN_NAMES[k]}")
    for i in list(range(85, 92)) + list(range(99, 113)):
        text = re.sub(rf"\b({ELEM}) Defense\b", r"\1 Affinity", train[i])
        if text != train[i]:
            lines.append(f"TRAINI {i}: {text}")
    keep = [l for l in OUT.read_text().splitlines() if not l.startswith(("STATUS ", "TRAINI "))] if OUT.exists() else []
    OUT.write_text("\n".join(keep + lines) + "\n")
    print(f"{OUT.relative_to(ROOT)}: {len(lines)} STATUS/TRAINI lines")


if __name__ == "__main__":
    main()
