#!/usr/bin/env bash
#
# Flash one XIAO, or every connected XIAO in turn, without unplugging anything.
#
#   ./tools/flash.sh --list                      what is connected, and its serial
#   ./tools/flash.sh                             flash ALL connected XIAOs, one at a time
#   ./tools/flash.sh --serial 4D14A35E33083C3D   flash just that board
#   ./tools/flash.sh --port /dev/cu.usbmodem101  flash whatever is on that port
#   ./tools/flash.sh --env survey_sense          override the environment
#
# WHY NOT JUST `--upload-port`
# ---------------------------
# You can, and for a one-off it is fine:
#
#     pio run -e survey -t upload --upload-port /dev/cu.usbmodem101
#
# Two things make that awkward as a habit.
#
# 1. The port you name is NOT the port the firmware is written to. This board
#    sets use_1200bps_touch, so PlatformIO opens the named port at 1200 baud,
#    which reboots the XIAO into its UF2 bootloader; the bootloader enumerates
#    as a DIFFERENT device, and PlatformIO then waits for that new port and
#    uploads there. So `--upload-port` names the board to *knock on*, not the
#    destination. (Having a second XIAO plugged in is safe: PlatformIO snapshots
#    the existing ports before knocking and ignores everything already present.)
#
# 2. /dev/cu.usbmodem101 and /dev/cu.usbmodem11301 are assigned by which
#    physical socket the cable is in and in what order things enumerated. Move a
#    cable, reboot, or flash one board and the numbers can shuffle - so the name
#    that flashed unit A this morning may be unit B this afternoon.
#
# The USB serial number does not move. On this board it is also the FICR device
# ID, which is exactly what the firmware prints for `id` and what it advertises
# as TrapSurvey-XXXX. One identifier, consistent from the cable to the console:
#
#     4D14A35E33083C3D    ->  device id 4D14A35E33083C3D  ->  TrapSurvey-3C3D
#
# This script resolves serial -> current port immediately before each upload, so
# a reshuffle mid-run cannot flash the wrong board.
#
set -uo pipefail
cd "$(dirname "$0")/.."

PIO="${PIO:-pio}"
command -v "$PIO" >/dev/null || PIO="$HOME/.platformio/penv/bin/pio"
PY="$HOME/.platformio/penv/bin/python"
command -v "$PY" >/dev/null 2>&1 || PY="python3"

ENVNAME="survey"
WANT_SERIAL=""
WANT_PORT=""
LIST_ONLY=0

while [ $# -gt 0 ]; do
  case "$1" in
    --list)   LIST_ONLY=1; shift ;;
    --serial) WANT_SERIAL="${2:-}"; shift 2 ;;
    --port)   WANT_PORT="${2:-}";   shift 2 ;;
    --env)    ENVNAME="${2:-}";     shift 2 ;;
    -h|--help) sed -n '2,45p' "$0"; exit 0 ;;
    *) echo "unknown option: $1"; exit 2 ;;
  esac
done

SCAN_PY='
import json,re,sys
PIDS={"0044":("plain XIAO nRF52840","app","survey"),
      "0045":("XIAO nRF52840 Sense","app","survey_sense"),
      "8044":("plain XIAO nRF52840","BOOTLOADER","survey"),
      "8045":("XIAO nRF52840 Sense","BOOTLOADER","survey_sense")}
try: devs=json.load(sys.stdin)
except Exception: sys.exit(1)
mode=sys.argv[1]
want=sys.argv[2] if len(sys.argv)>2 else ""
rows=[]
for d in devs:
    raw=(d.get("hwid") or "").upper()
    # VID:PID needs the spaces gone ("VID:PID = 2886:0044" appears on some
    # hosts); SER must be matched on the ORIGINAL, because once the spaces are
    # stripped "SER=4D14...3C3D LOCATION=0-1" becomes "...3C3DLOCATION=0-1" and
    # a greedy [0-9A-Z]+ happily eats the word LOCATION into the serial.
    m=re.search(r"VID:PID=2886:([0-9A-F]{4})", raw.replace(" ",""))
    if not m: continue
    pid=m.group(1).lower()
    sm=re.search(r"SER=([0-9A-F]+)(?:\s|$)", raw)
    rows.append((d.get("port",""), sm.group(1) if sm else "", pid))
if mode=="list":
    if not rows: sys.exit(1)
    for port,ser,pid in rows:
        name,phase,env=PIDS.get(pid,("unknown","?","?"))
        print("  %-24s serial %-18s %s, %s" % (port, ser or "?", name, phase))
        if phase=="BOOTLOADER":
            print("      in the bootloader - it is not running firmware right now")
        else:
            print("      running the %s build (PID says which build, not which board)" % env)
elif mode=="serials":
    for port,ser,pid in rows: print(ser or port)
elif mode=="port-of":
    for port,ser,pid in rows:
        if ser==want: print(port); sys.exit(0)
    sys.exit(1)
'

scan() { "$PIO" device list --json-output 2>/dev/null | "$PY" -c "$SCAN_PY" "$@"; }

if [ "$LIST_ONLY" = "1" ]; then
  echo "connected XIAOs:"
  scan list || echo "  (none with USB VID 0x2886)"
  exit 0
fi

flash_port() {
  local port="$1" label="$2"
  echo
  echo "=============================================================="
  echo " flashing $label"
  echo "   env  $ENVNAME"
  echo "   port $port"
  echo "=============================================================="
  "$PIO" run -e "$ENVNAME" -t upload --upload-port "$port"
  local rc=$?
  if [ $rc -ne 0 ]; then
    echo
    echo "FAILED ($rc) on $label."
    echo "  If it could not find the upload port, double-tap RESET on that"
    echo "  board - a XIAO-BOOT or XIAO-SENSE drive appears - and retry."
    return $rc
  fi
  echo "OK: $label"
  return 0
}

# --- explicit port: no resolution, just do it -------------------------------
if [ -n "$WANT_PORT" ]; then
  flash_port "$WANT_PORT" "$WANT_PORT"
  exit $?
fi

# --- one named serial -------------------------------------------------------
if [ -n "$WANT_SERIAL" ]; then
  PORT="$(scan port-of "$WANT_SERIAL")"
  if [ -z "$PORT" ]; then
    echo "No connected board with serial $WANT_SERIAL."
    echo
    echo "connected XIAOs:"
    scan list || echo "  (none)"
    exit 1
  fi
  flash_port "$PORT" "serial $WANT_SERIAL"
  exit $?
fi

# --- everything, one at a time ----------------------------------------------
SERIALS="$(scan serials)"
if [ -z "$SERIALS" ]; then
  echo "No Seeed devices found. Check the cables carry data, not just power."
  exit 1
fi

echo "will flash these, in order:"
scan list
FAILED=0
COUNT=0
for ser in $SERIALS; do
  COUNT=$((COUNT + 1))
  # Re-resolve every time: the previous upload re-enumerated the bus and the
  # port names may have shifted underneath us.
  PORT="$(scan port-of "$ser")"
  if [ -z "$PORT" ]; then
    # Not a serial we can resolve (or it was a bare port name); try it directly.
    PORT="$ser"
  fi
  flash_port "$PORT" "serial $ser" || FAILED=$((FAILED + 1))
  sleep 2
done

echo
echo "=============================================================="
if [ "$FAILED" -eq 0 ]; then
  echo " all $COUNT board(s) flashed with env $ENVNAME"
  echo
  echo " confirm they match before measuring anything:"
  echo "   ./tools/twoport.py check"
else
  echo " $FAILED of $COUNT board(s) FAILED - see above"
fi
echo "=============================================================="
exit $FAILED
