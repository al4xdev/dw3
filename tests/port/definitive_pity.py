#!/usr/bin/env python3
"""Definitive progression layer E (training pity): pure counter/guarantee rules compiled alone from
port/game/definitive_progression.c with tests/port/definitive_pity_driver.c, including DEF_TRAIN_PITY_AFTER=0
disable check. Needs only host gcc, no disc."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_pity"


def run_driver(extra_defs, tag):
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / f"definitive_pity_driver_{tag}"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "port/game"),
          *extra_defs,
          str(ROOT / "port/game/definitive_progression.c"),
          str(ROOT / "tests/port/definitive_pity_driver.c"), "-o", str(exe)]
    res = subprocess.run(cc, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"definitive_pity ({tag}): FAIL to compile:\n{res.stderr}")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    if run.returncode != 0:
        print(f"definitive_pity ({tag}): FAIL (exit {run.returncode})")
        return 1
    return 0


def main():
    rc = run_driver([], "default")
    if rc != 0:
        print("definitive_pity: FAIL")
        return 1
    # Also verify DEF_TRAIN_PITY_AFTER=0 keeps count and guaranteed at 0 (no crash or division).
    off_src = OUT / "off_check.c"
    off_src.write_text(
        '#include <assert.h>\n'
        '#include "definitive.h"\n'
        'int main(void) {\n'
        '    def_train_pity_reset();\n'
        '    def_train_pity_record(0);\n'
        '    def_train_pity_record(0);\n'
        '    def_train_pity_record(0);\n'
        '    assert(def_train_pity_count() == 0);\n'
        '    assert(def_train_pity_guaranteed() == 0);\n'
        '    return 0;\n'
        '}\n'
    )
    off_exe = OUT / "off_check"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-DDEF_TRAIN_PITY_AFTER=0",
          "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_progression.c"), str(off_src), "-o", str(off_exe)]
    if subprocess.run(cc).returncode != 0 or subprocess.run([str(off_exe)]).returncode != 0:
        print("definitive_pity (off): FAIL")
        return 1
    print("ok DEF_TRAIN_PITY_AFTER=0 disables pity")
    print("definitive_pity: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
