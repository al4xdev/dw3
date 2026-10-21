#!/usr/bin/env python3
"""Gamepad menu drawn on /dev/fb0. Exit 0 = play, 2 = quit."""
import mmap
import os
import struct
import sys

GAME = os.path.dirname(os.path.abspath(__file__))
STATE = os.path.join(GAME, "tui.state")
CFG = os.path.join(GAME, "settings.json")
FB = os.environ.get("DW3_FB", "/dev/fb0")
PAD = os.environ.get("DW3_PAD", "/dev/input/event0")

# linux/input.h on this handheld
K_A, K_B = 304, 305
K_SELECT, K_START = 314, 315
K_UP, K_DOWN = 544, 545

LANGS = ["english", "french", "german", "italian", "spanish", "usa", "japanese"]
PAD_FILE = os.path.join(GAME, "pad.map")
EVDEV = {
    304: "south", 305: "east", 307: "west", 308: "north",
    310: "leftshoulder", 311: "rightshoulder",
    312: "lefttrigger", 313: "righttrigger",
    314: "back", 315: "start",
    544: "dpup", 545: "dpdown", 546: "dpleft", 547: "dpright",
}
EVDEV_CODE = dict((name, code) for code, name in EVDEV.items())
ACTIONS = ["cross", "circle", "square", "triangle", "l1", "r1", "start", "select"]
ACTION_LABEL = {
    "cross": "CONFIRMA", "circle": "VOLTA", "square": "QUADRADO", "triangle": "TRIANGULO",
    "l1": "L1", "r1": "R1", "start": "START", "select": "SELECT",
}
LETTERS = "ABXYLRSE"
PRESETS = {
    "xbox": {
        "cross": ("south", "A"), "circle": ("east", "B"), "square": ("west", "X"),
        "triangle": ("north", "Y"), "l1": ("leftshoulder", "L"), "r1": ("rightshoulder", "R"),
        "start": ("start", "S"), "select": ("back", "E"),
    },
    "switch": {
        "cross": ("east", "A"), "circle": ("south", "B"), "square": ("north", "X"),
        "triangle": ("west", "Y"), "l1": ("leftshoulder", "L"), "r1": ("rightshoulder", "R"),
        "start": ("start", "S"), "select": ("back", "E"),
    },
    "modern": {
        "cross": ("east", "A"), "circle": ("north", "Y"), "square": ("west", "X"),
        "triangle": ("south", "B"), "l1": ("leftshoulder", "L"), "r1": ("rightshoulder", "R"),
        "start": ("start", "S"), "select": ("back", "E"),
    },
}

# 5x7, rows of bits. Enough for the menu.
GLYPH = {
    " ": ["00000"] * 7,
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01110"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    "J": ["00111", "00010", "00010", "00010", "10010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "10000", "11110", "00001", "00001", "11110"],
    "6": ["01110", "10000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00001", "01110"],
    "%": ["11001", "11010", "00010", "00100", "01000", "01011", "10011"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    ">": ["10000", "01000", "00100", "00010", "00100", "01000", "10000"],
}


def load_state():
    s = dict(hz=50, ff=0, ff_speed="4x", skip=0, battles=0, hit=1, saveany=0,
             party=0, share=50, xp=0, xp_mul=2, lang=0, lang_on=0)  # xp off, and the mildest step if it is turned on
    if os.path.isfile(STATE):
        for line in open(STATE):
            if "=" not in line:
                continue
            k, v = line.strip().split("=", 1)
            if k in ("ff_speed",):
                s[k] = v
            elif k in s:
                s[k] = int(v)
    return s


def save_state(s):
    with open(STATE, "w") as f:
        for k, v in s.items():
            f.write("%s=%s\n" % (k, v))


def find_disc():
    folder = os.path.join(GAME, "game")
    names = ["dw2003.cue", "dw2003.bin", "dw3.cue", "dw3.bin",
             "Digimon World 2003.cue", "Digimon World 2003.bin"]
    for name in names:
        path = os.path.join(folder, name)
        if os.path.isfile(path):
            return path
    if os.path.isdir(folder):
        for name in sorted(os.listdir(folder)):
            if name.endswith(".cue") or name.endswith(".bin"):
                return os.path.join(folder, name)
    return ""


def write_settings(s, disc):
    def b(v):
        return "true" if v else "false"
    lang = LANGS[s["lang"]] if 0 <= s["lang"] < len(LANGS) else "english"
    text = """{
  "schema": 1,
  "disc": { "path": "game/%s" },
  "video": { "window": true, "scale": 2, "fullscreen": true, "refresh": %d, "renderer": "gles" },
  "audio": { "mute": false },
  "memcard1": "card1.mcd",
  "memcard2": "card2.mcd",
  "watchdog": 0,
  "mods": {
    "fast_forward": { "enabled": %s, "speed": "%s", "mute": true, "hold": "", "toggle": "" },
    "skip_dialogues": { "enabled": %s, "fast_forward_waits": false },
    "battle_animations": { "enabled": %s, "hit_reaction": %s },
    "global_save": { "enabled": %s, "restore_map_state": true },
    "party_xp": { "enabled": %s, "share": %d, "knocked_out": false, "catch_up": false },
    "xp_boost": { "enabled": %s, "exp": %d, "form_exp": %d, "bits": %d, "manual": false },
    "preset_language": { "enabled": %s, "language": "%s" },
    "widescreen": { "enabled": false }
  },
  "input": { "gamepad": %s }
}
""" % (os.path.basename(disc), s["hz"], b(s["ff"]), s["ff_speed"], b(s["skip"]),
       b(s["battles"]), b(s["hit"]), b(s["saveany"]), b(s["party"]), s["share"],
       b(s["xp"]), s["xp_mul"], s["xp_mul"], s["xp_mul"], b(s["lang_on"]), lang,
       gamepad_json(load_pad()))
    with open(CFG, "w") as f:
        f.write(text)


def default_pad():
    return dict((k, v) for k, v in PRESETS["xbox"].items())


def load_pad():
    pad = default_pad()
    if not os.path.isfile(PAD_FILE):
        return pad
    for line in open(PAD_FILE):
        parts = line.split()
        if len(parts) == 3 and parts[0] in pad and parts[2] in LETTERS:
            pad[parts[0]] = (parts[1], parts[2])
    # A file written when a button could be bound twice (or by hand) has two actions on one button; the later one in
    # ACTIONS order moves to a free button, so a SELECT sharing CIRCLE cannot turn CIRCLE+START into the exit chord.
    seen = set()
    for action in ACTIONS:
        if pad[action][0] in seen:
            spare = free_place(pad, ())
            if spare is not None:
                pad[action] = (spare, pad[action][1])
        seen.add(pad[action][0])
    return pad


def free_place(pad, taken):
    """A button nobody holds, in a stable order (back, start and the dpad are not offered: they are the menu's)."""
    used = set(pad[a][0] for a in ACTIONS) | set(taken)
    for place in ("back", "guide", "lefttrigger", "righttrigger", "leftstick", "rightstick", "touchpad"):
        if place not in used:
            return place
    return None


def set_place(pad, action, place):
    """A gamepad button does one thing. Binding one that another action already holds swaps the two, which is what the
    player means ("this button should be cross now"), and keeps SELECT and START off the face buttons: bound twice, a
    SELECT on the same button as CIRCLE turns CIRCLE+START into the exit chord (which is what this machine had)."""
    old = pad[action][0]
    if place == old:
        return
    holder = next((a for a in ACTIONS if a != action and pad[a][0] == place), None)
    if holder is not None:
        if sum(1 for a in ACTIONS if pad[a][0] == old) == 1:
            pad[holder] = (old, pad[holder][1])
        else:
            spare = free_place(pad, (place,))
            if spare is not None:
                pad[holder] = (spare, pad[holder][1])
    pad[action] = (place, pad[action][1])


def save_pad(pad):
    with open(PAD_FILE, "w") as f:
        for action in ACTIONS:
            place, letter = pad[action]
            f.write("%s %s %s\n" % (action, place, letter))


def gamepad_json(pad):
    bits = []
    for action in ACTIONS:
        bits.append('"%s": "%s"' % (action, pad[action][0]))
    return "{ " + ", ".join(bits) + " }"


def apply_row(s, row):
    if row == 0:
        s["hz"] = 60 if s["hz"] == 50 else 50
    elif row == 1:
        if not s["ff"]:
            s["ff"] = 1
        else:
            order = ["2x", "4x", "8x", "unlimited"]
            if s["ff_speed"] not in order or s["ff_speed"] == "unlimited":
                s["ff"] = 0
                s["ff_speed"] = "4x"
            else:
                s["ff_speed"] = order[order.index(s["ff_speed"]) + 1]
    elif row == 2:
        s["skip"] ^= 1
    elif row == 3:
        s["battles"] ^= 1
    elif row == 4:
        s["hit"] ^= 1
    elif row == 5:
        s["saveany"] ^= 1
    elif row == 6:
        if not s["party"]:
            s["party"] = 1
        else:
            s["share"] += 25
            if s["share"] > 100:
                s["party"] = 0
                s["share"] = 50
    elif row == 7:
        if not s["xp"]:
            s["xp"] = 1
        else:
            s["xp_mul"] += 1
            if s["xp_mul"] > 5:
                s["xp"] = 0
                s["xp_mul"] = 2
    elif row == 8:
        if not s["lang_on"]:
            s["lang_on"] = 1
        else:
            s["lang"] += 1
            if s["lang"] > 6:
                s["lang_on"] = 0
                s["lang"] = 0


def rows(s):
    def on(v):
        return "ON" if v else "off"
    lang = LANGS[s["lang"]] if 0 <= s["lang"] < len(LANGS) else "english"
    return [
        "HZ                  %s" % s["hz"],
        "FAST-FORWARD        %s  %s" % (on(s["ff"]), s["ff_speed"].upper()),
        "SKIP DIALOGUES      %s" % on(s["skip"]),
        "SKIP BATTLE ANIMS   %s" % on(s["battles"]),
        "  HIT REACTION      %s" % on(s["hit"]),
        "SAVE ANYWHERE       %s" % on(s["saveany"]),
        "PARTY XP            %s  %s%%" % (on(s["party"]), s["share"]),
        "XP / BITS BOOST     %s  %sX" % (on(s["xp"]), s["xp_mul"]),
        "LANGUAGE            %s  %s" % (on(s["lang_on"]), lang.upper()),
        "BOTOES              REMAP",
        "PLAY",
    ]


class Screen(object):
    def __init__(self):
        self.fd = os.open(FB, os.O_RDWR)
        self.w, self.h, self.stride, self.bpp = 640, 480, 640 * 4, 32
        try:
            import fcntl
            var = fcntl.ioctl(self.fd, 0x4600, b"\0" * 160)  # FBIOGET_VSCREENINFO
            self.w = struct.unpack_from("I", var, 0)[0] or 640
            self.h = struct.unpack_from("I", var, 4)[0] or 480
            self.bpp = struct.unpack_from("I", var, 24)[0] or 32
            fix = fcntl.ioctl(self.fd, 0x4602, b"\0" * 80)
            self.stride = struct.unpack_from("I", fix, 16 + 16)[0] or self.w * 4
        except Exception:
            pass
        size = self.stride * self.h
        self.mm = mmap.mmap(self.fd, size, mmap.MAP_SHARED, mmap.PROT_WRITE | mmap.PROT_READ)
        self.scale = 3 if self.w >= 640 else 2

    def pix(self, x, y, color):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.mm.seek(y * self.stride + x * 4)
            self.mm.write(struct.pack("I", color))

    def fill(self, color):
        row = struct.pack("I", color) * self.w
        for y in range(self.h):
            self.mm.seek(y * self.stride)
            self.mm.write(row)

    def text(self, x, y, msg, color):
        for ch in msg:
            g = GLYPH.get(ch, GLYPH[" "])
            for row, bits in enumerate(g):
                for col, bit in enumerate(bits):
                    if bit == "1":
                        for dy in range(self.scale):
                            for dx in range(self.scale):
                                self.pix(x + col * self.scale + dx, y + row * self.scale + dy, color)
            x += 6 * self.scale
        return x

    def close(self):
        self.mm.close()
        os.close(self.fd)


def draw(screen, s, sel, disc, pad):
    screen.fill(0x001018)
    screen.text(24, 18, "DIGIMON WORLD 3", 0x3EE0C8)
    items = rows(s)
    y = 56
    for i, line in enumerate(items):
        if i == sel:
            color = 0xFFCC33
            screen.text(16, y, ">", color)
        else:
            color = 0xD0D0D0 if i < 10 else 0x40FF80
        screen.text(40, y, line, color)
        y += 10 * screen.scale
    # The legends are two lines and the file name a third: on this 640x480 panel the glyph cell is 6*scale = 18 px, so one
    # line of all three was 774 px wide (cut at the right edge) and the file name sat at h-18, cut by the bottom.
    step = 8 * screen.scale
    y = screen.h - 3 * step - 6
    screen.text(24, y, "%s MUDA   %s/START JOGA" % (pad["cross"][1], pad["circle"][1]), 0x888888)
    screen.text(24, y + step, "SELECT+START SAI", 0x888888)
    name = os.path.basename(disc) if disc else "NO DISC"
    if len(name) > 32:
        name = name[:29] + "..."
    screen.text(24, y + 2 * step, name, 0x666666)


def event_fmt():
    # aarch64 timeval is 16 bytes; the 32-bit compat event is 16 total.
    if struct.calcsize("P") == 8:
        return 24, "QQHHi"
    return 16, "IIHHi"


def pad_events(pad, capture=False):
    size, fmt = event_fmt()
    confirm = EVDEV_CODE.get(pad["cross"][0], K_A)
    back = EVDEV_CODE.get(pad["circle"][0], K_B)
    fd = os.open(PAD, os.O_RDONLY)
    select_down = start_down = False
    try:
        # Drop the button that launched the port. A held Start/A must not exit or start the game.
        import fcntl
        flags = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
        try:
            while os.read(fd, 64):
                pass
        except OSError:
            pass
        fcntl.fcntl(fd, fcntl.F_SETFL, flags)
        buf = b""
        while True:
            chunk = os.read(fd, 64)
            if not chunk:
                continue
            buf += chunk
            while len(buf) >= size:
                data, buf = buf[:size], buf[size:]
                _sec, _usec, typ, code, val = struct.unpack(fmt, data)
                if typ != 1:
                    continue
                if code == K_SELECT:
                    select_down = val != 0
                elif code == K_START:
                    start_down = val != 0
                if select_down and start_down:
                    return "quit"
                if val != 1:
                    continue
                if capture and code in EVDEV and not EVDEV[code].startswith("dp"):
                    return ("raw", code)
                if code == K_UP:
                    return "up"
                if code == K_DOWN:
                    return "down"
                if code == 546:
                    return "left"
                if code == 547:
                    return "right"
                if code == confirm:
                    return "a"
                if code == back or code == K_START:
                    return "play"
    finally:
        os.close(fd)


def draw_remap(screen, pad, sel, preset, capture):
    screen.fill(0x001018)
    screen.text(24, 16, "BOTOES", 0x3EE0C8)
    y = 48
    line = "PRESET              %s" % preset.upper()
    screen.text(16, y, ">" if sel == 0 else " ", 0xFFCC33)
    screen.text(40, y, line, 0xFFCC33 if sel == 0 else 0xD0D0D0)
    y += 10 * screen.scale
    for i, action in enumerate(ACTIONS):
        place, letter = pad[action]
        text = "%-12s %s  %s" % (ACTION_LABEL[action], place.upper()[:10], letter)
        screen.text(16, y, ">" if sel == i + 1 else " ", 0xFFCC33)
        screen.text(40, y, text, 0xFFCC33 if sel == i + 1 else 0xD0D0D0)
        y += 10 * screen.scale
    step = 8 * screen.scale
    y = screen.h - 4 * step - 6
    if capture:
        screen.text(24, y, "APERTE O BOTAO AGORA", 0xFFCC33)
    screen.text(24, y + step, "NOME = SISTEMA. LETRA = JOGO", 0x888888)
    screen.text(24, y + 2 * step, "START VOLTA   ESQ/DIR LETRA", 0x888888)
    screen.text(24, y + 3 * step, "BOTAO JA USADO TROCA DE DONO", 0x666666)


def remap(screen, pad):
    sel = 0
    preset = "xbox"
    capture = False
    n = 1 + len(ACTIONS)
    while True:
        draw_remap(screen, pad, sel, preset, capture)
        key = pad_events(pad, capture)
        if key == "quit":
            return "quit"
        if capture:
            if isinstance(key, tuple):
                set_place(pad, ACTIONS[sel - 1], EVDEV[key[1]])
                capture = False
            continue
        if key == "up":
            sel = (sel - 1) % n
        elif key == "down":
            sel = (sel + 1) % n
        elif key == "left" or key == "right":
            if sel == 0:
                continue
            action = ACTIONS[sel - 1]
            letter = pad[action][1]
            i = LETTERS.index(letter) if letter in LETTERS else 0
            i = (i + (1 if key == "right" else -1)) % len(LETTERS)
            pad[action] = (pad[action][0], LETTERS[i])
        elif key == "a":
            if sel == 0:
                names = ["xbox", "switch", "modern"]
                preset = names[(names.index(preset) + 1) % 3]
                for action in ACTIONS:
                    set_place(pad, action, PRESETS[preset][action][0])
                    pad[action] = (pad[action][0], PRESETS[preset][action][1])
            else:
                capture = True
        elif key == "play":
            save_pad(pad)
            return "back"


def draw_loading(screen, phase=0):
    """The wait before the game's first frame, with two lines of dots running opposite ways: a glyph cell is 18 px on
    this panel, so 28 characters fit and each frame costs one framebuffer fill plus the text."""
    screen.fill(0x001018)
    width = 28
    a = phase % width
    b = (width - phase) % width
    screen.text(24, 150, "." * a + ">" + "." * (width - a - 1), 0x3EE0C8)
    screen.text(24, 180, "CARREGANDO O DISCO", 0x3EE0C8)
    screen.text(24, 220, "." * b + "<" + "." * (width - b - 1), 0x3EE0C8)
    screen.text(24, 260, "ESPERE CERCA DE 1 MINUTO", 0xD0D0D0)
    screen.text(24, 300, "A TELA FICA PARADA DE PROPOSITO", 0x888888)


def main():
    s = load_state()
    disc = find_disc()
    pad = load_pad()
    sel = 10
    screen = Screen()
    launch = False
    try:
        draw(screen, s, sel, disc, pad)
        while True:
            key = pad_events(pad)
            if key == "up":
                sel = (sel - 1) % 11
            elif key == "down":
                sel = (sel + 1) % 11
            elif key == "a":
                if sel == 10:
                    launch = True
                    break
                if sel == 9:
                    if remap(screen, pad) == "quit":
                        screen.fill(0x000000)
                        return 2
                    save_pad(pad)
                else:
                    apply_row(s, sel)
            elif key == "play":
                launch = True
                break
            elif key == "quit":
                screen.fill(0x000000)
                return 2
            draw(screen, s, sel, disc, pad)
        if launch and disc:
            for phase in range(6):
                draw_loading(screen, phase * 4)
    finally:
        screen.close()
    if not disc:
        return 1
    save_state(s)
    write_settings(s, disc)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        import traceback
        with open(os.path.join(GAME, "menu.err"), "w") as f:
            traceback.print_exc(file=f)
        sys.exit(1)
