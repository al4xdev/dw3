#!/usr/bin/env python3
"""PRD 4 layer F: the equipment variants (build crafting). There is no free item ID (records_items has 1..402 and the
save's inventory is items[0x193]), so a variant is an existing shop item converted in place: same ID, icon (type),
slot, group, who can wear it, charisma, price and forge recipe; new modifiers, name and description. Three archetypes
for a party of two or three, one of each in every equipment shop's goods (a "tier"):
  - Speed: a one-handed weapon that gives up power for Boost (the definitive turn rule: the party opens, the enemy's
    first turn comes after its full delay, so a party faster than the enemy acts twice from the first turn);
  - Elemental: a head piece that gives up guard for the resists of one family (def_affinity reads them: Water+Ice and
    Thunder+Machine on the pair's sum), so WEAK! and the open target's hit (One More) do more;
  - Tank: body armour or a shield above the tier's best guard, with Spirit, paid with Boost.

  tools/venv/bin/python tools/gen_equipment.py            # write port/game/definitive_equipment.c and the review list
  tools/venv/bin/python tools/gen_equipment.py --check    # exit 1 if the committed table is not the rule's, or a
                                                          # variant breaks a rule below

Reads the items and their data from src/main/records.c, the shops' goods from src/stitshop/stitshop_800859C0.c, the
towns that open each shop from src/wstag (flag 0x7A00 + shop), the items FIGHTSTG gives an effect by ID from
src/fightstg/fightstg_8008D3B4.c, the set items from src/main/gamestate.c, the names and descriptions from
tools/text_override/en.txt; no disc. The numbers are definitive.h's (DEF_EQUIP_*).

Selection, per tier (a distinct goods list with weapons or armour; the per-Digimon set shop, whose every item is a set
piece, has none) and archetype, in the order Speed, Elemental, Tank:
  - candidates: Speed a one-handed weapon (not a rod or an arm guard), Elemental a head piece, Tank body armour or a
    shield; never an item FIGHTSTG knows by ID (status, critical), with a strong type, or of an item set;
  - the first by: strictly dominated in the tier (another item of the same slot that every wearer can wear, no dearer,
    at least as good in every stat, charisma included), then its trait (one with a bonus of the archetype's own stats
    first, one with a positive Power..Charisma bonus another archetype is about last), then redundancy (each
    wearer's best other item of the same slot in the tier, not yet a variant, its main stat over this one's: the
    least of them; 1 or more means nobody loses a plain option), then the cheaper, then the ID.
Budget: a stat's worth in points of Power (DEF_EQUIP_W_*); an item's score is the sum. A tier's budget for a variant is
the best score among the plain items of the tier of its category and slot that share a wearer with it (itself
included). A variant is built against that budget and the tier's best main stat (B):
  - Speed: power B * (100 - DEF_EQUIP_SPEED_COST) %, the item's accuracy, Boost filling the budget;
  - Elemental: guard B * (100 - DEF_EQUIP_ELEM_COST) %, the item's evasion, the family's resists filling the budget
    (split over a pair, Dark at twice the weight: def_affinity counts it double), at most DEF_EQUIP_ELEM_MAX each; the
    family is the item's own element's when it has one, else the one used least so far (ties: Fire, Water+Ice, Wind,
    Thunder+Machine, Dark);
  - Tank: guard B * (100 + DEF_EQUIP_TANK_GUARD) %, the item's evasion, Spirit B * DEF_EQUIP_TANK_SPIRIT %, and a
    Boost penalty that brings it back to the budget.
Checks (--check): the IDs are shop equipment of the category the row says; a variant's score is within its budget and
it is not a straight upgrade of any other item of its tier it shares a slot and a wearer with (variants included);
every variant has an ITMNAM and an ITMINF line in en.txt and nothing else does, the name no longer than the longest
equipment name of the game, the description within 34 characters (the window's, tests/port/text_override.py).
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "port/game/definitive_equipment.c"
OUT = ROOT / ".dumps/tasks/equipment"
EN = ROOT / "tools/text_override/en.txt"
NUM = r"-?0x[0-9A-Fa-f]+|-?\d+"
BONUS = {"0": 0, "RECORDS_BONUS_NONE": 0, "RECORDS_BONUS_POWER": 1, "RECORDS_BONUS_GUARD": 2, "RECORDS_BONUS_SPIRIT": 3,
         "RECORDS_BONUS_WISDOM": 4, "RECORDS_BONUS_BOOST": 5, "RECORDS_BONUS_CHARISMA": 6, "RECORDS_BONUS_ALL": 7,
         "RECORDS_BONUS_FIRE": 8, "RECORDS_BONUS_WATER": 9, "RECORDS_BONUS_ICE": 10, "RECORDS_BONUS_WIND": 11,
         "RECORDS_BONUS_THUNDER": 12, "RECORDS_BONUS_MACHINE": 13, "RECORDS_BONUS_DARK": 14}
STAT = {1: "Power", 2: "Guard", 3: "Spirit", 4: "Wisdom", 5: "Boost", 6: "Charisma", 8: "Fire", 9: "Water", 10: "Ice",
        11: "Wind", 12: "Thunder", 13: "Machine", 14: "Dark"}
ENUM = {v: k for k, v in BONUS.items() if k != "0"}
FAMILIES = [(8,), (9, 10), (11,), (12, 13), (14,)]  # definitive_mask.c's: Fire, Water+Ice, Wind, Thunder+Machine, Dark
SPEED, ELEMENT, TANK = 0, 1, 2
KINDS = ["DEF_EQUIP_SPEED", "DEF_EQUIP_ELEMENT", "DEF_EQUIP_TANK"]
KIND_PT = ["Speed", "Elemental", "Tank"]
ROOKIES = ["Kotemon", "Kumamon", "Monmon", "Agumon", "Veemon", "Guilmon", "Renamon", "Patamon"]  # members bits
DESC_MAX = 34  # the description window (tests/port/text_override.py's MAX)
TEXT_OK = re.compile(r"^[A-Za-z0-9 .,'/!-]+$")


def constants():
    h = (ROOT / "port/game/definitive.h").read_text()
    c = {k: int(v, 0) for k, v in re.findall(r"#define (DEF_EQUIP_\w+)\s+(\w+)", h)}
    for k in ("DEF_EQUIP_SPEED_COST", "DEF_EQUIP_ELEM_COST", "DEF_EQUIP_ELEM_MAX", "DEF_EQUIP_TANK_GUARD",
              "DEF_EQUIP_TANK_SPIRIT", "DEF_EQUIP_W_GUARD", "DEF_EQUIP_W_SPIRIT", "DEF_EQUIP_W_WISDOM",
              "DEF_EQUIP_W_BOOST", "DEF_EQUIP_W_HIT", "DEF_EQUIP_W_ELEMENT"):
        assert k in c, k
    return c


def items():
    """{id: item}: records_items' price, category, type and English name, and the data struct's fields."""
    s = (ROOT / "src/main/records.c").read_text()
    data = {}
    for m in re.finditer(r"Records(Weapon|Armor) records_item_(\d+) = \{(.*?)\}; /\* \d+ (.*?) \*/", s):
        v = [BONUS[t] if t.startswith("RECORDS") else int(t, 0) for t in re.findall(r"RECORDS_BONUS_\w+|" + NUM, m.group(3))]
        if m.group(1) == "Weapon":  # charisma, slot, group, members, -, bonus_values[2], power, bonus_stats[2], accuracy,
            # -, status_chance, status_power, strong_type, -
            d = dict(weapon=1, charisma=v[0], slot=v[1], group=v[2], members=v[3], values=v[5:7], main=v[7],
                     stats=v[8:10], hit=v[10], status=(v[12], v[13]), strong=v[14])
        else:  # charisma, slot, group, members, -, bonus_values[2], bonus_stats[2], guard, -, -, evasion, -
            d = dict(weapon=0, charisma=v[0], slot=v[1], group=v[2], members=v[3], values=v[5:7], stats=v[7:9],
                     main=v[9], hit=v[12], status=(0, 0), strong=0)
        data[int(m.group(2))] = d
    body = s[s.index("RecordsItem records_items[] = {"):]
    out = {}
    for m in re.finditer(r"\{ (\S+), (\d+), (\d+), (\w+), (\d+), \{ 0, 0 \} \}, /\* (\d+) (.*?) \*/", body):
        i = int(m.group(6))
        out[i] = dict(id=i, price=int(m.group(2)), cat=m.group(4), type=int(m.group(5)), name=m.group(7), **data.get(i, {}))
    return out


def goods():
    """[(goods list number, (item IDs))] and {list number: [shops]} (stitshop_shops: shop n sells stitshop_goods_n)."""
    s = (ROOT / "src/stitshop/stitshop_800859C0.c").read_text()
    lists = [(int(n), tuple(int(x, 0) for x in re.findall(NUM, b) if int(x, 0)))
             for n, b in re.findall(r"s16 stitshop_goods_(\d+)\[\] = \{(.*?)\};", s, re.S)]
    table = re.search(r"StitshopShop stitshop_shops\[\] = \{(.*?)\};", s, re.S).group(1)
    shops = defaultdict(list)
    for shop, n in enumerate(re.findall(r"stitshop_goods_(\d+)", table)):
        shops[int(n)].append(shop)
    return lists, shops


def shop_towns():
    """{shop: [stage maps whose stage file opens it]}: a talk setting flag 0x7A00 + n opens shop n (docs/MECHANICS.md)."""
    out = defaultdict(set)
    for path in sorted((ROOT / "src/wstag").glob("wstag*.c")):
        text = path.read_text()
        m = re.search(r"/\* WSTAG\d+: stage (0x[0-9A-Fa-f]+)", text)
        if not m:
            continue
        for f in set(re.findall(r"\b0x7A([01][0-9A-F])\b", text)):
            out[int(f, 16)].add(int(m.group(1), 16))
    return out


def id_effects():
    """The equipment FIGHTSTG gives an effect by its ID (fightstg_rules_get_stats: `p[i] == 0x97` ...)."""
    s = (ROOT / "src/fightstg/fightstg_8008D3B4.c").read_text()
    body = s[s.index("FightstgStats *fightstg_rules_get_stats(u8 side, s32 first, s32 member) {"):]
    body = body[:body.index("\n}\n")]
    return {int(x, 16) for x in re.findall(r"p\[i\] == (0x[0-9A-Fa-f]+)", body)}


def set_items():
    s = (ROOT / "src/main/gamestate.c").read_text()
    body = re.search(r"s16 gamestate_item_sets\[\]\[4\] = \{(.*?)\};", s, re.S).group(1)
    return {int(x, 0) for x in re.findall(NUM, body)}


def texts():
    """{(FILE, index): text} of en.txt's ITMNAM / ITMINF lines."""
    out = {}
    for line in EN.read_text().splitlines():
        m = re.match(r"^(ITMNAM|ITMINF) (\d+): (.*)$", line.split("#", 1)[0].rstrip())
        if m:
            out[(m.group(1), int(m.group(2)))] = m.group(3).strip()
    return out


# ---- stats and the budget

def vec(it, effects):
    """An item's stats as {name: value}: main and hit by category, each bonus stat, an ID effect, a strong type."""
    v = defaultdict(int)
    v["power" if it["weapon"] else "guard"] += it["main"]
    v["accuracy" if it["weapon"] else "evasion"] += it["hit"]
    for s, x in zip(it["stats"], it["values"]):
        if s:
            v[STAT.get(s, s)] += x
    v["charisma"] += it["charisma"]
    if it["id"] in effects:
        v["effect"] = 1
    if it.get("strong", 0) >= 2:
        v["strong"] = 1
    return v


def weight(c, name):
    w = {"power": 100, "Power": 100, "guard": c["DEF_EQUIP_W_GUARD"], "Guard": c["DEF_EQUIP_W_GUARD"],
         "Spirit": c["DEF_EQUIP_W_SPIRIT"], "Wisdom": c["DEF_EQUIP_W_WISDOM"], "Boost": c["DEF_EQUIP_W_BOOST"],
         "accuracy": c["DEF_EQUIP_W_HIT"], "evasion": c["DEF_EQUIP_W_HIT"], "Dark": 2 * c["DEF_EQUIP_W_ELEMENT"]}
    if name in ("Fire", "Water", "Ice", "Wind", "Thunder", "Machine"):
        return c["DEF_EQUIP_W_ELEMENT"]
    return w.get(name, 0)  # charisma, the ID effects, a strong type: not in the budget (a variant keeps none it lacks)


def score(c, v):
    """In hundredths of a Power point."""
    return sum(weight(c, k) * x for k, x in v.items())


def slot_key(it):
    return (it["weapon"], it["slot"])


def ge(a, b):
    """Stat vector a is at least b in every stat."""
    return all(a.get(k, 0) >= b.get(k, 0) for k in set(a) | set(b))


def candidate(kind, it):
    if kind == SPEED:
        return it["weapon"] and it["slot"] == 3 and it["type"] not in (5, 13)  # not a rod, not an arm guard
    if kind == ELEMENT:
        return not it["weapon"] and it["slot"] == 4
    return not it["weapon"] and it["slot"] in (2, 5)


# The stats an archetype is about: an item already leaning that way is converted first, and one with a positive stat
# another archetype is about (a speed cap, a Spirit headband) last, so the game's own trade-off pieces stay. The
# elements are not a trait here: in the definitive battle a resist of +10 is no defence and next to no affinity.
OWN = {SPEED: {5}, ELEMENT: set(range(8, 15)), TANK: {2, 3}}


def trait(kind, it):
    """0: it has a bonus of its archetype's; 1: none either way; 2: a positive bonus in a stat (1..6) of another."""
    own = [s for s, x in zip(it["stats"], it["values"]) if s and x > 0]
    if any(s in OWN[kind] for s in own):
        return 0
    return 2 if any(s <= 6 for s in own) else 1


def tiers(its):
    """[(rank, goods list, ids, shops)] of the distinct goods lists with weapons or armour, cheapest first."""
    lists, shops = goods()
    seen, out = {}, []
    for n, ids in lists:
        eq = tuple(i for i in ids if its.get(i, {}).get("cat") in ("RECORDS_ITEM_WEAPON", "RECORDS_ITEM_ARMOR"))
        if not eq:
            continue
        if eq in seen:
            seen[eq][1].extend(shops[n])
            continue
        seen[eq] = (n, list(shops[n]))
        out.append(eq)
    out.sort(key=lambda eq: (max(its[i]["price"] for i in eq), seen[eq][0]))
    return [(r, seen[eq][0], eq, sorted(seen[eq][1])) for r, eq in enumerate(out)]


def build(kind, base, peers, c, family):
    """The variant of `base` against the plain items of its slot in the tier that share a wearer (`peers`, base included):
    the new main, hit and two bonus (stat, value) pairs."""
    budget = max(score(c, p["vec"]) for p in peers)
    best = max(p["main"] for p in peers)
    hit = base["hit"]
    wh = c["DEF_EQUIP_W_HIT"] * hit
    if kind == SPEED:
        main = best * (100 - c["DEF_EQUIP_SPEED_COST"]) // 100
        boost = (budget - 100 * main - wh) // c["DEF_EQUIP_W_BOOST"]
        bonus = [(5, max(1, boost)), (0, 0)]
    elif kind == ELEMENT:
        main = best * (100 - c["DEF_EQUIP_ELEM_COST"]) // 100
        room = budget - c["DEF_EQUIP_W_GUARD"] * main - wh
        fam = FAMILIES[family]
        per = room // (len(fam) * weight(c, STAT[fam[0]]))
        per = max(1, min(per, c["DEF_EQUIP_ELEM_MAX"]))
        bonus = [(e, per) for e in fam] + [(0, 0)] * (2 - len(fam))
    else:
        main = best * (100 + c["DEF_EQUIP_TANK_GUARD"]) // 100
        spirit = max(1, best * c["DEF_EQUIP_TANK_SPIRIT"] // 100)
        over = c["DEF_EQUIP_W_GUARD"] * main + wh + c["DEF_EQUIP_W_SPIRIT"] * spirit - budget
        slow = max(1, -(-over // c["DEF_EQUIP_W_BOOST"]))
        bonus = [(3, spirit), (5, -slow)]
    return dict(main=main, hit=hit, bonus=bonus, budget=budget, best=best)


def variant_vec(base, v, effects):
    it = dict(base, main=v["main"], hit=v["hit"], stats=[s for s, _ in v["bonus"]], values=[x for _, x in v["bonus"]])
    return vec(it, effects)


def wears(a, b):
    return a["members"] & b["members"]


def plan():
    c, its = constants(), items()
    effects, sets = id_effects(), set_items()
    towns = shop_towns()
    for it in its.values():
        if "weapon" in it:
            it["vec"] = vec(it, effects)
    rows, used = [], [0] * len(FAMILIES)
    for rank, n, ids, shops in tiers(its):
        eq = [its[i] for i in ids]
        taken = set()
        for kind in (SPEED, ELEMENT, TANK):
            cands = [it for it in eq if candidate(kind, it) and it["id"] not in effects and it["strong"] < 2
                     and it["id"] not in sets and it["id"] not in taken]
            if not cands:
                continue

            def others(it, wearer=None):
                return [o for o in eq if o["id"] != it["id"] and slot_key(o) == slot_key(it) and o["id"] not in taken
                        and (o["members"] >> wearer) & 1]

            def dominated(it):
                return any(slot_key(o) == slot_key(it) and o["id"] != it["id"] and o["price"] <= it["price"]
                           and o["members"] & it["members"] == it["members"] and ge(o["vec"], it["vec"]) for o in eq)

            def redundancy(it):
                r = []
                for w in range(8):
                    if (it["members"] >> w) & 1:
                        alt = [o["main"] for o in others(it, w)]
                        r.append(max(alt) / max(1, it["main"]) if alt else 0.0)
                return min(r) if r else 0.0

            pick = min(cands, key=lambda it: (not dominated(it), trait(kind, it), -redundancy(it), it["price"], it["id"]))
            peers = [o for o in eq if slot_key(o) == slot_key(pick) and wears(o, pick) and o["id"] not in taken]
            family = 0
            if kind == ELEMENT:
                own = [s for s in pick["stats"] if 8 <= s <= 14]
                if own:
                    family = next(f for f, fam in enumerate(FAMILIES) if own[0] in fam)
                else:
                    family = min(range(len(FAMILIES)), key=lambda f: (used[f], f))
                used[family] += 1
            v = build(kind, pick, peers, c, family)
            v.update(kind=kind, base=pick, rank=rank, goods=n, shops=shops, family=family, dominated=dominated(pick),
                     redundancy=redundancy(pick), peers=[p["id"] for p in peers],
                     towns=sorted({m for s in shops for m in towns.get(s, ())}))
            taken.add(pick["id"])
            rows.append(v)
    return rows, its, c, effects


def render(rows):
    lines = ["/* Generated by tools/gen_equipment.py (PRD 4 layer F): the equipment variants, existing shop items converted in",
             " * place (definitive_variants.c writes them into the items' data when def_on). Per row: the item's ID, its",
             " * archetype, 1 for a weapon (power, accuracy) or 0 for armour (guard, evasion), the new main stat and hit, and",
             " * the two bonus stats and values (RecordsBonusStat). Regenerate instead of editing. */",
             '#include "records.h"', "", '#include "definitive.h"', "", "const DefEquipVariant def_equip_variants[] = {"]
    for v in sorted(rows, key=lambda v: v["base"]["id"]):
        b = v["base"]
        st = ", ".join(ENUM.get(s, "0") if s else "0" for s, _ in v["bonus"])
        vals = ", ".join(str(x) for _, x in v["bonus"])
        lines.append(f"    {{ {b['id']}, {KINDS[v['kind']]}, {b['weapon']}, {v['main']}, {v['hit']}, {{ {st} }}, {{ {vals} }} }},"
                     f" /* {b['name']} */")
    lines += ["};", f"const int def_equip_variant_count = {len(rows)};", ""]
    return "\n".join(lines)


def describe(it_vec):
    keys = ["power", "guard", "accuracy", "evasion"] + [STAT[k] for k in sorted(STAT)] + ["effect", "strong"]
    return ", ".join(f"{k} {it_vec[k]:+d}" if k in STAT.values() else f"{k} {it_vec[k]}" for k in keys
                     if it_vec.get(k, 0) and k != "charisma")


def checks(rows, its, c, effects):
    """[(ok, text)] of the table's rules."""
    out = []
    tx = texts()
    names = [it["name"] for it in its.values() if it["cat"] in ("RECORDS_ITEM_WEAPON", "RECORDS_ITEM_ARMOR",
                                                                 "RECORDS_ITEM_ACCESSORY")]
    longest = max(len(n) for n in names)
    ids = {v["base"]["id"] for v in rows}
    vecs = {v["base"]["id"]: variant_vec(v["base"], v, effects) for v in rows}
    for v in rows:
        b = v["base"]
        i = b["id"]
        it = its.get(i)
        out.append((it is not None and it["cat"] == ("RECORDS_ITEM_WEAPON" if b["weapon"] else "RECORDS_ITEM_ARMOR")
                    and (2 <= it["type"] <= 14 if b["weapon"] else 15 <= it["type"] <= 20),
                    f"{i} {b['name']}: shop equipment of the row's category (type {it and it['type']})"))
        out.append((all(s in STAT and s != 6 or s == 0 for s, _ in v["bonus"]) and all(-999 <= x <= 999 for _, x in v["bonus"])
                    and 0 <= v["main"] <= 999 and 0 <= v["hit"] <= 255, f"{i}: fields in range"))
        sv = score(c, vecs[i])
        out.append((sv <= v["budget"], f"{i}: score {sv / 100:.2f} within the tier's budget {v['budget'] / 100:.2f}"))
        # not a straight upgrade of any other item of its tier sharing its slot and a wearer (plain or variant)
        eq = [its[x] for x in next(t[2] for t in tiers(its) if t[1] == v["goods"])]
        for o in eq:
            if o["id"] == i or slot_key(o) != slot_key(b) or not wears(o, b):
                continue
            ov = vecs.get(o["id"], o["vec"])
            if ge(vecs[i], ov):
                out.append((False, f"{i}: a straight upgrade of {o['id']} {o['name']}"))
        name, desc = tx.get(("ITMNAM", i)), tx.get(("ITMINF", i))
        out.append((name is not None and desc is not None, f"{i}: ITMNAM and ITMINF lines in en.txt"))
        if name is not None:
            out.append((len(name) <= longest and TEXT_OK.match(name) is not None,
                        f"{i}: name {name!r} ({len(name)} <= {longest} characters, plain glyphs)"))
        if desc is not None:
            out.append((len(desc) <= DESC_MAX and TEXT_OK.match(desc) is not None,
                        f"{i}: description {desc!r} ({len(desc)} <= {DESC_MAX})"))
    extra = sorted(k for k in tx if k[1] not in ids)
    out.append((not extra, f"no ITMNAM/ITMINF line for an item that is not a variant {extra}"))
    per = defaultdict(set)
    for v in rows:
        per[v["goods"]].add(v["kind"])
    out.append((all(len(k) == 3 for k in per.values()), "every tier has the three archetypes"))
    return out


def all_dominated(its):
    """The IDs of the shop weapons and armour strictly dominated in a tier of theirs (plan()'s test)."""
    out = set()
    for _rank, _n, ids, _shops in tiers(its):
        eq = [its[i] for i in ids]
        for it in eq:
            if any(slot_key(o) == slot_key(it) and o["id"] != it["id"] and o["price"] <= it["price"]
                   and o["members"] & it["members"] == it["members"] and ge(o["vec"], it["vec"]) for o in eq):
                out.add(it["id"])
    return out


def write_md(rows, its, c, effects):
    OUT.mkdir(parents=True, exist_ok=True)
    tx = texts()
    lines = ["# Variações de equipamento (PRD 4 camada F), aplicadas", "",
             "Sem IDs livres: cada variação é um item de loja convertido no lugar (mesmo ID, ícone, slot, quem usa, carisma, "
             "preço e receita da forja). Por tier de loja, um de cada arquétipo: **Speed** (arma de uma mão: troca poder por "
             "Boost), **Elemental** (cabeça: troca defesa pelas resistências de uma família, que viram afinidade) e **Tank** "
             "(corpo ou escudo: acima da melhor defesa do tier, com Espírito, pago em Boost).", "",
             f"Orçamento: cada stat vale, em pontos de Power: Guard {c['DEF_EQUIP_W_GUARD'] / 100}, Spirit "
             f"{c['DEF_EQUIP_W_SPIRIT'] / 100}, Wisdom {c['DEF_EQUIP_W_WISDOM'] / 100}, Boost {c['DEF_EQUIP_W_BOOST'] / 100}, "
             f"precisão/evasão {c['DEF_EQUIP_W_HIT'] / 100}, resistência {c['DEF_EQUIP_W_ELEMENT'] / 100} (Trevas o dobro). "
             "O orçamento de uma variação é a maior pontuação entre os itens comuns do tier no mesmo slot que dividem um "
             "usuário com ela; B é a maior defesa/poder deles. Speed: poder B menos "
             f"{c['DEF_EQUIP_SPEED_COST']}%, Boost completa o orçamento. Elemental: defesa B menos {c['DEF_EQUIP_ELEM_COST']}%, "
             f"a família completa (máx. {c['DEF_EQUIP_ELEM_MAX']} por elemento). Tank: defesa B mais {c['DEF_EQUIP_TANK_GUARD']}%, "
             f"Espírito {c['DEF_EQUIP_TANK_SPIRIT']}% de B, Boost negativo volta ao orçamento.", "",
             f"Escolha: estritamente dominado no tier primeiro (entre as armas e armaduras de loja: "
             f"{', '.join(sorted({its[i]['name'] for i in all_dominated(its)}))}), depois o traço (um item que "
             "já puxa para o arquétipo antes, um com bônus positivo de Power..Charisma de outro arquétipo por último: o "
             "Kung Fu Cap de velocidade e o Headband de Espírito ficam), depois a redundância (r: o pior caso, entre quem pode "
             "usar, da melhor alternativa do mesmo slot no tier sobre este; r >= 1: ninguém perde uma opção comum), depois o "
             "mais barato. Fora: itens com efeito por ID no FIGHTSTG, com tipo forte, e os de conjunto (a loja 18 inteira).",
             "", "Calibração do Speed: na regra definitiva o jogador abre e o primeiro turno do inimigo vem depois do atraso "
             "inteiro dele, 1000 * raiz(Boost do jogador / Boost do inimigo), entre 707 e 1414; o nosso próximo é o inverso. "
             "Então quem tem mais Boost que o inimigo age duas vezes antes do primeiro turno dele, e no longo prazo faz "
             "Boost / Boost do inimigo ações por ação dele (até 2). O Boost dos Speed fica em 15% a 25% do Boost esperado de "
             "um rookie no nível em que a loja aparece (tabelas de level-up: cerca de 65 no nível 3, 150 no 15, 260 no 35, "
             "325 no 50), o bastante para virar uma luta equilibrada; a medição é do script do lab (equip_ingame.py).", "",
             "| tier | lojas (mapas) | arquétipo | ID | item | quem usa | antes | depois | nome novo | r | orçamento |",
             "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for v in rows:
        b = v["base"]
        after = variant_vec(b, v, effects)
        towns = " ".join(hex(m) for m in v["towns"])
        who = "todos" if b["members"] == 0xFF else " ".join(n for k, n in enumerate(ROOKIES) if (b["members"] >> k) & 1)
        lines.append(f"| {v['rank'] + 1} | {' '.join(map(str, v['shops']))} ({towns}) | {KIND_PT[v['kind']]} | {b['id']} | "
                     f"{b['name']} ({b['price']}) | {who} | {describe(b['vec'])} | {describe(after)} | "
                     f"{tx.get(('ITMNAM', b['id']), '?')} | {v['redundancy']:.2f}{' dominado' if v['dominated'] else ''} | "
                     f"{score(c, after) / 100:.1f} / {v['budget'] / 100:.1f} |")
    lines += ["", "Descrições (ITMINF):", ""]
    for v in sorted(rows, key=lambda v: v["base"]["id"]):
        i = v["base"]["id"]
        lines.append(f"- {i} {tx.get(('ITMNAM', i), '?')}: {tx.get(('ITMINF', i), '?')}")
    (OUT / "variantes.md").write_text("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    rows, its, c, effects = plan()
    text = render(rows)
    if a.check:
        ok = GEN.exists() and GEN.read_text() == text
        res = checks(rows, its, c, effects)
        bad = [t for good, t in res if not good]
        for t in bad:
            print("FAIL", t)
        print(f"{'ok' if ok else 'FAIL'} the table is the rule's ({len(rows)} variants); "
              f"{'ok' if not bad else 'FAIL'} {len(res) - len(bad)}/{len(res)} checks (budget, no straight upgrade, texts)")
        return 0 if ok and not bad else 1
    GEN.write_text(text)
    write_md(rows, its, c, effects)
    bad = [t for good, t in checks(rows, its, c, effects) if not good]
    for t in bad:
        print("FAIL", t)
    print(f"{GEN.relative_to(ROOT)}: {len(rows)} variants; {OUT / 'variantes.md'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
