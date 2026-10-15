# Hardware reference — XIAO nRF52840 + Wio-SX1262 (kit 102010710)

Working notes for the TrapLineLoRa hardware. Everything here is what the
firmware in `src/` assumes. Where a value matters electrically, the datasheets
in `docs/datasheets/` are the authority — treat this file as the map, not the
territory.

---

## 1. The kit

| | |
|---|---|
| Kit SKU | `102010710` — "XIAO nRF52840 & Wio-SX1262 Kit for Meshtastic" |
| Contents | XIAO nRF52840 (plain, **not** Sense) + Wio-SX1262 expansion board |
| Not included | L76K GNSS module, antenna, battery — all sold separately |
| Ships with | Meshtastic firmware pre-flashed |

The two boards mate through **two 2×7 board-to-board (B2B) connectors** on the
underside of the XIAO, not through the castellated side headers. The signals
still land on the standard XIAO `D0`–`D10` numbering, which is why the firmware
can use plain Arduino pin macros.

> ⚠️ The Wio-SX1262 shipped with the **XIAO ESP32-S3** kit has a *different* pin
> configuration. Seeed says so explicitly. Pin numbers are not portable between
> the two kits.

---

## 2. SX1262 ↔ XIAO nRF52840 pin map

| SX1262 / module signal | XIAO pin | nRF52840 port | Notes |
|---|---|---|---|
| NSS (SPI chip select) | `D4` | P0.04 | Active low. Also the XIAO's default SDA — not usable for I²C here. |
| DIO1 (IRQ) | `D1` | P0.03 | TxDone / RxDone / Timeout |
| NRST | `D2` | P0.28 | Active low |
| BUSY | `D3` | P0.29 | Must be low before each SPI command |
| RXEN (RF switch) | `D5` | P0.05 | Driven high for receive. Also the default SCL. |
| TXEN | — | — | Not wired. The SX1262 drives the TX path itself from `DIO2`. |
| SCK | `D8` | P1.13 | Default `SPI` instance |
| MISO | `D9` | P1.14 | |
| MOSI | `D10` | P1.15 | |
| TCXO supply | SX1262 `DIO3` | — | **1.8 V**. Wrong value ⇒ `BUSY` never releases ⇒ `SPI_CMD_TIMEOUT`. |

Two consequences worth remembering:

1. **`setDio2AsRfSwitch(true)` and `setRfSwitchPins(D5, RADIOLIB_NC)` are both
   required.** Configure only one and the module either transmits into a
   terminated port or hears nothing.
2. **`D4` and `D5` are gone.** They are the XIAO's default I²C pins, so any I²C
   sensor on a trap node needs `Wire` remapped to spare pins, or a soft-I²C.

Source of truth for this table: the Meshtastic `seeed_xiao_nrf52840_kit`
variant, which is the reference implementation for this exact kit.

---

## 3. Optional Seeed L76K GNSS

Not in the kit, but the Wio-SX1262 carries the footprint and the Meshtastic
variant defines it:

| Signal | XIAO pin |
|---|---|
| GNSS TX → XIAO RX | `D6` (P1.11) |
| GNSS RX ← XIAO TX | `D7` (P1.12) |
| Standby | `D0` (P0.02) |
| Baud | 9600 |

`D0` is also the kit's side button, so you get one or the other.

---

## 4. XIAO nRF52840 on-board resources

| Function | Pin / macro | Notes |
|---|---|---|
| RGB LED — red | `LED_RED` (P0.26) | **Active LOW** (common anode) |
| RGB LED — green | `LED_GREEN` (P0.30) | |
| RGB LED — blue | `LED_BLUE` (P0.06) | |
| Battery sense | `PIN_VBAT` (P0.31 / AIN7) | Through a 1.0 M / 0.51 M divider |
| Battery sense enable | `VBAT_ENABLE` (P0.14) | Drive **LOW** to enable, HIGH to stop the divider leaking |
| Charge current select | `HICHG` (P0.13) | LOW = 100 mA, high-Z = 50 mA |
| External charge detect | P0.23 | LOW while charging |
| QSPI flash | P25Q16H, 2 MB | Used by the UF2 bootloader / LittleFS |

Battery maths used in `main.cpp`:

```
Vbat_mV = adc_raw * 3000 / 4096 * 1510 / 510
```

with `analogReference(AR_INTERNAL_3_0)` and `analogReadResolution(12)`.

### Power figures worth knowing

| State | Rough current |
|---|---|
| nRF52840 System OFF, RAM retained | ~1–3 µA |
| nRF52840 running @ 64 MHz, radio off | ~5–8 mA |
| SX1262 sleep (cold start) | < 1 µA |
| SX1262 RX continuous | ~4.6 mA (boosted ~5.3 mA) |
| SX1262 TX @ +22 dBm | ~118 mA |

A trap node that wakes, transmits one short frame and sleeps again is dominated
by the TX burst plus whatever the sensor costs. Continuous RX is the thing that
kills battery life — a listening base station wants mains or a big panel.

---

## 5. Flashing and recovery

- **Normal upload**: PlatformIO uses `nrfutil` over the USB CDC port with a
  1200 bps touch to enter the bootloader. Just `pio run -t upload`.
- **Force the bootloader**: double-tap RESET quickly. A `XIAO-SENSE` /
  `XIAO-BOOT` USB drive appears and the LED breathes. Drop a `.uf2` on it, or
  retry the upload.
- **Bricked / wrong softdevice**: the UF2 bootloader lives below the
  application, so a bad app cannot destroy it — double-tap and reflash.
- **SWD**: pads are on the underside if you need a J-Link or CMSIS-DAP. The
  board JSON already lists `jlink`, `nrfjprog`, `cmsis-dap` and `blackmagic` as
  upload protocols.

---

## 6. Toolchain notes

The official PlatformIO `nordicnrf52` platform has **no** XIAO board
definitions. `platformio.ini` therefore uses:

- `platform = https://github.com/maxgerhardt/platform-nordicnrf52.git` — adds
  `xiaoble_adafruit` (plain) and `xiaoblesense_adafruit` (Sense), both of which
  select the `Seeed_XIAO_nRF52840` variant and the S140 v7.3.0 softdevice.
- `platform_packages = framework-arduinoadafruitnrf52 @ https://github.com/Seeed-Studio/Adafruit_nRF52_Arduino.git#master`
  — Seeed's fork of the Adafruit nRF52 BSP, which is where the
  `Seeed_XIAO_nRF52840` variant files actually live. Upstream Adafruit does not
  carry them.

Flash budget with the S140 softdevice: ~811 KB application flash, ~232 KB RAM.

### Verified against the installed toolchain (2026-08-21)

Checked directly against `~/.platformio` on the development Mac, so these are
facts rather than assumptions:

| | |
|---|---|
| Board definition | `xiaoble_adafruit` present in the `nordicnrf52@src-…` platform (maxgerhardt fork) |
| Arduino core | `framework-arduinoadafruitnrf52-seeed`, carrying `variants/Seeed_XIAO_nRF52840` and `…_Sense` |
| RadioLib | resolves to **7.7.1** |
| Variant defines | `LED_RED 11`, `LED_GREEN 13`, `LED_BLUE 12`, `PIN_VBAT 32`, `VBAT_ENABLE 14`, `PIN_SPI_SCK 8`, `PIN_SPI_MISO 9`, `PIN_SPI_MOSI 10`, `D0`–`D10` all present |
| Survey firmware footprint | **171 kB flash of 811 kB (21 %)**, **19 kB RAM of 238 kB (8 %)** |

RadioLib 7.7.1 confirmed to carry every call the firmware makes:
`getRSSI(bool)`, `getPacketLength(bool update = true)`, `readData(uint8_t*, size_t)`,
`standby()`, `setDio1Action()`, `setDio2AsRfSwitch()`, `setCurrentLimit()`,
`setCRC()`, `setFrequency()`, `setBandwidth()`, `setSpreadingFactor()`,
`setOutputPower()`. `setRfSwitchPins()` is declared on `Module`, and reached
through the radio object — confirmed by the firmware compiling and linking.

### Settled from Seeed's KiCad project

`docs/datasheets/Seeed-Studio-XIAO-nRF52840_V1.1_SCH & PCB_251222_01/` is Seeed's
own schematic and PCB for board revision v1.1. Reading the netlist out of it
settles three things that were previously assumption or contradiction.

#### 1. The LEDs are active LOW — the BSP header is wrong

Part **RGB6**, a 4-pad common-anode RGB LED:

| RGB6 pad | via | to |
|---|---|---|
| `+` | — | **3V3** |
| `R-` | R7, 2.2 kΩ | P0.26 (`LED_RED`, pin 11) |
| `G-` | R4, **10 kΩ** | P0.30 (`LED_GREEN`, pin 13) |
| `B-` | R3, 2.2 kΩ | P0.06 (`LED_BLUE`, pin 12) |

Anode on 3V3, cathodes to GPIO through resistors. **Driving a pin LOW lights it.**
`board_pins.h` is correct with `LED_ON = LOW`; the BSP's `#define LED_STATE_ON 1`
is simply wrong, and the bench test is no longer needed.

#### 2. Green is a sixth as bright as red

That 10 kΩ on green against 2.2 kΩ on the others is not a typo — it is current
balancing for a more efficient die, and it makes green far dimmer:

| Channel | Resistor | Approx. current |
|---|---|---|
| Red | 2.2 kΩ | ~640 µA |
| Blue | 2.2 kΩ | ~230 µA |
| **Green** | **10 kΩ** | **~50 µA — barely visible outdoors** |

Use red for anything that has to be seen in daylight. The survey firmware
originally used a duty-cycled green as its "beaconing" indicator; that has been
changed to a red flash for this reason.

Note the scale: red held on is ~640 µA, more than twice the node's entire
297 µA average current budget. Flash, never hold.

#### 3. The battery divider, confirmed exactly

| Part | Value | From | To |
|---|---|---|---|
| R16 | **1 MΩ 1 %** | `VBAT` | P0.31 (`PIN_VBAT`, AIN7) |
| R17 | **510 kΩ 1 %** | P0.31 | P0.14 (`VBAT_ENABLE`) |

So `Vbat = Vadc × 1510 / 510`, which is what `readBatteryMillivolts()` computes.
The schematic net is named **`P0.14_~{READ_BAT}`** — the overbar confirms it is
**active low**, so driving it LOW enables the divider, exactly as §8.4 requires.

#### 4. Charging cannot be disabled in firmware — this decides Q12

The charger is a **BQ25101YFPT** (U2). Its active-low enable is the net `/CE`,
and the only thing on that net besides the chip is **R20, 10 kΩ to GND**. No
GPIO reaches it.

**Charging is therefore permanently enabled whenever VBUS is present, and no
firmware can turn it off.** `P0.13_HICHG` only selects the *rate*, by switching
R12 (2.7 kΩ) in parallel with the ISET resistor — 50 mA or 100 mA, never zero.

The consequence for §17 Q12: a lithium **primary** cell on the BAT pad is unsafe
on this board, because USB is also how you flash it and any connection will
start charging a non-rechargeable cell. Using primaries means physically
removing R20 or the charger, which is a board modification on 32 units.
Rechargeable chemistry — Li-ion, or LiFePO4 with its own caveats — avoids that,
at the cost of the sub-zero charging hazard already noted.

#### 5. D0–D10, confirmed against the netlist

`D0`=P0.02, `D1`=P0.03, `D2`=P0.28, `D3`=P0.29, `D4`=P0.04, `D5`=P0.05,
`D6`=P1.11 (TX), `D7`=P1.12 (RX), `D8`=P1.13 (SCK), `D9`=P1.14 (MISO),
`D10`=P1.15 (MOSI) — matching the table in §2 exactly.

### Building

Two paths, and they answer different questions.

**`tools/build.sh` — the real build.** Needs `pio` and therefore the Mac: the
toolchain in `~/.platformio` is a **Mach-O x86_64** binary. Builds, links, runs
the unit tests, reports size, and writes everything to `logs/`.

**`tools/check-arm.sh` — compile check without PlatformIO.** Bootstraps its own
toolchains and framework sources into a cache and compiles `src/` with the same
flags the platform uses. Runs anywhere with network access; a cold bootstrap is
about 26 seconds and 1.4 GB of cache. It does not link, so it cannot report size
or catch link errors — but it catches API mismatches and warnings against the
*real* headers.

It runs **two compilers**, and the pair is the point:

| | |
|---|---|
| **production — gcc 7.2.1** | The exact toolchain PlatformIO ships for this platform (`toolchain-gccarmnoneeabi 1.70201.0`), pulled from the PlatformIO registry. Its warnings are precisely what a real build produces. **This is the gate.** |
| **modern — gcc 13.3.1** | A stricter second opinion, from the xpack GitHub releases. Advisory only. It has already caught a format-truncation bug — a `%ld` that could need 14 bytes in a 12-byte buffer — that 7.2.1 waved through in two files. |

Keeping both matters because they answer different questions: 7.2.1 says *will
this build*, 13.3.1 says *is this actually correct*.

Flags come from the platform's own builder, `builder/frameworks/arduino/adafruit.py`.
Three of them are easy to get wrong:

| Flag | Why it matters |
|---|---|
| **`-std=gnu++11`** | The target is **not** C++17, whatever your editor's IntelliSense believes. `[env:native]` is pinned to gnu++11 for the same reason — testing shared code at a newer standard than it ships under just hides the failure until flash time. |
| `-DUSE_TINYUSB` (with `-DUSBCON`, VID/PID, manufacturer, product) | From the core's `platform.txt` `build.flags.usb`. Without it the core never declares `Serial` and every console source fails to compile. |
| `-DSOFTDEVICE_PRESENT -DS140` | Selects the SoftDevice headers. Omit and the BLE API vanishes. |

Include paths that are not obvious from `adafruit.py` alone: the bundled
FreeRTOS (`cores/nRF5/freertos/{Source/include,config,portable/GCC/nrf52}`) and
`libraries/Adafruit_nRFCrypto/src`, which `BLESecurity.h` pulls in.

### `Serial` is USB CDC, and it stops when the node sleeps

RadioLib emits a compile-time warning worth taking seriously:

> Use of USB CDC for debug output is not recommended (might stop on first
> sleep). Use hardware UART instead.

On this board `Serial` is `Adafruit_USBD_CDC`, not a UART. For the survey tool —
always on a power bank, always enumerated — it does not matter. For the node
firmware it will: writes to an unopened CDC port waste cycles, and CDC does not
survive a sleep cycle intact.

`console::log()` already guards on `if (Serial)`. When the node firmware gains
real sleep, the options are to drop serial logging entirely (BLE is the field
interface anyway) or move it to a hardware UART on `D6`/`D7`.

### Gotchas

- `newlib-nano` on this core omits floating-point support from `printf`/`scanf`.
  The firmware carries RSSI and SNR as tenths in integers for that reason. If
  you need `%f`, add `-u _printf_float` (and `-u _scanf_float`) to
  `build_flags`, at a cost of a few KB.
- The Bluefruit softdevice owns several hardware peripherals and all interrupt
  priorities above a threshold. Long `delay()` calls inside an ISR will drop BLE
  connections — keep the RadioLib DIO1 handler to setting a flag, as it is now.

---

## 7. Reference links

Local copies of the PDFs live in `docs/datasheets/` — run
`docs/datasheets/fetch_datasheets.sh` to populate it (needs internet).

- Kit wiki: <https://wiki.seeedstudio.com/xiao_nrf52840&_wio_SX1262_kit_for_meshtastic/>
- Wio-SX1262 wiki: <https://wiki.seeedstudio.com/wio_sx1262/>
- XIAO nRF52840 wiki: <https://wiki.seeedstudio.com/XIAO_BLE/>
- Meshtastic device page: <https://meshtastic.org/docs/hardware/devices/seeed-studio/wio-series/wio-sx1262s/>
- RadioLib API: <https://jgromes.github.io/RadioLib/>
- NZ short range device GURL: <https://www.rsm.govt.nz/licensing/spectrum-and-licensing/general-user-licences/>
