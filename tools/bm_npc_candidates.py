#!/usr/bin/env python3
"""PRD 4 layer G: the town figures that could be the Black Market's trader (port/game/definitive_market_screen.c,
`traders`: a map and a FieldstgPlacedActor.id).

  tools/venv/bin/python tools/bm_npc_candidates.py              # the towns of the Guardromon inns, with the lines
  tools/venv/bin/python tools/bm_npc_candidates.py 0x2AC 0x20A  # the towns of these maps only
  tools/venv/bin/python tools/bm_npc_candidates.py --all        # every actor, not only the candidates

A town is every stage that talks from the same text file as an inn's map (port/game/definitive_travel.c's nodes): the
stage files (src/wstag/wstag*.c, "WSTAGnnn: stage 0x...") set fieldstg_stage.talk_file = records_language + 0xNN, and
a placed actor's talk (FieldstgTalk.message) is that file's entry, shown by fieldstg_dialog_create_talk. A candidate is
a figure whose talk the Black Market can take over and lose nothing:
  - placed on every visit (no flags_required), one actor of that ID on the stage (its key1 names it alone);
  - one line, said whatever the flags, setting no flag (no item, no event behind it);
  - no event script of the stage moves it, makes it speak or plays its animation;
  - not one of the IDs the field treats apart (the player's, the save terminals' 0x148/0x15F/0x160, the signs and
    special figures of fieldstg_actor_run_state's step 0x4A, 0x180..0x182).
The redirects of fieldstg_talk_redirects (an actor whose talk is another's) are not in the C source: check the pick
on the lab. With the user's disc (extracted/disc, iso/dw2003.bin) each line is decoded through tools/dump_text.py;
without it the file and entry are printed, for `tools/dump_text.py 0xNN -l ENG`.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPECIAL = {1, 0x6A, 0x146, 0x147, 0x21, 0x148, 0x15F, 0x160, 0x15B, 0x180, 0x181, 0x182} | set(range(0x4D, 0x58)) | set(
    range(0x154, 0x159))
# The event script commands whose first argument (or the third, for DIALOG) is an actor (include/fieldstg.h).
ACTOR_ARGS = {"PLACE": 0, "ANIM": 0, "WALK": 0, "WAIT_WALK": 0, "WAIT_ANIM": 0, "DIALOG": 2, "CAMERA_FOLLOW": 1}


def num(x):
    return int(x, 0)


def inn_maps():
    s = (ROOT / "port/game/definitive_travel.c").read_text()
    body = s[s.index("static const NetNode nodes[NT_NODES] = {"):]
    body = body[:body.index("};")]
    return [num(m) for m in re.findall(r"\{ (0x[0-9A-Fa-f]+), \d+, \d+, \d+ \}", body)]


def parse_stage(path):
    s = path.read_text(errors="replace")
    head = re.search(r"/\* (WSTAG\d+): stage (0x[0-9A-Fa-f]+) \((fieldstg_stages(?:_2d)?)\)", s)
    talk = re.search(r"fieldstg_stage\.talk_file = records_language \+ (0x[0-9A-Fa-f]+);", s)
    if not head or not talk:
        return None
    flags = {m.group(1): [num(x) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(2))]
             for m in re.finditer(r"u16 (\w+)\[\d*\] = \{([^}]*)\};", s)}
    talks = {}
    for m in re.finditer(r"FieldstgTalk (\w+)\[\d*\] = \{(.*?)\};\n", s, re.S):
        rows = [(r, st, int(msg)) for r, st, msg in re.findall(r"\{ (\w+), (\w+), (-?\d+) \}", m.group(2))]
        # fieldstg_actor_run_state takes the first entry whose flags are set; one without flags always is, so what
        # follows it (the { NULL, NULL, 0 } after the last) is never said.
        k = next((i for i, r in enumerate(rows) if r[0] == "NULL"), len(rows) - 1)
        talks[m.group(1)] = rows[:k + 1]
    actors = []
    for m in re.finditer(r"FieldstgPlacedActor (\w+) = \{ (\w+), (\w+), (-?\w+), (-?\w+), (-?\w+), (-?\w+), (-?\w+) \};", s):
        actors.append(dict(sym=m.group(1), req=m.group(2), talks=m.group(3), id=num(m.group(4)), x=num(m.group(6)),
                           y=num(m.group(7)), dir=num(m.group(8))))
    moved = set()
    for m in re.finditer(r"FIELDSTG_EVENT_(\w+)\(([^)]*)\)", s):
        k = ACTOR_ARGS.get(m.group(1))
        args = [a.strip() for a in m.group(2).split(",")]
        if k is not None and k < len(args):
            try:
                moved.add(num(args[k]))
            except ValueError:
                pass
    return dict(file=path.name, wstag=head.group(1), stage=num(head.group(2)), table=head.group(3),
                talk_file=num(talk.group(1)), flags=flags, talks=talks, actors=actors, moved=moved)


def judge(st, a):
    """(candidate, why not)"""
    why = []
    if a["id"] in SPECIAL:
        why.append("special ID")
    if a["req"] != "NULL":
        why.append(f"present only with flags {fmt_flags(st, a['req'])}")
    if sum(b["id"] == a["id"] for b in st["actors"]) > 1:
        why.append("ID placed twice")
    real = st["talks"].get(a["talks"], [])
    if len(real) != 1:
        why.append(f"{len(real)} lines by flags")
    if any(x[1] != "NULL" for x in real):
        why.append("its talk sets flags " + ", ".join(fmt_flags(st, x[1]) for x in real if x[1] != "NULL"))
    if a["id"] in st["moved"]:
        why.append("an event script uses it")
    return not why, why


def fmt_flags(st, sym):
    vals = st["flags"].get(sym)
    if vals is None:
        return sym
    pairs = []
    for i in range(0, len(vals) - 1, 2):
        if vals[i] == 0xFFFF:
            break
        pairs.append(f"0x{vals[i]:04X}={vals[i + 1]}")
    return "[" + " ".join(pairs) + "]"


def text_reader(lang):
    """entry(talk_file, index) -> text, or None without the disc."""
    try:
        import disc_files as df
        import dump_text as dt
        tables = dt.char_tables()
    except Exception as e:  # no disc: the lead decodes by hand
        print(f"# no disc ({e.__class__.__name__}): lines shown as file/entry for tools/dump_text.py", file=sys.stderr)
        return None
    cache = {}

    def entry(talk_file, index):
        if talk_file not in cache:
            e = dt.find_file(f"0x{talk_file:X}", lang)
            d = (df.DISC / e.path).read_bytes()
            found = dt.tables_of(d)
            cache[talk_file] = (d, found[0][1] if found else [])
        d, offs = cache[talk_file]
        if not 0 <= index < len(offs):
            return f"<no entry {index}>"
        return dt.decode(d, offs[index], tables, latin=lang != "JPN")[0].replace("\n    ", " / ")
    return entry


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("maps", nargs="*", help="maps whose towns to list (default: every Guardromon inn's)")
    ap.add_argument("--all", action="store_true", help="every placed actor, not only the candidates")
    ap.add_argument("-l", "--lang", default="ENG")
    a = ap.parse_args()
    stages = [st for st in (parse_stage(p) for p in sorted((ROOT / "src/wstag").glob("wstag*.c"))) if st]
    by_stage = {st["stage"]: st for st in stages}
    maps = [num(m) for m in a.maps] or inn_maps()
    entry = text_reader(a.lang)
    seen_files = []
    for m in maps:
        st = by_stage.get(m)
        if st is None:
            print(f"# map 0x{m:X}: no stage file")
            continue
        if st["talk_file"] not in seen_files:
            seen_files.append(st["talk_file"])
    for tf in seen_files:
        town = sorted((st for st in stages if st["talk_file"] == tf), key=lambda st: st["stage"])
        inns = [f"0x{m:X}" for m in maps if by_stage.get(m) and by_stage[m]["talk_file"] == tf]
        print(f"\n## town of talk file records_language + 0x{tf:X} (inn map {', '.join(inns)}): "
              f"{len(town)} stages")
        for st in town:
            rows = []
            for ac in st["actors"]:
                ok, why = judge(st, ac)
                if not ok and not a.all:
                    continue
                lines = []
                for req, _set, msg in st["talks"].get(ac["talks"], []):
                    said = entry(tf, msg) if entry else f"file 0x{tf:X} ({a.lang}) entry {msg}"
                    lines.append(f"{msg}: {said}" + ("" if req == "NULL" else f"  (with flags {fmt_flags(st, req)})"))
                rows.append((ok, ac, why, lines))
            if not rows:
                continue
            print(f"### map 0x{st['stage']:X} ({st['wstag']}, {st['file']}, {st['table']})")
            for ok, ac, why, lines in rows:
                mark = "CANDIDATE" if ok else "no: " + "; ".join(why)
                print(f"  {{ 0x{st['stage']:X}, {ac['id']} }}  actor {ac['id']} (0x{ac['id']:X}) at ({ac['x']}, {ac['y']})"
                      f" dir {ac['dir']}  {mark}")
                for ln in lines:
                    print(f"      {ln}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
