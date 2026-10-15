# TrapLineLoRa

Long-range LoRa trap line monitoring on the **Seeed Studio XIAO nRF52840 + Wio-SX1262 Kit** (SKU `102010710`), Arduino framework, PlatformIO.

24 single-trigger traps on a 2.5 km line, reporting to one base station at the house 600 m beyond the first trap. Direct node-to-base range is far short of that, so **every node relays for the ones beyond it** — a chain, not a star.

Measured, not assumed: at ground level a 250 m hop does not close at any spreading factor, but with **both antennas 1 m up it closes at SF10 with 17.5 dB of margin** — 1 m of height is worth 15.4 dB. That single result sets the hardware: antenna height is a requirement, not an optimisation. See **[`docs/field-results.md`](docs/field-results.md)**.

The design is captured in **[`docs/design-spec.md`](docs/design-spec.md)**. Read that first; this README only covers building and running what exists.

---

## What's in here right now

**The Phase 0 survey tool.** Before any of the relay layer gets written, the line has to be measured — because whether a hop actually closes in wet bush at trap height is the assumption the whole architecture rests on.

Build two units. One beacons, the other listens and tells you, on your phone, whether that hop will hold.

```
TRAP 7 → 8  ·  listen

heard 47/50 (94%)   rssi -113.2 dBm   snr +2.5 dB
path loss 139.2 dB  ·  far noise floor -122 dBm
predicted at far end: rssi -113.2 dBm  snr +8.8 dB

  SF7  margin  +16.3 dB   OK
  SF10 margin  +23.8 dB   OK
  SF12 margin  +28.8 dB   OK

recommend SF7   ·   set with: sf 7
```

**Packet error rate is the headline, not RSSI.** A site at −118 dBm with 94 % delivery is usable; the same RSSI at 60 % delivery is not, and RSSI alone cannot tell the two apart.

---

## Building

1. Install [VS Code](https://code.visualstudio.com/) and the **PlatformIO IDE** extension.
2. Open this folder. The first build downloads the toolchain, Seeed's nRF52 Arduino core and RadioLib — expect a few minutes.
3. Flash both units:
   ```bash
   pio run -e survey -t upload
   ```
   If upload can't find the port, double-tap the XIAO's RESET button to force the UF2 bootloader and retry.
4. Run the unit tests — no hardware needed:
   ```bash
   pio test -e native
   ```

### The build environments

There are **three**, and `default` is not one of them — `default_envs = survey`
in `[platformio]` just says which one a bare `pio run` picks.

| Environment | Runs on | What it builds | When you use it |
|---|---|---|---|
| **`survey`** | XIAO nRF52840 (the kit board) | The survey firmware — all seven files in `src/` | Almost always. This is the default. |
| `survey_sense` | XIAO nRF52840 **Sense** | Identical firmware, different board definition | Only if you have Sense hardware. See the note below. |
| **`native`** | Your PC | `aes128.cpp`, `tlp.cpp`, `link_math.cpp` + `test/` | `pio test -e native` — unit tests, no hardware |

`[nrf52_common]` is **not** an environment. PlatformIO only treats `[env:*]`
sections as environments; that one is a plain block that `survey` and
`survey_sense` pull in with `extends`, so the platform, framework, warning flags
and RadioLib dependency are written once.

**`survey` vs `survey_sense`** differ only in `board`. The Sense adds an IMU and
a PDM microphone, and the survey tool uses neither. The two variants have
identical pin maps — D0–D10, all three LEDs, `READ_BAT`, QSPI and NFC are the
same numbers in both — so either build runs on either board. The kit (SKU
102010710) ships the *plain* board, so `survey` is the one to use.

Two consequences worth knowing before they cost you an afternoon:

- **The USB product string and PID come from the board definition you built
  against, not from the silicon.** Flash `survey_sense` and `pio device list`
  reports "XIAO nRF52840 Sense" on any board. To find out what the hardware
  actually is, double-tap RESET and read the bootloader drive name: `XIAO-BOOT`
  is plain, `XIAO-SENSE` is a Sense. The bootloader is factory programmed and
  `-t upload` never touches it. See `docs/bring-up.md` §1b.
- Seeed's Sense variant has a typo in `initVariant()` — it writes to
  `PIN_QSPI_CS` where the plain one writes to `LED_BUILTIN` — so the red LED
  stays lit until `setup()` clears it. Cosmetic, but it looks like a fault.

**`native` has two settings that are easy to miss.** `build_src_filter` restricts
it to the three translation units with no Arduino or nRF headers — that is the
whole reason the protocol core can be tested on a PC. And `test_build_src = yes`
is what links `src/` into the test binary at all; without it PlatformIO builds
only `test/` and the link fails with every symbol undefined.

What `native` does **not** cover: `main`, `survey`, `console` and `device_id`,
which all touch Arduino, RadioLib or Bluefruit. Those are checked by
`tools/check-arm.sh` (compile only) and, ultimately, by running on hardware.

Or use the wrappers, which log everything to `logs/`:

```bash
./tools/build.sh        # build + unit tests + size, needs pio (and therefore the Mac)
./tools/check-arm.sh    # cross-compile check, no pio — bootstraps its own toolchain
./tools/monitor.sh      # serial capture from one unit
./tools/flash.sh        # upload to one board, or every board, by serial number
./tools/twoport.py      # drive BOTH units at once, scripted
```

### Flashing with more than one board plugged in

`pio run -e survey -t upload --upload-port /dev/cu.usbmodemXXX` works, but the
port you name is the board PlatformIO *knocks on*, not where the firmware lands:
this board sets `use_1200bps_touch`, so opening that port at 1200 baud reboots
it into the UF2 bootloader, which enumerates as a **different** device that
PlatformIO then waits for and uploads to. A second XIAO being connected is safe
— PlatformIO snapshots the existing ports first and ignores them.

The nuisance is that `usbmodem101` / `usbmodem11301` are assigned by socket and
enumeration order, so they shuffle. The **USB serial number** doesn't, and on
this board it is also the FICR device ID — the same string the firmware prints
for `id` and advertises as `TrapSurvey-XXXX`:

```
4D14A35E33083C3D  ->  device id 4D14A35E33083C3D  ->  TrapSurvey-3C3D
```

```bash
./tools/flash.sh --list                     # ports, serials, and which build each runs
./tools/flash.sh                            # flash every connected board in turn
./tools/flash.sh --serial 4D14A35E33083C3D  # just that one
```

It re-resolves serial → port immediately before each upload, so a re-enumeration
mid-run cannot flash the wrong board, and `--list` will tell you if two boards
are running *different environments* — another way for a pair to diverge without
saying so.

`twoport.py` runs the desk procedure end to end — sets power and antenna on both
units, beacons one way, waits for enough packets, records a mark, swaps
direction, repeats, and dumps the CSV. Typing that by hand into two phones gets
the timing subtly different every time, and delivery rate is sensitive to
exactly that.

```bash
./tools/twoport.py check      # who are they, are they alive
./tools/twoport.py desk       # the full §4–§6 procedure, both directions
./tools/twoport.py noise      # noise floor on both
./tools/twoport.py cmd status # one command to both units
```

It finds both boards by USB VID, warns if one is sitting in the bootloader, and
writes `logs/twoport-<stamp>.log` plus a real `logs/survey-<stamp>.csv`. pyserial
comes from PlatformIO's virtualenv, so there is nothing to install — the script
re-execs itself into that interpreter if the system python cannot import it.

`check-arm.sh` compiles `src/` for cortex-m4 against the real framework headers
using flags lifted from the platform's builder script. It does not link, so it
cannot report size — but it runs anywhere with network access and catches API
mismatches and warnings that stub headers never would. `docs/hardware-reference.md`
covers the flags that are easy to get wrong.

---

## Using the survey tool

Each unit advertises as `TrapSurvey-XXXX`, where `XXXX` is the low 16 bits of its hardware device ID, so you can tell two boxes apart. Connect with **Bluefruit Connect** or **nRF Connect**, open the UART view, and type `help`.

```
role beacon|listen|idle
sf <7..12>          bw <125|62.5>       freq <kHz>
power <-9..22>      antenna <dBi>       interval <ms>
margin <dB>         noise               reset
mark <label>        log                 clear
status              id
```

A typical hop measurement:

| | |
|---|---|
| Leave unit A at trap 7 | `role beacon` |
| Carry unit B to trap 8 | `role listen`, then wait ~2 minutes |
| Record it | `mark trap-07-to-08` |
| At the end of the day | `log` → CSV, copy out of the app |

Three things that will otherwise cost you a repeat visit:

- **Set `antenna <dBi>` honestly on both units.** Every margin the tool reports is computed from it.
- **Measure at the real antenna height and orientation.** Holding the unit at head height overstates the link by 10 dB or more.
- **Measure the skip-hop too** (7→9, 8→10). That is the fallback path when a node dies, and the only thing between one dead node and twenty orphans.

A survey unit draws about 8 mA. Run it from a USB power bank — four hours of surveying is roughly fifty days of a deployed node's energy budget.

---

## Layout

```
platformio.ini              survey / survey_sense / native environments
include/  src/
  main.cpp                  survey tool entry point
  survey.{h,cpp}            beacon + listen roles, marks, CSV
  console.{h,cpp}           BLE UART + USB serial console
  tlp.{h,cpp}               Trap Line Protocol frames (spec §7)
  aes128.{h,cpp}            AES-128 + CMAC, the frame MIC
  link_math.{h,cpp}         path loss, per-SF margin, packet error rate
  device_id.{h,cpp}         FICR.DEVICEID — immutable hardware identity
  board_pins.h              XIAO nRF52840 ↔ Wio-SX1262 pin map
  radio_config.h            PHY defaults + NZ regulatory notes
test/test_core/             native unit tests (AES, CMAC, framing, link maths)
examples/pingpong/          the original bring-up sketch
docs/
  design-spec.md            the system design — read this first
  design-log.md             how it got here: measurements, defects, decisions
  field-results.md          measured propagation, and what it settled
  range-walk.md             measuring a hop in the field
  hardware-reference.md     pin maps, electrical notes, bootloader/DFU
  datasheets/               datasheet index and fetch script
```

`aes128`, `tlp` and `link_math` are deliberately free of Arduino and nRF headers so they compile and test on a PC. That separation is worth keeping as the protocol grows — the relay layer's trickiest logic (sequence windows, duplicate suppression, hop selection) is exactly the kind of thing that should be testable without a radio.

---

## Radio defaults

915.0 MHz, BW 125 kHz, **SF10**, CR 4/5, private sync word `0x12`, +22 dBm — see `include/radio_config.h`.

These sit in the 915–928 MHz band under New Zealand's *General User Radio Licence for Short Range Devices*, which permits 30 dBm EIRP. At +22 dBm conducted you may spend up to 8 dBi of antenna gain and stay legal.

**Never transmit without an antenna connected** — reflected power can destroy the SX1262's PA.

### The network key in this repository is a placeholder

`TRAPLINE_DEFAULT_KEY` in `include/radio_config.h` is the ASCII string
`TrapLineDEVKEY01`. It is a development value, it is public, and **it
authenticates nothing**. Frames carry a 4-byte AES-128-CMAC for authenticity,
not encryption — so anyone who can read this file can forge a frame on any
network still running the default.

Generate a real key and load it over BLE before deployment. Do not commit it.

---

## What's next

Phase 0 is: build this, then walk the line. Nothing in the relay layer should be written until the hop margins are measured — its spreading factor, its fallback strategy and its energy budget all depend on numbers that don't exist yet.

Independent of the survey, and buildable in parallel: the node's sleep architecture and the current measurement that proves it (NFR-2), the trap sensor input, and flash-persisted configuration. See §16 of the spec.
