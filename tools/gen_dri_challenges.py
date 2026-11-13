#!/usr/bin/env python3
"""PRD 4 layer H: the DRI Agents' Gauntlet. Each challenge is one battle whose three enemy members are the stages of an
evolution line, Champion to Mega; the battle brings in the next member when one falls (wfightmn_knockout), so the
"next form" comes in at its own full HP while the party keeps its state. The owner approves the list.

  tools/venv/bin/python tools/gen_dri_challenges.py            # write port/game/definitive_dri_table.c and the list
  tools/venv/bin/python tools/gen_dri_challenges.py --check    # exit 1 if the committed table is not the rule's

Reads the battles and their enemies from src/fieldstg/fieldstg_80083784.c, the stages' battle lists from src/wstag/*.c
(gen_hunt_targets.py's readers), the species from port/game/definitive_enemies.c (its IDs, and its comments' names for
the list), the items from src/main/records.c, the Rare Targets from port/game/definitive_hunt_targets.c and the commons'
names from tools/text_override/en.txt: no disc. The numbers are definitive.h's (DEF_DRI_*, DEF_HUNT_*).

The lines (LINES below) are a choice, not a rule: the disc's enemy species have no Greymon, Garurumon line, Growlmon,
ExVeemon, Angemon or Gallantmon, so a line's Champion is a stand-in of the same kind, and the Gabumon line is not there
at all. Every line's Ultimate and Mega are the game's own (the Tamers' rivals and the BK Megas). The rule for the rest:
  - challenge k's Champion fights at DEF_DRI_LEVEL_FIRST + k * DEF_DRI_LEVEL_GAP, each next stage DEF_DRI_LEVEL_STEP
    higher; a stage's HP and stat_scale follow its species' own rows in the battle table (the game raises a species
    that way: Tyrannomon's stat_scale is 16 at level 6, 83 at 52): between two of its levels, the line between them;
    past its lowest or highest, that row times (level ratio) ** (DEF_DRI_SCALE_EXP_PCT / 100), the growth of the game's
    own rows of one species; MP as the nearest row's; HP never past DEF_HUNT_HP_MAX (an s16);
  - the battle it is set up as: a kind-2 battle (escape blocked, the bosses' immunities, its XP and Bits on a win) with
    none of the line's species in it, not one wfightmn gives special rules (types 1-4) nor the final boss's, its own level
    closest to the Mega's;
  - its unlock: the strongest common Hunt Material whose band (the species that drop it, by their highest random
    encounter) tops out at or under the Champion's level, DEF_DRI_COMMON_LO..HI of it rising with the challenge (rounded
    to 5); and one rare item (two from challenge DEF_DRI_TWO_RARES): Rare Targets at most DEF_DRI_RARE_OVER levels above
    the Champion, the line's own species first, then the strongest, then a second lead species;
  - its rewards: the line's family of elemental "Power" accessories (records.c: "Fire Power 1", "2", "S"), tier n for the
    n-th stage felled. No new items.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import gen_hunt_targets as gh  # noqa: E402

GEN = ROOT / "port/game/definitive_dri_table.c"
OUT = ROOT / ".dumps/tasks/dri"
FINAL_BOSS = {442, 466, 467}  # Galacticmon's phases (gen_enemy_weaknesses.FINAL_BOSS)
# Battles wfightmn gives rules of their own (state.type 1..4: 0x143, 0xB, 0x1E, 0x144) and the final boss's.
SPECIAL = {0x143, 0xB, 0x1E, 0x144, 325, 326}

# The lines, in the order of their Mega's story battle (BK MegaGargomon 45, BK Imperialdramon 46, BK Seraphimon 47,
# BK WarGreymon 53, BK WarGrowlmon 55): stages as enemy species IDs, Champion first; the family's three item IDs.
LINES = [
    dict(name="Terriermon (Guardromon)", family="Metal Power", stages=(435, 40, 443), rewards=(354, 355, 356),
         note="the PRD's own example of a line without its rookie: Guardromon and Andromon, machines, lead to BK MegaGargomon"),
    dict(name="Veemon", family="Bolt Power", stages=(135, 450, 444), rewards=(351, 352, 353),
         note="no ExVeemon: Devidramon, a Champion dragon resisting Thunder/Machine"),
    dict(name="Patamon", family="Wind Power", stages=(7, 452, 447), rewards=(348, 349, 350),
         note="no Angemon: Airdramon, the Champion holy dragon, resisting Fire/Wind"),
    dict(name="Agumon", family="Fire Power", stages=(8, 449, 448), rewards=(339, 340, 341),
         note="no Greymon: Tyrannomon, the Champion dinosaur"),
    dict(name="Guilmon", family="Dark Power", stages=(69, 451, 445), rewards=(357, 358, 359),
         note="no Growlmon nor Gallantmon: Flarerizamon, then BK WarGrowlmon as the Mega; Dark for a Virus line "
              "(Fire is Agumon's)"),
]

# The DRI Agents: (map, actor ID) pairs after whose own talk the list opens (the actor ID is FieldstgPlacedActor.id, the
# actor's base.key1); map -1 matches any map. Found on the lab with tools/find_dri_agents.py (needs the disc): the DDNA
# quests' researchers (Kazuki and Michael on 0x21D; Bob, Johann, Jimmy, Ken, Yuji, Yang). Their story's pig forms
# ("Oink...", actors 157..161) are left out: a quest's state, not an agent.
AGENTS = [
    (-1, 43),  # every map it is on: 0x21d, 0x226, 0x22b, 0x22e, 0x258
    (-1, 44),  # every map it is on: 0x21d, 0x258
    (-1, 163),  # every map it is on: 0x258
]


def constants():
    h = (ROOT / "port/game/definitive.h").read_text()
    c = {k: int(v, 0) for k, v in re.findall(r"#define (DEF_\w+)\s+(0x[0-9A-Fa-f]+|\d+)", h)}
    for k in ("DEF_DRI_CHALLENGES", "DEF_DRI_STAGES", "DEF_DRI_LEVEL_FIRST", "DEF_DRI_LEVEL_GAP", "DEF_DRI_LEVEL_STEP",
              "DEF_DRI_COMMON_LO", "DEF_DRI_COMMON_HI", "DEF_DRI_RARE_OVER", "DEF_DRI_TWO_RARES", "DEF_DRI_SCALE_EXP_PCT",
              "DEF_HUNT_HP_MAX", "DEF_DRI_RIVALS",
              "DEF_HUNT_MATERIALS"):
        assert k in c, k
    return c


def species():
    """{id: name} of the enemy species (the first row of an ID: the later ones are the same species' second records)."""
    s = (ROOT / "port/game/definitive_enemies.c").read_text()
    out, order = {}, []
    for sid, name in re.findall(r"\{ (\d+), \{[^}]*\}, [01] \}, /\* (.*?) \*/", s):
        order.append(int(sid))
        out.setdefault(int(sid), name)
    return out, order


def battle_rows():
    """The enemy rows (species, level, hp, mp, stat_scale) and, per battle, (row indices, kind, escape blocked)."""
    src = (ROOT / "src/fieldstg/fieldstg_80083784.c").read_text()
    body = src[src.index("RecordsEnemy fieldstg_battle_enemies"):]
    body = body[:body.index("\n};")]
    rows = [tuple(int(x) for x in m) for m in re.findall(r"\{\s*(-?\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", body)]
    bt = src[src.index("FieldstgBattle fieldstg_battles"):]
    battles = []
    for es, _fs, kind, bl in re.findall(r"\{\s*\{([^}]*)\},\s*(0x[0-9A-Fa-f]+|\d+),\s*(\d+),\s*\{([^}]*)\}\s*\}", bt):
        idx = [int(x) for x in re.findall(r"\[(\d+)\]", es) if int(x)]
        flags = [int(x, 0) for x in bl.split(",")]
        battles.append(dict(rows=idx, kind=int(kind), escape=flags[11], blocked=sum(flags)))
    return rows, battles


def items():
    s = (ROOT / "src/main/records.c").read_text()
    body = s[s.index("RecordsItem records_items[] = {"):]
    body = body[:body.index("\n};")]
    return {int(m.group(3)): dict(cat=m.group(2), name=m.group(4))
            for m in re.finditer(r"\{ (\S+), \d+, \d+, (\w+), \d+, \{ 0, 0 \} \}, /\* (\d+) (.*?) \*/", body)}


def rares(rows, battles):
    """[dict(index, level, lead, variant)] of def_hunt_targets (the index is the rare item's)."""
    s = (ROOT / "port/game/definitive_hunt_targets.c").read_text()
    out = []
    for i, (_r, b, lv, v) in enumerate(re.findall(r"\{ 0x([0-9A-F]+), (\d+), (\d+), (\d+) \}", s)):
        out.append(dict(index=i, level=int(lv), lead=rows[battles[int(b)]["rows"][0]][0], variant=int(v)))
    return out


def material_tops(order):
    """Each common material's band top: the highest random-encounter level of the species that drop it."""
    s = (ROOT / "port/game/definitive_hunt_targets.c").read_text()
    body = re.search(r"def_hunt_commons\[DEF_BESTIARY_SPECIES\] = \{(.*?)\};", s, re.S).group(1)
    com = [int(x, 0) for x in re.sub(r"/\*.*?\*/", "", body).split(",") if x.strip()]
    info = gh.battle_info()
    index = {}
    for i, sid in enumerate(order):
        index.setdefault(sid, i)
    level = {}
    for f in gh.stage_maps():
        for enc, _scr in gh.stage_lists(ROOT / f"src/wstag/wstag{f}.c"):
            for b in enc:
                for sid, lv in info[b]["enemies"]:
                    if sid in index:
                        level[index[sid]] = max(level.get(index[sid], 0), lv)
    tops = {}
    for i, m in enumerate(com):
        if m < 0xFF and i in level:
            tops[m] = max(tops.get(m, 0), level[i])
    return tops


def rival_texts():
    """The Tamers' texts ($DriRival<n>, ...Hello, ...Won, ...Lost in en.txt), by name."""
    s = (ROOT / "tools/text_override/en.txt").read_text()
    return {k: v.strip() for k, v in re.findall(r"^\$(DriRival\d+\w*): (.*)$", s, re.M)}


def material_names():
    s = (ROOT / "tools/text_override/en.txt").read_text()
    return {int(i): n.strip() for i, n in re.findall(r"^\$HuntMat(\d+): (.*)$", s, re.M)}


def roman(n):
    return "X" * (n // 10) + ["", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"][n % 10]


def stage_of(sid, level, rows, battles, c):
    """Species `sid` at `level`, from its own rows: per level the strongest (stat_scale, then HP)."""
    used = {r for b in battles for r in b["rows"] if rows[r][0] == sid}
    assert used, f"species {sid} fights in no battle"
    by_level = {}
    for r in sorted(used):
        _s, lv, hp, mp, sc = rows[r]
        if lv not in by_level or (sc, hp) > by_level[lv][1:3]:
            by_level[lv] = (lv, sc, hp, mp)
    pts = sorted(by_level.values())
    lo = [p for p in pts if p[0] <= level]
    hi = [p for p in pts if p[0] >= level]
    if lo and hi:
        a, b = lo[-1], hi[0]
        t = 0 if b[0] == a[0] else (level - a[0]) / (b[0] - a[0])
        sc, hp = a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t
        mp = a[3] if level - a[0] <= b[0] - level else b[3]
    else:
        a = hi[0] if hi else lo[-1]
        f = (level / a[0]) ** (c["DEF_DRI_SCALE_EXP_PCT"] / 100)
        sc, hp, mp = a[1] * f, a[2] * f, a[3]
    return dict(digimon=sid, level=level, hp=min(c["DEF_HUNT_HP_MAX"], int(hp + 0.5)), mp=mp,
                stat_scale=max(1, int(sc + 0.5)))


def round5(v):
    return max(5, (v + 2) // 5 * 5)


def table():
    c = constants()
    names, order = species()
    rows, battles = battle_rows()
    its = items()
    tops = material_tops(order)
    targets = rares(rows, battles)
    n = len(LINES)
    assert 0 < n <= c["DEF_DRI_CHALLENGES"], n
    assert n <= c["DEF_DRI_RIVALS"], f"{n} lines, DEF_DRI_RIVALS {c['DEF_DRI_RIVALS']}: each line needs its Tamer"
    say = rival_texts()
    for k in range(n):
        for part in ("", "Hello", "Won", "Lost"):
            assert f"DriRival{k}{part}" in say, f"tools/text_override/en.txt has no $DriRival{k}{part}"
    out = []
    for k, line in enumerate(LINES):
        stages = line["stages"]
        assert len(stages) == c["DEF_DRI_STAGES"] and all(s in names for s in stages), line
        for item in line["rewards"]:
            assert its.get(item, {}).get("cat") == "RECORDS_ITEM_ACCESSORY", (line, item)
        first = c["DEF_DRI_LEVEL_FIRST"] + k * c["DEF_DRI_LEVEL_GAP"]
        levels = [first + i * c["DEF_DRI_LEVEL_STEP"] for i in range(c["DEF_DRI_STAGES"])]
        st = [stage_of(s, lv, rows, battles, c) for s, lv in zip(stages, levels)]
        top = levels[-1]
        cand = [i for i, b in enumerate(battles)
                if b["kind"] == 2 and b["escape"] and b["rows"] and i not in SPECIAL
                and not any(rows[r][0] in stages or rows[r][0] in FINAL_BOSS for r in b["rows"])]
        battle = min(cand, key=lambda i: (abs(max(rows[r][1] for r in battles[i]["rows"]) - top), i))
        fits = [m for m, t in tops.items() if t <= first]
        common = max(fits) if fits else 0
        common_n = round5(c["DEF_DRI_COMMON_LO"] + (c["DEF_DRI_COMMON_HI"] - c["DEF_DRI_COMMON_LO"]) * k // max(1, n - 1))
        pool = sorted((t for t in targets if t["level"] <= first + c["DEF_DRI_RARE_OVER"]),
                      key=lambda t: (t["lead"] not in stages, -t["level"], t["variant"], t["index"]))
        picks = pool[:1]
        if k >= c["DEF_DRI_TWO_RARES"] and pool:
            other = [t for t in pool[1:] if t["lead"] != pool[0]["lead"]] or pool[1:]
            picks += other[:1]
        assert picks, f"no rare item for challenge {k}"
        out.append(dict(k=k, line=line, stages=st, battle=battle, common=common, common_n=common_n,
                        rares=[(t["index"], 1) for t in picks], rare_info=picks))
    return out, names, its, rows, battles


def rare_name(t, names):
    return f"{names.get(t['lead'], t['lead'])} Core {roman(t['variant'])}"


def c_text(rows_, names):
    lines = ["/* Generated by tools/gen_dri_challenges.py (PRD 4 layer H): the DRI Agents' Gauntlet, an evolution line's",
             " * stages as the three enemy members of one battle, the battle it is set up as, the Synergy accessory of each",
             " * tier, and what unlocking it takes; then the DRI Agents. Regenerate instead of editing; the owner approves",
             " * the list. */",
             '#include "definitive.h"', "",
             "const DefDriChallenge def_dri_challenges[] = {"]
    for r in rows_:
        st = r["stages"]
        desc = ", ".join(f"{names[s['digimon']]} Lv{s['level']}" for s in st)
        lines.append(f"    /* {r['k']} {r['line']['name']} line, {r['line']['family']}: {desc} */")
        stages = ", ".join(f"{{ {s['digimon']}, {s['level']}, {s['hp']}, {s['mp']}, {s['stat_scale']} }}" for s in st)
        rare = r["rares"] + [(-1, 0)] * (2 - len(r["rares"]))
        lines.append(f"    {{ {{ {stages} }}, {r['battle']}, {{ {', '.join(str(x) for x in r['line']['rewards'])} }}, "
                     f"{r['common']}, {r['common_n']}, {{ {rare[0][0]}, {rare[1][0]} }}, {{ {rare[0][1]}, {rare[1][1]} }} }},")
    lines += ["};", f"const int def_dri_challenge_count = {len(rows_)};", "",
              "/* (map, actor ID); map -1: any map. */",
              "const DefDriAgent def_dri_agents[] = {"]
    for m, a in AGENTS:
        lines.append(f"    {{ {m}, {a} }},")
    if not AGENTS:
        lines.append("    { 0, 0 }, /* none yet (a placeholder; the count is 0): tools/find_dri_agents.py on the lab */")
    lines += ["};", f"const int def_dri_agent_count = {len(AGENTS)};", ""]
    return "\n".join(lines)


def write_md(rows_, names, its, battles, rows):
    mats = material_names()
    out = ["# Desafios do Gauntlet DRI (PRD 4 camada H, proposta para o dono)", "",
           "Cada desafio é uma batalha só, com os três estágios da linha como os três inimigos, em ordem: quando um cai,",
           "o próximo entra com o HP cheio e a party continua como estava. Nível do Champion: "
           "`DEF_DRI_LEVEL_FIRST` + k × `DEF_DRI_LEVEL_GAP`; cada estágio `DEF_DRI_LEVEL_STEP` acima do anterior.",
           "HP e `stat_scale` vêm da linha da própria espécie na tabela de batalhas mais perto do nível, escalados pela",
           "razão dos níveis. Recompensa: o acessório elemental da família, tier n no n-ésimo estágio derrubado, uma vez.",
           "", "| # | linha | estágios (nível, HP, escala) | batalha-modelo (XP, Bits da vitória) | destravar | recompensas | nota |",
           "| --- | --- | --- | --- | --- | --- | --- |"]
    rw = (ROOT / "src/stfgtrep/stfgtrep_80082E70.c").read_text()
    rw = rw[rw.index("StfgtrepReward stfgtrep_rewards[] = {"):]
    rewards = [tuple(int(x) for x in m) for m in re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", rw[:rw.index("\n};")])]
    for r in rows_:
        st = "; ".join(f"{names[s['digimon']]} Lv{s['level']} HP{s['hp']} x{s['stat_scale']}/16" for s in r["stages"])
        b = r["battle"]
        who = ", ".join(f"{names.get(rows[i][0], rows[i][0])} Lv{rows[i][1]}" for i in battles[b]["rows"])
        cost = f"{mats.get(r['common'], r['common'])} ×{r['common_n']}, " + ", ".join(
            f"{rare_name(t, names)} ×1" for t in r["rare_info"])
        rew = ", ".join(its[i]["name"] for i in r["line"]["rewards"])
        tamer = rival_texts().get(f"DriRival{r['k']}", "?")
        out.append(f"| {r['k']} | {r['line']['name']} ({r['line']['family']}; Tamer: {tamer}) | {st} | {b}: {who} "
                   f"({rewards[b][1]} XP, {rewards[b][2]} Bits) | {cost} | {rew} | {r['line']['note']} |")
    out += ["", "DRI Agents: " + (", ".join(f"mapa 0x{m:X} ator {a}" if m >= 0 else f"ator {a} (qualquer mapa)"
                                         for m, a in AGENTS) or "nenhum ainda (rodar tools/find_dri_agents.py no lab)")]
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "desafios.md").write_text("\n".join(out) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="exit 1 if the committed table is not the rule's")
    a = ap.parse_args()
    rows_, names, its, rows, battles = table()
    text = c_text(rows_, names)
    if a.check:
        ok = GEN.exists() and GEN.read_text() == text
        print(f"{'ok' if ok else 'FAIL'} port/game/definitive_dri_table.c is the rule's ({len(rows_)} challenges, "
              f"{len(AGENTS)} DRI Agents)")
        return 0 if ok else 1
    GEN.write_text(text)
    write_md(rows_, names, its, battles, rows)
    print(f"written {GEN} ({len(rows_)} challenges) and {OUT / 'desafios.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
