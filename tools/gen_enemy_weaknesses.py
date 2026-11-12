#!/usr/bin/env python3
"""The enemy table of the definitive combat (PRD layer 3): every species gets its archetype's weaknesses and strong
resistances (plain 2+1, wall 1+2, late 2+2, boss 2+3, final boss 2+4), the rest neutral; a few species carry the Dark mark.

  tools/venv/bin/python tools/gen_enemy_weaknesses.py            # proposal: .dumps/tasks/persona/{fraquezas.md,
                                                                 #   definitive_enemies.proposed.c}; the game is not touched
  tools/venv/bin/python tools/gen_enemy_weaknesses.py --apply    # also write port/game/definitive_enemies.c (the owner approved)

Deterministic (fixed seed). Reads SDIGIEDT.PRO and the names from the user's disc, the techniques from src/main/records.c
and the battles from src/fieldstg/fieldstg_80083784.c; the disc is never written. Rules (PRD layer 3):
  - the house element of a species (its techniques' commonest element, else its one high resist, else its type) is never
    a weakness and is the natural strong resistance;
  - a species that already has exactly two weaknesses keeps them; too many: the two most coherent with the theme; too
    few: filled by opposition, then by balance, so the count per element evens out and two species of one battle do
    not share a pair;
  - weak = 60, strong = 300, neutral 100..115 (the current value when it is already there);
  - Dark: a few holy species have it as their only weakness, a few more (and the final boss) as an extra one; resisting
    it is just as rare (DARK_RESIST: eight species), so that Dark stays a useful element.
"""
import argparse
import random
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import disc_files as df  # noqa: E402
import dump_text as dt  # noqa: E402

SEED = 20261112
EL = ["Fire", "Water", "Ice", "Wind", "Thunder", "Machine", "Dark"]
WEAK, STRONG, WEAK_MAX, STRONG_MIN = 60, 300, 75, 250
NEUTRAL = (100, 115)
# What a species of each type is, by the techniques the type uses (a count over the disc): None = no clear theme.
TYPE_HOUSE = {2: 0, 3: 6, 4: 6, 5: 5, 6: 2, 7: 3, 8: 4, 10: 1, 11: 3}
# The elements that answer a house element, best first (the theme of what hurts it).
OPPOSE = {0: [1, 2, 3], 1: [4, 2, 3], 2: [0, 4, 3], 3: [4, 2, 5], 4: [3, 0, 1], 5: [4, 1, 0], 6: [0, 4, 3]}
DARK_ONLY = {447, 452, 178, 453}      # BK Seraphimon, MagnaAngemon, Knightmon, Taomon: holy, weak only to Dark
DARK_EXTRA = {269, 442, 466, 467}     # Valkyrimon and Galacticmon (the three phases): Dark on top of two others
# Resisting Dark is as rare as being weak to it (owner's rule: Dark must stay a useful element): only these eight, the
# demons and the undead of the list, have it as their strong resistance; every other species gets another element.
DARK_RESIST = {382, 236, 177, 223, 364, 312, 302, 251}  # Armageddemon, VenomMyotismon, LadyDevimon, Phantomon, Mummymon,
                                                        # Ghoulmon, Gargoylemon, Apokarimon
FINAL_BOSS = {442, 466, 467}   # Galacticmon, the three phases: four resistances
LATE_LEVEL = 45                # from this level a mob resists two elements
WALL_MAX_LEVEL = 30            # below this a share of the mobs are "walls": one weakness, two resistances
WALL_SHARE = 0.2
OUT = ROOT / ".dumps/tasks/persona"
TECH_FIELDS = ("mp power element target accuracy defense_stat element_power strong_type kind effect_chance effect_power "
               "anim_stage anim_effect hit_sound anim_script hit_count").split()


def species():
    ed = (df.DISC / "AAA/PRO/SDIGIEDT.PRO").read_bytes()
    tables = dt.char_tables()
    e = dt.find_file("DIGNAM", "ENG")
    d = (df.DISC / e.path).read_bytes()
    (_, offs), = dt.tables_of(d)
    names = [dt.decode(d, o, tables, latin=False)[0] for o in offs]
    out, o = [], 0
    while True:
        f = struct.unpack_from("<7h5h12hBB", ed, o)
        if f[0] == 0:
            return out
        out.append(dict(id=f[0], name=names[f[3]] if f[3] < len(names) else "?", techs=[t for t in f[4:7] if t],
                        resists=list(f[12:19]), type=f[24]))
        o += 0x46


def tech_elements():
    src = (ROOT / "src/main/records.c").read_text()
    body = src[src.index("RecordsTechnique records_techniques[] = {"):]
    body = body[:body.index("\n};")]
    ds = []
    for line in body.splitlines()[1:]:
        nums = [int(x, 0) for x in re.findall(r"-?0x[0-9A-Fa-f]+|-?\d+", line)]
        if nums:
            ds.append(dict(zip(TECH_FIELDS, nums))["defense_stat"])
    return ds


def battles():
    """(species lists per battle, {species: max level}, {species in a boss battle}). A boss battle is kind 2 with at least
    nine of the twelve blocked[] flags set (the effects that fail against it)."""
    src = (ROOT / "src/fieldstg/fieldstg_80083784.c").read_text()
    body = src[src.index("RecordsEnemy fieldstg_battle_enemies"):]
    body = body[:body.index("\n};")]
    rows = [tuple(int(x) for x in m) for m in re.findall(r"\{\s*(-?\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}", body)]
    bt = src[src.index("FieldstgBattle fieldstg_battles"):]
    out, level, boss = [], {}, set()
    for es, _fs, kind, bl in re.findall(r"\{\s*\{([^}]*)\},\s*(0x[0-9A-Fa-f]+|\d+),\s*(\d+),\s*\{([^}]*)\}\s*\}", bt):
        idx = [int(x) for x in re.findall(r"\[(\d+)\]", es) if int(x)]
        ids = [rows[i][0] for i in idx]
        out.append(sorted(set(ids)))
        for i in idx:
            level[rows[i][0]] = max(level.get(rows[i][0], 0), rows[i][1])
        if int(kind) == 2 and sum(int(x) for x in bl.split(",")) >= 9:
            boss.update(ids)
    return out, level, boss


def house_element(sp, ds):
    c = Counter(ds[t - 1] - 2 for t in sp["techs"] if t <= len(ds) and ds[t - 1] >= 2)
    if sp["id"] not in DARK_RESIST:
        c.pop(6, None)  # Dark is the house of the eight above only
    if c:
        top = max(c.values())
        return [e for e in sorted(c) if c[e] == top][0]
    top = 7 if sp["id"] in DARK_RESIST else 6
    hi = max(range(top), key=lambda e: sp["resists"][e])
    if sp["resists"][hi] >= 160 and sp["resists"][:top].count(sp["resists"][hi]) == 1:
        return hi
    th = TYPE_HOUSE.get(sp["type"])
    return th if th != 6 or sp["id"] in DARK_RESIST else None


def weakness_mask(r, dark_weak):
    """tools' twin of port/game/definitive_mask.c (kept in step by the check below): the elements r[0..6] are weak to."""
    order = sorted(range(6), key=lambda e: (r[e], e))
    median2 = r[order[2]] + r[order[3]]
    mask = 0
    taken = 0
    for e in order:
        if taken < 2 and 2 * r[e] <= median2 - 40:
            mask |= 1 << e
            taken += 1
    return mask | (1 << 6 if dark_weak else 0)


def archetype(sid, level, boss, rng):
    """(weaknesses, strong resistances, name). Persona-like variety:
    boss battles resist a lot (the final boss most), late mobs two, a few weak early mobs are "walls" (one weakness, two
    resistances: the weakness is everything), the rest are the plain two and one."""
    if sid in FINAL_BOSS:
        return 2, 4, "final"
    if boss:
        return 2, 3, "boss"
    if level >= LATE_LEVEL:
        return 2, 2, "late"
    if level <= WALL_MAX_LEVEL and rng.random() < WALL_SHARE:
        return 1, 2, "wall"
    return 2, 1, "plain"


def propose(sp_list, battle_info, ds):
    battle_list, level, boss = battle_info
    rng = random.Random(SEED)
    batt_of = defaultdict(list)
    for b, ids in enumerate(battle_list):
        for i in ids:
            batt_of[i].append(b)
    info = {}
    for sp in sp_list:
        r = sp["resists"]
        allowed = 7 if sp["id"] in DARK_RESIST else 6
        info[sp["id"]] = dict(sp=sp, house=house_element(sp, ds), allowed=allowed,
                              strong=[e for e in range(allowed) if r[e] >= STRONG_MIN],
                              weak=[e for e in range(6) if r[e] <= WEAK_MAX])
    weak_count, strong_count = Counter(), Counter()
    used_pairs = defaultdict(set)  # battle -> {frozenset of weaknesses}
    order = list(sp_list)
    rng.shuffle(order)
    for sp in order:
        x = info[sp["id"]]
        x["nw"], x["ns"], x["kind"] = archetype(sp["id"], level.get(sp["id"], 0), sp["id"] in boss, rng)
        if sp["id"] in DARK_ONLY:
            # no elemental weakness: with three or more strong values among the six, the median rises and the neutral
            # ones would count as weak, so at most two (the rule's median stays neutral)
            x["nw"], x["ns"] = 0, min(x["ns"], 2)

    # strong resistances: the ones it has (the house first), then the house, then whatever evens the table out
    for sp in order:
        x = info[sp["id"]]
        picks = []
        for e in sorted(x["strong"], key=lambda e: (e != x["house"], -sp["resists"][e])):
            if len(picks) < x["ns"]:
                picks.append(e)
        if len(picks) < x["ns"] and x["house"] is not None and x["house"] not in picks and x["house"] < x["allowed"]:
            picks.append(x["house"])
        while len(picks) < x["ns"]:
            picks.append(min((e for e in range(x["allowed"]) if e not in picks), key=lambda e: (strong_count[e], rng.random())))
        x["pick_strong"] = picks
        x["house"] = picks[0] if x["house"] is None else x["house"]
        for e in picks:
            strong_count[e] += 1

    def rank(x, e):
        p = OPPOSE[x["house"]]
        return p.index(e) if e in p else len(p)

    def conflicts(sid, pair):
        return any(pair in used_pairs[b] for b in batt_of[sid])

    # weaknesses: a plain species that already has exactly its two keeps them
    for sp in order:
        sid, x = sp["id"], info[sp["id"]]
        if x["nw"] == 0:
            x["pick_weak"] = []
            continue
        cand = [e for e in x["weak"] if e not in x["pick_strong"] and e != x["house"]]
        if x["kind"] == "plain" and len(cand) == 2:
            x["pick_weak"] = sorted(cand)
            for e in cand:
                weak_count[e] += 1
            for b in batt_of[sid]:
                used_pairs[b].add(frozenset(cand))
    for sp in order:
        sid, x = sp["id"], info[sp["id"]]
        if "pick_weak" in x:
            continue
        pool = [e for e in range(6) if e not in x["pick_strong"] and e != x["house"]]
        if len(pool) < x["nw"]:  # every element taken by resistances: the house may give way
            pool = [e for e in range(6) if e not in x["pick_strong"]]
        keep = [e for e in x["weak"] if e in pool]
        chosen = []
        for _ in range(x["nw"]):
            options = [e for e in pool if e not in chosen]

            def cost(e):
                c = rank(x, e) * 3 + weak_count[e] * 1.5 + (0 if e in keep else 1)
                if chosen and conflicts(sid, frozenset(chosen + [e])):
                    c += 100
                return (c, rng.random())

            chosen.append(min(options, key=cost))
        x["pick_weak"] = sorted(chosen)
        for e in chosen:
            weak_count[e] += 1
        for b in batt_of[sid]:
            used_pairs[b].add(frozenset(chosen))

    rows = []
    for sp in sp_list:
        sid, x = sp["id"], info[sp["id"]]
        dark = sid in DARK_ONLY or sid in DARK_EXTRA
        new = []
        for e in range(7):
            v = sp["resists"][e]
            if e in x["pick_strong"]:
                new.append(v if v >= STRONG_MIN else STRONG)
            elif e in x["pick_weak"]:
                new.append(WEAK)
            elif e == 6 and dark:
                new.append(WEAK)
            else:
                new.append(v if NEUTRAL[0] <= v <= NEUTRAL[1] else rng.randint(*NEUTRAL))
        rows.append(dict(id=sid, name=sp["name"], type=sp["type"], house=x["house"], before=sp["resists"], after=new,
                         dark=int(dark), dark_only=int(sid in DARK_ONLY), kind=x["kind"], nw=x["nw"], ns=x["ns"],
                         level=level.get(sid, 0), boss=int(sid in boss)))
    return rows


def check(rows):
    """Every species, through the game's own rule (weakness_mask): exactly its intended weaknesses, exactly its strong ones."""
    for r in rows:
        a = r["after"]
        mask = weakness_mask(a, r["dark"])
        weak = [e for e in range(6) if mask >> e & 1]
        strong = [e for e in range(7) if a[e] >= STRONG_MIN]
        assert len(weak) == r["nw"], (r["id"], r["name"], r["kind"], weak, a)
        assert len(strong) == r["ns"], (r["id"], r["name"], r["kind"], strong, a)
        assert not set(weak) & set(strong), (r["id"], r["name"])
        assert bool(mask >> 6 & 1) == bool(r["dark"])


def fmt(v):
    return "W" if v <= WEAK_MAX else "S" if v >= STRONG_MIN else "."


def write(rows, apply):
    OUT.mkdir(parents=True, exist_ok=True)
    wc, sc = Counter(), Counter()
    for r in rows:
        for e in range(6):
            wc[e] += r["after"][e] <= WEAK_MAX
        for e in range(7):
            sc[e] += r["after"][e] >= STRONG_MIN
    kinds = Counter(r["kind"] for r in rows)
    lines = ["# Fraquezas dos inimigos (camada 3): proposta, aguardando aprovação", "",
             "Arquétipos (estilo Persona): **plain** 2 fracos + 1 forte; **wall** (mob fraco) 1 fraco + 2 fortes; **late** (nível ≥ "
             f"{LATE_LEVEL}) 2 fracos + 2 fortes; **boss** (luta de chefe) 2 fracos + 3 fortes; **final** (Galacticmon) 2 fracos + 4 fortes. "
             "Contagem: " + ", ".join(f"{k} {kinds[k]}" for k in ("plain", "wall", "late", "boss", "final")) + ".", "",
             f"{len(rows)} espécies. Semente {SEED}. Fraco = 60, forte = 300, neutro 100–115. Marca de Trevas (fraco a Trevas): **D** (só Trevas: **D!**). Resistir a Trevas é raro de propósito (regra do dono).",
             "Colunas `antes`/`depois`: um caractere por elemento (Fogo Água Gelo Vento Trovão Máquina Trevas): `W` fraco, `S` forte, `.` neutro.", "",
             "| Elemento | Fraquezas | Resistências fortes |", "|---|---|---|"]
    dark_weak = sum(r["dark"] for r in rows)
    lines += [f"| {EL[e]} | {wc[e] if e < 6 else dark_weak} | {sc[e]} |" for e in range(7)]
    lines += ["", "| ID | Espécie | Nível | Tipo | Arquétipo | Casa | Antes | Depois | Valores depois | Trevas |", "|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        mark = "D!" if r["dark_only"] else "D" if r["dark"] else ""
        lines.append(f"| {r['id']} | {r['name']} | {r['level']} | {r['type']} | {r['kind']} | {EL[r['house']]} | {''.join(fmt(v) for v in r['before'])} | "
                     f"{''.join(fmt(v) for v in r['after'])} | {' '.join(str(v) for v in r['after'])} | {mark} |")
    (OUT / "fraquezas.md").write_text("\n".join(lines) + "\n")
    c = ["/* Generated by tools/gen_enemy_weaknesses.py (definitive combat PRD, layer 3): do not edit. */",
         '#include "definitive.h"', "", "const DefEnemyRow def_enemy_rows[] = {"]
    for r in rows:
        c.append(f"    {{ {r['id']}, {{ {', '.join(str(v) for v in r['after'])} }}, {r['dark']} }}, /* {r['name']} */")
    c += ["};", f"const int def_enemy_row_count = {len(rows)};", ""]
    (OUT / "definitive_enemies.proposed.c").write_text("\n".join(c))
    if apply:
        (ROOT / "port/game/definitive_enemies.c").write_text("\n".join(c))
    print(f"{len(rows)} species; weak per element {[wc[e] for e in range(6)]}; strong {[sc[e] for e in range(7)]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true", help="write port/game/definitive_enemies.c")
    a = ap.parse_args()
    rows = propose(species(), battles(), tech_elements())
    check(rows)
    write(rows, a.apply)


if __name__ == "__main__":
    main()
