#!/usr/bin/python3
"""ledd: the one process allowed to write the handheld's LED bar.

Started by the game's led_bar mod (port/game/led.c) from the package's directory, and installed there by the handheld
menu (handheld/dw3-menu.py). The palette and the measurements behind it are docs/HANDHELD.md, "The LED bar is on a
UART, not in sysfs"; the prototype it grew from, with every experiment that produced it, is .dumps/led-test/.

Why a daemon at all: the bar is an MCU on /dev/ttyS2, and what makes a change
expensive is not the byte (1 ms on the wire at 9600 8N1) but paying the setup
every time. Measured on this console: the vendor script 66 ms a call, the
mcu_led binary alone 18 ms, open+write+close with no fork at all 14 ms. Held
open, a write is ~0.2 ms.

The lock, in three layers:
  1. singleton - flock(LOCK_EX | LOCK_NB) on LOCK. The kernel releases it when
     the process dies, so a crash leaves no stale lock to clear by hand.
  2. one owner of the wire - this is the only process that opens the tty.
     Clients talk to the socket and never touch /dev/ttyS2, so two writers can
     never interleave bytes on the line.
  3. serialised requests - one connection at a time, the same mode deduped and
     writes rate limited, so a flood from several clients still yields at most
     50 writes a second.
     Callers already queued when the daemon is about to write are coalesced:
     each is answered, and only the newest mode is written. The bar has one
     state, so the ones in between are noise, not information.

The socket is in /tmp because the card's filesystem refuses Unix sockets.
Nothing is written outside /tmp, the bar and the regulator it already owns:
ee_led.mode in emuelec.conf is never touched, so EmulationStation's own setting
is left as it was found.

    ledd.py --detach    double fork + setsid (this image has no setsid(1))
"""

import fcntl
import os
import select
import signal
import socket
import sys
import termios
import time

TTY = "/dev/ttyS2"
SOCK = "/tmp/ledd.sock"
LOCK = "/tmp/ledd.lock"
REG = "/sys/kernel/debug/regulator/vcc-led/enable"
LOG = "/tmp/led-test/ledd.log"

# Between two writes. 20 ms is a policy floor, not the wire's: a byte at 9600 8N1
# is ~1 ms, so 2 ms still leaves the line 10x under its capacity and lets a
# measurement see the real cost instead of the limiter.
MIN_GAP_S = float(os.environ.get("LEDD_MIN_GAP", "0.02"))
OFF = 9
REJECTED = (10, 11, 12, 13, 14, 15, 16)  # mcu_led's own range check refuses these

# What the bar actually does, as measured on the device. The names in the
# middle column are what mcu_led_ctrl.sh calls each byte - its author's intent -
# and the last column is what the owner's eye reported. Two places where the two
# disagree, both worth remembering:
#   * mode 17 (LED_BREATH_G) was seen breathing BLUE in one run, then green in
#     another. The blue came right after the rail had been raised again, so the
#     first byte after power-up is suspect, not the byte's meaning.
#   * nothing "plain" is steady: a mode change fades, and modes 17..24 breathe.
#
#   byte  vendor name          measured
#   1     LED_G                green
#   2     LED_B                blue, the most saturated
#   3     LED_R                red
#   4     LED_G_B              cyan
#   5     LED_R_G              yellow, a little green
#   6     LED_B_R              purple
#   7     LED_R_B_G            white, lilac-tinted
#   8     LED_SCROLLING        every colour rotating
#   9     LED_OFF              dark (the rail stays up)
#
#   17..23 = the same seven in the same order, breathing (17 green, 18 blue,
#   19 red, 20 cyan, 21 yellow, 22 purple, 23 white). The owner's rule: "the
#   same colour as the plain one, only breathing". 21..23 were not told apart by
#   eye - three breathing reds in a row - and the rule is what settles them.
MODES = {
    "green": (1, "LED_G"),
    "blue": (2, "LED_B"),
    "red": (3, "LED_R"),
    "green_blue": (4, "LED_G_B"),
    "red_green": (5, "LED_R_G"),
    "blue_red": (6, "LED_B_R"),
    "all": (7, "LED_R_B_G"),
    "scrolling": (8, "LED_SCROLLING"),
    "off": (9, "LED_OFF"),
    "breath_green": (17, "LED_BREATH_G"),
    "breath_blue": (18, "LED_BREATH_B"),
    "breath_red": (19, "LED_BREATH_R"),
    "breath_green_blue": (20, "LED_BREATH_G_B"),
    "breath_red_green": (21, "LED_BREATH_R_G"),
    "breath_blue_red": (22, "LED_BREATH_B_R"),
    "breath_all": (23, "LED_BREATH_R_B_G"),
    "breath_white": (24, "LED_BREATH"),
    "mode25": (25, "MODE_25"),  # accepted by the binary, not exposed by the script
}
NAME_OF = {byte: name for byte, name in MODES.values()}

log_file = None


def log(msg):
    global log_file
    if log_file is None:
        # The directory is under /tmp, which a reboot empties: create it, and never let a log problem keep the line
        # closed - the bar matters, the file does not.
        try:
            os.makedirs(os.path.dirname(LOG), exist_ok=True)
            log_file = open(LOG, "a", buffering=1)
        except OSError:
            log_file = False
    if not log_file:
        return
    log_file.write("%s %s\n" % (time.strftime("%H:%M:%S"), msg))


class Bar:
    def __init__(self):
        self.fd = None
        self.byte = None  # the last byte written, None before the first
        self.last = 0.0
        self.powered = False

    def open(self):
        self.fd = os.open(TTY, os.O_WRONLY | os.O_NOCTTY | os.O_NONBLOCK)
        t = termios.tcgetattr(self.fd)
        t[0] = 0  # iflag: raw
        t[1] = 0  # oflag: raw
        t[2] = termios.CS8 | termios.CLOCAL | termios.CREAD  # 8N1, no flow control
        t[3] = 0  # lflag: raw
        t[4] = t[5] = termios.B9600
        termios.tcsetattr(self.fd, termios.TCSANOW, t)

    def power(self, on):
        """The vcc-led regulator, as mcu_led_ctrl.sh handles it: up for any mode
        but OFF, down only for OFF."""
        if on == self.powered:
            return
        with open(REG, "w") as f:
            f.write("1" if on else "0")
        self.powered = on

    def set(self, byte, keep_power=False, force=False):
        """keep_power: write the byte and leave the regulator alone. That is what
        `dark` is for - the LEDs out while the rail stays up, the off half of a
        pulse. Dropping the rail on every pulse would flap the regulator ten
        times a second for no gain. force: say it again although the byte is
        already there, which is the only way to heal an MCU that fell back to its
        own demo."""
        if byte == self.byte and not force:
            # The byte is already there, but the rail may not be: `off` after a
            # pulse ends on a dark byte written with keep_power, and skipping the
            # power step here would leave the regulator up behind a dark bar.
            if not keep_power:
                self.power(byte != OFF)
            return "same"
        wait = MIN_GAP_S - (time.monotonic() - self.last)
        if wait > 0:
            time.sleep(wait)
        was_up = self.powered
        if not keep_power:
            self.power(byte != OFF)
        os.write(self.fd, bytes([byte]))
        if not keep_power and not was_up and byte != OFF:
            # The first byte after the rail comes up is lost (docs/HANDHELD.md, "Open items": a blue nobody asked for,
            # right after vcc-led rose). A lost mode byte leaves the MCU in its own demo - every colour cycling - which
            # looks exactly like the table being ignored. Repeating it costs one byte and closes that.
            os.write(self.fd, bytes([byte]))
        self.byte = byte
        self.last = time.monotonic()
        return "ok"

    def close(self):
        if self.fd is None:
            return
        try:
            self.power(False)
            os.write(self.fd, bytes([OFF]))
        except OSError:
            pass
        os.close(self.fd)
        self.fd = None


def resolve(req):
    """A request as (byte, name), or (None, an error to send back)."""
    key = req.strip().lower()
    if key in MODES:
        return MODES[key]
    try:
        byte = int(key, 0)
    except ValueError:
        return None, "err unknown mode %r" % req
    if byte in REJECTED:
        return None, "err mode %d is refused by the MCU's range check" % byte
    if not 1 <= byte <= 25:
        return None, "err mode %d is out of range (1..9, 17..25)" % byte
    return byte, NAME_OF.get(byte, "RAW_%d" % byte)


# ---- the pulse: what the bar cannot say with colour, it says with time ----
# The ladder the owner asked for. Every step but the last is ONE byte and the
# MCU animates it: green, green breathing, lilac breathing, red breathing. The
# last step is the only thing the bar cannot do itself - 17..24 are all breath,
# none of them a blink - so it is the one case where the daemon runs its own
# timer (200 ms on, 200 ms off), ~8 bytes a second.
#
# (period, duty, mode): period None means "write the mode and leave it".
LEVELS = (
    (None, 1.00, "green"),        # 1: 100-81%, green
    (None, 1.00, "breath_green"),  # 2: 80-61%, green breathing
    (None, 1.00, "breath_all"),    # 3: 60-41%, lilac breathing (23)
    (None, 1.00, "breath_red"),    # 4: 40-21%, red breathing
    (0.40, 0.50, "red"),           # 5: 20-1%, red blinking (ours)
    (None, 1.00, "scrolling"),     # 6: 0, out of the battle
)


class Pulse:
    """The pulse lives here, not in the caller: a client sends one level when
    the health changes, and the animation keeps its own time even while the game
    is busy with a 30 ms frame."""

    def __init__(self, bar):
        self.bar = bar
        self.level = None
        self.owner = None  # the pid the animation belongs to, or None
        self.checked = 0.0
        self.on = False
        self.next_edge = 0.0

    def set(self, level, owner=None):
        self.level = level
        self.owner = owner
        self.on = False
        self.next_edge = 0.0  # one edge due immediately

    def cancel(self):
        self.level = None
        self.owner = None

    def owner_gone(self):
        """The animation must not outlive the process that asked for it: a game
        that crashes mid-battle would leave the bar blinking red for ever. The
        check is a kill(pid, 0) at most once a second, not per edge."""
        if self.owner is None:
            return False
        now = time.monotonic()
        if now - self.checked < 1.0:
            return False
        self.checked = now
        try:
            os.kill(self.owner, 0)
        except ProcessLookupError:
            log("the animation's owner (pid %d) is gone: going dark" % self.owner)
            return True
        except PermissionError:
            pass  # someone else's process: it exists
        return False

    def tick(self):
        """Run the edges that are due; the seconds until the next one, or None."""
        if self.level is None:
            return None
        if self.owner_gone():
            self.cancel()
            self.bar.set(OFF)  # and drop the rail: we are not coming back
            return None
        period, duty, mode = LEVELS[self.level]
        if period is None:  # a factory mode: one byte, the MCU does the rest
            # Not keep_power: the rail is down when the daemon starts and the byte alone lights nothing. This is the
            # same trap set() warns about for OFF - a byte written behind a down regulator is a dark bar that looks
            # like a working one.
            self.bar.set(MODES[mode][0])
            return None
        now = time.monotonic()
        if now >= self.next_edge:
            if self.on:
                self.bar.set(OFF, keep_power=True)
                self.on = False
                self.next_edge = now + period * (1.0 - duty)
            else:
                self.bar.set(MODES[mode][0])
                self.on = True
                self.next_edge = now + period * duty
            now = time.monotonic()
        return max(0.0, self.next_edge - now)


def handle(bar, pulse, req, state):
    if not req:
        return "err empty request"
    if req in ("quit", "stop"):
        state["quit"] = True
        return "ok quitting"
    if req in ("status", "?"):
        level = "none" if pulse.level is None else str(pulse.level + 1)
        if bar.byte is None:
            return "status none level %s" % level
        return "status %d %s level %s" % (bar.byte, NAME_OF.get(bar.byte, "RAW_%d" % bar.byte), level)
    if req.startswith("level"):
        # "level N [pid]": the optional pid is whose animation this is, so the
        # bar cannot outlive a game that crashed.
        argv = req.split()
        try:
            n = int(argv[1])
            owner = int(argv[2]) if len(argv) > 2 else None
        except (IndexError, ValueError):
            return "err level needs 1..%d and an optional pid" % len(LEVELS)
        if not 1 <= n <= len(LEVELS):
            return "err level %d is outside 1..%d" % (n, len(LEVELS))
        if pulse.level != n - 1:
            # Take the bar: whatever was running belongs to the previous reading, and leaving it on until the next tick
            # means the new state is drawn over the old animation (a blink's dark half, a breathing colour) for as long
            # as the gap allows. Off first, then the new mode - the rail comes back up with it, which is also the moment
            # the MCU loses the first byte, so that path is exercised on every change.
            bar.set(OFF)
        pulse.set(n - 1, owner)
        period, duty, mode = LEVELS[n - 1]
        shape = "factory mode" if period is None else "blinking, %.2f s, %d%% on" % (period, duty * 100)
        log("level %d: %s (%s)%s" % (n, mode, shape, "" if owner is None else ", owner pid %d" % owner))
        return "ok level %d (%s, %s)%s" % (n, mode, shape, "" if owner is None else ", owner %d" % owner)
    pulse.cancel()  # any explicit mode ends an animation: it is a statement
    if req in ("dark", "dim"):
        # LEDs out, rail up: the off half of a pulse.
        what = bar.set(OFF, keep_power=True)
        log("%-18s byte %2d %s (rail kept up)" % ("DARK", OFF, what))
        return "%s DARK byte %d" % (what, OFF)
    byte, name = resolve(req)
    if byte is None:
        return name
    what = bar.set(byte)
    log("%-18s byte %2d %s" % (name, byte, what))
    return "%s %s byte %d" % (what, name, byte)


def read_request(conn, empty):
    """The last complete line one client sent, or `empty` if it sent none.

    A client that pipelines several modes in one write gets them coalesced
    here: the earlier ones are states nobody will ever see on the bar.
    """
    data = b""
    while not data.endswith(b"\n"):
        chunk = conn.recv(4096)
        if not chunk:
            break
        data += chunk
    lines = [line for line in data.split(b"\n") if line.strip()]
    if not lines:
        return empty
    if len(lines) > 1:
        log("coalesced %d pipelined request(s); the newest state wins" % (len(lines) - 1))
    return lines[-1].decode("utf-8", "replace").strip()


def serve(bar):
    if os.path.exists(SOCK):
        os.unlink(SOCK)  # safe: the lock says no other ledd is alive
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(SOCK)
    srv.listen(4)
    log("listening on %s; %s open at 9600 8N1, regulator %d" % (SOCK, TTY, int(bar.powered)))
    state = {"quit": False}
    pulse = Pulse(bar)
    last_say = time.monotonic()
    while not state["quit"]:
        wait = pulse.tick()  # the animation keeps its own time
        # The MCU has no acknowledgement: a byte lost on the wire or swallowed by the power-up leaves it in its own
        # demo (every colour cycling) for ever, and a client that only speaks on change never corrects it. Saying the
        # current mode again every ten seconds costs one byte and heals that without the game knowing.
        if pulse.level is not None and LEVELS[pulse.level][0] is None and time.monotonic() - last_say > 10.0:
            # Factory modes only: an animated level has its own edges and a second voice here would fight it.
            bar.set(MODES[LEVELS[pulse.level][2]][0], force=True)
            last_say = time.monotonic()
        # The wait a pulse asks for, but never unbounded: the re-say above and the owner check inside tick only run when
        # this loop comes round, and a factory mode asks for "no edge, ever" - with a blocking select they would never
        # run at all.
        timeout = 10.0 if wait is None else min(wait, 10.0)
        if not select.select([srv], [], [], timeout)[0]:
            continue  # a pulse edge (or the re-say) was due, not a client
        conn, _ = srv.accept()
        conn.settimeout(2.0)
        try:
            req = read_request(conn, "err empty request")
            # Anyone who knocked while this one was being read is answered here
            # and dropped: only the newest state is worth a byte.
            stale = 0
            while select.select([srv], [], [], 0)[0]:
                other, _ = srv.accept()
                other.settimeout(2.0)
                newer = read_request(other, "")
                if newer:
                    req = newer
                    stale += 1
                try:
                    other.sendall(b"coalesced\n")
                except OSError:
                    pass
                other.close()
            if stale:
                log("coalesced %d queued request(s); the newest state wins" % stale)
            reply = handle(bar, pulse, req, state)
        except socket.timeout:
            reply = "err timeout"
        except OSError as exc:
            reply = "err %s" % exc
        try:
            conn.sendall((reply + "\n").encode())
        except OSError:
            pass
        finally:
            conn.close()
    srv.close()
    os.unlink(SOCK)


def main():
    if "--detach" in sys.argv:
        # This image has no setsid(1), and without a new session the daemon dies
        # with the ssh session that started it.
        if os.fork() > 0:
            os._exit(0)
        os.setsid()
        if os.fork() > 0:
            os._exit(0)
        devnull = os.open(os.devnull, os.O_RDWR)
        for fd in (0, 1, 2):
            os.dup2(devnull, fd)
    lock_fd = os.open(LOCK, os.O_CREAT | os.O_RDWR, 0o644)
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        log("another ledd holds %s: leaving, so the line keeps one owner" % LOCK)
        return 1
    bar = Bar()
    bar.open()

    def bye(signum, frame):
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, bye)
    signal.signal(signal.SIGINT, bye)
    try:
        serve(bar)
    finally:
        bar.close()
        if os.path.exists(SOCK):
            os.unlink(SOCK)  # a stale socket would only mislead a client
        log("stopped; bar off, regulator down")
    return 0


if __name__ == "__main__":
    sys.exit(main())
