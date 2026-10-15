#!/usr/bin/env bash
#
# Cross-compile src/ for the real target without PlatformIO.
#
# WHY THIS EXISTS
#   PlatformIO's toolchain is installed per-machine and is a macOS binary on the
#   development Mac, so `pio` can only run there. This script bootstraps its own
#   Linux toolchains and framework sources into a cache, then compiles our
#   sources with the same flags PlatformIO uses — so a compile check can run
#   anywhere with network access: CI, a Linux box, a sandbox.
#
#   It compiles only. Linking, size and upload stay with `pio` on the Mac, where
#   the linker script, SoftDevice and bootloader live. Use tools/build.sh there.
#
# TWO COMPILERS, ON PURPOSE
#   production (gcc 7.2.1) — byte-for-byte the compiler PlatformIO ships for
#     this platform, pulled from the PlatformIO registry. Its warnings are
#     exactly the warnings a real build produces. This is the gate.
#   modern (gcc 13.3.1) — a stricter second opinion. It has already caught a
#     format-truncation bug that 7.2.1 waved through. Advisory, not a gate.
#
#   Flags are lifted from the platform's own builder script:
#     platforms/nordicnrf52*/builder/frameworks/arduino/adafruit.py
#   Note -std=gnu++11: the target is NOT C++17, whatever your editor thinks.
#
# USAGE
#   ./tools/check-arm.sh                 # both compilers
#   ./tools/check-arm.sh --production    # just gcc 7.2.1 (what will really build)
#   ./tools/check-arm.sh --modern        # just gcc 13.3.1
#   ./tools/check-arm.sh --clean         # re-fetch everything
#   CACHE=/path ./tools/check-arm.sh     # override the dependency cache
#
set -uo pipefail
cd "$(dirname "$0")/.."
PROJECT="$PWD"

CACHE="${CACHE:-${TMPDIR:-/tmp}/traplinelora-armcheck}"

PIO_GCC_URL="https://dl.registry.platformio.org/download/platformio/tool/toolchain-gccarmnoneeabi/1.70201.0/toolchain-gccarmnoneeabi-linux_x86_64-1.70201.0.tar.gz"
XPACK_VER="13.3.1-1.1"
XPACK_URL="https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v${XPACK_VER}/xpack-arm-none-eabi-gcc-${XPACK_VER}-linux-x64.tar.gz"
CORE_REPO="https://github.com/maxgerhardt/framework-arduinoadafruitnrf52-seeed.git"
RADIOLIB_REPO="https://github.com/jgromes/RadioLib.git"
RADIOLIB_TAG="7.7.1"
CMSIS_REPO="https://github.com/ARM-software/CMSIS_5.git"

SD="s140_nrf52_7.3.0_API"
VARIANT="Seeed_XIAO_nRF52840"

RUN_PROD=1
RUN_MODERN=1
case "${1:-}" in
  --clean)      rm -rf "$CACHE" ;;
  --production) RUN_MODERN=0 ;;
  --modern)     RUN_PROD=0 ;;
  "" )          ;;
  *) echo "unknown option: $1"; exit 2 ;;
esac
mkdir -p "$CACHE"

fetch_tar() {  # fetch_tar <dir> <description> <url> [strip-components]
  [ -d "$CACHE/$1" ] && return 0
  echo "  fetching $2 ..."
  mkdir -p "$CACHE/$1"
  curl -sSL "$3" | tar xz -C "$CACHE/$1" --strip-components="${4:-0}" \
    || { echo "  FAILED — no network?"; exit 1; }
}
fetch_git() {  # fetch_git <dir> <description> <command...>
  [ -d "$CACHE/$1" ] && return 0
  echo "  fetching $2 ..."
  shift 2
  "$@" || { echo "  FAILED — no network?"; exit 1; }
}

echo "dependency cache: $CACHE"
[ $RUN_PROD -eq 1 ]   && fetch_tar gcc-7  "gcc 7.2.1, PlatformIO's own (~110 MB)" "$PIO_GCC_URL"
[ $RUN_MODERN -eq 1 ] && fetch_tar gcc-13 "gcc 13.3.1 xpack (~230 MB)"           "$XPACK_URL" 1
fetch_git core     "Seeed nRF52 core"       git clone --depth 1 -q "$CORE_REPO" "$CACHE/core"
fetch_git RadioLib "RadioLib $RADIOLIB_TAG" git -c advice.detachedHead=false clone --depth 1 -b "$RADIOLIB_TAG" -q "$RADIOLIB_REPO" "$CACHE/RadioLib"
if [ ! -d "$CACHE/CMSIS_5" ]; then
  echo "  fetching CMSIS headers ..."
  git clone --depth 1 --filter=blob:none --sparse -q "$CMSIS_REPO" "$CACHE/CMSIS_5" || exit 1
  ( cd "$CACHE/CMSIS_5" && git sparse-checkout set CMSIS/Core/Include CMSIS/DSP/Include -q )
fi

CORE="$CACHE/core/cores/nRF5"
NORDIC="$CORE/nordic"
LIBS="$CACHE/core/libraries"

INCLUDES=(
  -I "$PROJECT/include"
  -I "$CORE"
  -I "$NORDIC"
  -I "$NORDIC/nrfx" -I "$NORDIC/nrfx/hal" -I "$NORDIC/nrfx/mdk" -I "$NORDIC/nrfx/soc"
  -I "$NORDIC/nrfx/drivers/include" -I "$NORDIC/nrfx/drivers/src"
  -I "$NORDIC/softdevice/$SD/include" -I "$NORDIC/softdevice/$SD/include/nrf52"
  -I "$CACHE/core/variants/$VARIANT"
  -I "$CACHE/CMSIS_5/CMSIS/Core/Include" -I "$CACHE/CMSIS_5/CMSIS/DSP/Include"
  -I "$LIBS/Bluefruit52Lib/src"
  -I "$LIBS/Adafruit_TinyUSB_Arduino/src"
  -I "$LIBS/Adafruit_TinyUSB_Arduino/src/arduino"
  -I "$LIBS/InternalFileSytem/src"
  -I "$LIBS/Adafruit_LittleFS/src"
  -I "$LIBS/Adafruit_nRFCrypto/src"
  -I "$LIBS/SPI"
  # Bundled FreeRTOS — rtos.h pulls these in and adafruit.py does not list them.
  -I "$CORE/freertos/Source/include"
  -I "$CORE/freertos/config"
  -I "$CORE/freertos/portable/GCC/nrf52"
  -I "$CORE/freertos/portable/CMSIS/nrf52"
  -I "$CACHE/RadioLib/src"
)

FLAGS=(
  -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16
  -Ofast -ffunction-sections -fdata-sections -nostdlib
  --param max-inline-insns-single=500
  -fno-rtti -fno-exceptions -std=gnu++11 -fno-threadsafe-statics
  -DF_CPU=64000000L -DARDUINO=10804 -DARDUINO_ARCH_NRF52
  -DARDUINO_NRF52_ADAFRUIT -DNRF52_SERIES -DLFS_NAME_MAX=64 -DDX_CC_TEE
  -DSOFTDEVICE_PRESENT -DS140
  -DARDUINO_Seeed_XIAO_nRF52840 -DNRF52840_XXAA
  -DCFG_DEBUG=0 -DTRAPLINE_SERIAL_BAUD=115200
  # From the core's platform.txt `build.flags.usb`. Without USE_TINYUSB the core
  # never declares `Serial` and every console source fails to compile.
  -DUSBCON -DUSE_TINYUSB
  -DUSB_VID=0x2886 -DUSB_PID=0x8044
  '-DUSB_MANUFACTURER="Seeed"' '-DUSB_PRODUCT="XIAO nRF52840"'
)

# Mirrors build_src_flags in platformio.ini — keep the two in step.
WARN=( -Wall -Wextra -Wshadow -Wno-unused-parameter )

mkdir -p logs
overall=0

run_one() {  # run_one <label> <g++ path> <logfile> <gate 0|1>
  local label="$1" gxx="$2" log="logs/$3" gate="$4"
  local out="$CACHE/obj-$label"
  mkdir -p "$out"; : > "$log"

  echo
  echo "--- $label: gcc $("$gxx" -dumpversion), cortex-m4, -std=gnu++11"
  local rc=0
  for f in src/*.cpp; do
    printf "    %-24s " "$f"
    if "$gxx" -c -o "$out/$(basename "$f" .cpp).o" "$f" \
          "${FLAGS[@]}" "${WARN[@]}" "${INCLUDES[@]}" >>"$log" 2>&1; then
      echo "ok"
    else
      echo "FAILED"
      rc=1
    fi
  done

  # Only our own files count; third-party warnings are noise we cannot fix.
  local ours
  ours=$(grep -E "warning:" "$log" 2>/dev/null | grep -E "^src/" | sort -u)
  if [ -n "$ours" ]; then
    echo "    warnings in our code:"
    echo "$ours" | sed "s#$PROJECT/##" | sed 's/^/      /'
    [ "$gate" -eq 1 ] && rc=1
  else
    echo "    no warnings in our code"
  fi

  if [ $rc -ne 0 ]; then
    grep -E "error:" "$log" | sed "s#$PROJECT/##" | head -15 | sed 's/^/      /'
    [ "$gate" -eq 1 ] && overall=1
  fi
  return 0
}

[ $RUN_PROD -eq 1 ]   && run_one production "$CACHE/gcc-7/bin/arm-none-eabi-g++"  check-arm-production.log 1
[ $RUN_MODERN -eq 1 ] && run_one modern     "$CACHE/gcc-13/bin/arm-none-eabi-g++" check-arm-modern.log     0

echo
if [ $overall -eq 0 ]; then
  echo "PASS — clean under the production compiler"
else
  echo "FAIL — see logs/check-arm-production.log"
fi
exit $overall
