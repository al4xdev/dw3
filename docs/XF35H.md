# The XF35H: panel, LED bar and rumble

Everything here is this one console - the XF35H (RK3326, 640x480) this port was tuned on and whose LED bar the `led_bar`
mod drives. Nothing in this document generalises to another device.

The second machine this port runs on: hostname `alex_retro`, EmuELEC 4.7-Nexus nightly 20250820, board
`Rockchip rk3326 evb lpddr3 v12 board for linux`, kernel `5.10.160`, aarch64, panel on DSI-1. Access: `ssh
root@<ip>` (dropbear, port 22, password `emuelec`). The address is DHCP — it moved from `192.168.0.103` to
`192.168.0.106` between sessions; reserve it before scripting anything against it.

The sshd is not part of the image's boot: it comes from a PortMaster port (`/storage/roms/ports_scripts/
server_ssh_enable.sh`) started from inside `emustation.service`'s cgroup. It survives `systemctl stop
emustation` (the unit is `KillMode=process`) but a reboot does not bring it back on its own — physical access
is the fallback.

Everything below was established by read-only inspection (sysfs/`/proc` reads, `cat` of the script and of the
binary) plus two reversible writes: the backlight and `systemctl stop emustation`. Nothing else on the
machine was modified.

## Blanking the panel (backlight)

- Node: `/sys/class/backlight/backlight/{brightness,max_brightness,actual_brightness}`, max 255. Values seen:
  79 after boot, 13 later — EmulationStation drives it.
- `echo 0 > brightness` blanks the panel at once and keeps it blank while EmulationStation keeps drawing
  (this is the panel, not the compositor). Held for more than a minute with ES running.
- `echo 79 > brightness` (or any reboot, which reinitialises the driver) brings it back.
- `/sys/class/graphics/fb0/blank` exists on this kernel but is a no-op: writing `1` leaves it empty. Do not
  build anything on framebuffer blanking here.

## Stopping and starting the frontend

`/usr/lib/systemd/system/emustation.service`: `ExecStartPre=emustation-config`, `ExecStartPre=/usr/bin/
killes.sh`, `ExecStart=/bin/bash -c 'ee_splash_intro_delay.sh && emulationstation'`, `KillMode=process`,
`Restart=always`, `RestartSec=2`, `StartLimitInterval=0`, `WantedBy=emuelec.target`, `disabled`.

`Restart=always` only covers unexpected exits: after an explicit `systemctl stop emustation` the unit stays
`inactive` (`pidof emulationstation` empty, verified for minutes) until `systemctl start emustation` or a
reboot. The sshd living in that cgroup is not killed by the stop.

## The LED bar is on a UART, not in sysfs

`/sys/class/leds/` is empty and the joystick's `capabilities/led` is `0`, so there is no `brightness` file to
write: the bar is an MCU spoken to over serial by a userspace helper.

`/usr/bin/mcu_led_ctrl.sh <MODE>` is the sanctioned entry point. It raises the `vcc-led` regulator through
`/sys/kernel/debug/regulator/vcc-led/enable` (unless the mode is `deinit`/`LED_OFF`), then runs
`/usr/bin/mcu_led /dev/ttyS2 chgmode <N> 1`.

| mode | byte | mode | byte |
| --- | --- | --- | --- |
| `LED_G` | 1 | `LED_BREATH_G` | 17 |
| `LED_B` | 2 | `LED_BREATH_B` | 18 |
| `LED_R` | 3 | `LED_BREATH_R` | 19 |
| `LED_G_B` | 4 | `LED_BREATH_G_B` | 20 |
| `LED_R_G` | 5 | `LED_BREATH_R_G` | 21 |
| `LED_B_R` | 6 | `LED_BREATH_B_R` | 22 |
| `LED_R_B_G` | 7 | `LED_BREATH_R_B_G` | 23 |
| `LED_SCROLLING` | 8 | `LED_BREATH` (all three) | 24 |
| `LED_OFF` / `deinit` | 9 | | |

`init` is not a byte of its own: the script substitutes the persisted mode, `ee_led.mode=` in
`/storage/.config/emuelec/configs/emuelec.conf` (default `LED_OFF`), and `/usr/bin/emuelec_autostart.sh`
applies that value at boot.

In EmulationStation the bar is the "LEDS SETTINGS" screen, option "LED mode", setting id `ee_led.mode`
(`GuiMenu::openLEDSettings`); the binary carries the string `/usr/bin/mcu_led_ctrl.sh ` and runs it as soon
as the option changes, so the screen is a live preview and the config file only remembers it.

### The wire protocol behind `mcu_led`

Disassembly of `/usr/bin/mcu_led` (stripped aarch64 ELF, 67 KB; `.dumps/emuelec/mcu_led.dis`):

- `open(path, O_WRONLY | O_NOCTTY | O_NONBLOCK)`; termios with `PARENB`, `CSTOPB` and `CSIZE` cleared, `CS8`
  and `CLOCAL` set — **9600 8N1**, no flow control. The tool never reads from the port.
- `mcu_led <dev> chgmode <mode> [times]`: `mode` must be **1..9 or 17..25** (`10..16`, `<= 0`, `> 25` print
  `Invalid mode value: %d` and exit `-1`); `times` is `1..9`, default `1`.
- `mcu_led <dev> init <leds> [times]`: `leds` must be `4` or `8` (byte `0x20` or `0x21`), same `times`.
- What goes out is that **single byte, repeated `times` times**, one `write()` per byte. There is no header,
  length or checksum.

So a mode change is a handful of bytes at 9600 baud — nothing to frame, nothing to acknowledge.

### What the bar actually does (measured, not the script's names)

The names above are the vendor script's intent. Writing the bytes by hand and looking at the bar gives a different
table in two ways, and the second one matters: **the combinations mix**. Mode 4 is not "green and blue side by side",
it is cyan; 5 is yellow, 6 purple, 7 a white pulled towards lilac.

| byte | script name | measured | byte | script name | measured |
| --- | --- | --- | --- | --- | --- |
| 1 | `LED_G` | green | 17 | `LED_BREATH_G` | green, breathing |
| 2 | `LED_B` | blue, the most saturated | 18 | `LED_BREATH_B` | blue, breathing |
| 3 | `LED_R` | red | 19 | `LED_BREATH_R` | red, breathing |
| 4 | `LED_G_B` | **cyan** | 20 | `LED_BREATH_G_B` | cyan, breathing |
| 5 | `LED_R_G` | **yellow**, a little green | 21 | `LED_BREATH_R_G` | yellow, breathing |
| 6 | `LED_B_R` | **purple** | 22 | `LED_BREATH_B_R` | purple, breathing |
| 7 | `LED_R_B_G` | **white**, lilac-tinted | 23 | `LED_BREATH_R_B_G` | white/lilac, breathing |
| 8 | `LED_SCROLLING` | every colour rotating | 24 | `LED_BREATH` | (not told apart from 23 by eye) |

**The rule that closes 17–24:** the breathing bytes are the plain ones in the same order — "the same colour, only
breathing". 21–23 were not told apart by eye (three breathing reds in a row) and the rule is what settles them.

Two more things the eye taught:

- **Nothing plain is steady.** A mode change fades, which reads as a breath when it is watched for a few seconds; and
  17–24 breathe on their own. There is **no blink mode**: a blink has to be made by writing 9 and the mode back, which
  is what the `led_bar` mod's daemon does for its last step.
- **There is no brightness.** A mode byte and nothing else; intensity can only be a duty cycle in time.

### Driving it: the daemon, and what each path costs

Measured on the console, per change: `/usr/bin/mcu_led_ctrl.sh LED_x` **66 ms**, `/usr/bin/mcu_led` alone **18 ms**,
opening and closing `/dev/ttyS2` with no fork at all **~14 ms** (the tty's drain, in jiffies), **the same fd held open
0.15–0.9 ms**, and through the daemon **1.7–2.4 ms** (max 5.8) for a whole socket round trip. So the expensive part is
not the byte: it is paying the setup again.

That is why the bar is driven by one process, `handheld/ledd.py` (`ledd` in the package), and not by whoever wants a
colour: it holds the fd, takes an `flock` so there is only ever one of it, coalesces callers that queue behind each
other, runs the animation's own clock, and turns the bar off within a second of the pid that asked for the animation
going away — a crash leaves no bar blinking for ever. The game's side is `port/game/led.c` (`docs/LAUNCHER.md`, "LED
bar"): one message per change of reading.

## Rumble

- Device `play_joystick` (`/devices/platform/play_joystick`), exposed as `/dev/input/event0` and
  `/dev/input/js0` (its keyboard half is `zed_keyboard`, `event5`).
- `/sys/class/input/input4/capabilities/ff` = `107030000 0`: **FF_RUMBLE**, **FF_PERIODIC** (square,
  triangle, sine) and **FF_GAIN**. No `FF_CONSTANT`, no separate left/right motors — one motor.
- `libSDL2.so` is on the image, so `SDL_GameControllerRumble()` / `SDL_HapticRumblePlay()` reach it with no
  extra dependency. There is no `evtest` or `fftest` to poke it by hand — but **`python3` 3.11.2 is there** (the
  handheld menu runs on it, and so does `ledd`), with `mkfifo` and `nc`.

## What the game can do from C

- **LED bar, cheapest correct path:** `system("/usr/bin/mcu_led_ctrl.sh LED_R")` — or `posix_spawn` to skip
  the shell. It keeps the regulator handling and the mode table in one place, and it is exactly what
  EmulationStation does. Cost is a fork+exec+shell per call (milliseconds): fine on events (menu, damage,
  power-up, death), wrong per frame. **Measured at 66 ms a call: not fine on anything that repeats.** The mod uses
  the daemon instead.
- **LED bar, direct path:** replicate the script (`vcc-led` on, open `/dev/ttyS2` at 9600 8N1, write the mode
  byte). Worth it only if the fd is then **kept open**: an open/write/close per change costs ~14 ms, the same fd held
  open 0.15–0.9 ms. It means owning a UART shared with the MCU that also handles charging — which is why exactly one
  process does it (`ledd`) and everyone else talks to that process.
- **Rumble:** free through SDL, no shell involved.

## Open items

- **A blue that nobody asked for.** During an alternation of 1 and 17 the bar was seen breathing blue, and the daemon's
  own log proves no blue byte was written. It happened right after a block that had dropped and raised `vcc-led`, so the
  first byte after power-up is the suspect. Cheap mitigation if it recurs: write the mode twice when the rail was just
  raised.
- Mode 25 passes the binary's range check and the script does not expose it; never exercised.
- `init`'s `4`/`8` argument (bar length?) was read, not exercised.
- No custom colour or pattern exists: the MCU takes mode bytes only. Anything finer needs the protocol
  captured (`cat -v /dev/ttyS2` while ES switches mode, or `strace -e write /usr/bin/mcu_led`).
- The `times` argument: the binary repeats the byte (1..9) and whether the MCU does anything with a repeat is untested.

## Reproducing the inspection

```fish
sshpass -p emuelec ssh root@192.168.0.106 'cat /usr/bin/mcu_led_ctrl.sh' > .dumps/emuelec/mcu_led_ctrl.sh
sshpass -p emuelec ssh root@192.168.0.106 'cat /usr/bin/mcu_led'        > .dumps/emuelec/mcu_led.bin
cd .dumps/emuelec && uv run --with capstone python mcu_dis.py   # writes mcu_led.dis and mcu_led.rodata.txt
```

`mcu_dis.py` needs `capstone` (the host's binutils has no aarch64 support).
