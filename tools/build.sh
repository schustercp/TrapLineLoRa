#!/usr/bin/env bash
#
# Build, test and record the output where Claude can read it.
#
# The toolchain in ~/.platformio is a macOS (Mach-O) binary, so it cannot be
# executed from the Linux side of the device bridge. The practical loop is:
# you run this, it leaves logs in logs/, and those get read from the repo.
#
#   ./tools/build.sh              # build survey + run native tests
#   ./tools/build.sh survey       # just the firmware
#   ./tools/build.sh test         # just the unit tests
#   ./tools/build.sh clean        # wipe build state first
#
set -uo pipefail
cd "$(dirname "$0")/.."

mkdir -p logs
PIO="${PIO:-pio}"
command -v "$PIO" >/dev/null || PIO="$HOME/.platformio/penv/bin/pio"

what="${1:-all}"
rc=0

stamp() { date "+%Y-%m-%d %H:%M:%S"; }

run() {   # run <logfile> <description> <command...>
  local log="logs/$1"; shift
  local desc="$1"; shift
  {
    echo "=== $desc"
    echo "=== $(stamp)"
    echo "=== $*"
    echo
  } > "$log"
  echo "--> $desc  (logs/$(basename "$log"))"
  "$@" >> "$log" 2>&1
  local status=$?
  echo "" >> "$log"
  echo "=== exit status $status" >> "$log"
  if [ $status -ne 0 ]; then
    rc=$status
    echo "    FAILED (exit $status) — see $log"
    tail -25 "$log" | sed 's/^/    /'
  else
    echo "    ok"
  fi
  return 0
}

if [ "$what" = "clean" ]; then
  run clean.log "pio run -t clean" "$PIO" run -t clean
  what="all"
fi

if [ "$what" = "all" ] || [ "$what" = "test" ]; then
  run test-native.log "native unit tests" "$PIO" test -e native
fi

if [ "$what" = "all" ] || [ "$what" = "survey" ]; then
  run build-survey.log "build survey firmware" "$PIO" run -e survey
  # Memory report is worth keeping alongside the build.
  run size-survey.log "memory usage" "$PIO" run -e survey -t size
fi

echo
if [ $rc -eq 0 ]; then
  echo "all good — logs in logs/"
else
  echo "something failed — logs in logs/"
fi

# Warnings are the point of this exercise; surface them even on success.
if grep -qiE "warning:" logs/*.log 2>/dev/null; then
  echo
  echo "warnings:"
  grep -hiE "warning:" logs/*.log | sed 's/^/  /' | sort -u | head -40
fi

exit $rc
