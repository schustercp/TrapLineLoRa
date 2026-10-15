# Design log — Phase 0 bring-up, survey tooling and field results

Written 2026-10-15. Covers the bench bring-up, the survey tooling built around it,
the firmware defects that work exposed, the first real propagation measurements,
and the design decisions those measurements settled.

This is the narrative record. The reference docs it points at are the ones to
read when doing the work:

| document | what it is for |
|---|---|
| `docs/design-spec.md` | the system design |
| `docs/bring-up.md` | first contact with a board, at a bench |
| `docs/range-walk.md` | measuring a hop in the field, plus antenna A/B |
| `docs/field-results.md` | the measurements and their interpretation |
| `docs/hardware-reference.md` | pin maps, electrical notes, toolchain facts |

---

## 0. Where everything lives

Three separate places, which caused real confusion during this work:

**The repository — your Mac.** `~/workspace/TrapLineLoRa`
Firmware, tools, docs, logs. This is the project. Everything in the table above
is here.

**The claude.ai Project — not on disk.** Docs under a `claude/` path
(`claude/design-decisions.md`, `claude/power-and-battery.md`,
`claude/open-questions-and-gotchas.md`, `claude/phase0-field-results.md`,
`claude/hardware-and-toolchain.md`) live in the **TrapLineLoRa project** in the
Claude app, visible from any conversation, and are *not* files in the repo.
They are durable cross-session context, not deliverables.
`claude/phase0-field-results.md` duplicates `docs/field-results.md`; the repo
copy is the one to edit.

**The cloud container — scratch.** Work happens in an ephemeral Linux container
that cannot see `/dev/cu.*` on the Mac. Anything produced there must be written
back to the repo explicitly or it is lost.

> **Consequence for serial work:** the bridge reaches the Mac's *files*, not its
> *devices*. That is why `tools/twoport.py` exists — it runs on the Mac, drives
> both radios, and writes a transcript into `logs/`, which is readable from the
> cloud side.

---

## 1. The measurements, and what they settled

Full detail and caveats in `docs/field-results.md`.

### Ground level (2026-08-23, dense city street, traffic)

| dist | path loss | delivery | deep fades | rec SF |
|---|---|---|---|---|
| 100 m | 134.4 dB | 98 % | 24 / 59 | SF8 |
| 150 m | 142.4 dB | 98 % | 13 / 60 | SF11 |

Path-loss exponent **n = 4.5** — textbook two-ray ground reflection. Excess loss
over free space **63 dB at 100 m**. Extrapolated to 250 m this gave SF10 +2.5 dB
and **no spreading factor reaching the 15 dB requirement, not even SF12.**

That result is what prompted the height test, and is now superseded by it.

### Both radios at 1 m (2026-08-24, same street, 2 dBi flex)

| dist | path loss | mean | min | max | spread | fades | run | delivery | SF10 margin |
|---|---|---|---|---|---|---|---|---|---|
| 80 m | 123.5 dB | -97.5 | -114.0 | -89.0 | 25.0 dB | 18 | 5 | 100 % | +31.5 |
| **140 m** | **125.6 dB** | -99.6 | -102.0 | -98.0 | **4.0 dB** | **0** | 0 | 100 % | +29.4 |
| **250 m** | **137.5 dB** | -111.5 | -122.0 | -102.0 | 20.0 dB | 12 | 4 | **98 %** | **+17.5** |

**250 m closes at SF10 with +17.5 dB, on the 2 dBi flex antennas already owned.**
This is the measurement the whole architecture rested on.

**1 m of height is worth 15.4 dB**, measured like-for-like against the previous
day's 150 m ground run (142.4 dB → 127.0 dB). Two-ray says loss scales as
`1/(h_tx·h_rx)²`, so that implies the earlier setup was effectively at 0.41 m
rather than the 0.1 m assumed — consistent with the units sitting on something.

| antennas | SF10 max hop, ground | SF10 max hop, 1 m |
|---|---|---|
| 2 dBi flex | 133 m | **282 m** |
| 4 dBi | 163 m | 343 m |

The **80 m point is contaminated** — it fits n = 0.86, physically impossible. The
fade columns say why: 18 deep fades, longest run 5, 25 dB spread. Traffic across
the path. 140 m is the clean reference (zero fades, 4 dB spread) and every fit is
anchored there. Without the fade instrumentation that point would have passed for
a real propagation result.

---

## 2. Design decisions this settled

### The line, as it actually is

```
line length      2500 m
radio locations  16 spots today (8 of them with two traps)
traps            24
radios           24   -- one per trap, because the doubles may get strung out
hops to base     15 today, up to 23 if spread
avg spacing      167 m today, 109 m if fully spread
first hop        600 m from the house to trap 1
```

### SF10, and only SF10

24 nodes means the node nearest the base aggregates 24 reports, and that is what
sets the energy:

| SF | relay | standby | BLE | total | vs 110 µA | life on 1250 mAh |
|---|---|---|---|---|---|---|
| **SF10** | 48.4 | 28.8 | 5.7 | **82.9 µA** | OK | **1.72 yr** |
| SF11 | 103.8 | 28.8 | 5.7 | 138.3 µA | over by 28 | 1.03 yr |
| SF12 | 188.7 | 28.8 | 5.7 | 223.2 µA | over by 113 | 0.64 yr |

Each SF step buys ~2.5 dB and roughly doubles airtime. **SF11 and SF12 do not
fit the battery at 24 nodes.** SF11 becomes viable only at 24-hourly sweeps
(86.4 µA, 1.65 yr), which doubles fault-detection time from 36 h to 72 h — the
trade the spec already rejected once in Q20.

With 1 m antennas, SF10 has **+25.8 dB at 167 m** and +34.5 dB at 109 m. The
spreading-factor ladder is no longer needed.

### Antenna height is a hardware requirement, not an optimisation

It is the only lever that is both large and free. The others:

| source of dB | available | cost |
|---|---|---|
| more TX power | 0 dB — already +22, and 30 dBm EIRP is the NZ ceiling | — |
| higher SF | +2.5 / +5 dB | 2× / 4× battery; breaks NFR-2 |
| better antenna | ~4 dB (2 → 4 dBi both ends) | money, mounting, ground-plane risk |
| **get off the ground** | **15.4 dB measured at 1 m** | **a length of conduit** |

**The 4 dBi antenna upgrade is no longer needed.** 1 m of post beats it twice
over and sidesteps the ground-plane problem entirely.

### Skip-hop redundancy comes back at 1 m

At ground level a node could not reach past a dead neighbour, leaving 15 hops in
series with no fallback. At 1 m:

| reach | SF10 | SF11 | SF12 |
|---|---|---|---|
| 2 × 167 m = 334 m | +11.5 | +14.0 | **+16.5 OK** |
| 2 × 109 m = 218 m | **+20.3 OK** | +22.8 | +25.3 |

334 m meets the full requirement at SF12 and carries +11.5 dB at SF10 — enough
for a **degraded failover path**. The relay layer should be designed for it; it
is the only answer to a chain of single points of failure.

### The 600 m base hop works, on height not on the Yagi

Calibrating two-ray against the measured 150 m ground point (clutter term
15.4 dB) and re-evaluating with the base elevated:

| base height | path loss | +9 dBi yagi | SF10 margin |
|---|---|---|---|
| 0.1 m | 166.5 dB | -133.5 dBm | **-4.5 dB SHORT** |
| 3 m | 136.9 dB | -103.9 dBm | +25.1 dB |
| **6 m** | **130.9 dB** | **-97.9 dBm** | **+31.1 dB** |
| 10 m | 126.5 dB | -93.5 dBm | +35.5 dB |

A 6 m roof mount at 600 m is a **better link than 150 m ground-to-ground**. It
also reaches past a dead first node — 767 m at +26.8 dB, 934 m at +23.3 dB — so
the base should try nodes 2 and 3 when node 1 goes quiet.

Two things decide whether it works in practice:

- **Coax will eat the Yagi.** 15 m of RG58 is 9.8 dB, netting −0.8 dB from a
  9 dBi antenna. Put the **radio at the antenna** and run power and data down
  Cat 5, keeping coax to a short pigtail.
- **The Yagi is only regulated on transmit.** NZ's GURL caps 30 dBm EIRP, so a
  9 dBi Yagi means dropping base TX to 21 dBm — negligible. **Receive gain is
  unregulated**, and node→base is the direction carrying every trap report.

### Still unmeasured

- **Bush, not city.** Every number here is urban. Wet foliage at antenna height
  is the case that decides the design.
- Wet and winter — repeat in August.
- The 600 m base hop with the Yagi actually mounted.
- The real 15 inter-site gaps. **The longest gap governs the whole line's SF.**

---

## 3. Mounting and hardware decisions

### Put the enclosure at 1 m, not the antenna on a coax

Preferred build: enclosure at ~1 m on a post, antenna directly on it, and the
**trap sensor wire runs down** to the trap. A sensor is a switch closure — two
core wire, loss irrelevant, no connectors, no sealing problem.

If the device must live on the trap and coax must run up:

| cable | 1.3 m | +3 connectors | total | both ends |
|---|---|---|---|---|
| RG178 (typical u.FL pigtail) | 1.92 | 0.75 | 2.67 dB | 5.35 dB |
| RG58 | 0.86 | 0.75 | 1.61 dB | 3.22 dB |
| LMR-240 | 0.33 | 0.75 | 1.07 dB | 2.15 dB |

Still a large net win (height 15.4 dB minus 2–5 dB), and at 167 m there is 20+ dB
of margin on any cable. Three things hurt more than the dB:

- **Water** in the dielectric is catastrophic and permanent. Seal the top joint
  with self-amalgamating tape and leave a drip loop.
- **The u.FL connector** is rated ~30 mating cycles and is not a field connector.
  Fit the pigtail once, strain-relieve it inside the box, service at the
  bulkhead SMA only.
- **The braid becomes the counterpoise.** A vertical coax feeding a bare whip
  puts common-mode current on the outside of the braid, which radiates and
  distorts the pattern. Use a sleeve dipole or a proper ground-plane antenna, or
  choke the braid below the antenna.

### Antenna inside PVC conduit — yes

Absorption is negligible at 915 MHz: a 3 mm wall is **~0.01 dB**, and 0.016 of a
wavelength in the PVC. Making the conduit the mast is a good build — it gets the
height, protects the antenna, shields the coax from UV, and removes the exposed
top connector.

- **Use GREY, not orange.** Per Iplex NZ's product sheet, grey is UV resistant
  and orange is not: *"If used unprotected from the sun above ground
  UV/weathering resistance of the orange conduit will be limited — colour fading
  with some loss of impact strength will occur over time."* Colour is **not** the
  duty rating; both come in medium and heavy duty.
- **Heavy duty, 50 mm** — stiffness so the mast does not wave (orientation is
  worth more than most purchasable dB), and 23 mm clearance around a centred
  element, inside the λ/10 = 3.3 cm detuning guidance.
- **Vent at the bottom, cap the top.** A sealed pipe condenses internally, and
  water pooling *around* the element is a real loss — unlike the wall, that is
  not negligible.
- **Nothing metallic in the top third**, no foil tape, no metal cap, no
  expanding foam.

### Antenna orientation and install consistency

A vertical element radiates the same pointing up or down — polarisation and the
horizontal pattern are unchanged. Pointing **down sheds water**, which on a
24-node multi-year install is worth more than the geometry.

Pointing down lowers the element by its own length, and because loss goes as
`1/(h_tx·h_rx)²` that counts at both ends:

| antenna | penalty, both ends |
|---|---|
| short flex ~5 cm | 0.9 dB |
| quarter-wave 8.2 cm | 1.4 dB |
| half-wave 16.4 cm | 2.9 dB |

Fix: mount the box one antenna-length higher. Keep the element **≥8 cm (λ/4)**
from the box, battery and any metal.

**Install tolerance matters more than people expect:**

| height spread at 1 m | link spread |
|---|---|
| ± 5 cm | 1.74 dB |
| ± 10 cm | 3.49 dB |
| ± 20 cm | 7.04 dB |

Eyeballing "about a metre" across 24 nodes buys several dB of variation that will
look like propagation and send someone chasing ghosts. **Cut a stick and use it
at every site.**

---

## 4. Firmware defects found during bring-up

Every one of these was found by using the tool, not by reading the code. Several
would have silently corrupted a day of survey data.

| # | defect | why it mattered |
|---|---|---|
| 1 | `setRole()` never reset the PER counters | Walking to the next site carried the previous site's counters, and every beacon sent *during the walk* was booked as a loss. A perfect site opened ~300 packets in the red. **Delivery rate — the headline number — would have been fiction at every site after the first.** |
| 2 | `noise` printed a value it never stored | The figure on screen was not the figure in the CSV or the outgoing beacon. |
| 3 | A listener never measured its own noise floor | Only `role beacon` did, so a listener carried `0.0` until it had once been a beacon, then carried the *previous site's* floor all day. |
| 4 | Noise sampling window 10× shorter than a packet | 15 samples over 30 ms against 289 ms of SF10 airtime: a burst inside a packet had *every* sample contaminated and the median could not help. ~1 reading in 7 was measuring the beacon. |
| 5 | `measureNoiseFloor()` railed in `role idle` | `GetRssiInst` is only meaningful in RX; in standby the register rails at 0xFF → **−127.5 dBm**, which read as a spectacularly quiet site. My own `proc_noise` caused it by sending `role idle` first. |
| 6 | `status` printed an unmeasured floor as `0.0 dBm` | Absence of a measurement displayed as a measurement. Now prints `not measured`. |
| 7 | `mark` recorded a single packet's RSSI | A bench run saw −66.0 and −56.0 dBm **three seconds apart** on a motionless desk. Whichever frame landed before someone typed `mark` became the permanent record. Marks now record window mean, min, max and last, with path loss and margins from the mean. |
| 8 | Fade depth alone could not diagnose | 19 dB one way and 2 dB the other across the same boards minutes apart. Added `deep_fades`, `fade_run_max`, `worst_at_s`: **consecutive** means something crossed the path, **scattered** means an intermittent connector. |
| 9 | `worst_at_s` was absolute uptime | Reported "worst at t+253 s" inside a 130-second window. Now relative to the window start. |
| 10 | CSV header truncated | Header is 212 bytes; the caller buffer was 200. `recommended_sf` was sheared to `re`, leaving 20 data fields under a header whose last label was garbage. Now guarded by `static_assert` against `SURVEY_CSV_MIN_BUF`, verified to fire. |
| 11 | Fade statistics are censored at range | `rssi_min` and `deep_fades` only count frames that **decoded**. Both ground-level marks bottomed out at exactly −126.0 dBm, 6 dB above the SF10 floor, so **100 m showed 24 deep fades and 150 m only 13** — the weaker site looked more stable. A bias pointing the wrong way. Firmware now flags it within 10 dB of the demodulation floor. |

Two more findings that are not code defects but cost real time:

- **The USB product string does not identify the board.** In application mode the
  PID and product string come from the board definition compiled against, so
  `-e survey_sense` makes any board claim to be a Sense. Only the bootloader PID
  is evidence (`2886:8044` plain, `2886:8045` Sense), or double-tap RESET and
  read the drive name (`XIAO-BOOT` / `XIAO-SENSE`). The two Arduino variants have
  identical pin maps, so a mismatched build still runs.
- **Two units silently on different builds.** One board ran a build three fixes
  old for a whole session and nothing on screen said so. The serial banner now
  carries a build stamp, there is a `ver` command, and `twoport.py check`
  compares them and shouts `!! MISMATCHED FIRMWARE`.

### Verification

Three layers, all green: 16 native unit tests (`pio test -e native`), cross
compile clean under **gcc 7.2.1 (production gate) and 13.3.1 (advisory)** via
`tools/check-arm.sh`, and the hardware itself.

Two of the native tests exist specifically to pin defect #1 — a site change
without a reset, and a far-end reboot mid-survey.

---

## 5. Tools built

| tool | what it does |
|---|---|
| `tools/build.sh` | build + unit tests + size, logs to `logs/`. **Does not upload.** |
| `tools/check-arm.sh` | cross-compile check against real framework headers, two compilers, bootstraps its own toolchain |
| `tools/monitor.sh` | serial capture from one unit; finds the XIAO by USB VID and decodes the PID |
| `tools/flash.sh` | upload by **USB serial number**, not port name |
| `tools/twoport.py` | drives **both** radios at once, scripted, and writes a transcript |

### `flash.sh` — why not just `--upload-port`

`pio run -e survey -t upload --upload-port /dev/cu.usbmodemXXX` works, but the
port named is the board PlatformIO *knocks on*: `use_1200bps_touch` reboots it
into the UF2 bootloader, which enumerates as a **different** device that
PlatformIO then waits for. And `usbmodem101` vs `usbmodem11301` is assigned by
socket and enumeration order, so the name that flashed unit A this morning may be
unit B this afternoon.

The USB serial number does not move, and on this board it is also the FICR device
ID — the same string the firmware prints for `id` and advertises as
`TrapSurvey-XXXX`:

```
4D14A35E33083C3D  ->  device id 4D14A35E33083C3D  ->  TrapSurvey-3C3D
```

`flash.sh` re-resolves serial → port immediately before each upload, so a
re-enumeration mid-run cannot flash the wrong board.

### `twoport.py`

```bash
./tools/twoport.py check      # ids, build stamps, status -- flags mismatched firmware
./tools/twoport.py desk       # full §4-§6 desk procedure, both directions
./tools/twoport.py noise      # noise floor on both
./tools/twoport.py dump       # pull marks -> logs/survey-*.csv
./tools/twoport.py cmd status # one command to both
```

`dump` and `cmd` run with **one** unit connected, which is how you come back from
a range walk. It never opens a port at 1200 baud (that reboots the board into the
bootloader), warns if a board is sitting in the bootloader, runs `caffeinate` on
macOS, and detects a host sleep mid-measurement — a run once jumped from "90 s to
go" to eight minutes later, gathering 253 packets in what the log called a 130 s
window.

### Field commands

```
park     leave this one at the site  (beacon, full power, 1 s interval)
walk     carry this one             (listen + live readout every 5 s)
watch <s>|off
```

The walk readout is self-printing so nobody types commands while holding a radio
in a hedge:

```
18pkt 100% -104.5 dBm snr -3.2  SF10 +11.8 dB  MARGINAL
        needs SF11 here
```

`GOOD` requires ≥99 % delivery **and** ≥15 dB margin; either alone misleads.
Out of range the numbers are *replaced*, not left stale:

```
· · ·  NOTHING HEARD for 14 s  — out of range, or turn back
```

Silence longer than 30 s starts a fresh window automatically (defect #1's field
equivalent) and says so.

---

## 6. Operating notes worth not rediscovering

- **Marks live in RAM.** A power cycle loses the whole walk, silently. Dump over
  BLE (`log`, then share from Bluefruit Connect) **before unplugging anything** —
  the XIAO has one USB-C port, so swapping from a power bank to the laptop cuts
  power in between. Flash persistence is not implemented (spec §16, Phase 1).
- **Stand clear while it measures.** Standing over the units cost **19 dB** in one
  direction and nothing in the other — nine frames in sixty-four about 15 dB
  down. Set them on the ground, step back three or four metres, then read.
- **Reported SNR saturates around +10 dB.** On a strong link it means "loud" and
  nothing more. It only becomes informative near the demodulation threshold.
- **The noise floor instrument bottoms out at −113 dBm.** True thermal + NF is
  about −117, so margins at genuinely quiet sites are up to ~4 dB pessimistic —
  bounded, and in the safe direction.
- **Desk noise is the computer.** While the Mac slept, the measured floor fell
  from −102 to −114 dBm.
- **Never transmit without an antenna.** Reflected power can destroy the SX1262's
  PA. A listener never transmits, so removing *its* antenna is safe — never the
  beacon's.
- **`VBAT_ENABLE` (P0.14) stays driven LOW.** Driving it high puts P0.31 at its
  3.60 V absolute maximum; floating is worse. Costs 2.8 µA.
- **LEDs are active LOW** and green is on a 10 kΩ resistor (~50 µA, invisible
  outdoors). Flash, never hold — red held on is 6× the entire average budget.

---

## 7. Next actions, in order

1. **Survey the real 15 gaps.** 2.5 km, fifteen measurements, one afternoon with
   `park`/`walk`. The longest gap governs the whole line's spreading factor — one
   220 m gap would force SF11 on all 24 nodes.
2. **Repeat the 1 m test somewhere that looks like the deployment.** Every number
   here is urban. This is the remaining architecture risk.
3. **Measure the 600 m base hop** with the Yagi at roof height. Validates the
   height model, the Yagi and the most critical link in the network at once.
4. **Flash persistence for marks** (LittleFS) — removes the lose-a-whole-day risk.
5. **Spec rework.** §8–§11 are written against 32 nodes and 31 hops. Needs 24
   nodes, 24 traps, 2.5 km, SF10-mandatory, dual trap inputs at co-located spots,
   co-location transmit scheduling, and skip-hop as a designed fallback.
6. **Phase 1 current measurement** against NFR-2 (≤110 µA). Still a gate.

### Co-located radios need protocol work

Two radios at one spot saturate each other — even 10 m apart is −25.7 dBm, and
~16 m of separation is needed to get below −30 dBm. Nothing is damaged (the
SX1262 tolerates +10 dBm) but a node half a metre from its twin is **deaf to the
rest of the line whenever that twin transmits**. They share a hop, so a naive
schedule gives them the same slot and they block each other every sweep.

Co-located nodes need different transmit slots, and TOPO discovery needs to
recognise "this neighbour is on top of me" as its own case. The upside: they are
genuine redundancy at 8 of 15 hops — and stringing them out, which helps the
radio, removes it.
