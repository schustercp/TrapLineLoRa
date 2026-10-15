# Bench bring-up and desk survey

First contact with the hardware. Work through this once per board; it takes
about ten minutes for the first and two for the rest.

Everything here happens at a desk. The point is to prove the radio works and
the tools work **before** anyone walks into the bush, because debugging a dead
node 3 km from the road is a different kind of afternoon.

---

## 0. Before power is applied

> ⚠️ **Fit the antenna first, every time.** The SX1262's power amplifier can be
> damaged by its own reflected power. A board that has ever transmitted without
> an antenna is suspect for the rest of its life, and you will not be able to
> tell by looking.

Check the Wio-SX1262 is fully seated on both board-to-board connectors. A
partially seated module is the most likely cause of the first failure below.

---

## 1. Flash it

> **`tools/build.sh` does not upload.** It builds, tests and reports size. If
> you have only run that, the board is still carrying whatever it had before —
> the factory Meshtastic image, or an earlier build. The upload is a separate
> command.

```bash
./tools/build.sh              # build + unit tests, logs to logs/
pio run -e survey -t upload   # <-- this is the one that flashes it
```

Watch for `Uploading` and a success line. If the upload cannot find a port,
**double-tap RESET** on the XIAO. A drive named `XIAO-*` appears and the LED
breathes; retry the upload.

**How to tell what is actually on the board:** the survey firmware advertises as
`TrapSurvey-XXXX`. If BLE shows `Meshtastic_XXXX`, it is still the factory
image. If it shows `TrapLine-01`, it is the old ping-pong sketch. If it shows
nothing at all, see §2.

## 1a. The boot signature

The firmware blinks a code on the LEDs at startup, and this is the only "I am
alive" signal that does not depend on USB or BLE working:

| What you see on power-up | What it means |
|---|---|
| **2 red blinks**, then **3 blue blinks** | Healthy. `setup()` ran, radio initialised. |
| 2 red blinks, then red 120 ms on / 380 ms off forever | Radio init failed — read the serial log for the code |
| 2 red blinks, then nothing, repeating | Crashing after `setup()` started. Boot loop. |
| **Nothing at all** | Firmware is not running: not flashed, wrong image, or stuck in the bootloader |

Once running, a **faint blue tick every 3 seconds** means the main loop is
alive. An idle unit that has heard no beacons shows nothing else, so this tick
is how you know it is working rather than hung.

---

## 1b. Which board is this, actually?

`pio device list` will happily tell you the board is a "XIAO nRF52840 Sense".
**Do not believe it.** In application mode the USB product string and PID come
from the `usb_product` and `hwids` fields of the *board definition you built
against* — so flashing `survey_sense` makes any board call itself a Sense, and
flashing `survey` makes any board call itself a plain one. It is circular
evidence and it caused an hour of confusion on the first bring-up.

| PID | Means |
|---|---|
| `2886:0044` | application built with `-e survey` is running |
| `2886:0045` | application built with `-e survey_sense` is running |
| `2886:8044` | **bootloader** — hardware is a plain XIAO nRF52840 |
| `2886:8045` | **bootloader** — hardware is a XIAO nRF52840 **Sense** |

The `0x80xx` PIDs are the useful ones. The UF2 bootloader is factory
programmed and is not touched by `pio run -t upload`, so what it declares is
what the silicon is.

**The test — ten seconds, no guessing:**

1. **Double-tap RESET** on the XIAO. The red LED starts breathing and a drive
   mounts.
2. Look at the drive name, or run `./tools/monitor.sh --list`:

| Drive name | `INFO_UF2.TXT` says | Board | Build with |
|---|---|---|---|
| **`XIAO-BOOT`** | `Board-ID: Seeed_XIAO_nRF52840` | plain XIAO nRF52840 | `-e survey` |
| **`XIAO-SENSE`** | `Board-ID: Seeed_XIAO_nRF52840_Sense` | XIAO nRF52840 Sense | `-e survey_sense` |

Kit SKU 102010710 ships the **plain** board, so `XIAO-BOOT` is expected.

**Does it matter if you flash the wrong one?** Barely, and that is worth
knowing so nobody chases it as a fault. The two variants are byte-for-byte
identical in pin mapping — D0–D10, all three LEDs, `READ_BAT`, QSPI and NFC are
the same numbers in both `variant.cpp` files. The only functional difference is
a typo in Seeed's Sense variant: its `initVariant()` writes to `PIN_QSPI_CS`
where the plain one writes to `LED_BUILTIN`, so on the Sense build the red LED
is left on for the few milliseconds until our `setup()` clears it. Nothing the
survey tool uses is affected. Flash the matching environment anyway, so that
`--list` keeps telling the truth.

---

## 2. Watch it boot

```bash
./tools/monitor.sh --list     # what serial devices exist
./tools/monitor.sh            # finds the XIAO by USB VID and attaches
```

> The script now selects the port by Seeed's USB vendor ID (0x2886). Letting
> PlatformIO choose on its own is not safe on macOS — on the first attempt it
> attached to a Bluetooth headset (`/dev/cu.JBLLIVEPRO2TWS`) and sat there
> showing nothing, which looked exactly like a dead board.

**If the log fills with `Disconnected` / `Reconnecting` in a loop**, the port is
appearing and vanishing. That is USB re-enumeration, which means **the board is
resetting repeatedly** — a boot loop, not a serial problem. Check the LED boot
signature in §1a: repeating red blinks with nothing after them confirms it.

A healthy boot looks like this:

```
TrapLineLoRa survey tool — XIAO nRF52840 + Wio-SX1262
device 3F2A1C0044B7C218  ·  built Aug 22 2026 09:14:03
[radio] initialising SX1262 ...
[radio] 915.000 MHz  SF10  BW 125 kHz  22 dBm  sync 0x12
ready — listening. Type `help`, or `role beacon` on the other unit.
```

**If it does not:**

| What you see | What it means | What to do |
|---|---|---|
| `begin() failed: -2` | `CHIP_NOT_FOUND` — no SPI response | Re-seat the Wio-SX1262 on the B2B connectors |
| `begin() failed: -705` | `SPI_CMD_TIMEOUT` — BUSY never released | Almost always TCXO. `LORA_TCXO_VOLTAGE` must be 1.8 V for this module |
| Red LED blinking 120 ms on / 380 ms off, forever | Fatal radio init failure | The serial log says which |
| Port not listed at all | USB CDC not enumerated | Check the cable is data-capable, not charge-only |
| **Port found and attached, but nothing ever prints** | See below — usually not a fault | Unplug/replug, or press RESET once, with the monitor already running |

**The silent-but-connected case is the one that will fool you.** If
`--list` shows a `2886:00xx` PID, the *application* is running: TinyUSB is up,
`setup()` got far enough to enumerate. The board is alive. What you are missing
is the boot banner, because `console::log()` drops output when `Serial` is
false, and `Serial` on TinyUSB is false until the host asserts DTR — which does
not happen until the monitor attaches, several seconds after the banner was
printed. After that an idle unit in `role listen` with nothing to hear prints
nothing at all, so the screen stays blank forever.

Current firmware fixes this: it watches for the DTR rising edge and
re-announces itself when a monitor attaches. If you see nothing, **the board is
carrying an older build** — reflash (§1). Failing that, press RESET once while
the monitor is running and catch the banner live.

The `--list` PID is also how you confirm the flash took: `0044` means `survey`
is on there, `0045` means `survey_sense`.

**Also check the LEDs while you are here.** They should be **off** when idle.
If all three are lit at rest, the polarity is inverted — the schematic says
active-low (`docs/hardware-reference.md`) and `LED_ON` in `include/board_pins.h`
is the single line to change. This is not expected; the netlist is unambiguous.

---

## 3. Connect the iPhone

Install **Bluefruit Connect** (Adafruit) from the App Store. nRF Connect works
too, but Bluefruit's UART view is the friendlier of the two for typing commands.

> Web Bluetooth does not work on any iOS browser — Chrome for iOS is a WebKit
> wrapper. A native app is the only route on iPhone (spec §17 Q11).

1. Open Bluefruit Connect → the device appears as **`TrapSurvey-XXXX`**, where
   `XXXX` is the last four hex digits of the board's hardware device ID. That is
   how you tell three identical boxes apart.
2. Connect → choose **UART**.
3. You should immediately see:
   ```
   TrapLineLoRa survey tool — connected.
   type `help` for commands, `status` for the current measurement.
   ```
4. Type `help` and send.

**If typed commands do nothing**, the app is not appending a line ending. The
firmware works around this — it treats a 150 ms gap in the character stream as
the end of a line — so it should just work. If it still does not, enable the
EOL / newline switch in the app's UART settings.

Useful first commands:

```
id          the 64-bit hardware device ID
status      everything the unit currently knows
help        the full command list
```

---

## 4. Two units, one metre apart — turn the power down first

> **Do not run a desk test at +22 dBm.** At one metre the receiving unit sees
> about **−6 dBm**, which saturates the front end: the AGC clips, RSSI stops
> tracking reality, and every derived number — path loss, predicted SNR, margin
> — becomes fiction. `status` will warn you if RSSI goes above −30 dBm, but it
> is better not to get there.

On **both** units:

```
power -9              minimum for the SX1262 high-power PA
antenna 2             tell the tool what is actually fitted — every margin depends on it
```

Then set the roles. On unit **B**:

```
role beacon
```

On unit **A** (the one you are watching):

```
role listen
```

Put two or three metres between them. Within a few seconds unit A should start
flashing **blue** on each beacon received, and unit B flashes **red** every four
seconds to show it is beaconing.

> Blue and red, not green: the schematic puts green on a 10 kΩ resistor against
> 2.2 kΩ for the others, so green draws ~50 µA and is effectively invisible.

---

## 5. Read the measurement

Wait about a minute — the tool needs 50 beacons for a meaningful delivery rate,
which at a 2 s interval is under two minutes. Then on unit A:

```
status
```

Expect something like:

```
role listen  ·  SF10  BW 125.0 kHz  -9 dBm  ant 2.0 dBi
freq 915.000 MHz  ·  our noise floor -97.0 dBm
heard 90/90  ·  rssi -39.0 dBm  snr 8.0 dB  ·  1 s ago
delivery 100 %
path loss 34.0 dB  ·  far noise floor -101.0 dBm
predicted at far end: rssi -39.0 dBm  snr 62.0 dB
  SF7  margin +69.5 dB   OK
  ...
recommend SF7   ·   set with: sf 7
```

**What to check, in order of what it tells you:**

| Reading | Healthy at 1–3 m | What a bad value means |
|---|---|---|
| `heard N/M` | equal, `delivery 100 %` | Gaps at 3 m mean a real problem — interference, or a bug. Do not proceed to the field. |
| `rssi` | −35 to −55 dBm | Above −30, reduce power or separation. Below −70, something is wrong with an antenna. |
| `snr` | +8 to +10 dB | **Saturated, and therefore uninformative.** The SX126x stops reporting above roughly +10 dB no matter how strong the signal is. It means "loud", nothing more. |
| `path loss` | 34–40 dB | Check it by hand — see below. |
| `noise floor` | −95 to −110 dBm on a desk | Not −120. See "the noise floor number" below; this is the one reading that needs thought. |

**Check `path loss` by hand.** The tool computes

```
path_loss = far_tx_dbm + far_ant_dbi + our_ant_dbi − rssi_dbm
```

so at `power -9`, `antenna 2` on both units and RSSI −39 dBm that is
`−9 + 2 + 2 + 39 = 34 dB`. Free-space loss at 915 MHz is
`32.45 + 20·log₁₀(f_MHz) + 20·log₁₀(d_km)` = **37.7 dB at 2 m**, 31.7 dB at
1 m. A reading of 34 dB therefore means the units were about 1.3 m apart, or
2 m apart with a helpful desk reflection. Anything in the low-to-mid 30s at
arm's length is right. If the arithmetic above does not reproduce the number
the tool printed, that is a bug, not a measurement.

### The noise floor number

`noise` reports the SX1262's **instantaneous channel RSSI** (`GetRssiInst`),
median of 15 samples. That is a *pre-despreading* measurement, and it is not
the same quantity as the LoRa demodulator's noise reference. Two consequences
that matter:

- **It will never read −120 dBm.** Thermal noise in 125 kHz is −123 dBm, and
  with the receiver's own noise figure the true floor is around −117 dBm — but
  the RSSI block bottoms out well above that. On a desk, −95 to −110 dBm is
  the normal range. A first bench run measured −97 dBm on one unit and
  −101 dBm on the other.
- **The tool's margins are computed against it**, via
  `predicted_snr = predicted_rssi − far_noise`. So if that figure sits 20 dB
  above the demodulator's real reference, **every margin the tool reports is
  20 dB pessimistic** — three spreading factors' worth. It would push a link
  that runs happily at SF7 all the way to SF12, quadrupling airtime and
  wrecking the energy budget. Being wrong in the safe direction is still being
  wrong.

**So find out which it is, before the survey — it takes thirty seconds:**

> Unplug the antenna from the unit in **`role listen`** and type `noise`.
>
> This is safe. A listener never transmits — only `role beacon` calls
> `sendBeacon()` — so there is no open-port PA hazard. **Never do this to the
> beacon unit.**

**Answered, 2026-08-22.** Antenna off: **−113.0 dBm**, repeatable. Antenna on:
**−89.0 dBm**, repeatable. A 24 dB difference means the antenna is hearing
something real, so the reading is genuine ambient interference and **the
margin calculation is sound**. No correction needed.

That also calibrates the instrument. −113 dBm with no antenna is the SX1262's
RSSI floor; the true demodulation reference is about −117 dBm (−123 dBm
thermal in 125 kHz, plus ~6 dB noise figure). So:

- The tool **cannot read below −113 dBm.** At a genuinely quiet bush site it
  will report −113 and mean "quiet", not "exactly −113".
- Margins at quiet sites are therefore **up to 4 dB pessimistic** — bounded,
  in the safe direction, and nothing like the 20 dB that was feared.
- A desk figure of −89 to −97 dBm is ordinary office RF. Do not read it as a
  fault, and do not expect it in the field.

Then confirm the recording path works end to end:

```
mark desk-test-2m
log
```

`log` should print a CSV header and one row. That is the artefact a day of
walking produces, so it is worth seeing it work while sitting down.

---

## 6. Swap the roles

Set A to `role beacon` and B to `role listen`, and repeat §5. Two things this
catches that a one-way test cannot: a transmitter that is deaf, and a receiver
that cannot transmit. Both ends must work, because in the field every node is
both.

---

## 7. Three boards

Repeat §§1–3 for the other two. Then a useful last check: put **two** units in
`role beacon` at once and one in `role listen`.

Both beacons share a sequence-number space, so the listener will report a
delivery rate well under 100 % and its `seq` deltas will jump around. **That is
correct behaviour, not a fault** — it is what two nodes transmitting on one
channel looks like. It is worth seeing once so it is recognised in the field,
where it would mean an unexpected second transmitter on 915.0 MHz.

Set one back to `role idle` and delivery should return to 100 %.

---

## Exit criteria

Before anything goes outside:

- [ ] All three boards boot with no radio error
- [ ] LEDs are dark at idle, blue flashes on receive, red on beacon
- [ ] iPhone connects to each by name and `help` responds
- [ ] 100 % delivery at 2–3 m in **both** directions, at `power -9`
- [ ] RSSI, SNR, path loss and noise floor all in the ranges above
- [ ] `mark` and `log` produce a CSV row
- [ ] Battery reading is sane on a charged pack (`status`, once battery support
      lands — the survey build reports the radio only)

The moment any of these fails is the moment to stop, because everything the
survey produces afterwards depends on the tool being trustworthy.
