#!/usr/bin/env python3
"""PRD 4 layer H: find the DRI Agents, the field actors whose talk opens the Gauntlet's list. Needs the user's disc.

  tools/venv/bin/python tools/find_dri_agents.py               # actors whose talk text says "DRI"
  tools/venv/bin/python tools/find_dri_agents.py -g Agent -g D.R.I.   # other words (case-insensitive), any of them

For each stage file (src/wstag/*.c): its map (the file's header comment, as tools/gen_hunt_targets.py reads it), its talk
file (fieldstg_stage.talk_file = records_language + ID), and each placed actor (FieldstgPlacedActor: ID, x, y) with the
messages of its talks (FieldstgTalk). The talk file's ENG entries are decoded with tools/dump_text.py; an actor matches
when one of its messages has one of the words. Prints each match, then the AGENTS rows for tools/gen_dri_challenges.py:
an actor ID that only DRI talks use is offered for any map (-1), else per map.
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import disc_files as df  # noqa: E402
import dump_text as dt  # noqa: E402
import gen_hunt_targets as gh  # noqa: E402


def stage_actors(path):
    """(talk file IDs, [(actor ID, x, y, [messages])]) of a stage file."""
    s = path.read_text()
    talk_files = sorted({int(x, 0) for x in re.findall(r"talk_file = records_language \+ (0x[0-9A-Fa-f]+|\d+);", s)})
    talks = {}
    for name, body in re.findall(r"FieldstgTalk (\w+)\[\d*\] = \{(.*?)\};", s, re.S):
        talks[name] = [int(m) for m in re.findall(r"\{\s*\w+,\s*\w+,\s*(\d+)\s*\}", body)]
    actors = []
    for _name, body in re.findall(r"FieldstgPlacedActor (\w+) = \{(.*?)\};", s, re.S):
        f = [x.strip() for x in body.split(",")]
        if len(f) != 7:
            continue
        actors.append((int(f[2], 0), int(f[4], 0), int(f[5], 0), talks.get(f[1], [])))
    return talk_files, actors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-g", "--grep", action="append", help="a word to look for (default: DRI, as a word)")
    a = ap.parse_args()
    words = a.grep or []
    pat = re.compile("|".join(re.escape(w) for w in words), re.I) if words else re.compile(r"\bDRI\b")
    tables = dt.char_tables()
    cache = {}

    def entries(fid):
        if fid not in cache:
            e = dt.find_file(hex(fid), "ENG")
            d = (df.DISC / e.path).read_bytes()
            found = dt.tables_of(d)
            cache[fid] = (d, found[0][1]) if found else (d, [])
        return cache[fid]

    hits = defaultdict(set)   # actor ID -> maps where it says a word
    uses = defaultdict(set)   # actor ID -> every map it is placed on
    for f, maps in sorted(gh.stage_maps().items()):
        talk_files, actors = stage_actors(ROOT / f"src/wstag/wstag{f}.c")
        for aid, x, y, msgs in actors:
            for m in maps:
                uses[aid].add(m)
            for fid in talk_files:
                d, offs = entries(fid)
                for msg in msgs:
                    if not 0 <= msg < len(offs):
                        continue
                    text = dt.decode(d, offs[msg], tables)[0].replace("\n    ", " ")
                    if pat.search(text):
                        for m in maps:
                            hits[aid].add(m)
                        print(f"wstag{f} map {', '.join(hex(m) for m in maps)} actor {aid} at ({x}, {y}) talk file "
                              f"0x{fid:X} message {msg}: {text[:140]}")
    print("\nAGENTS = [")
    for aid in sorted(hits):
        if hits[aid] == uses[aid]:
            print(f"    (-1, {aid}),  # every map it is on: {', '.join(hex(m) for m in sorted(uses[aid]))}")
        else:
            for m in sorted(hits[aid]):
                print(f"    (0x{m:X}, {aid}),")
    print("]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
