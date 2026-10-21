#!/usr/bin/env python3
"""Gamepad menu drawn on /dev/fb0. Exit 0 = play, 2 = quit."""
import mmap
import os
import struct
import sys

GAME = os.path.dirname(os.path.abspath(__file__))
STATE = os.path.join(GAME, "tui.state")
CFG = os.path.join(GAME, "settings.json")
LOGS = os.path.join(GAME, "logs")
FB = os.environ.get("DW3_FB", "/dev/fb0")
PAD = os.environ.get("DW3_PAD", "/dev/input/event0")

# The LED bar's daemon (handheld/ledd.py): it owns the tty and the regulator, and every port on the card asks it for the
# bar instead of writing the line itself. Because it is shared, the installed copy and the record that somebody accepted
# the risk live in ONE place for the whole card, not inside a port: a second port would otherwise ship a second copy that
# drifts from this one, and its "installed" flag would say nothing about the others.
#   /storage/.config/ledd/ledd.py    the one daemon the card runs (this port's copy put there on install)
#   /storage/.config/ledd/installed  who installed it, when, and the hash of the copy that was installed
LED_SOCK = "/tmp/ledd.sock"
LED_DIR = os.environ.get("DW3_LED_DIR", "/storage/.config/ledd")
LED_FLAG = os.path.join(LED_DIR, "installed")
LED_COPY = os.path.join(LED_DIR, "ledd.py")

# The palette, and what each colour means. One rule: a button you can press is amber, a value that is on is green, an
# explanation is grey, and anything that is a warning or a failure is never the same colour as a normal value.
C_TITLE = 0x3EE0C8  # screen titles
C_TEXT = 0xD0D0D0   # labels and values that are words
C_DIM = 0x707070    # "off", and the shape of things
C_ON = 0x40FF80     # "ON", an action row, a success
C_KEY = 0xFFCC33    # a button you press, and the selected row
C_WARN = 0xFFCC33   # a risk stated before it is taken
C_BAD = 0xFF6060    # something that did not work
C_NOTE = 0x888888   # the explanation line

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
# The rows are what the button does inside the game, not what the console that sold it calls it: a player hunting for
# "back" has nothing to do with the PlayStation's symbols. The four face rows are the bits the game reads after it
# rotates the pad for the non-Japanese languages (src/main/pad.c): circle confirms, cross goes back, which is the
# convention the game's own text is drawn in.
ACTIONS = ["circle", "cross", "triangle", "square", "l1", "r1", "start", "select"]
FACE = ["circle", "cross", "triangle", "square"]
ACTION_LABEL = {
    "circle": "CONFIRM", "cross": "BACK", "triangle": "TRIANGLE", "square": "SQUARE",
    "l1": "L1", "r1": "R1", "start": "START", "select": "SELECT",
}
VALID_LETTERS = set("ABXYLRSE-")  # "-": the game's text keeps the button's own symbol


def key_label(pad, action):
    """What is printed on the button, which is not the same thing for every action: the face buttons carry a letter
    (A/B/X/Y, the one the game's text shows) and the rest carry their own name (START, SELECT, L1, R1). A legend that
    said "S PLAY" was naming a pad letter nobody can read on the device."""
    _place, letter = pad[action]
    return {"start": "START", "select": "SELECT", "l1": "L1", "r1": "R1"}.get(action, letter)


# A layout is what is printed on the pad: a letter for each of the four positions. Picking one also puts the face
# buttons where that pad has them (A confirming, B going back), and from then on the letters follow the *positions* -
# moving a single row can never leave the game's text naming a button that does nothing, which is the whole reason the
# letter used to be a second thing to edit.
LAYOUTS = {
    "xbox": {
        "place": {"circle": "south", "cross": "east", "triangle": "north", "square": "west"},
        "letter": {"south": "A", "east": "B", "north": "Y", "west": "X"},
    },
    "switch": {
        "place": {"circle": "east", "cross": "south", "triangle": "north", "square": "west"},
        "letter": {"south": "B", "east": "A", "north": "X", "west": "Y"},
    },
    "ps1": {
        # The PlayStation's own: that pad is drawn with symbols, so the text keeps them (nothing replaces them).
        "place": {"circle": "east", "cross": "south", "triangle": "north", "square": "west"},
        "letter": {},
    },
}
LAYOUT_NAMES = ["xbox", "switch", "ps1"]


def letter_for(layout, place):
    """The letter the game's text shows for a button at `place`; "-" leaves the button's own symbol."""
    return LAYOUTS[layout]["letter"].get(place, "-")

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


def defaults():
    """The tuned setup, and what RESET SETTINGS puts back: the owner's own settings on the handheld, so a player who never
    opens this menu gets the configuration that was found to play well - 8x fast-forward with the audio muted, the whole
    party sharing all the experience, a mild XP boost, every digivolution learning from a battle, and no logs."""
    return dict(hz=50, ff=1, ff_speed="8x", skip=0, battles=0, hit=0, saveany=0,
                party=1, share=100, xp=1, xp_mul=6, all_forms=1, logs=0, lang=0, lang_on=0)


def load_state():
    s = defaults()
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


def write_atomic(path, text):
    """Write through a temporary file and a rename, the way the port's memory card does: a reset or a power cut in the
    middle leaves the file as it was, not half of a new one. Without this, a truncated settings.json makes the game exit
    at 64 on the next boot, and the player sees "it does not start" with nothing to point at."""
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(text)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def save_state(s):
    write_atomic(STATE, "".join("%s=%s\n" % (k, v) for k, v in s.items()))


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
    # The renderer is the player's choice, not the menu's: it is not on any row here, so it is carried over from the file
    # that is already there. Writing a fixed "gles" silently overrode anyone who had chosen "software" - and made every
    # A/B test that flipped the file meaningless, because the menu ran first and put it back.
    try:
        import json

        renderer = json.load(open(CFG)).get("video", {}).get("renderer", "gles")
    except (OSError, ValueError):
        renderer = "gles"
    if renderer not in ("gles", "software", "gpu"):
        renderer = "gles"
    text = """{
  "schema": 1,
  "disc": { "path": "game/%s" },
  "video": { "window": true, "scale": 2, "fullscreen": true, "refresh": %d, "renderer": "%s" },
  "audio": { "mute": false },
  "memcard1": "card1.mcd",
  "memcard2": "card2.mcd",
  "watchdog": 0,
  "debug": { "logs": "%s" },
  "mods": {
    "fast_forward": { "enabled": %s, "speed": "%s", "mute": true, "hold": "", "toggle": "" },
    "skip_dialogues": { "enabled": %s, "fast_forward_waits": false },
    "battle_animations": { "enabled": %s, "hit_reaction": %s },
    "global_save": { "enabled": %s, "restore_map_state": true },
    "party_xp": { "enabled": %s, "share": %d, "knocked_out": false, "catch_up": false },
    "xp_boost": { "enabled": %s, "exp": %d, "form_exp": %d, "bits": %d, "manual": %s, "all_forms": %s },
    "preset_language": { "enabled": true, "language": "english" },
    "widescreen": { "enabled": false },
    "led_bar": { "enabled": %s }
  },
  "input": { "gamepad": %s }
}
""" % (os.path.basename(disc), s["hz"], renderer, LOG_LEVELS[s["logs"] % len(LOG_LEVELS)], b(s["ff"]), s["ff_speed"], b(s["skip"]),
       b(s["battles"]), b(s["hit"]), b(s["saveany"]), b(s["party"]), s["share"],
       b(s["xp"]), s["xp_mul"], s["xp_mul"], s["xp_mul"], b(s["xp_mul"] > 5), b(s["all_forms"]),
       b(led_mod_on()), gamepad_json(load_pad()))
    write_atomic(CFG, text)


def default_pad():
    pad = {"l1": ("leftshoulder", "L"), "r1": ("rightshoulder", "R"), "start": ("start", "S"),
           "select": ("back", "E")}
    apply_layout(pad, "xbox")
    return pad


def apply_layout(pad, layout):
    """A layout is a pad's lettering and, as a starting point, where that pad keeps A and B: the face rows move to its
    positions and take its letters. The other four rows keep the buttons the player chose; the menu shows their names."""
    for action in FACE:
        place = LAYOUTS[layout]["place"][action]
        pad[action] = (place, letter_for(layout, place))


def next_layout(current, direction):
    step = 1 if direction == "right" else -1
    i = LAYOUT_NAMES.index(current) if current in LAYOUT_NAMES else 0
    return LAYOUT_NAMES[(i + step) % len(LAYOUT_NAMES)]


def load_pad():
    pad = default_pad()
    if not os.path.isfile(PAD_FILE):
        return pad
    for line in open(PAD_FILE):
        parts = line.split()
        if len(parts) == 3 and parts[0] in pad and parts[2] in VALID_LETTERS:
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


def detect_layout(pad):
    """Which layout the letters say it is. The family belongs to the pad, not to one button, so the row still reads
    SWITCH after a single row is moved; letters that match none of the families read CUSTOM."""
    for name in LAYOUT_NAMES:
        if all(pad[action][1] == letter_for(name, pad[action][0]) for action in FACE):
            return name
    return "custom"


def set_place(pad, action, place, layout):
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
    # The letter follows the position, so the game's text cannot name a button that does nothing - not even after the
    # swap above moved the row that held this one.
    for a in (action, holder):
        if a is not None and a in FACE:
            pad[a] = (pad[a][0], letter_for(layout, pad[a][0]))


def save_pad(pad):
    write_atomic(PAD_FILE, "".join("%s %s %s\n" % (action, pad[action][0], pad[action][1]) for action in ACTIONS))


def gamepad_json(pad):
    bits = []
    for action in ACTIONS:
        bits.append('"%s": "%s"' % (action, pad[action][0]))
    return "{ " + ", ".join(bits) + " }"


# The menu is grouped: the main screen holds one row per group and the actions, and each group opens as its own screen
# with its own rows. Fourteen rows on a 640x480 panel were already colliding with the legend, and the font could not grow
# to be read at arm's length. Each entry is (key, label, value function); a key is what the settings file calls the thing,
# so the toggle below and the text above cannot drift apart.
GROUPS = [
    ("game", "GAME", [
        ("ff", "FAST-FORWARD"),
        ("skip", "SKIP DIALOGUES"),
        ("battles", "SKIP BATTLE ANIMS"),
        ("hit", "  HIT REACTION"),
        ("saveany", "SAVE ANYWHERE"),
    ]),
    ("xp", "XP", [
        ("party", "PARTY XP"),
        ("xp", "XP / BITS BOOST"),
        ("all_forms", "  ALL FORMS' XP"),
    ]),
    ("video", "VIDEO", [
        ("hz", "HZ"),
        ("renderer", "RENDERER"),
    ]),
    ("debug", "DEBUG", [
        ("logs", "LOGS"),
        ("save_logs", "SAVE LOGS"),
    ]),
]

LOG_LEVELS = ["off", "soft", "heavy"]


def group_rows(s):
    """The main screen: the groups, then what acts on the console or runs once, then PLAY."""
    return [
        "GAME                SETTINGS",
        "XP                  SETTINGS",
        "VIDEO               SETTINGS",
        "DEBUG               SETTINGS",
        "LED BAR             %s" % led_state(),
        "BUTTONS             REMAP",
        "RESET SETTINGS      ASK",
        "PLAY",
    ]


def field_text(s, key):
    """Label's value, as the screen shows it."""
    def on(v):
        return "ON" if v else "off"
    if key == "ff":
        return "%s  %s" % (on(s["ff"]), s["ff_speed"].upper())
    if key == "party":
        return "%s  %s%%" % (on(s["party"]), s["share"])
    if key == "xp":
        return "%s  %sX" % (on(s["xp"]), s["xp_mul"])
    if key == "hz":
        return "%s" % s["hz"]
    if key == "renderer":
        return renderer_get().upper()
    if key == "logs":
        return LOG_LEVELS[s["logs"] % len(LOG_LEVELS)].upper()
    if key == "save_logs":
        return "ZIP"
    if key in ("skip", "battles", "hit", "saveany", "all_forms"):
        return on(s[key])
    return ""


def field_toggle(s, key):
    """A press on the row: the same cycles the single-column menu had, one per setting."""
    if key == "hz":
        s["hz"] = 60 if s["hz"] == 50 else 50
    elif key == "ff":
        if not s["ff"]:
            s["ff"] = 1
        else:
            order = ["2x", "4x", "8x", "unlimited"]
            if s["ff_speed"] not in order or s["ff_speed"] == "unlimited":
                s["ff"] = 0
                s["ff_speed"] = "4x"
            else:
                s["ff_speed"] = order[order.index(s["ff_speed"]) + 1]
    elif key == "party":
        if not s["party"]:
            s["party"] = 1
        else:
            s["share"] += 25
            if s["share"] > 100:
                s["party"] = 0
                s["share"] = 50
    elif key == "xp":
        # off, then 2x up to 20x one step at a time, and back to off. 6x is the tuned default: with the game's own exp
        # tables a battle hands over ~244, so 6x puts the end of the story (level 50) around 80 battles and level 99 at
        # ~600 - a modern pace rather than the original's ~3,500. Above 5x the settings' `manual` flag is written on, or
        # the game's own restriction would cap the figure at 5x.
        if not s["xp"]:
            s["xp"] = 1
            s["xp_mul"] = 6
        else:
            s["xp_mul"] += 1
            if s["xp_mul"] > 20:
                s["xp"] = 0
                s["xp_mul"] = 6
    elif key == "renderer":
        renderer_toggle()
    elif key == "logs":
        s["logs"] = (s["logs"] + 1) % len(LOG_LEVELS)
    elif key in ("skip", "battles", "hit", "saveany", "all_forms"):
        s[key] ^= 1


def group_screen(name):
    for key, title, entries in GROUPS:
        if key == name:
            return title, entries
    return "", []


def led_ask(request, timeout=0.4):
    """One request to the daemon, one reply. Returns the reply line, or None when nothing answers at the socket."""
    import socket

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect(LED_SOCK)
        s.sendall((request + "\n").encode())
        return s.recv(200).decode(errors="replace").strip()
    except OSError:
        return None
    finally:
        s.close()


_led_seen = [0.0, "not installed"]


def led_mod_on():
    """Whether the game's led_bar mod is enabled in settings.json - the file the game reads, and the only thing that
    makes it drive the bar."""
    import json

    try:
        cfg = json.load(open(CFG))
    except (OSError, ValueError):
        return False
    return bool(cfg.get("mods", {}).get("led_bar", {}).get("enabled"))


def led_mod_set(on):
    """Write it, now: the game starts right after PLAY and reads the file once."""
    import json

    try:
        cfg = json.load(open(CFG))
    except (OSError, ValueError):
        cfg = {}
    cfg.setdefault("mods", {}).setdefault("led_bar", {})["enabled"] = bool(on)
    write_atomic(CFG, json.dumps(cfg, indent=2))


def led_state():
    """The row: "not installed" until a daemon answers, then the mod's own switch as ON/off. A daemon that answers IS
    the install - the marker only says who accepted the risk, and a card where another port installed it shows as
    installed here too. Kept for a second: the menu redraws on every key and a socket that is not there costs the
    connect timeout each time."""
    import time

    now = time.time()
    if now - _led_seen[0] > 1.0:
        if led_ask("status"):
            text = "ON" if led_mod_on() else "off"
        elif os.path.isfile(LED_COPY):
            text = "installed, off"
        else:
            text = "not installed"
        _led_seen[0], _led_seen[1] = now, text
    return _led_seen[1]


def renderer_get():
    """Which renderer settings.json asks for. gles is the default (that is what the file ships with); software is the one
    a player picks here for the 2D-heavy sessions, where it measures faster than the GPU (57.7 against 43.5 fps on the
    scripted boot, because a light frame pays the same ~3.2 ms present either way)."""
    import json

    try:
        name = json.load(open(CFG)).get("video", {}).get("renderer", "gles")
    except (OSError, ValueError):
        return "gles"
    return name if name in ("gles", "software", "gpu") else "gles"


def renderer_set(name):
    """Write it now: the game reads the file once, right after PLAY."""
    import json

    try:
        cfg = json.load(open(CFG))
    except (OSError, ValueError):
        cfg = {}
    cfg.setdefault("video", {})["renderer"] = name
    write_atomic(CFG, json.dumps(cfg, indent=2))


def renderer_toggle():
    renderer_set("software" if renderer_get() == "gles" else "gles")


def led_start_daemon():
    """Start the card's daemon: the shared copy when it is there, this port's own file otherwise (a port that was never
    installed with the menu still gives the bar a chance). ledd.py double-forks and takes a lock, so a second copy
    exits by itself instead of opening the line twice."""
    import subprocess
    import time

    path = LED_COPY if os.path.isfile(LED_COPY) else os.path.join(GAME, "ledd.py")
    try:
        subprocess.Popen([sys.executable or "python3", path, "--detach"], cwd=GAME,
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError:
        return False
    for _ in range(20):  # it opens a tty and a regulator: a second at worst
        if led_ask("status", 0.2):
            return True
        time.sleep(0.1)
    return False


def led_install():
    """Put this port's copy of the daemon where the whole card finds it, start it, and record who accepted the risk.
    No second daemon and no second copy: if the shared file is already there with other bytes, this port's version
    replaces it (the port that installs last wins, which is the same rule any package has)."""
    import hashlib
    import time

    try:
        os.makedirs(LED_DIR, exist_ok=True)
        with open(os.path.join(GAME, "ledd.py"), "rb") as f:
            body = f.read()
        old = None
        if os.path.isfile(LED_COPY):
            with open(LED_COPY, "rb") as f:
                old = f.read()
        if old != body:
            tmp = LED_COPY + ".tmp"
            with open(tmp, "wb") as f:
                f.write(body)
            os.replace(tmp, LED_COPY)
    except OSError as e:
        return False, "CANNOT WRITE %s" % LED_DIR
    if not led_start_daemon():
        return False, "THE DAEMON DID NOT COME UP"
    with open(LED_FLAG, "w") as f:
        f.write("%s %s %s\n" % (os.path.basename(GAME), hashlib.sha1(body).hexdigest()[:12],
                                time.strftime("%Y-%m-%d %H:%M")))
    return True, "/tmp/ledd.sock ANSWERS status"


def draw_led(screen, pad, step, ok, detail=None):
    """The warning before anything is installed (step 0), then what happened (step 1), and the reason when it failed
    (step 2)."""
    screen.fill(0x001018)
    screen.text(24, 16, "LED BAR", C_TITLE)
    y = 60
    line = 10 * screen.scale
    if step == 0:
        lines = (
            ("THIS IS THE XF35H: RK3326, 640X480,", C_TEXT),
            ("WITH ITS LED BAR ON /dev/ttyS2.", C_TEXT),
            ("ONE DAEMON OWNS THAT LINE AND THE", C_TEXT),
            ("REGULATOR. EVERY PORT ASKS IT.", C_TEXT),
            ("", 0),
            ("ON ANOTHER CONSOLE THOSE BYTES MAY", C_WARN),
            ("REACH A UART THAT IS NOT A LED.", C_WARN),
            ("THAT IS THE RISK; HENCE THIS ASKS.", C_WARN),
            ("", 0),
            ("%s INSTALL" % pad["cross"][1], C_ON),
            ("%s CANCEL" % pad["circle"][1], C_NOTE),
        )
    else:
        head = "THE DAEMON IS RUNNING" if ok else (detail or "THE DAEMON DID NOT COME UP")
        lines = (
            (head, C_ON if ok else C_BAD),
            ("", 0),
            ("/tmp/ledd.sock ANSWERS status" if ok else "SEE /tmp/led-test/ledd.log", C_NOTE),
            ("THE COPY AND THE RECORD LIVE IN" if ok else "", C_NOTE),
            ("/storage/.config/ledd, SO THE NEXT" if ok else "", C_NOTE),
            ("PORT REUSES THEM INSTEAD OF ITS OWN." if ok else "", C_NOTE),
            ("", 0),
            ("%s BACK" % pad["cross"][1], C_NOTE),
        )
    for text, color in lines:
        if text:
            screen.text(24, y, text, color)
        y += line


def led_setup(screen, pad):
    """The row's action: warn, ask, and only then install (one copy on the card, one daemon) and record who said yes."""
    draw_led(screen, pad, 0, None)
    while True:
        key = pad_events(pad)
        if key == "quit":
            return "quit"
        if key == "b":
            return None
        if key == "a":
            ok, detail = led_install()
            draw_led(screen, pad, 1, ok)
            if not ok:
                draw_led(screen, pad, 2, False, detail)
            while True:  # the press that got here must not dismiss the report
                key = pad_events(pad)
                if key == "quit":
                    return "quit"
                if key == "a":
                    return None


def save_logs():
    """Everything a bug report needs, in one zip beside the port: the game's and the menu's logs, the crash reports, the
    settings the run used and the pad mapping the player ended up with. The memory cards and the disc are deliberately
    left out - they are the player's, and they are the one thing here that is not a diagnostic. Returns the file name, or
    an error string, so the row can say what happened."""
    import time
    import zipfile

    name = "dw3-logs-%s.zip" % time.strftime("%Y%m%d-%H%M%S")
    path = os.path.join(GAME, name)
    try:
        z = zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED)
    except OSError:
        return "FAILED"
    with z:
        for f in (CFG, os.path.join(GAME, "pad.map"), STATE):
            if os.path.isfile(f):
                try:
                    z.write(f, os.path.basename(f))
                except OSError:
                    pass
        for f in sorted(os.listdir(GAME)) if os.path.isdir(GAME) else []:
            if f.startswith("crash-") and f.endswith(".txt"):
                try:
                    z.write(os.path.join(GAME, f), "crashes/" + f)
                except OSError:
                    pass
        if os.path.isdir(LOGS):
            for f in sorted(os.listdir(LOGS)):
                src = os.path.join(LOGS, f)
                if os.path.isfile(src):
                    try:
                        z.write(src, "logs/" + f)
                    except OSError:
                        pass
        # What the machine and the run were, without having to read the log to find out.
        try:
            import json

            renderer = json.load(open(CFG)).get("video", {}).get("renderer", "gles")
        except (OSError, ValueError):
            renderer = "?"
        note = "renderer: %s\nsettings: %s\n" % (renderer, CFG)
        try:
            note += "uname: %s\n" % " ".join(os.uname())
        except OSError:
            pass
        try:
            z.writestr("system.txt", note + "date: %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
        except OSError:
            pass
    return name


def rows(s, screen="main"):
    """The lines of one screen: the grouped main list, or a group's own settings."""
    if screen == "main":
        return group_rows(s)
    _title, entries = group_screen(screen)
    out = []
    for key, label in entries:
        pad = 20 - len(label)
        out.append(label + " " * max(1, pad) + field_text(s, key))
    return out


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


def draw(screen, s, sel, disc, pad, page="main"):
    screen.fill(0x001018)
    title, _entries = group_screen(page)
    screen.text(24, 18, "DIGIMON WORLD 3" if page == "main" else "DIGIMON WORLD 3  >  " + title, 0x3EE0C8)
    items = rows(s, page)
    step = 8 * screen.scale
    legend = screen.h - 2 * step - 8
    y = 56
    if not items:
        items = ["NOTHING HERE"]
    # Fit whatever is on this screen above the legend, in font units and not in pixels: the old division mixed the two and
    # a fourteen-row list came out 43 px too tall, on top of the legend. A group screen has three to five rows, so this
    # gives them the largest step (10 units = 30 px).
    room = (legend - 10 - y) // max(1, len(items))
    row_step = max(6, min(10, room // screen.scale))
    for i, line in enumerate(items):
        if i == sel:
            screen.text(16, y, ">", C_KEY)
            screen.text(40, y, line, C_KEY)
        else:
            label, value = line[:20].rstrip(), line[20:].strip()
            if not value:  # an action row: PLAY, BUTTONS, LED BAR when it has no state
                screen.text(40, y, label, C_ON)
            else:
                screen.text(40, y, label, C_TEXT)
                color = C_ON if value in ("ON", "running") else C_DIM if value == "off" else C_TEXT
                screen.text(40 + 20 * 6 * screen.scale, y, value, color)
        y += row_step * screen.scale
    # Two legends, each saying what the button does. The disc's file name used to sit on a third line: it copied the
    # "dw2003.bin" into a menu that already knows which disc it found, and cost the last row its place.
    # The roles, not the symbols: circle is the game's confirm and cross its back (the pad is rotated into that
    # convention), so the menu must read the same way.
    x = screen.text(24, legend, key_label(pad, "circle"), C_KEY)
    x = screen.text(x, legend, " SELECT   ", C_NOTE)
    if page != "main":
        x = screen.text(x, legend, key_label(pad, "cross"), C_KEY)
        x = screen.text(x, legend, " BACK   ", C_NOTE)
    x = screen.text(x, legend, key_label(pad, "start"), C_KEY)
    screen.text(x, legend, " PLAY", C_NOTE)
    if disc:
        x = screen.text(24, legend + step, "SELECT+START", C_KEY)
        screen.text(x, legend + step, " QUIT", C_NOTE)
    else:
        screen.text(24, legend + step, "NO DISC IN game/", C_BAD)


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
                if code == back:
                    return "b"
                if code == K_START:
                    return "play"
    finally:
        os.close(fd)


def draw_remap(screen, pad, sel, layout, capture):
    screen.fill(0x001018)
    screen.text(24, 16, "BUTTONS", C_TITLE)
    y = 48
    screen.text(16, y, ">" if sel == 0 else " ", C_KEY)
    screen.text(40, y, "LAYOUT", C_KEY if sel == 0 else C_TEXT)
    screen.text(40 + 20 * 6 * screen.scale, y, layout.upper(), C_KEY if sel == 0 else C_ON)
    y += 10 * screen.scale
    for i, action in enumerate(ACTIONS):
        place, letter = pad[action]
        here = sel == i + 1
        screen.text(16, y, ">" if here else " ", C_KEY)
        screen.text(40, y, "%-12s" % ACTION_LABEL[action], C_KEY if here else C_TEXT)
        # The place is what the pad reports; the letter is what the game's text will show for it, read off the layout -
        # nothing here is typed in, so the two columns cannot disagree.
        screen.text(40 + 13 * 6 * screen.scale, y, place.upper()[:10], C_KEY if here else C_NOTE)
        if action in FACE and letter != "-":
            screen.text(40 + 25 * 6 * screen.scale, y, letter, C_KEY if here else C_ON)
        y += 10 * screen.scale
    # Three legends, each one an instruction. The old footer was four lines of jargon ("NAME = SYSTEM. LETTER = GAME",
    # "A USED BUTTON CHANGES OWNER") that named the columns instead of saying what to press.
    step = 8 * screen.scale
    y = screen.h - 3 * step - 6
    if capture:
        screen.text(24, y, "PRESS THE BUTTON NOW - ANY OF THEM", C_WARN)
    else:
        x = screen.text(24, y, key_label(pad, "circle"), C_KEY)
        screen.text(x, y, ": PICK A ROW, THEN THE BUTTON", C_NOTE)
    x = screen.text(24, y + step, "LEFT/RIGHT", C_KEY)
    screen.text(x, y + step, ": LAYOUT, WHICH SETS THE LETTERS", C_NOTE)
    x = screen.text(24, y + 2 * step, key_label(pad, "start"), C_KEY)
    screen.text(x, y + 2 * step, ": BACK TO THE MENU", C_NOTE)


def remap(screen, pad):
    sel = 0
    layout = detect_layout(pad)
    capture = False
    n = 1 + len(ACTIONS)
    while True:
        draw_remap(screen, pad, sel, layout, capture)
        key = pad_events(pad, capture)
        if key == "quit":
            return "quit"
        if capture:
            if isinstance(key, tuple):
                set_place(pad, ACTIONS[sel - 1], EVDEV[key[1]], layout)
                capture = False
            continue
        if key == "up":
            sel = (sel - 1) % n
        elif key == "down":
            sel = (sel + 1) % n
        elif (key == "left" or key == "right") and sel == 0:
            layout = next_layout(layout, key)
            apply_layout(pad, layout)
        elif key == "a":
            if sel == 0:
                layout = next_layout(layout, "right")
                apply_layout(pad, layout)
            else:
                capture = True
        elif key == "play" or key == "b":
            save_pad(pad)
            return "back"


def confirm_reset(screen, pad):
    """Ask, because there is no undo: A puts everything back - the settings, the pad map and the LED mod - and writes the
    files, B leaves it alone. The memory cards are not part of it: a reset is for a broken configuration, not for
    starting the game over."""
    screen.fill(0x001018)
    screen.text(24, 40, "RESET SETTINGS?", C_TITLE)
    y = 110
    for line, color in (
        ("PUT EVERY SETTING BACK TO THE", C_TEXT),
        ("TUNED DEFAULTS AND WRITE THE FILE:", C_TEXT),
        ("BUTTONS TO THE XBOX LAYOUT AND THE", C_TEXT),
        ("LED BAR SWITCHED BACK ON.", C_TEXT),
        ("", 0),
        ("A NINTENDO PAD NEEDS ITS LAYOUT PUT", C_NOTE),
        ("BACK IN BUTTONS AFTERWARDS.", C_NOTE),
        ("THE MEMORY CARDS ARE NEVER TOUCHED.", C_NOTE),
        ("", 0),
        ("%s YES" % pad["cross"][1], C_WARN),
        ("%s NO" % pad["circle"][1], C_NOTE),
    ):
        if line:
            screen.text(24, y, line, color)
        y += 10 * screen.scale
    while True:
        key = pad_events(pad)
        if key == "quit":
            return "quit"
        if key == "a":
            return "yes"
        if key == "b":
            return "no"


def draw_loading(screen, phase=0):
    """The wait before the game's first frame, with two lines of dots running opposite ways: a glyph cell is 18 px on
    this panel, so 28 characters fit and each frame costs one framebuffer fill plus the text."""
    screen.fill(0x001018)
    width = 28
    a = phase % width
    b = (width - phase) % width
    screen.text(24, 150, "." * a + ">" + "." * (width - a - 1), 0x3EE0C8)
    screen.text(24, 180, "LOADING THE DISC", 0x3EE0C8)
    screen.text(24, 220, "." * b + "<" + "." * (width - b - 1), 0x3EE0C8)
    screen.text(24, 260, "WAIT ABOUT A MINUTE", 0xD0D0D0)
    screen.text(24, 300, "THE SCREEN STAYS STILL ON PURPOSE", 0x888888)


def main():
    s = load_state()
    disc = find_disc()
    pad = load_pad()
    page = "main"
    count = len(rows(s, page))
    # On the main screen: PLAY last, then BUTTONS, LED BAR, and the four group rows above them, counted from the end so a
    # row added above cannot move any of them. SAVE LOGS is not here any more: it lives in the DEBUG group with the logs
    # level, which is the only place anyone looks for it.
    play = count - 1
    reset_row = count - 2
    buttons = count - 3
    led_row = count - 4
    group_base = 0  # the first len(GROUPS) rows are the groups, in GROUPS order
    sel = play
    screen = Screen()
    launch = False
    try:
        draw(screen, s, sel, disc, pad, page)
        while True:
            key = pad_events(pad)
            if key == "up":
                sel = (sel - 1) % count
            elif key == "down":
                sel = (sel + 1) % count
            elif key == "b":
                if page != "main":
                    page, sel, count = "main", play, len(rows(s, "main"))
                    draw(screen, s, sel, disc, pad, page)
                continue
            elif key == "a":
                if page != "main":
                    _title, entries = group_screen(page)
                    if 0 <= sel < len(entries):
                        if entries[sel][0] == "save_logs":
                            zipped = save_logs()
                            draw(screen, s, sel, disc, pad, page)
                            screen.text(24, screen.h - 8 * screen.scale - 8, "SAVED %s" % zipped, C_NOTE)
                            continue
                        field_toggle(s, entries[sel][0])
                elif sel == play:
                    launch = True
                    break
                elif sel == reset_row:
                    answer = confirm_reset(screen, pad)
                    if answer == "quit":
                        screen.fill(0x000000)
                        return 2
                    if answer == "yes":
                        s.clear()
                        s.update(defaults())
                        save_state(s)
                        # The pad goes back too: a reset exists to undo a bad state, and a remap that made the menu or
                        # the game unplayable is exactly the kind of thing someone resets to get out of. The layout is
                        # the Xbox one (default_pad), so a Nintendo-style pad needs its layout row put back by hand.
                        pad.clear()
                        pad.update(default_pad())
                        save_pad(pad)
                        if disc:
                            write_settings(s, disc)
                        led_mod_set(True)
                elif sel == buttons:
                    if remap(screen, pad) == "quit":
                        screen.fill(0x000000)
                        return 2
                    save_pad(pad)
                elif sel == led_row:
                    if led_state() == "not installed":
                        if led_setup(screen, pad) == "quit":
                            screen.fill(0x000000)
                            return 2
                    else:
                        # Installed: the row is a toggle for the mod, and the file is written now because the game reads
                        # it once, right after PLAY.
                        led_mod_set(not led_mod_on())
                        _led_seen[0] = 0.0
                elif group_base <= sel < group_base + len(GROUPS):
                    page = GROUPS[sel - group_base][0]
                    sel, count = 0, len(rows(s, page))
            elif key == "play":
                launch = True
                break
            elif key == "quit":
                screen.fill(0x000000)
                return 2
            draw(screen, s, sel, disc, pad, page)
        if launch and disc:
            for phase in range(6):
                draw_loading(screen, phase * 4)
    finally:
        screen.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        import traceback
        with open(os.path.join(GAME, "menu.err"), "w") as f:
            traceback.print_exc(file=f)
        sys.exit(1)
