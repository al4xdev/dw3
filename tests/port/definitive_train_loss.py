#!/usr/bin/env python3
"""Definitive progression layer I (resistance training without secondary stat loss):
checks DEF_TRAIN_NO_STAT_LOSS=1 (default) and =0 (disabled) from port/game/definitive_progression.c."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_train_loss"


def run_case(tag, extra_defs, expected):
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / f"definitive_train_loss_{tag}"
    cc = [
        "gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "port/game"),
        f"-DEXPECTED_ON={expected}",
        *extra_defs,
        str(ROOT / "port/game/definitive_progression.c"),
        str(ROOT / "tests/port/definitive_train_loss_driver.c"),
        "-o", str(exe),
    ]
    res = subprocess.run(cc, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"definitive_train_loss ({tag}): FAIL to compile:\n{res.stderr}")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    if run.returncode != 0:
        print(f"definitive_train_loss ({tag}): FAIL (exit {run.returncode})")
        return 1
    return 0


def main():
    if run_case("default", [], 1) != 0:
        print("definitive_train_loss: FAIL")
        return 1
    if run_case("off", ["-DDEF_TRAIN_NO_STAT_LOSS=0"], 0) != 0:
        print("definitive_train_loss: FAIL")
        return 1
    print("definitive_train_loss: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
