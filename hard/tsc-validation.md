# TSC capacitive sensing — validation & prototyping

## Quick start: bench test on the ARDUINO headers (no soldering, no bucket)

**Yes, TSC pins reach the ARDUINO headers.** TSC Group 3 lands on three
ARDUINO pins (DS11929 pin table; UM2819 Table 9; Zephyr `stm32wb55rgvx-pinctrl`):

| TSC IO | MCU pin | ARDUINO | Connector-pin | Use |
| --- | --- | --- | --- | --- |
| G3_IO1 | PA15 | D5 | CN9-6 | not used (board DTS gives it to `pwm2`) |
| **G3_IO2** | **PC10** | **D4** | **CN9-5** | **sampling cap `Cs`** |
| G3_IO3 | PC11 | — | morpho only | — |
| **G3_IO4** | **PC12** | **D8** | **CN5-1** | **electrode / test cap** |
| — | GND | GND | **CN5-7** (or CN6-6/7) | ground |

No other TSC group has two IOs on the ARDUINO headers. G1 (PB12–15) and G2
(PB4–7) are morpho-only, and G4 (PC6 = D2) and G7 (PB9 = D14) have just one
ARDUINO pin each. PC10 and PC12 drive nothing else on the board (no LED,
button, or solder bridge).

> Requires the overlay set to `group = <3>`, `sampling-io = <TSC_IO2>`,
> `channel-ios = <TSC_IO4>`, pinctrl `tsc_g3_io2_pc10 tsc_g3_io4_pc12`. The
> shell prints `g3 count = …`. If the firmware still uses Group 1, use the
> morpho wiring in §4 instead.

UM2819 places the ARDUINO sockets on the board's **bottom side**.

### Parts (make these as jumper leads)

| Lead | Between | Part in the lead |
| --- | --- | --- |
| **A — Cs** | D4 (CN9-5) ↔ GND (CN5-7) | **10 nF film or C0G** (not X7R/X5R) |
| **B — Rs** | D8 (CN5-1) ↔ free end "E" | **1 kΩ** (2.2 kΩ is also fine) |
| **C — Cx** | E ↔ GND (CN5-7 or CN6-6/7) | swap in **2.2 / 18 / 33 / 47 pF C0G** |

```
 D4 (PC10) ──[ 10 nF ]── GND            lead A: keep it short
 D8 (PC12) ──[ 1 kΩ ]── E ──[ Cx ]── GND  leads B + C
```

### Procedure

1. Flash, open the shell (`uart:~$`). With **nothing** plugged in, `tsc` shows
   about `1`. That's expected: no Cs means no count.
2. Plug in **lead A** only. `tsc` should now read a large number (thousands).
   If it still shows ~1, lead A is on the wrong pin or the cap is open.
3. Add **lead B** with E left floating. Run `tsc` 5×. That reading is the
   **"empty"** baseline.
4. Add **lead C** with each cap in turn and run `tsc` 5× per cap:
   2.2 pF → 18 pF → 33 pF → 47 pF.
5. Fill in the table. The count should **drop** as Cx grows (N ∝ 1/Cx).

| Cx | `tsc` readings (5×) | mean |
| --- | --- | --- |
| open (E floating) | | |
| 2.2 pF | | |
| 18 pF | | |
| 33 pF | | |
| 47 pF | | |

**Pass:** open > 2.2 > 18 > 33 > 47 pF, with each step well above the
reading-to-reading jitter.

**Troubleshooting:**
- Reads ~16383 (ceiling): use a **4.7 nF** Cs.
- 33 pF is hardly below open: use a **22 nF** Cs.
- Readings jump around: shorten the leads, take your hand away (a finger adds
  pF), and keep the pump wiring away.

### Bench result (2026-10-11, Cs = 10 nF ceramic "103", Rs = 1 kΩ)

| D8 load | Count | Jitter |
| --- | --- | --- |
| open | 355 | steady |
| 2 pF (nominal) | 305 | 304–305 |
| 33 pF | 136 | steady |

A straight-line fit gives `1/N ≈ 1.31e-4 · (Cx + Cpar)` with **Cpar ≈ 21 pF**
(leads + header + pin) and k ≈ 0.76. The parasitic swamps the 2 pF part,
which is why the counts are ~10× below the §2 table. This is treated as the
worst case, since a real band and its leads add more.

Firmware thresholds set from this fit (`firm/src/level.c`): **trigger < 220**
(≈ 14 pF of water signal) and **release > 260** (≈ 8 pF). Expected: dry band
~330, half ~195, full ~138. If Cpar grows by more than ~8 pF, the dry reading
falls below 260: re-measure and re-set.

**Using a 100 nF "104" Cs** would give ~10× the counts (dry ~3.4k, full
~1.4k), still under the 16383 ceiling as long as Cx + Cpar ≥ ~5 pF. That's
true with this wiring. If you swap it in, scale both thresholds ×10.

---

How to characterize the STM32 TSC water-level sensing on a **bare Nucleo-WB55RG**
(no bucket yet), pick the sampling capacitor, and set firmware thresholds.

## 0. Measured electrode capacitance (bench experiment)

Measured on a mock tank — plastic box **100 × 100 × 70 mm**, **2 mm** walls,
with two **25 mm-wide copper-foil** strips glued as horizontal bands at
mid-height on adjacent faces, meeting at a corner with a **~2 mm gap** (the two
bands are the two TSC electrodes; water between/behind them is the dielectric):

| Tank state | `Cx` (LCR meter) |
| --- | --- |
| Empty | **~2 pF** |
| Half (water to mid-band) | **~18 pF** |
| Full | **~34 pF** |

Reasoning for a real bucket: a larger tank widens the band separation but also
grows the band area — both scale ~linearly, so the **dry** value should stay
similar (~2 pF), while **full** may be a bit higher. Taking the wall thickness
(≤ ~4 mm) as the limiting series dielectric, the practical range to design for is
about **2 → 20 pF** (with full possibly to ~34 pF).

> **Parasitics matter at this scale.** The TSC channel pin + wiring add a few pF
> (Cpar ≈ 3–10 pF) *in parallel* with the electrode, so the TSC effectively sees
> `Cx + Cpar`. That raises the "empty" floor (helpful — fewer counts, more
> max-count headroom) and compresses the relative swing. Measure the bare-pin
> baseline first (see §8) to back out Cpar.

Earlier revisions of this doc assumed ~50–250 pF; the numbers below are redone
for the measured 2–34 pF.

## 1. How the TSC count behaves

The TSC charges the electrode (`Cx`) and transfers its charge into an external
**sampling capacitor `Cs`** repeatedly; it counts the charge-transfer cycles `N`
needed for `Cs` to reach the input threshold `V_IH`:

```
Vcs(N) = VDD · [1 − (Cs/(Cs+Cx))^N]      → solve Vcs(N)=V_IH
N = ln(1 − V_IH/VDD) / ln(Cs/(Cs+Cx))
For Cs ≫ Cx:   N ≈ k · (Cs / Cx),   k = ln(1/(1−V_IH/VDD)) ≈ 0.7 … 1.2
```

Key consequences:

- **N ∝ Cs/Cx.** Water rises → `Cx` increases → **N decreases**. So in firmware a
  *count drop* = *water present* (this is why the `tsc-keys` "press"/touch event
  maps to water detected in `level.c`).
- Bigger `Cs` → more counts → finer resolution, but longer acquisition and risk
  of hitting the max-count ceiling.
- Linearity/robustness want `Cs ≫ Cx` (rule of thumb `Cs/Cx` ≳ 100).

`k` depends on the exact IO threshold, so treat the predicted counts below as
order-of-magnitude and **calibrate by measuring**.

## 2. Choosing the sampling capacitor `Cs`

Because `Cx` is now small (2 pF), `N ≈ k·Cs/Cx` gets **large**, so `Cs` must be
small to keep the empty count under the max-count ceiling
(`st,max-count-value = 16383`). Predicted `N ≈ k·Cs/Cx` (k ≈ 0.7–1.2), for the
bare electrode (no Cpar — the worst case for max-count):

| `Cs` | N empty (2 pF) | N trigger (18 pF) | N full (34 pF) | Note |
| --- | --- | --- | --- | --- |
| 4.7 nF | ~1.6k–2.8k | ~180–310 | ~100–170 | conservative / fast |
| **10 nF** | **~3.5k–6.0k** | **~390–670** | **~210–350** | **recommended** |
| 22 nF | ~7.7k–13.2k | ~860–1.5k | ~450–780 | more resolution; near the ceiling at 2 pF |
| 47 nF | ~16k–28k ⚠ | — | — | **exceeds max-count** — do not use |

**Recommended start: `Cs = 10 nF.`** Even at a bare 2 pF with k = 1.2 the empty
count (~6k) stays well under 16383, and once Cpar is included (effective
empty ~7–12 pF) it drops to ~1k–2k — lots of headroom. The empty → full swing is
thousands of counts, far above noise. Use **22 nF** for more resolution only if
measured counts leave ceiling room; **4.7 nF** if counts run high or you want
faster acquisition. (This is ~5× smaller than the old 47 nF, because `Cx`
turned out ~20× smaller than assumed.)

**Cs type matters:** use a **film** cap (polyester/PP, e.g. WIMA MKS2) or
**C0G/NP0** — low tempco, low voltage-coefficient. **Do NOT use X7R/X5R
ceramics** for `Cs`; their capacitance drifts with temperature and bias and will
swamp the water signal. 50 V, ±5–10 %, radial through-hole is fine.

## 3. Through-hole parts for the bare-board prototype

### 3a. Sampling capacitor (one per active group)
- **10 nF film/C0G** (primary). Also handy: **4.7 nF** and **22 nF** to bracket.

### 3b. Electrode emulators — validate *without* a bucket
Stand in for `Cx` with precise, stable **C0G/NP0 through-hole ceramics** (±5 %,
50 V). Plug one at a time from the channel node to GND to sweep the curve:

| Emulates | Cap |
| --- | --- |
| Empty (~2 pF) | ~**2.2 pF** (or nothing — just the bare-pin parasitic) |
| Trigger / half (~18 pF) | **18 pF** (or 15 pF ∥ 2.2 pF) |
| Full (~34 pF) | **33 pF** ✅ *(you have these)* |
| Over-full margin | 47 pF |

A small kit — **2.2 / 10 / 18 / 33 / 47 pF C0G** — lets you plot N vs Cx and set
thresholds before any copper goes on a bucket. The **33 pF** you already have is
a perfect "full" stand-in (see §4a for the quick test).

### 3c. Series resistor (electrode)
- **Rs = 1 kΩ … 10 kΩ**, 1/4 W through-hole, in series with the channel IO
  (ESD/EMC; also sets τ = Rs·Cx; at 10 kΩ·34 pF ≈ 0.34 µs it's tiny, so timing
  is not a constraint at this Cx). Start at ~1–2.2 kΩ.
- Sampling IO needs **no** series resistor.

## 4. Alternative wiring on the morpho header (TSC Group 1)

The firmware now uses Group 3 on the ARDUINO headers (see Quick start). Group 1
below works only if the overlay is switched back to G1: sampling =
`TSC_IO1` (PB12), channel = `TSC_IO2` (PB13).

```
  PB12 (G1_IO1, sampling) ──┬── Cs 10nF ── GND
                            (keep this lead short)

  PB13 (G1_IO2, channel) ── Rs 1–2.2k ──┬── electrode  OR  test-cap Cx ── GND
```

- PB12–PB15 are on the ST **morpho** header (CN10) — confirm exact pins against
  the Nucleo-WB55 user manual (UM) before wiring. `GND`/`3V3` are on the morpho
  too.
- Short leads, one solid ground reference, keep the pump wiring away from the
  electrode node.

### 4a. Quick 33 pF test (do this first)

You have 33 pF caps — this needs no bucket:

1. **Cs:** `PB12 ── 10 nF ── GND`.
2. **Baseline:** leave `PB13` open (just the Rs + bare pin). On the shell run
   `tsc` a few times. This is the **parasitic floor** (≈ empty); expect maybe
   ~700–3000 counts depending on wiring/Cpar. Note it.
3. **"Full":** add `PB13 ── 33 pF ── GND` (in parallel with the parasitic, so
   effective Cx ≈ 33 pF + Cpar). Run `tsc` again — expect roughly **~200–350**.

The count should **drop a lot** when you add the 33 pF (N ∝ 1/Cx). That drop is
your empty↔full dynamic range. If "full" (33 pF) isn't comfortably below the
baseline, raise `Cs` (→ 22 nF) for more resolution; if the baseline is near
16383, lower `Cs` (→ 4.7 nF). Then sweep 2.2 / 18 / 33 pF to map the curve.

## 5. Firmware instrumentation (done)

The raw per-group count is already exposed (see `firm/src/tsc.c`): the firmware
samples Group 1 and prints the count with the **`tsc`** shell command, and also
publishes it live in the BLE Level characteristic (`rawTSC`) and the app's
"raw TSC" readout.

```
uart:~$ tsc
g1 count = 712
```

(With nothing wired it reads ~1 — the acquisition finishes in one cycle, see
`LEVEL_COUNT_MIN_VALID` in `level.c`.)

## 6. Validation procedure

1. **Bench sweep (emulated Cx).** With `Cs = 10 nF`, run §4a, then sweep the C0G
   caps (2.2 → 33 pF) in turn; record the average of ~100 `tsc` samples. Plot
   **N vs Cx** and **1/N vs Cx** — the latter should be ~linear (N ∝ 1/Cx).
   Confirm bare (empty) and 33 pF (full) counts are well separated and both
   inside `[noise , max-count]`.
2. **Right-size Cs.** If empty-count is near 16383 → lower `Cs`; if the swing is
   small vs noise → raise `Cs`. Re-sweep.
3. **Noise / SNR.** Log ~1000 samples at a fixed Cx; compute mean and σ. Require
   **swing ≫ σ** (target SNR ≳ 10). Watch drift over minutes and with a warm
   hand near the setup (temp/humidity proxy).
4. **Real electrode.** Build the copper-tape electrode on the bucket wall; repeat
   dry vs. water at the trigger height; compare to the emulated numbers and to
   the LCR-meter reading of the real `Cx`.
5. **Thresholds + hysteresis.** Choose `N_trigger` (water detected, count below)
   and `N_release` (count above), with a hysteresis band ~10–20 % of the swing
   to stop chatter. Prefer **baseline tracking** (slow-moving reference) over a
   fixed threshold so temperature/humidity drift doesn't cause false trips —
   then set `LEVEL_TRIGGER_COUNT`/`LEVEL_RELEASE_COUNT` in `level.c` (or grow it
   into a baseline-tracking algorithm).

## 7. If counts saturate or are unstable

Tune the acquisition (overlay props), roughly in this order:

- **`st,max-count-value`** — raise/lower the ceiling to fit the empty count
  (first lever here, since small `Cx` makes empty counts large).
- **`st,charge-transfer-pulse-high/low`** — at 2–34 pF, `τ = Rs·Cx` is < 1 µs so
  the default short pulses are fine; only lengthen if transfers look incomplete.
- **`st,pulse-generator-prescaler`** — adjust the charge-transfer clock.
- **`st,spread-spectrum`** — enable for EMC if counts are noisy near switching
  loads (the pump).

## 8. Acceptance criteria

Stable, repeatable separation between empty (~2 pF) and full (~34 pF) with
SNR ≳ 10 and no false trips across the expected temperature/humidity range once
baseline tracking is on.

> **Firmware thresholds:** `level.c` uses trigger 220 / release 260, from the
> Quick-start bench fit (Cs = 10 nF, Cpar ≈ 21 pF). Re-set them after the
> real-band test.

## BOM (prototyping)

| Qty | Part | Notes |
| --- | --- | --- |
| 1–3 | 10 nF film/C0G (+ 4.7 nF, 22 nF) | sampling `Cs`, 50 V — **not X7R** |
| 1 ea | 2.2 / 10 / 18 / 33 / 47 pF C0G | electrode emulators, 50 V ±5 % (33 pF ≈ full) |
| 2 | 1 kΩ, 2.2 kΩ resistor 1/4 W | series `Rs` (start ~1–2.2 k) |
| 1 | copper foil/tape | the real electrode (later) |
| — | LCR meter or DMM w/ cap range | ground-truth real `Cx` |
