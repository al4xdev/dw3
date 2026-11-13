#!/usr/bin/env python3
"""PRD 4 layer H (the DRI Agents' Gauntlet): port/game/definitive_dri.c compiled with definitive_bestiary.c against a
driver with stand-in challenges and agents: the stage-to-tier rule, the agents' match, the unlock (once, paid exactly,
never again), the rewards (each tier once, the best stage only rises), the second card record's bytes 152..157 written
and read back (an older record as nothing unlocked) and a replay script (nothing). Then the generated table checked
against its rule (tools/gen_dri_challenges.py --check) and against the game's data: every stage an enemy species, every
battle and reward an existing one, every unlock's material and rare item in range. Needs only the host gcc."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_dri"


def table_ok():
    gen = (ROOT / "port/game/definitive_dri_table.c").read_text()
    h = (ROOT / "port/game/definitive.h").read_text()
    cap = int(re.search(r"#define DEF_DRI_CHALLENGES (\d+)", h).group(1))
    num = r"(-?\d+)"
    stage = r"\{ " + ", ".join([num] * 5) + r" \}"
    rows = re.findall(r"\{ \{ " + stage + ", " + stage + ", " + stage + r" \}, (\d+), \{ (\d+), (\d+), (\d+) \}, "
                      r"(-?\d+), (\d+), \{ (-?\d+), (-?\d+) \}, \{ (\d+), (\d+) \} \},", gen)
    count = int(re.search(r"def_dri_challenge_count = (\d+);", gen).group(1))
    species = {int(x) for x in re.findall(r"\{ (\d+), \{[^}]*\}, [01] \}",
                                          (ROOT / "port/game/definitive_enemies.c").read_text())}
    battles = int(re.search(r"FieldstgBattle fieldstg_battles\[(\d+)\]",
                            (ROOT / "src/fieldstg/fieldstg_80083784.c").read_text()).group(1))
    targets = int(re.search(r"def_hunt_target_count = (\d+);",
                            (ROOT / "port/game/definitive_hunt_targets.c").read_text()).group(1))
    acc = {int(i) for i in re.findall(r"RECORDS_ITEM_ACCESSORY, \d+, \{ 0, 0 \} \}, /\* (\d+) ",
                                      (ROOT / "src/main/records.c").read_text())}
    ok = len(rows) == count and 0 < count <= cap
    for r in rows:
        v = [int(x) for x in r]
        st = [v[i * 5:i * 5 + 5] for i in range(3)]
        battle, rewards, common, common_n, rare, rare_n = v[15], v[16:19], v[19], v[20], v[21:23], v[23:25]
        ok &= all(s[0] in species and 0 < s[1] <= 99 and 0 < s[2] <= 30000 and s[4] > 0 for s in st)
        ok &= st[0][1] < st[1][1] < st[2][1]
        ok &= 0 < battle < battles and all(x in acc for x in rewards) and rewards == sorted(rewards)
        ok &= (common == -1 and common_n == 0) or (0 <= common < 16 and 0 < common_n <= 99)
        ok &= all((x == -1 and k == 0) or (0 <= x < targets and 0 < k <= 99) for x, k in zip(rare, rare_n))
    print(f"{'ok' if ok else 'FAIL'} the generated table: {count} challenges (room for {cap}), enemy species climbing "
          f"in level, existing battles, accessories as rewards, materials and rare items in range")
    return ok


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_dri_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"), "-I", str(ROOT / "tests/host"),
          "-I", str(ROOT / "psxstack/include"), "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_dri.c"), str(ROOT / "port/game/definitive_bestiary.c"),
          str(ROOT / "port/game/definitive_glyph.c"), str(ROOT / "port/game/definitive_text.c"),
          str(ROOT / "tests/port/definitive_dri_driver.c"), "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_dri: FAIL: does not compile")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    bad = run.returncode != 0
    gen = subprocess.run([sys.executable, str(ROOT / "tools/gen_dri_challenges.py"), "--check"], capture_output=True,
                         text=True)
    print(gen.stdout, end="")
    bad |= gen.returncode != 0
    bad |= not table_ok()
    print("definitive_dri: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
