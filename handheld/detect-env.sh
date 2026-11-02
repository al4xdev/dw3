#!/bin/sh
# Environment detection for the dw3 launcher. Sourced; detect_env only exports variables.
# It never launches the game. Probe overrides (DW3_PROBE_*) exist so the self-test can
# simulate devices without touching this machine's real Wayland or framebuffer.
#
# TODO: validate — Wayland socket paths, ldd on EmuELEC, Mali node name on XF35H,
# SDL autodetection when the driver is left unset.

detect_env() {
  fb="${DW3_PROBE_FB:-}"
  if [ -z "$fb" ]; then
    if [ -r /dev/fb0 ] && [ -w /dev/fb0 ]; then
      fb=1
    else
      fb=0
    fi
  fi

  wayland_display="${DW3_PROBE_WAYLAND_DISPLAY:-}"
  if [ -z "$wayland_display" ]; then
    if [ -n "${WAYLAND_DISPLAY:-}" ]; then
      wayland_display=1
    else
      wayland_display=0
    fi
  fi

  wayland_socket="${DW3_PROBE_WAYLAND_SOCKET:-}"
  if [ -z "$wayland_socket" ]; then
    wayland_socket=0
    runtime="${XDG_RUNTIME_DIR:-}"
    if [ -n "$runtime" ]; then
      if [ -n "${WAYLAND_DISPLAY:-}" ] && [ -S "$runtime/$WAYLAND_DISPLAY" ]; then
        wayland_socket=1
      elif ls "$runtime"/wayland-* >/dev/null 2>&1; then
        wayland_socket=1
      fi
    fi
  fi

  cpu_part="${DW3_PROBE_CPU_PART:-}"
  if [ -z "$cpu_part" ] && [ -r /proc/cpuinfo ]; then
    cpu_part=$(awk -F': ' '/CPU part/ { gsub(/^[ \t]+/, "", $2); print $2; exit }' /proc/cpuinfo 2>/dev/null || true)
  fi

  mali="${DW3_PROBE_MALI:-}"
  if [ -z "$mali" ]; then
    mali=0
    if [ -e /dev/mali0 ] || ls /sys/class/misc/mali* >/dev/null 2>&1; then
      mali=1
    fi
  fi

  pactl_ok="${DW3_PROBE_PACTL:-}"
  if [ -z "$pactl_ok" ]; then
    pactl_ok=0
    if command -v timeout >/dev/null 2>&1 && command -v pactl >/dev/null 2>&1; then
      if timeout 2 pactl info >/dev/null 2>&1; then
        pactl_ok=1
      fi
    fi
  fi

  video_reason=""
  menu_reason=""
  renderer_reason=""
  audio_reason=""

  # Display. Override, then a real Wayland socket (even if a dummy fb0 exists),
  # then the reference framebuffer, then SDL with the driver left unset.
  if [ -n "${DW3_FORCE_VIDEO:-}" ]; then
    DW3_VIDEO="$DW3_FORCE_VIDEO"
    video_reason="DW3_FORCE_VIDEO"
    if [ "$DW3_VIDEO" = "fbdev" ]; then
      DW3_MENU=fb
      menu_reason="forced fbdev"
      if [ -z "${SDL_VIDEODRIVER:-}" ]; then
        SDL_VIDEODRIVER=kmsdrm
      fi
    else
      DW3_MENU=skip
      menu_reason="forced $DW3_VIDEO"
    fi
  elif [ "$wayland_display" = "1" ] && [ "$wayland_socket" = "1" ]; then
    DW3_VIDEO=sdl2
    SDL_VIDEODRIVER=wayland
    DW3_MENU=skip
    video_reason="Wayland display and socket present; fb0 ignored (dummy fb0 must not win)"
    menu_reason="Wayland: framebuffer menu skipped"
  elif [ "$wayland_display" = "1" ] && [ "$wayland_socket" != "1" ]; then
    DW3_VIDEO=sdl2
    unset SDL_VIDEODRIVER
    DW3_MENU=skip
    video_reason="WAYLAND_DISPLAY set but socket missing; SDL chooses the driver (TODO: validate)"
    menu_reason="Wayland env without a resolved socket"
  elif [ "$fb" = "1" ]; then
    DW3_VIDEO=fbdev
    DW3_MENU=fb
    # Legacy launcher on the reference handheld. Do not inherit a stale driver.
    SDL_VIDEODRIVER=kmsdrm
    video_reason="/dev/fb0 writable and no Wayland socket"
    menu_reason="framebuffer menu"
  else
    DW3_VIDEO=sdl2
    unset SDL_VIDEODRIVER
    DW3_MENU=skip
    video_reason="no writable /dev/fb0 and no Wayland socket; SDL chooses the driver (TODO: validate)"
    menu_reason="no framebuffer"
  fi

  if [ "$pactl_ok" = "1" ]; then
    SDL_AUDIODRIVER=pulseaudio
    audio_reason="pactl info succeeded"
  else
    SDL_AUDIODRIVER=alsa
    audio_reason="pactl unavailable or failed; alsa"
  fi

  # Renderer allowlist. gles only on the reference shape. Anything else is software.
  allow_gles=0
  gles_why="software: "
  if [ "$wayland_display" = "1" ] || [ "$wayland_socket" = "1" ]; then
    gles_why="${gles_why}Wayland present"
  elif [ "$fb" != "1" ]; then
    gles_why="${gles_why}no writable /dev/fb0"
  elif [ "$cpu_part" != "0xd04" ]; then
    gles_why="${gles_why}CPU part ${cpu_part:-unknown} != 0xd04"
  elif [ "$mali" != "1" ]; then
    gles_why="${gles_why}no Mali node (/sys/class/misc/mali* or /dev/mali0)"
  else
    allow_gles=1
    gles_why="gles: no Wayland, writable /dev/fb0, CPU part 0xd04, Mali node present"
  fi

  if [ "$allow_gles" = "1" ]; then
    DW3_RENDERER=gles
  else
    DW3_RENDERER=software
  fi
  renderer_reason="$gles_why"

  if [ -n "${DW3_FORCE_RENDERER:-}" ]; then
    DW3_RENDERER="$DW3_FORCE_RENDERER"
    renderer_reason="DW3_FORCE_RENDERER=$DW3_FORCE_RENDERER (overrides allowlist)"
  elif [ -f settings.json ]; then
    file_renderer=$(python3 -c 'import json; print(json.load(open("settings.json")).get("video", {}).get("renderer", ""))' 2>/dev/null || true)
    if [ -n "$file_renderer" ]; then
      DW3_RENDERER="$file_renderer"
      renderer_reason="settings.json video.renderer=$file_renderer (overrides allowlist)"
    fi
  fi

  export DW3_VIDEO DW3_MENU DW3_RENDERER SDL_AUDIODRIVER
  if [ -n "${SDL_VIDEODRIVER:-}" ]; then
    export SDL_VIDEODRIVER
  fi

  echo "detect: DW3_VIDEO=$DW3_VIDEO ($video_reason)"
  echo "detect: DW3_MENU=$DW3_MENU ($menu_reason)"
  echo "detect: DW3_RENDERER=$DW3_RENDERER ($renderer_reason)"
  echo "detect: SDL_AUDIODRIVER=$SDL_AUDIODRIVER ($audio_reason)"
  if [ -n "${SDL_VIDEODRIVER:-}" ]; then
    echo "detect: SDL_VIDEODRIVER=$SDL_VIDEODRIVER"
  else
    echo "detect: SDL_VIDEODRIVER unset"
  fi
}

# Self-test when executed, never when sourced.
if [ "${0##*/}" = "detect-env.sh" ]; then
  fail=0
  check() {
    label="$1"
    expect="$2"
    got="$3"
    if [ "$got" = "$expect" ]; then
      echo "ok $label=$got"
    else
      echo "FAIL $label: got '$got' expected '$expect'"
      fail=1
    fi
  }
  run_case() {
    name="$1"
    unset DW3_VIDEO DW3_MENU DW3_RENDERER SDL_VIDEODRIVER SDL_AUDIODRIVER DW3_FORCE_VIDEO DW3_FORCE_RENDERER
    unset DW3_PROBE_FB DW3_PROBE_WAYLAND_DISPLAY DW3_PROBE_WAYLAND_SOCKET DW3_PROBE_CPU_PART DW3_PROBE_MALI DW3_PROBE_PACTL
    shift
    # shellcheck disable=SC2068
    eval "$@"
    echo "----- $name -----"
    detect_env
  }

  cd "$(dirname "$0")" || exit 1
  rm -f settings.json

  run_case "a-reference" \
    DW3_PROBE_FB=1 DW3_PROBE_WAYLAND_DISPLAY=0 DW3_PROBE_WAYLAND_SOCKET=0 \
    DW3_PROBE_CPU_PART=0xd04 DW3_PROBE_MALI=1 DW3_PROBE_PACTL=0
  check video fbdev "$DW3_VIDEO"
  check menu fb "$DW3_MENU"
  check renderer gles "$DW3_RENDERER"
  check audio alsa "$SDL_AUDIODRIVER"
  check driver kmsdrm "$SDL_VIDEODRIVER"

  # Legacy launcher exported exactly these three. Anything else is additive.
  legacy_video=fbdev
  legacy_driver=kmsdrm
  legacy_audio=alsa
  if [ "$DW3_VIDEO" = "$legacy_video" ] && [ "$SDL_VIDEODRIVER" = "$legacy_driver" ] && [ "$SDL_AUDIODRIVER" = "$legacy_audio" ]; then
    echo "ok regression: reference exports match legacy DW3_VIDEO/SDL_VIDEODRIVER/SDL_AUDIODRIVER"
  else
    echo "FAIL regression diff"
    fail=1
  fi

  run_case "b-wayland" \
    DW3_PROBE_FB=1 DW3_PROBE_WAYLAND_DISPLAY=1 DW3_PROBE_WAYLAND_SOCKET=1 \
    DW3_PROBE_CPU_PART=0xd04 DW3_PROBE_MALI=1 DW3_PROBE_PACTL=1
  check video sdl2 "$DW3_VIDEO"
  check menu skip "$DW3_MENU"
  check renderer software "$DW3_RENDERER"
  check driver wayland "$SDL_VIDEODRIVER"

  run_case "c-fallback" \
    DW3_PROBE_FB=0 DW3_PROBE_WAYLAND_DISPLAY=0 DW3_PROBE_WAYLAND_SOCKET=0 \
    DW3_PROBE_CPU_PART=0x805 DW3_PROBE_MALI=0 DW3_PROBE_PACTL=0
  check video sdl2 "$DW3_VIDEO"
  check menu skip "$DW3_MENU"
  check renderer software "$DW3_RENDERER"
  if [ -z "${SDL_VIDEODRIVER:-}" ]; then
    echo "ok driver unset"
  else
    echo "FAIL driver should be unset, got $SDL_VIDEODRIVER"
    fail=1
  fi

  run_case "d-override" \
    DW3_FORCE_RENDERER=software DW3_PROBE_FB=1 DW3_PROBE_WAYLAND_DISPLAY=0 \
    DW3_PROBE_WAYLAND_SOCKET=0 DW3_PROBE_CPU_PART=0xd04 DW3_PROBE_MALI=1 DW3_PROBE_PACTL=0
  check video fbdev "$DW3_VIDEO"
  check renderer software "$DW3_RENDERER"

  exit "$fail"
fi
