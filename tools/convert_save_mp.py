#!/usr/bin/env python3
"""Rescale max MP/MP and grant retroactive TP for old DW3 saves (raw PS1 .mcd card).

Usage: convert_save_mp.py [--dry-run] card.mcd
"""
import argparse
import os
import struct
import sys

# digimon index -> (old initial MP, old gain/level, new initial MP, new gain/level). Edit here.
MP_TABLE = {
    0: (100, 55, 140, 57),  # Kotemon
    1: (10, 30, 140, 57),   # Kumamon
    2: (80, 45, 140, 57),   # Monmon
    4: (150, 50, 150, 57),  # Veemon
    5: (130, 60, 140, 60),  # Guilmon
    # 3 Agumon, 6 Renamon, 7 Patamon: unchanged
}
NAMES = ["Kotemon", "Kumamon", "Monmon", "Agumon", "Veemon", "Guilmon", "Renamon", "Patamon"]

# TP per level band (matches port/game/definitive.h / definitive_progression.c).
TP_GAIN_LV_2_20 = 7
TP_GAIN_LV_21_40 = 9
TP_GAIN_LV_41_50 = 30
TP_GAIN_LV_51_UP = 0
TP_CAP = 99

MP_CAP = 9999
CARD_SIZE = 0x20000
BLOCK = 0x2000
SAVE_NAMES = {b"BESLES-03936DMW3-EUR", b"BASLUS-01436DMW3-USA", b"BISLPS-03446DMW3-JPN"}
SAVE_BLOCKS = 4
SLOT_DATA = 0x26C4
SLOT_STRIDE = 0x2700
SLOT_HDR = 0x300          # slots start this far into the file
MARKER_MP = b"MPV1"       # legacy: MP only
MARKER = b"DFV2"          # current: MP + retroactive TP
MARKER_OFF = 0x26EC       # tail of the slot part: outside the checksum, not used by the game


def tp_gain(level):
    if level <= 1:
        return 0
    if level <= 20:
        return TP_GAIN_LV_2_20
    if level <= 40:
        return TP_GAIN_LV_21_40
    if level <= 50:
        return TP_GAIN_LV_41_50
    return TP_GAIN_LV_51_UP


def tp_total(level):
    return sum(tp_gain(lv) for lv in range(2, level + 1))


def retroactive_tp(level, tp):
    lv = min(max(level, 1), 50)
    delta = max(0, tp_total(lv) - 5 * (lv - 1))
    raw = tp + delta
    ntp = max(0, min(TP_CAP, raw))
    lost = max(0, raw - TP_CAP)
    return ntp, lost


def find_save_block(data):
    """Return the first card block of the game save, verified against the directory."""
    if data[:2] != b"MC":
        sys.exit("error: not a raw memory card (missing 'MC' header)")
    found = []
    for i in range(1, 16):
        e = data[i * 0x80:(i + 1) * 0x80]
        if e[0] != 0x51 or e[0x0A:0x1E].rstrip(b"\0") not in SAVE_NAMES:
            continue
        # chain: next (+0x08) is a 0-based block number, i.e. directory index - 1
        chain, cur = [i], i
        while True:
            nxt = struct.unpack_from("<H", data, cur * 0x80 + 8)[0]
            if nxt == 0xFFFF:
                break
            cur = nxt + 1
            if not 1 <= cur <= 15 or cur in chain or len(chain) > 15:
                sys.exit(f"error: bad directory chain for save at block {i}")
            chain.append(cur)
        if chain != list(range(i, i + SAVE_BLOCKS)):
            sys.exit(f"error: save at block {i} is not {SAVE_BLOCKS} contiguous blocks (chain {chain})")
        found.append(i)
    if not found:
        sys.exit("error: no DW3 save found in the card directory")
    if len(found) > 1:
        sys.exit(f"error: several DW3 saves in the directory (blocks {found}); refusing to guess")
    return found[0]


def xor_sum(buf):
    x = 0
    for b in buf:
        x ^= b
    return x


def convert_slot(data, off, n, dry):
    """Convert one slot in place (data is a bytearray). Returns True if it changed."""
    slot = data[off:off + SLOT_DATA]
    if slot[2] != 4:
        print(f"slot {n}: version {slot[2]}, empty/invalid - skipped")
        return False
    marker = data[off + MARKER_OFF:off + MARKER_OFF + 4]
    if marker == MARKER:
        print(f"slot {n}: already converted ({MARKER.decode()}) - skipped")
        return False
    if xor_sum(slot[4:]) != slot[0]:
        print(f"slot {n}: bad checksum - skipped")
        return False
    do_mp = marker != MARKER_MP
    party = struct.unpack_from("<3i", slot, 0x70)
    note = " (upgrading MPV1 -> DFV2: MP kept, adding TP)" if not do_mp else ""
    print(f"slot {n}:{note}")
    print("  idx name     lvl   mp/max_mp (old)  ->  mp/max_mp (new)    tp (old -> new)")
    for d in range(8):
        base = 0x75C + d * 0x3DC
        joined = struct.unpack_from("<i", slot, base + 4)[0]
        if not joined and d not in party:
            continue
        o = off + base + 0xC + 0x1C
        level, tp, _hp, _mhp, mp, mmp = struct.unpack_from("<6h", data, o)
        if do_mp and d in MP_TABLE:
            oi, og, ni, ng = MP_TABLE[d]
            delta = (ni - oi) + (ng - og) * (level - 1)
            nmmp = max(0, min(MP_CAP, mmp + delta))
            nmp = min(max(0, min(MP_CAP, mp + delta)), nmmp)
        else:
            nmp, nmmp = mp, mmp
        ntp, lost = retroactive_tp(level, tp)
        warn = f"  WARNING: {lost} TP lost to cap ({TP_CAP})" if lost > 0 else ""
        print(f"  {d:<3} {NAMES[d]:<8} {level:>3}   {mp:>5}/{mmp:<5}      ->  {nmp:>5}/{nmmp:<5}      {tp:>2} -> {ntp:>2}{warn}")
        struct.pack_into("<h", data, o + 2, ntp)
        if do_mp:
            struct.pack_into("<h", data, o + 8, nmp)
            struct.pack_into("<h", data, o + 10, nmmp)
    data[off + MARKER_OFF:off + MARKER_OFF + 4] = MARKER
    data[off] = xor_sum(data[off + 4:off + SLOT_DATA])
    return True


def backup_path(path):
    p, k = path + ".bak", 2
    while os.path.exists(p):
        p, k = f"{path}.bak{k}", k + 1
    return p


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("card")
    ap.add_argument("--dry-run", action="store_true", help="show before/after, write nothing")
    args = ap.parse_args()

    with open(args.card, "rb") as f:
        orig = f.read()
    if len(orig) != CARD_SIZE:
        sys.exit(f"error: expected {CARD_SIZE} bytes, got {len(orig)}")
    blk = find_save_block(orig)
    print(f"save found at block {blk} (file offset 0x{blk * BLOCK:X})")

    data = bytearray(orig)
    changed = [convert_slot(data, blk * BLOCK + SLOT_HDR + n * SLOT_STRIDE, n, args.dry_run)
               for n in range(3)]
    if not any(changed):
        print("nothing to write")
        return
    if args.dry_run:
        print("dry run: nothing written")
        return
    bak = backup_path(args.card)
    with open(bak, "wb") as f:
        f.write(orig)
    with open(args.card, "wb") as f:
        f.write(data)
    print(f"backup: {bak}\nwritten: {args.card}")


if __name__ == "__main__":
    main()
