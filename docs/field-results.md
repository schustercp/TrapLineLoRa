# Phase 0 — measured propagation results

Running record of real range measurements. Each entry needs the conditions, not
just the numbers: a range figure without terrain, weather and antenna height is
an anecdote.

Raw logs and CSVs live in `logs/`. This file is the interpretation.

---

## 2026-08-24 — same street, **both radios 1 m off the ground**

**Conditions:** dense urban, traffic passing, both radios at **1 m**. +22 dBm,
**2 dBi flex** both ends, SF10/BW125. Noise floor -114/-115 dBm (instrument
floor). Source: `logs/survey-20260824-183322.csv`

| dist | path loss | mean | min | max | spread | fades | run | delivery | SF10 margin |
|---|---|---|---|---|---|---|---|---|---|
| 80 m | 123.5 dB | -97.5 | -114.0 | -89.0 | 25.0 dB | 18 | 5 | 100 % | **+31.5** |
| **140 m** | **125.6 dB** | -99.6 | -102.0 | -98.0 | **4.0 dB** | **0** | 0 | 100 % | **+29.4** |
| **250 m** | **137.5 dB** | -111.5 | -122.0 | -102.0 | 20.0 dB | 12 | 4 | **98 %** | **+17.5** |

### 250 m closes, at SF10, on the antennas we already own

This is the measurement the architecture rested on, and it passes: **98 %
delivery at 250 m with +17.5 dB of SF10 margin**, above the 15 dB requirement,
using the 2 dBi flex rather than any upgrade.

### 1 m of height is worth 15.4 dB — measured, not modelled

Like for like against the previous day's ground-level run:

```
ground level, 150 m : 142.4 dB (measured 2026-08-23)
1 m height,   150 m : 127.0 dB (from the clean 140 m point)
GAIN FROM 1 m       :  15.4 dB
```

Two-ray predicts loss ∝ 1/(h_tx·h_rx)², so 15.4 dB implies the earlier setup was
effectively at **0.41 m**, not the 0.1 m assumed — consistent with the units
sitting on top of something rather than on the dirt. The lesson stands either
way: **height is the cheapest decibels in this design, by a wide margin.**

What it does to the usable hop:

| antennas | SF10 max hop, ground | SF10 max hop, 1 m |
|---|---|---|
| 2 dBi flex | 133 m | **282 m** |
| 4 dBi | 163 m | 343 m |

**The 4 dBi antenna upgrade is no longer needed.** 1 m of post beats it twice
over and costs nothing per node.

### What this does to the real line

24 radios, 2.5 km, 16 locations → 167 m average spacing:

| layout | spacing | SF10 margin at 1 m |
|---|---|---|
| today: 24 radios at 16 locations | 167 m | **+25.8 dB** |
| strung out: 24 locations | 109 m | +34.5 dB |
| hypothetical 250 m spacing | 250 m | +17.5 dB |

Every layout closes comfortably at SF10 — **the only spreading factor the
battery can afford** (83 µA, 1.72 years; SF11 is 138 µA, SF12 is 223 µA).

### Skip-hop redundancy comes back

At ground level a node could not reach past a dead neighbour, leaving every hop
a single point of failure. At 1 m:

| reach | SF10 | SF11 | SF12 |
|---|---|---|---|
| 2 × 167 m = 334 m | +11.5 | +14.0 | **+16.5 OK** |
| 2 × 109 m = 218 m | **+20.3 OK** | +22.8 | +25.3 |

334 m meets the full 15 dB requirement at SF12, and carries +11.5 dB at SF10 —
fine for a **degraded failover path** even if it would not be accepted as a
primary hop. The chain can be made self-healing.

### The 80 m point is contaminated — read the fade columns

80 m shows *less* improvement over 140 m than free space alone would give
(fitted n = 0.86, which is physically impossible). The fade columns say why:
**18 deep fades, longest run 5, 25 dB spread.** Something crossed that path
repeatedly — traffic, most likely.

140 m is the clean reference: **zero deep fades, 4 dB spread, 100 % delivery.**
All the fits above are anchored there.

At 250 m the minimum of -122 dBm sits 10 dB above the SF10 floor of -132, right
at the censoring boundary, so those fade counts are lower bounds.

**This is exactly what the fade instrumentation was added for**, and it earned
its place: without it the 80 m point would have looked like a real propagation
result and corrupted the exponent fit.

### Consequences for the design

1. **SF10 everywhere.** Confirmed affordable and now with 26 dB of margin at the
   real spacing.
2. **Skip the 4 dBi antennas.** Spend the effort on mounting instead.
3. **1 m antenna height becomes a hardware requirement**, not an optimisation.
   The enclosure should sit at ~1 m on a post with the antenna on top, so there
   is no coax run — the trap sensor wire runs down to the trap instead.
4. **Design the relay layer for skip-hop.** It is now viable, and it is the only
   answer to 15 hops in series with no redundancy.

### Still unmeasured

- **Bush, not city.** Every number here is urban. Wet foliage at antenna height
  is the case that decides the design.
- **Wet and winter.** Repeat in August.
- The 600 m base hop with the Yagi at roof height.

---

## 2026-08-23 — dense city street, **ground level**

**Conditions:** both radios at ground level as a trap would sit. +22 dBm, 2 dBi,
SF10/BW125, 1 s beacon. Noise floor -114 dBm.
Source: `logs/survey-20260823-162832.csv`

| dist | mean RSSI | min | path loss | delivery | deep fades | run | rec SF |
|---|---|---|---|---|---|---|---|
| 100 m | -108.4 | -126.0 | 134.4 dB | 98 % (59/60) | 24 / 59 | 7 | SF8 |
| 150 m | -116.4 | -126.0 | 142.4 dB | 98 % (60/61) | 13 / 60 | 3 | SF11 |

Path-loss exponent **n = 4.5** — textbook two-ray ground reflection, which is
what two antennas near the ground should produce. Excess loss over free space
**63 dB at 100 m**.

Extrapolated to 250 m this gave SF10 +2.5 dB and SF12 +7.5 dB — **no spreading
factor reaching 15 dB**, which is what prompted the height test above. That
extrapolation is now superseded: at 1 m the same 250 m hop measures +17.5 dB.

### The censoring problem — a bias that flatters bad sites

`rssi_min` and `deep_fades` only count frames that **decoded**. Both marks
bottomed out at exactly -126.0 dBm, 6 dB above the SF10 floor of -132: deeper
fades never arrived. So **100 m recorded 24 deep fades and 150 m only 13**,
making the weaker site look more stable. It was not — it had less room above the
cliff for a fade to be seen in.

Firmware now flags this when the weakest frames come within 10 dB of the
demodulation floor (`SURVEY_CENSOR_MARGIN_DB`). **Delivery rate is the statistic
to trust at range; min and fade counts are lower bounds.**

### A note on the recorded order

The two marks came off as 150 m (uptime 1166 s) then 100 m (uptime 1325 s) — the
150 m point was recorded first. CSV order is insertion order, not walking order.
