#!/bin/sh
# Passive environment probe. Writes ports/dw3/logs/probe.txt and changes nothing else.
# Sourced variables are only read. Missing tools are logged, never treated as failure.
set -u

HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
LOG_DIR="$HERE/logs"
OUT="$LOG_DIR/probe.txt"
BIN="$HERE/dw2003"
mkdir -p "$LOG_DIR"

{
  echo "========== dw3 probe =========="
  echo "date: $(date 2>/dev/null || echo unavailable)"
  echo "uname -m: $(uname -m 2>/dev/null || echo unavailable)"
  echo "uname -a: $(uname -a 2>/dev/null || echo unavailable)"
  echo "--- /etc/os-release ---"
  if [ -r /etc/os-release ]; then
    cat /etc/os-release
  else
    echo "missing"
  fi
  echo "--- framebuffer ---"
  if [ -e /dev/fb0 ]; then
    echo "/dev/fb0: exists"
    if [ -r /dev/fb0 ] && [ -w /dev/fb0 ]; then
      echo "/dev/fb0: readable and writable"
    else
      echo "/dev/fb0: not readable+writable"
    fi
    ls -l /dev/fb0 2>/dev/null || true
  else
    echo "/dev/fb0: absent"
  fi
  echo "--- /dev/dri ---"
  ls -l /dev/dri 2>/dev/null || echo "absent"
  echo "--- wayland ---"
  echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-}"
  echo "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-}"
  echo "SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-}"
  runtime="${XDG_RUNTIME_DIR:-}"
  if [ -n "$runtime" ]; then
    ls -l "$runtime"/wayland-* 2>/dev/null || echo "no wayland socket in XDG_RUNTIME_DIR"
    if [ -n "${WAYLAND_DISPLAY:-}" ]; then
      if [ -S "$runtime/$WAYLAND_DISPLAY" ]; then
        echo "socket $runtime/$WAYLAND_DISPLAY: present"
      else
        echo "socket $runtime/$WAYLAND_DISPLAY: absent (TODO: validate)"
      fi
    fi
  else
    echo "XDG_RUNTIME_DIR unset; wayland socket not resolved (TODO: validate)"
  fi
  echo "--- audio ---"
  if command -v timeout >/dev/null 2>&1 && command -v pactl >/dev/null 2>&1; then
    timeout 2 pactl info 2>&1 || echo "pactl info failed or timed out"
  else
    echo "timeout or pactl unavailable"
  fi
  if command -v aplay >/dev/null 2>&1; then
    aplay -l 2>&1 || true
  else
    echo "aplay unavailable"
  fi
  echo "--- cpu / gpu ---"
  if [ -r /proc/cpuinfo ]; then
    grep -E 'CPU part|Hardware|model name|Processor' /proc/cpuinfo | head -n 20 || true
  else
    echo "/proc/cpuinfo unreadable"
  fi
  ls -d /sys/class/misc/mali* 2>/dev/null || echo "no /sys/class/misc/mali*"
  if [ -e /dev/mali0 ]; then
    echo "/dev/mali0: exists"
  else
    echo "/dev/mali0: absent"
  fi
  ls /sys/class/devfreq 2>/dev/null || echo "no /sys/class/devfreq"
  echo "--- armhf loader / ldd ---"
  echo "DEVICE_HAS_ARMHF=${DEVICE_HAS_ARMHF:-}"
  if [ ! -f "$BIN" ]; then
    echo "binary missing: $BIN"
  elif ! command -v ldd >/dev/null 2>&1; then
    echo "ldd unavailable; library check skipped (TODO: validate)"
  else
    ldd "$BIN" 2>&1 || true
  fi
  echo "--- PortMaster context ---"
  echo "CFW_NAME=${CFW_NAME:-}"
  echo "DEVICE=${DEVICE:-}"
  echo "DISPLAY_WIDTH=${DISPLAY_WIDTH:-}"
  echo "DISPLAY_HEIGHT=${DISPLAY_HEIGHT:-}"
  echo "directory=${directory:-}"
} > "$OUT"

echo "probe: wrote $OUT"
