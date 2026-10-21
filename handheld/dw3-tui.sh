#!/bin/sh
# TUI launcher for Digimon World 3. Writes settings.json, then replaces itself with the game.
# Type a number and press enter. q quits. Empty enter on PLAY starts.
set -eu

game_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
cfg="$game_dir/settings.json"
state="$game_dir/tui.state"
bin="$game_dir/dw2003"

disc=""
for c in \
  "$game_dir/game/dw2003.cue" "$game_dir/game/dw2003.bin" \
  "$game_dir/game/dw3.cue" "$game_dir/game/dw3.bin" \
  "$game_dir/game/Digimon World 2003.cue" "$game_dir/game/Digimon World 2003.bin"; do
  if [ -f "$c" ]; then disc=$c; break; fi
done
if [ -z "$disc" ]; then
  disc=$(find "$game_dir/game" -maxdepth 1 \( -name '*.cue' -o -name '*.bin' \) 2>/dev/null | head -n 1 || true)
fi

hz=50
ff=0
ff_speed=4x
skip=0
battles=0
hit=1
saveany=0
party=0
share=50
xp=0
xp_mul=2
lang=0
lang_on=0

if [ -f "$state" ]; then
  # shellcheck disable=SC1090
  . "$state"
fi

langs="english french german italian spanish usa japanese"
lang_label() {
  i=0
  for w in $langs; do
    if [ "$i" = "$1" ]; then printf '%s' "$w"; return; fi
    i=$((i + 1))
  done
  printf english
}
onoff() { if [ "$1" = 1 ]; then printf '\033[1;32mON\033[0m'; else printf '\033[2moff\033[0m'; fi; }

save_state() {
  cat > "$state" << EOF
hz=$hz
ff=$ff
ff_speed=$ff_speed
skip=$skip
battles=$battles
hit=$hit
saveany=$saveany
party=$party
share=$share
xp=$xp
xp_mul=$xp_mul
lang=$lang
lang_on=$lang_on
EOF
}

bool() { if [ "$1" = 1 ]; then printf true; else printf false; fi; }

write_settings() {
  cat > "$cfg" << EOF
{
  "schema": 1,
  "disc": { "path": "game/$(basename "$disc")" },
  "video": { "window": true, "scale": 2, "fullscreen": true, "refresh": $hz, "renderer": "software" },
  "audio": { "mute": false },
  "memcard1": "card1.mcd",
  "memcard2": "card2.mcd",
  "watchdog": 0,
  "mods": {
    "fast_forward": { "enabled": $(bool "$ff"), "speed": "$ff_speed", "mute": true, "hold": "", "toggle": "" },
    "skip_dialogues": { "enabled": $(bool "$skip"), "fast_forward_waits": false },
    "battle_animations": { "enabled": $(bool "$battles"), "hit_reaction": $(bool "$hit") },
    "global_save": { "enabled": $(bool "$saveany"), "restore_map_state": true },
    "party_xp": { "enabled": $(bool "$party"), "share": $share, "knocked_out": false, "catch_up": false },
    "xp_boost": { "enabled": $(bool "$xp"), "exp": $xp_mul, "form_exp": $xp_mul, "bits": $xp_mul, "manual": false },
    "preset_language": { "enabled": $(bool "$lang_on"), "language": "$(lang_label "$lang")" },
    "widescreen": { "enabled": false }
  }
}
EOF
}

draw() {
  printf '\033[2J\033[H'
  printf '\033[1;36mDIGIMON WORLD 3\033[0m  \033[2mR36S\033[0m\n'
  printf '\033[36m------------------------------\033[0m\n'
  printf '1  Hz                  \033[1m%s\033[0m\n' "$hz"
  printf '2  Fast-forward        %s  \033[1m%s\033[0m\n' "$(onoff "$ff")" "$ff_speed"
  printf '3  Skip dialogues      %s\n' "$(onoff "$skip")"
  printf '4  Skip battle anims   %s\n' "$(onoff "$battles")"
  printf '5    hit reaction      %s\n' "$(onoff "$hit")"
  printf '6  Save anywhere       %s\n' "$(onoff "$saveany")"
  printf '7  Party XP            %s  \033[1m%s%%\033[0m\n' "$(onoff "$party")" "$share"
  printf '8  XP / bits boost     %s  \033[1m%sx\033[0m\n' "$(onoff "$xp")" "$xp_mul"
  printf '9  Language            %s  \033[1m%s\033[0m\n' "$(onoff "$lang_on")" "$(lang_label "$lang")"
  printf '\033[36m------------------------------\033[0m\n'
  printf '\033[1;32m0  PLAY\033[0m\n'
  printf 'q  quit\n'
  printf '\n'
  if [ -n "$disc" ]; then
    printf '\033[2mdisc %s\033[0m\n' "$(basename "$disc")"
  else
    printf '\033[1;31mno disc in game/\033[0m\n'
  fi
  printf '\033[2mD-pad move   A toggle   B/Start play   Select+Start quit\033[0m\n'
  printf '\033[2mkeyboard: number + enter\033[0m\n'
  printf '> '
}

pad_read() {
  # evdev gamepad, if present. Prints one token: up down left right a b start select quit none
  python3 - "$1" << 'PY' || printf none
import select, sys
try:
    import evdev
except ImportError:
    sys.exit(0)
want = ("event-joystick", "event")
devs = []
for path in evdev.list_devices():
    try:
        d = evdev.InputDevice(path)
    except OSError:
        continue
    if "joy" in (d.name or "").lower() or "pad" in (d.name or "").lower() or path.endswith("event0"):
        devs.append(d)
if not devs:
    sys.exit(0)
timeout = float(sys.argv[1])
r, _, _ = select.select(devs, [], [], timeout)
if not r:
    sys.exit(0)
for ev in r[0].read():
    if ev.type == 1 and ev.value == 1:
        name = r[0].absinfo  # placeholder, replaced below
PY
}

if [ ! -x "$bin" ]; then
  printf 'dw2003 missing: %s\n' "$bin" >&2
  exit 1
fi

while :; do
  draw
  IFS= read -r choice || exit 0
  case "$choice" in
    1) if [ "$hz" = 50 ]; then hz=60; else hz=50; fi ;;
    2)
      if [ "$ff" = 0 ]; then
        ff=1
      else
        case "$ff_speed" in
          2x) ff_speed=4x ;;
          4x) ff_speed=8x ;;
          8x) ff_speed=unlimited ;;
          *) ff=0; ff_speed=4x ;;
        esac
      fi
      ;;
    3) skip=$((1 - skip)) ;;
    4) battles=$((1 - battles)) ;;
    5) hit=$((1 - hit)) ;;
    6) saveany=$((1 - saveany)) ;;
    7)
      if [ "$party" = 0 ]; then
        party=1
      else
        share=$((share + 25))
        if [ "$share" -gt 100 ]; then party=0; share=50; fi
      fi
      ;;
    8)
      if [ "$xp" = 0 ]; then
        xp=1
      else
        xp_mul=$((xp_mul + 1))
        if [ "$xp_mul" -gt 5 ]; then xp=0; xp_mul=2; fi
      fi
      ;;
    9)
      if [ "$lang_on" = 0 ]; then
        lang_on=1
      else
        lang=$((lang + 1))
        if [ "$lang" -gt 6 ]; then lang_on=0; lang=0; fi
      fi
      ;;
    0|'') break ;;
    q|Q) printf '\033[2J\033[H'; exit 0 ;;
  esac
done

if [ -z "$disc" ]; then
  printf '\033[2J\033[Hno disc image in %s/game\n' "$game_dir"
  exit 1
fi

save_state
write_settings
printf '\033[2J\033[H\033[36mstarting...\033[0m\n'
export DW3_VIDEO=fbdev
export SDL_VIDEODRIVER=kmsdrm
export SDL_AUDIODRIVER=alsa
cd "$game_dir"
exec "$bin" --config "$cfg" --window --fullscreen --watchdog 0
