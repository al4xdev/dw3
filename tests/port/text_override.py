#!/usr/bin/env python3
"""The port's text overrides (definitive combat PRD, layer 1): port/game/definitive_text.c is what
tools/gen_text_override.py makes of tools/text_override/en.txt, and every description fits the window (34 characters,
the longest text of the disc). Needs the disc (the glyph table is read from the EXE)."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAX = 34


def main():
    py = ROOT / "tools/venv/bin/python"
    py = str(py) if py.exists() else sys.executable
    if subprocess.run([py, str(ROOT / "tools/gen_text_override.py"), "--check"]).returncode != 0:
        print("text_override: FAIL: port/game/definitive_text.c is not what tools/gen_text_override.py makes of "
              "tools/text_override/en.txt (run it)")
        return 1
    bad = 0
    for n, line in enumerate((ROOT / "tools/text_override/en.txt").read_text().splitlines(), 1):
        if line.strip() and not line.lstrip().startswith("#"):
            text = line.split(":", 1)[1].strip()
            if len(text) > MAX:
                print(f"text_override: en.txt:{n}: {len(text)} characters, the limit is {MAX}: {text}")
                bad += 1
    print("text_override: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
