# TSC capacitive sensing — validation & prototyping

How to characterize the STM32 TSC water-level sensing on a **bare Nucleo-WB55RG**
(no bucket yet), pick the sampling capacitor, and set firmware thresholds.

Working assumption for this doc (to be ground-truthed with an LCR meter):

| Bucket state | Electrode capacitance `Cx` |
| --- | --- |
| Empty | **~50 pF** |
| Full  | **~250 pF** (5× empty) |

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

`Cs/Cx` ratio and predicted count `N ≈ k·Cs/Cx` (k≈0.7–1.2), vs our max-count
ceiling (`st,max-count-value = 16383` in the overlay):

| `Cs` | `Cs/Cx` empty (50 pF) | `Cs/Cx` full (250 pF) | N empty (≈) | N full (≈) | Swing |
| --- | --- | --- | --- | --- | --- |
| 22 nF  | 440 | 88  | ~310–530   | ~60–105   | ~250–420  |
| **47 nF** | **940** | **188** | **~660–1130** | **~130–225** | **~530–900** |
| 100 nF | 2000 | 400 | ~1400–2400 | ~280–480  | ~1100–1900 |
| 220 nF | 4400 | 880 | ~3100–5300 | ~620–1060 | ~2500–4200 |

**Recommended start: `Cs = 47 nF.`** `Cs/Cx` stays in the linear regime
(188–940), empty count sits comfortably below 16383 (headroom if the electrode
turns out drier than 50 pF), and the empty↔full swing (~500–900 counts) is huge
relative to expected noise. Move to **100 nF** if you want more resolution
(acquisition time is irrelevant for a slow level sensor); drop to **22 nF** if
counts come out too high or you want faster acquisition.

**Cs type matters:** use a **film** cap (polyester/PP, e.g. WIMA MKS2) or
**C0G/NP0** — low tempco, low voltage-coefficient. **Do NOT use X7R/X5R
ceramics** for `Cs`; their capacitance drifts with temperature and bias and will
swamp the water signal. 50 V, ±5–10 %, radial through-hole is fine.

## 3. Through-hole parts for the bare-board prototype

### 3a. Sampling capacitor (one per active group)
- **47 nF film** (primary). Also buy **22 nF** and **100 nF** to bracket.

### 3b. Electrode emulators — validate *without* a bucket
Stand in for `Cx` with precise, stable **C0G/NP0 through-hole ceramics** (±5 %,
50 V). Plug one at a time from the channel node to GND to sweep the curve:

| Emulates | Cap |
| --- | --- |
| Empty (~50 pF) | **47 pF** |
| ¼–½ full | 100 pF, 150 pF |
| Full (~250 pF) | **220 pF** (or 220 pF ∥ 33 pF ≈ 253 pF) |
| Over-full margin | 270 pF |

A small kit — **47 / 68 / 100 / 150 / 220 / 270 pF C0G** — lets you plot N vs Cx
and set thresholds before any copper goes on a bucket.

### 3c. Series resistor (electrode)
- **Rs = 2.2 kΩ … 10 kΩ**, 1/4 W through-hole, in series with the channel IO
  (ESD/EMC; also sets the charge time constant τ = Rs·Cx; at 10 kΩ·250 pF ≈
  2.5 µs — the charge-transfer pulse must be ≥ a few τ, see §6). Start at 10 kΩ;
  reduce if large-Cx acquisition saturates.
- Sampling IO needs **no** series resistor.

## 4. Wiring on the Nucleo (TSC Group 1)

Matches the overlay (`firm/boards/nucleo_wb55rg.overlay`): G1 sampling =
`TSC_IO1` (PB12), channel = `TSC_IO2` (PB13).

```
  PB12 (G1_IO1, sampling) ──┬── Cs 47nF ── GND
                            (keep this lead short)

  PB13 (G1_IO2, channel) ── Rs 10k ──┬── electrode  OR  test-cap Cx ── GND
```

- PB12–PB15 are on the ST **morpho** header (CN10) — confirm exact pins against
  the Nucleo-WB55 user manual (UM) before wiring.
- Short leads, one solid ground reference, keep the pump wiring away from the
  electrode node.

## 5. Firmware instrumentation (prerequisite)

The shipped `tsc-keys` driver only emits press/release — **it does not expose the
raw count**, which is exactly what characterization needs. Add a raw-count
readout before collecting data, e.g. a shell command:

```
uart:~$ tsc read      # trigger one acquisition, print the Group-1 count
g1 count = 712
```

Implementation options: read the TSC `IOGxCR[CNT]` count register for the group
after an acquisition, or add a tiny raw acquisition path alongside the input
driver. (Ask and I'll wire this up as `src/tsc_raw.c` + a shell command.)

## 6. Validation procedure

1. **Bench sweep (emulated Cx).** With `Cs = 47 nF`, plug each C0G cap
   (47→270 pF) in turn; record the average of ~100 `tsc read` samples. Plot
   **N vs Cx** and **1/N vs Cx** — the latter should be ~linear (N ∝ 1/Cx).
   Confirm: 47 pF and 220 pF counts are well separated and both inside
   `[noise , max-count]`.
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
   then fold these into the `tsc-keys` `noise-threshold`/`oversampling`/
   `sampling-interval-ms` (or a custom level algorithm).

## 7. If counts saturate or are unstable

Tune the acquisition (overlay props), roughly in this order:

- **`st,charge-transfer-pulse-high/low`** — increase so each pulse fully
  charges the (large) `Cx` through `Rs` (needs ≥ a few τ).
- **`st,pulse-generator-prescaler`** — adjust the charge-transfer clock.
- **`st,max-count-value`** — raise/lower the ceiling to fit the empty count.
- **`st,spread-spectrum`** — enable for EMC if counts are noisy near switching
  loads (the pump).

## 8. Acceptance criteria

Stable, repeatable separation between empty (~50 pF) and full (~250 pF) with
SNR ≳ 10 and no false trips across the expected temperature/humidity range once
baseline tracking is on.

## BOM (prototyping)

| Qty | Part | Notes |
| --- | --- | --- |
| 1–3 | 47 nF film cap (+ 22 nF, 100 nF) | sampling `Cs`, 50 V, film/C0G — **not X7R** |
| 1 ea | 47/68/100/150/220/270 pF C0G ceramic | electrode emulators, 50 V ±5 % |
| 2 | 10 kΩ, 2.2 kΩ resistor 1/4 W | series `Rs` (start 10 k) |
| 1 | copper tape / small PCB | the real electrode (later) |
| — | LCR meter or DMM w/ cap range | ground-truth real `Cx` |
