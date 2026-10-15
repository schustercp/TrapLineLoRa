# Range walk — how far does one hop actually reach?

The whole architecture rests on one number: **how far a ground-level node can
reach another ground-level node in real vegetation.** The spec assumes ~600 m
direct and picks 250 m hops with margin to spare. That is a calculation. This
walk is the measurement, and it is the single most valuable hour in Phase 0.

A garden is a fine place to do it. The absolute distance you get at home will
not match the bush — your hedge is not wet bush and your fence is not a gully —
but everything else about the procedure is identical, and doing it here first
means the mistakes happen where the kettle is.

---

## What you need

- Both units flashed with the **same build** (`./tools/twoport.py check`).
- **Antennas fitted on both.** Non-negotiable — a bare PA port can destroy the
  transmitter, and the parked unit transmits continuously for the whole walk.
- A phone with **Bluefruit Connect**, paired to the unit you carry.
- USB power bank for the parked unit if you want more than a couple of hours.
- Some way to know how far you have gone. Phone maps with a dropped pin is
  easiest; pacing is fine too (a normal stride is close to 0.75 m).

---

## 1. Park one unit

Put it where a trap would actually sit: **on the ground**, antenna **vertical**,
in the open or in the vegetation — whatever you are trying to represent. Then
connect over BLE and type:

```
park
```

That sets full power (+22 dBm), a 1 s beacon interval, and starts beaconing. It
answers:

```
ok — PARK. Beaconing at full power, 1 s interval.
Put it where a trap would sit, antenna vertical, and walk away.
Red LED flashes every 4 s. Nothing else to do here.
```

Now disconnect the phone and leave it. The red flash every four seconds is your
only confirmation from a distance that it is still alive — worth a glance before
you set off, because a walk against a dead beacon looks exactly like a walk out
of range.

> **Why 1 s and not 2 s.** You will make a lot of stops, and each one needs
> ~20 packets before the delivery rate means anything. At 1 s that is 20
> seconds; at 2 s it is 40. Over fifteen stops that is five minutes of standing
> in a hedge you get back. It is a ~29 % duty cycle, which is fine for a one-off
> test under NZ's GURL for 915–928 MHz (no duty-cycle condition), but it is not
> what a deployed node does.

---

## 2. Set up the unit you carry

Connect the phone to the other unit and type:

```
walk
```

```
ok — WALK. Listening, live readout every 5 s.
At each stop: put it ON THE GROUND, step back, wait 30 s, read.
Then `mark <distance>`. `watch off` to stop the readout.
```

From here **you do not need to type anything** until you want to record a stop.
A line appears on your phone every five seconds by itself.

---

## 3. Read the line

```
18pkt 100% -104.5 dBm snr -3.2  SF10 +11.8 dB  MARGINAL
        needs SF11 here
```

| Field | Meaning |
|---|---|
| `18pkt` | packets in this window. **Under ~20, the delivery rate is not yet meaningful.** |
| `100%` | delivery rate — **the headline number** |
| `-104.5 dBm` | mean RSSI over the window, not the last packet |
| `snr -3.2` | mean SNR. Now that you are at range this is a real measurement, not the saturated ~+8 dB you saw on the desk |
| `SF10 +11.8 dB` | margin above the SF10 demodulation threshold |
| `MARGINAL` | the verdict |

**The verdict needs both numbers**, because either alone lies. 100 % delivery at
+2 dB margin is one wet week from failing; 40 % delivery at +20 dB margin is
already broken.

| Verdict | Means |
|---|---|
| `GOOD` | ≥99 % delivery **and** ≥15 dB margin. A hop you can build on. |
| `MARGINAL` | ≥90 % and ≥6 dB. Works today, will not work in rain, in leaf, or in five years. |
| `FAILING` | anything less, or no SF closes at all. |
| `settling` | fewer than two packets — keep waiting. |

And when you have gone too far:

```
· · ·  NOTHING HEARD for 14 s  — out of range, or turn back
```

That deliberately replaces the numbers rather than leaving the last good reading
on screen, because a stale reading and a healthy link look identical.

---

## 4. The stop procedure

At each measuring point:

1. **Put the unit on the ground.** Same height as the parked one, antenna
   vertical. This is the measurement you actually want — a node-to-node hop
   between two ground-level traps.
2. **Step back three or four metres** and stand still. BLE reaches perfectly
   well from there.
3. **Wait ~30 seconds** — you want `20pkt` or more before you believe the
   percentage.
4. Read the line. Then:
   ```
   mark 150m-past-shed
   ```

Put the distance in the label. It is the only place it gets recorded.

> **Step back, seriously.** On the bench, standing over the units cost **19 dB**
> in one direction and nothing in the other — nine frames in sixty-four about
> 15 dB down. Your body is the single largest error source in this whole
> procedure, and it is free to eliminate.

You do **not** need to type `reset` between stops. Silence longer than 30 s
starts a fresh window automatically and says so:

```
-- new window: 84 s silence, previous site discarded --
```

Without that, every beacon sent while you were walking would count as a lost
packet at the next stop, and the delivery rate would be fiction everywhere
except the first site.

---

## 5. Where to walk

Do not just go in a straight line down the road. Three passes tell you far more:

| Pass | What it isolates |
|---|---|
| **Clear ground** — lawn, driveway, open paddock | the best case; near free-space, and your sanity check on the maths |
| **Through vegetation** — hedge, shrubs, trees | the attenuation that actually matters. Expect it to be brutal at ground level. |
| **Over a rise / behind a building** | diffraction loss with no line of sight — the case the 250 m chain must survive |

Take a `mark` at each of these on **both** sides of the point where the verdict
falls from `GOOD` to `MARGINAL`. That transition is the number you came for.

---

## 6. Getting the data off

> ### ⚠️ Marks live in RAM. Nothing survives a power cycle.
>
> `g_marks[]` is a plain array in memory. Unplug the power bank, press RESET, or
> let a flat cell die, and **the whole walk is gone** — silently, with no error.
> There is no flash persistence yet (spec §16, Phase 1).
>
> So the order matters: **dump over BLE before you disconnect anything.**

### In the field, over BLE — do this first

While still connected in Bluefruit Connect, and before touching the power:

```
log
```

The CSV prints straight into the UART view. Then either:

- **Share it** — Bluefruit Connect's UART view has an export/share button; mail
  it to yourself. This is the safest option because it leaves the phone with a
  copy.
- **Screenshot it** — ugly, but a photo of the numbers beats losing them.

Only once you have it somewhere should you unplug anything.

### At the Mac, over USB — the good copy

If the unit stayed powered on the walk home, plug it in and:

```bash
./tools/twoport.py dump
```

That works with **one** unit connected — which is the normal case, since the
other is still in the garden. It sends `log`, waits for the `(N marks)`
sentinel rather than guessing a duration, and writes a real file:

```
04:27:34 [--] A: 3 mark(s) recovered

CSV written: logs/survey-20260823-042731.csv  (3 rows)
```

If the unit lost power, it says so rather than writing a confidently empty file:

```
!! A reports 0 marks.
!! Either nothing was recorded, or it lost power since the walk —
!! marks are held in RAM and do not survive a power cycle.
```

> Plugging into the Mac is itself a risk: the XIAO has one USB-C port, so
> swapping from a power bank to the laptop **cuts power in between**. If you
> want the USB copy, either run the whole walk off the laptop cable, or fit a
> LiPo to the BAT pads so the unit rides through the changeover — otherwise
> treat the BLE dump as the only copy.

The columns that matter for this exercise:

- `delivery_pct` — the headline
- `rssi_dbm`, `rssi_min_dbm` — mean and worst; a big spread means a fady path
- `deep_fades`, `fade_run_max` — **consecutive** means something crossed the
  path, **scattered** means suspect an antenna connector
- `recommended_sf` — what this hop would need

---

## What to expect, and what it means

At **+22 dBm both ends, 2 dBi antennas, SF10, both radios at ground level**, the
budget says the link closes down to about **−132 dBm**. Ground-level paths lose
far more than free space — the ground reflection cancels much of the direct ray,
and vegetation at antenna height is the worst possible place for it.

| What you find | What it means for the design |
|---|---|
| `GOOD` well past 250 m in vegetation | The 250 m chain has real margin. Good news, and it may allow skipping a node when one dies. |
| `GOOD` to roughly 250 m, `MARGINAL` beyond | Exactly what the spec assumes. Proceed, and hold the 15 dB margin requirement. |
| `MARGINAL` before 250 m | **Architecture problem.** Either the hop spacing must come down, the antennas must come up off the ground, or the whole line runs at SF12. Find this out now, not with 32 units in the bush. |

Whatever the number, record it against **the conditions**: dry or wet, summer or
winter foliage, antenna height, and what was between the two units. A range
figure without those is not a measurement, it is an anecdote — and the wet-winter
case is the one that decides whether the network works in August.

---

## Appendix: measuring what an antenna actually delivers

Run this whenever the antenna changes. **Never trust the number on the bag** —
cheap 915 MHz antennas are routinely sold as "3–5 dBi" when the realised figure
on a board like this is lower than the flex they replace.

The reason is the ground plane. At 915 MHz:

| | length | needs a ground plane? |
|---|---|---|
| quarter-wave monopole (most "wire" whips) | 8.2 cm | **yes — about 8 cm of counterpoise** |
| half-wave dipole / sleeve / end-fed | 16.4 cm | **no** |

The XIAO nRF52840 board is 21 × 17.5 mm. The largest counterpoise it can offer a
whip is roughly 1 cm against the ~8 cm needed, so a quarter-wave monopole here is
missing most of its ground plane: efficiency drops, the pattern tilts, and the
coax braid starts radiating. **Prefer a half-wave design** — dipole, sleeve
dipole or end-fed — which is self-resonant and does not care.

### The A/B test

The tool computes path loss from the gain you *declare*, so declaring the new
number proves nothing. Instead, hold the declared gain fixed and let the change
in RSSI reveal the truth.

1. Set up a **fixed geometry** outdoors — 30–50 m, both units on the ground,
   nothing moving. Not the desk: near-field and room multipath swamp a 2 dB
   difference.
2. Leave `antenna 2` set on both units for the whole test, whatever is fitted.
3. `mark ab-old-old` with the existing antennas. Wait for 30+ packets.
4. Swap **one** unit's antenna. `mark ab-new-old`.
5. Swap the other. `mark ab-new-new`.
6. `log`, and read `rssi_dbm` (the window mean, not a single frame):

```
gain of one new antenna  = rssi(ab-new-old) - rssi(ab-old-old)
sanity check             = rssi(ab-new-new) - rssi(ab-old-old)
                           should be about twice the first
```

If the second step does not roughly double the first, the environment moved
between marks and the test is void — repeat it. A real +2 dBi antenna shows up
as **+2 dB per end, +4 dB for the pair**.

Only once you have that number do you set `antenna <real dBi>` on both units,
and change `SURVEY_DEFAULT_ANT_DBI` in `include/radio_config.h` to match. Every
margin the tool reports depends on that figure being honest.

### Two things worth more than the gain

- **Polarisation discipline.** A flex antenna taped at an angle to a box can
  cost 10 dB or more against a properly vertical whip — several times what the
  gain upgrade buys. Whatever is fitted, get it vertical and keep it vertical.
- **Height.** See the measured results in `docs/field-results.md`: at ground
  level the excess loss over free space is ~63 dB at 100 m. Getting the antenna
  off the ground is worth far more than any antenna you can buy.

### Regulatory headroom

NZ's GURL for Short Range Devices permits **30 dBm EIRP** in 915–928 MHz. At
+22 dBm conducted you may spend **up to 8 dBi** of antenna gain:

| gain | EIRP | |
|---|---|---|
| 2 dBi | 24 dBm | OK |
| 4 dBi | 26 dBm | OK |
| 8 dBi | 30 dBm | at the limit |
| 10 dBi | 32 dBm | **over — illegal** |

So a 4 dBi antenna is comfortably legal, and there is room to go further if a
real one can be found.
