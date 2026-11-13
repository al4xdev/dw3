#!/usr/bin/env python3
"""The Rare Targets of the hunt (PRD 4 layer A): one existing boss battle per region with random encounters, its level
raised to the region's own encounters. The owner approves the table before it is applied.

  tools/venv/bin/python tools/gen_hunt_targets.py            # proposal: .dumps/tasks/hunt/alvos.md; the game is untouched
  tools/venv/bin/python tools/gen_hunt_targets.py --apply    # also write port/game/definitive_hunt_targets.c

A region is a world map area plus the server bit (ststatus_map_areas, the byte as stored). Reads the stages' battle lists
from src/wstag/*.c, each stage file's map from its own header comment, the battles and
their enemies from src/fieldstg/fieldstg_80083784.c, and the species' names from the user's disc through
gen_enemy_weaknesses.py. A boss battle is gen_enemy_weaknesses.py's: kind 2 with at least nine blocked[] flags.

Proposal rules:
  - a region takes part when one of its stages has a random encounter list with a nonzero rate;
  - its target is a boss battle whose own level is between the region's strongest random encounter (T) and
    T + LEVEL_SPAN: one its own stages' scripts start when there is one, else one not used by another region, closest
    to T + LEVEL_OVER. The bosses a region's own scripts start are often optional super-bosses far above its
    encounters (a level-70 boss in a level-6 area), so the region's own boss alone is not the rule;
  - the target fights at T + LEVEL_OVER, never below the boss's own level; no boss in range, no target (listed);
  - each target battle gives a Hunt Material of its own (layer B): the battles in the order of the lowest level they
    fight at (then their number), the weakest one's the first material. More battles than MATERIALS share them in that
    order. A band of levels instead left most materials without a source: the targets fight at a few levels.
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import gen_enemy_weaknesses as gw  # noqa: E402

LEVEL_OVER = 3       # levels above the region's strongest random encounter
LEVEL_SPAN = 15      # a boss qualifies for a region when its own level is within this many levels above the region's
MATERIALS = 16       # DEF_HUNT_MATERIALS (port/game/definitive.h)
OUT = ROOT / ".dumps/tasks/hunt"
GEN = ROOT / "port/game/definitive_hunt_targets.c"
NUM = r"(0x[0-9A-Fa-f]+|\d+)"


def ints(text):
    return [int(x, 0) for x in re.findall(r"-?0x[0-9A-Fa-f]+|-?\d+", text)]


def map_areas():
    s = (ROOT / "src/ststatus/ststatus_80099B6C.c").read_text()
    body = re.search(r"u8 ststatus_map_areas\[0xF0\] = \{(.*?)\};", s, re.S).group(1)
    return ints(body)


def stage_maps():
    """{wstag file number: [maps]}: each stage file names its own stage in its first comment ("WSTAG321: stage 0x28A").
    fieldstg_stages's second field is not the file's number, so it cannot be used for this."""
    out = defaultdict(list)
    for path in sorted((ROOT / "src/wstag").glob("wstag*.c")):
        m = re.search(r"/\* WSTAG(\d+): stage (0x[0-9A-Fa-f]+|\d+)", path.read_text())
        if m:
            out[int(m.group(1))].append(int(m.group(2), 0))
    return out


def stage_lists(path):
    """[(encounter battles with a nonzero rate, scripted battles)] for each FieldstgBattleLists of a stage file."""
    s = path.read_text()
    listed = {n: int(b, 0) for n, b in re.findall(r"FieldstgListedBattle (\w+) = \{\s*" + NUM, s)}
    lists = {}
    for n, rate, refs in re.findall(r"FieldstgBattleList (\w+) = \{\s*" + NUM + r",\s*\{([^}]*)\}", s, re.S):
        lists[n] = (int(rate, 0), [listed[r] for r in re.findall(r"&(\w+)", refs) if r in listed])
    out = []
    for body in re.findall(r"FieldstgBattleLists \w+ = \{(.*?)\};", s, re.S):
        refs = re.findall(r"&(\w+)|(NULL)", body)
        flat = [a or None for a, _ in refs]
        enc, scripted = flat[:3], flat[3] if len(flat) > 3 else None
        battles_enc = set()
        for e in enc:
            if e and e in lists and lists[e][0] != 0:
                battles_enc.update(b for b in lists[e][1] if b)
        battles_scr = set(b for b in (lists[scripted][1] if scripted in lists else []) if b)
        out.append((battles_enc, battles_scr))
    return out


def battle_info():
    """[{enemies: [(species, level)], kind, boss}] per fieldstg_battles index (parsed as gen_enemy_weaknesses does)."""
    src = (ROOT / "src/fieldstg/fieldstg_80083784.c").read_text()
    body = src[src.index("RecordsEnemy fieldstg_battle_enemies"):]
    body = body[:body.index("\n};")]
    rows = [tuple(int(x) for x in m) for m in re.findall(r"\{\s*(-?\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", body)]
    bt = src[src.index("FieldstgBattle fieldstg_battles"):]
    out = []
    for es, _fs, kind, bl in re.findall(r"\{\s*\{([^}]*)\},\s*(0x[0-9A-Fa-f]+|\d+),\s*(\d+),\s*\{([^}]*)\}\s*\}", bt):
        idx = [int(x) for x in re.findall(r"\[(\d+)\]", es) if int(x)]
        out.append(dict(enemies=[(rows[i][0], rows[i][1]) for i in idx], kind=int(kind),
                        boss=int(kind) == 2 and sum(int(x) for x in bl.split(",")) >= 9))
    return out


def material_names():
    """The Hunt Materials' names, $HuntMat0.. in the text overrides."""
    s = (ROOT / "tools/text_override/en.txt").read_text()
    return {int(i): n.strip() for i, n in re.findall(r"^\$HuntMat(\d+): (.*)$", s, re.M)}


def assign_materials(rows):
    targets = [r for r in rows if r["battle"] is not None]
    low = {}
    for r in targets:
        low[r["battle"]] = min(low.get(r["battle"], r["level"]), r["level"])
    order = sorted(low, key=lambda b: (low[b], b))
    n = len(order)
    rank = {b: (i if n <= MATERIALS else i * MATERIALS // n) for i, b in enumerate(order)}
    names = material_names()
    for r in targets:
        r["material"] = rank[r["battle"]]
        r["material_name"] = names.get(r["material"], "?")


def propose():
    areas = map_areas()
    battles = battle_info()
    names = {sp["id"]: sp["name"] for sp in gw.species()}
    regions = defaultdict(lambda: dict(enc=set(), scr=set(), maps=set()))
    for f, maps in sorted(stage_maps().items()):
        lists = stage_lists(ROOT / f"src/wstag/wstag{f}.c")
        for m in maps:
            region = areas[m & 0xFF]
            for enc, scr in lists:
                regions[region]["enc"] |= enc
                regions[region]["scr"] |= scr
            regions[region]["maps"].add(m)
    # The final boss's battles (gen_enemy_weaknesses.FINAL_BOSS: Galacticmon's phases) are never a Rare Target: the
    # battle runs the final phases' rules (wfightmn: state.type 4) wherever it starts.
    own = {b: max(lv for _, lv in x["enemies"]) for b, x in enumerate(battles)
           if x["boss"] and x["enemies"] and not any(sp in gw.FINAL_BOSS for sp, _ in x["enemies"])}
    home = {}
    for region, r in regions.items():
        for b in r["scr"]:
            if b in own:
                home.setdefault(b, region)
    rows, used = [], set()
    for region in sorted(regions, key=lambda k: max((lv for b in regions[k]["enc"] for _, lv in battles[b]["enemies"]),
                                                    default=0)):
        r = regions[region]
        if not r["enc"]:
            continue
        top = max((lv for b in r["enc"] for _, lv in battles[b]["enemies"]), default=0)
        want = top + LEVEL_OVER
        fits = [b for b, lv in own.items() if top <= lv <= top + LEVEL_SPAN]
        # Same region first, then a boss not yet used elsewhere, then the closest level to the wanted one.
        fits.sort(key=lambda b: (home.get(b) != region, b in used, abs(own[b] - want), b))
        pick = fits[0] if fits else None
        row = dict(region=region, area=region & 0x7F, server=region >> 7, maps=sorted(r["maps"]), top=top, battle=pick)
        if pick is not None:
            used.add(pick)
            row.update(level=max(own[pick], want), own=own[pick], home=home.get(pick),
                       who=", ".join(f"{names.get(sp, sp)} Lv{lv}" for sp, lv in battles[pick]["enemies"]))
        rows.append(row)
    assign_materials(rows)
    rows.sort(key=lambda x: x["region"])
    return rows


def write_md(rows):
    OUT.mkdir(parents=True, exist_ok=True)
    lines = ["# Alvos Raros por região (proposta, PRD 4 camada A)", "",
             f"Regra: um chefe do jogo com nível próprio entre o maior encontro aleatório da região (T) e T + {LEVEL_SPAN}, "
             f"de preferência um que os próprios estágios da região iniciam, sem repetir chefe entre regiões; luta no nível "
             f"T + {LEVEL_OVER}, nunca abaixo do nível do chefe. Aprove, risque ou troque. Material de Caça (camada B): um "
             f"por batalha de alvo, na ordem do menor nível em que ela luta.", "",
             "| região | área | servidor | mapas | maior encontro | batalha | da região? | inimigos (nível original) | nível proposto | material |",
             "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for r in rows:
        maps = " ".join(hex(m) for m in r["maps"][:4]) + (" …" if len(r["maps"]) > 4 else "")
        if r["battle"] is None:
            lines.append(f"| 0x{r['region']:02X} | {r['area']} | {r['server']} | {maps} | {r['top']} | — | — | sem chefe no nível | — | — |")
        else:
            same = "sim" if r["home"] == r["region"] else (f"0x{r['home']:02X}" if r["home"] is not None else "nenhuma")
            lines.append(f"| 0x{r['region']:02X} | {r['area']} | {r['server']} | {maps} | {r['top']} | {r['battle']} | {same} | "
                         f"{r['who']} | {r['level']} | {r['material']} {r['material_name']} |")
    (OUT / "alvos.md").write_text("\n".join(lines) + "\n")


def write_c(rows):
    targets = [r for r in rows if r["battle"] is not None]
    lines = ["/* Generated by tools/gen_hunt_targets.py --apply (PRD 4 layer A): the Rare Target of each region, an existing",
             " * boss battle, the level it fights at and its Hunt Material (layer B). Regenerate instead of editing; the owner",
             " * approves the table. */",
             '#include "definitive.h"', "",
             "const DefHuntTarget def_hunt_targets[] = {"]
    for r in targets:
        lines.append(f"    {{ 0x{r['region']:02X}, {r['battle']}, {r['level']}, {r['material']} }}, /* {r['who']}: "
                     f"{r['material_name']} */")
    lines += ["};", f"const int def_hunt_target_count = {len(targets)};", ""]
    GEN.write_text("\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true", help="also write port/game/definitive_hunt_targets.c")
    args = ap.parse_args()
    rows = propose()
    write_md(rows)
    print(f"{len(rows)} regions with random encounters, {sum(r['battle'] is not None for r in rows)} with a target: "
          f"{OUT / 'alvos.md'}")
    if args.apply:
        write_c(rows)
        print(f"written {GEN}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
