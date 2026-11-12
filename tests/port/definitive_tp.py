#!/usr/bin/env python3
"""Definitive progression layer D (TP): port/game/definitive_progression.c's TP table and tools/convert_save_mp.py
conversion, idempotency, --dry-run warning, and binary diff. Needs only the host gcc, no disc."""
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/port-test/definitive_tp"

sys.path.insert(0, str(ROOT / "tools"))
import convert_save_mp as conv


def make_synthetic_card():
    buf = bytearray(conv.CARD_SIZE)
    buf[0:2] = b"MC"
    # Directory blocks 1..4 for DW3 save
    for idx, blk in enumerate(range(1, 5)):
        d = bytearray(0x80)
        d[0] = 0x51
        if idx == 0:
            d[0x0A:0x0A + 20] = b"BESLES-03936DMW3-EUR"
        nxt = blk if idx < 3 else 0xFFFF
        struct.pack_into("<H", d, 8, nxt)
        buf[blk * 0x80:(blk + 1) * 0x80] = d

    def init_slot(slot_idx, entries, marker=b"\0\0\0\0"):
        off = 1 * conv.BLOCK + conv.SLOT_HDR + slot_idx * conv.SLOT_STRIDE
        buf[off + 2] = 4
        struct.pack_into("<3i", buf, off + 0x70, 0, -1, -1)
        for d, level, tp, mp, mmp in entries:
            base = 0x75C + d * 0x3DC
            struct.pack_into("<i", buf, off + base + 4, d + 3)
            o = off + base + 0xC + 0x1C
            struct.pack_into("<6h", buf, o, level, tp, 500, 500, mp, mmp)
        buf[off + conv.MARKER_OFF:off + conv.MARKER_OFF + 4] = marker
        buf[off] = conv.xor_sum(buf[off + 4:off + conv.SLOT_DATA])

    # Slot 0: fresh save with lv 1, lv 20, lv 40, lv 50, lv 70
    init_slot(0, [
        (0, 1, 0, 100, 100),     # Kotemon lv 1 -> +0 TP (0), MP +40 -> 140
        (1, 20, 10, 580, 580),   # Kumamon lv 20 -> +38 TP -> 48
        (2, 40, 20, 1835, 1835), # Monmon lv 40 -> +118 TP -> 99 (lost 39)
        (3, 50, 0, 1000, 1000),  # Agumon lv 50 -> +368 TP -> 99 (lost 269)
        (4, 70, 15, 3600, 3600), # Veemon lv 70 -> +368 TP -> 99 (lost 284)
    ])
    # Slot 1: already MPV1 -> only TP should be updated, MP untouched
    init_slot(1, [
        (0, 20, 5, 1223, 1223),  # Kotemon lv 20 -> +38 TP -> 43, MP stays 1223
        (1, 30, 50, 1800, 1800), # Kumamon lv 30 -> total(30)=223, 5*29=145, delta=78 -> 99 (lost 29)
    ], marker=b"MPV1")
    # Slot 2: left empty (version 0)
    return bytes(buf)


def allowed_diff_offsets(orig):
    allowed = set()
    blk = 1
    for slot_idx, do_mp in ((0, True), (1, False)):
        off = blk * conv.BLOCK + conv.SLOT_HDR + slot_idx * conv.SLOT_STRIDE
        allowed.add(off)  # checksum byte
        allowed.update(range(off + conv.MARKER_OFF, off + conv.MARKER_OFF + 4))
        for d in range(8):
            base = 0x75C + d * 0x3DC
            joined = struct.unpack_from("<i", orig, off + base + 4)[0]
            if not joined:
                continue
            o = off + base + 0xC + 0x1C
            allowed.update(range(o + 2, o + 4))  # TP (s16)
            if do_mp and d in conv.MP_TABLE:
                allowed.update(range(o + 8, o + 12))  # MP, max_MP
    return allowed


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / "definitive_tp_driver"
    cc = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "port/game"),
          str(ROOT / "port/game/definitive_progression.c"), str(ROOT / "tests/port/definitive_tp_driver.c"),
          "-o", str(exe)]
    if subprocess.run(cc).returncode != 0:
        print("definitive_tp: FAIL: does not compile")
        return 1
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end="")
    bad = sum(1 for l in run.stdout.splitlines() if l.startswith("FAIL"))
    if run.returncode:
        bad += 1

    # Check definitive.h constants match convert_save_mp.py
    h = (ROOT / "port/game/definitive.h").read_text()
    pairs = [
        ("DEF_TP_GAIN_LV_2_20", conv.TP_GAIN_LV_2_20),
        ("DEF_TP_GAIN_LV_21_40", conv.TP_GAIN_LV_21_40),
        ("DEF_TP_GAIN_LV_41_50", conv.TP_GAIN_LV_41_50),
        ("DEF_TP_GAIN_LV_51_UP", conv.TP_GAIN_LV_51_UP),
        ("DEF_TP_CAP", conv.TP_CAP),
    ]
    for name, val in pairs:
        m = re.search(rf"#define\s+{name}\s+(\d+)", h)
        ok = m is not None and int(m.group(1)) == val
        bad += not ok
        print(f"{'ok' if ok else 'FAIL'} {name} == convert_save_mp ({val})")

    # Synthetic card conversion test
    for p in OUT.glob("card*.mcd*"):
        p.unlink()
    card = OUT / "card.mcd"
    orig = make_synthetic_card()
    card.write_bytes(orig)

    dry = subprocess.run([sys.executable, str(ROOT / "tools/convert_save_mp.py"), "--dry-run", str(card)],
                         capture_output=True, text=True)
    ok_dry = dry.returncode == 0 and "WARNING:" in dry.stdout and card.read_bytes() == orig
    bad += not ok_dry
    print(f"{'ok' if ok_dry else 'FAIL'} convert_save_mp --dry-run warns and leaves file untouched")

    conv_run = subprocess.run([sys.executable, str(ROOT / "tools/convert_save_mp.py"), str(card)],
                              capture_output=True, text=True)
    after = card.read_bytes()
    bak = OUT / "card.mcd.bak"
    ok_bak = conv_run.returncode == 0 and bak.exists() and bak.read_bytes() == orig
    bad += not ok_bak
    print(f"{'ok' if ok_bak else 'FAIL'} convert_save_mp creates backup")

    diff_idxs = {i for i in range(len(orig)) if orig[i] != after[i]}
    allowed = allowed_diff_offsets(orig)
    unexpected = diff_idxs - allowed
    ok_diff = len(unexpected) == 0 and len(diff_idxs) > 0
    bad += not ok_diff
    print(f"{'ok' if ok_diff else 'FAIL'} binary diff only in expected fields (unexpected={len(unexpected)})")

    # Verify exact resulting TP / MP / markers / checksums
    def read_dig(slot_idx, d):
        off = 1 * conv.BLOCK + conv.SLOT_HDR + slot_idx * conv.SLOT_STRIDE
        o = off + 0x75C + d * 0x3DC + 0xC + 0x1C
        return struct.unpack_from("<6h", after, o)

    checks = [
        ("slot0 d0 tp=0, mp=140", read_dig(0, 0)[1] == 0 and read_dig(0, 0)[4:6] == (140, 140)),
        ("slot0 d1 tp=48", read_dig(0, 1)[1] == 48),
        ("slot0 d2 tp=99", read_dig(0, 2)[1] == 99),
        ("slot0 d3 tp=99", read_dig(0, 3)[1] == 99),
        ("slot0 d4 tp=99", read_dig(0, 4)[1] == 99),
        ("slot1 d0 tp=43, mp=1223", read_dig(1, 0)[1] == 43 and read_dig(1, 0)[4:6] == (1223, 1223)),
        ("slot1 d1 tp=99", read_dig(1, 1)[1] == 99),
    ]
    for slot_idx in (0, 1):
        off = 1 * conv.BLOCK + conv.SLOT_HDR + slot_idx * conv.SLOT_STRIDE
        checks.append((f"slot{slot_idx} marker DFV2", after[off + conv.MARKER_OFF:off + conv.MARKER_OFF + 4] == b"DFV2"))
        checks.append((f"slot{slot_idx} checksum", conv.xor_sum(after[off + 4:off + conv.SLOT_DATA]) == after[off]))
    for name, ok in checks:
        bad += not ok
        print(f"{'ok' if ok else 'FAIL'} {name}")

    # Idempotency check
    idem = subprocess.run([sys.executable, str(ROOT / "tools/convert_save_mp.py"), str(card)],
                          capture_output=True, text=True)
    ok_idem = idem.returncode == 0 and card.read_bytes() == after and not (OUT / "card.mcd.bak2").exists()
    bad += not ok_idem
    print(f"{'ok' if ok_idem else 'FAIL'} convert_save_mp idempotent on second run")

    print("definitive_tp: " + ("FAIL" if bad else "pass"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
