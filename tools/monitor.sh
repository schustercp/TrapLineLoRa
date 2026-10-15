#!/usr/bin/env bash
#
# Capture serial output to logs/serial-<timestamp>.log while also showing it.
#
# Auto-detects the XIAO by its USB vendor ID (0x2886, Seeed). Letting
# PlatformIO pick the port on its own is not safe on macOS — it will happily
# attach to a Bluetooth headset, which is exactly what happened the first time
# this script was used.
#
#   ./tools/monitor.sh                     # find the XIAO and attach
#   ./tools/monitor.sh /dev/cu.usbmodem101 # or name the port yourself
#   ./tools/monitor.sh --list              # just show what is connected
#
# --list also decodes the USB product ID. Read that carefully: in application
# mode the PID comes from the *firmware you flashed*, not from the silicon, so
# it tells you which environment is on the board and nothing else. Only the
# bootloader PID (0x80xx) is evidence about the hardware — see docs/bring-up.md
# §1b.
#
# Ctrl-C to stop. The log stays in logs/.
#
set -uo pipefail
cd "$(dirname "$0")/.."

mkdir -p logs
PIO="${PIO:-pio}"
command -v "$PIO" >/dev/null || PIO="$HOME/.platformio/penv/bin/pio"
PY="$HOME/.platformio/penv/bin/python"
command -v "$PY" >/dev/null 2>&1 || PY="python3"

SEEED_VID="2886"

# Shared by --list and the auto-detect path.
DECODE_PY='
import json,re,sys
PIDS = {
  "0044": ("XIAO nRF52840",        "application", "survey"),
  "8044": ("XIAO nRF52840",        "BOOTLOADER",  "survey"),
  "0045": ("XIAO nRF52840 Sense",  "application", "survey_sense"),
  "8045": ("XIAO nRF52840 Sense",  "BOOTLOADER",  "survey_sense"),
}
def seeed(d):
    h = (d.get("hwid") or "").upper().replace(" ", "")
    m = re.search(r"VID:PID=2886:([0-9A-F]{4})", h)
    return m.group(1).lower() if m else None
try:
    devs = json.load(sys.stdin)
except Exception:
    sys.exit(1)
mode = sys.argv[1]
hits = [(d, seeed(d)) for d in devs]
hits = [(d, p) for d, p in hits if p]
if mode == "port":
    if not hits: sys.exit(1)
    print(hits[0][0].get("port", ""))
    sys.exit(0)
if not hits:
    sys.exit(1)
for d, pid in hits:
    name, phase, env = PIDS.get(pid, ("unknown Seeed device", "?", "?"))
    print("  %s" % d.get("port", "?"))
    print("     USB 2886:%s  ->  %s, %s" % (pid, name, phase))
    if phase == "BOOTLOADER":
        print("     UF2 bootloader is running. This PID IS evidence about the")
        print("     hardware: it is factory-programmed. Board = %s." % name)
    elif phase == "application":
        print("     NOTE: this PID comes from the flashed firmware (env %s)," % env)
        print("     not from the silicon. It is not proof of which board this is.")
'

decode_all() { "$PIO" device list --json-output 2>/dev/null | "$PY" -c "$DECODE_PY" show; }
find_port()  { "$PIO" device list --json-output 2>/dev/null | "$PY" -c "$DECODE_PY" port; }

show_list() {
  echo "connected serial devices:"
  "$PIO" device list 2>/dev/null | sed 's/^/  /'
  echo
  if decode_all; then :; else
    echo "  (no Seeed device with USB VID 0x$SEEED_VID present)"
  fi
}

if [ "${1:-}" = "--list" ]; then
  show_list
  exit 0
fi

PORT="${1:-}"
if [ -z "$PORT" ]; then
  PORT="$(find_port)"
  if [ -z "$PORT" ]; then
    echo "No Seeed device (USB VID 0x$SEEED_VID) found."
    echo
    show_list
    echo
    echo "If the board is plugged in but not listed:"
    echo "  * double-tap RESET — a XIAO-* drive should appear, which means the"
    echo "    bootloader is alive and the application is not"
    echo "  * check the cable carries data, not just power"
    exit 1
  fi
  echo "found XIAO on $PORT"
fi

LOG="logs/serial-$(date +%Y%m%d-%H%M%S).log"
echo "logging to $LOG   (Ctrl-C to stop)"
echo "--- port $PORT" > "$LOG"
"$PIO" device monitor -e survey --port "$PORT" 2>&1 | tee -a "$LOG"
