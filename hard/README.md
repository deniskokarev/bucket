# hard — hardware notes

## Sensor: capacitive water-level electrode

The level sensor is a capacitive electrode read by the STM32WB **TSC** (Touch
Sensing Controller). The goal is a no-moving-parts, no-contact-with-water
sensor: the electrode sits on the **outside** of the bucket wall, and water
rising behind it increases the measured capacitance.

Starting points to characterize:

- **Electrode:** vertical strip of copper tape (or a small PCB) on the outer
  bucket wall at the desired trigger height. A second strip can act as the TSC
  **sampling capacitor** / shield.
- **Sampling capacitor (Cs):** TSC needs an external sampling cap on the
  sampling IO of the group (typ. 10–50 nF). Value trades sensitivity vs.
  acquisition time.
- **Series resistor:** ~10 kΩ in series with the sensing electrode is typical
  for TSC robustness.
- **Grounding:** the system ground reference matters a lot for capacitive
  sensing stability; keep the pump's switching noise away from the electrode.

Expect raw TSC counts to drift with temperature and humidity — the firmware
uses hysteresis (and later, baseline tracking) rather than a fixed threshold.

See **[tsc-validation.md](tsc-validation.md)** for the bench characterization
plan, sampling-capacitor sizing, and the through-hole parts to prototype on a
bare Nucleo (assuming empty ≈ 50 pF, full ≈ 250 pF).

## Actuator: evacuation pump

- Low-voltage diaphragm/peristaltic pump.
- Switched by a logic-level **MOSFET** (or a relay) from the `pump` GPIO.
- Add a **flyback diode** across an inductive pump/relay load.
- Pump power is separate from the Nucleo's 3V3; share grounds.

## TODO

- [ ] Finalize electrode geometry + Cs value against a real bucket.
- [ ] Map chosen TSC group/channel IOs to Nucleo-WB55 header pins (see the
      STM32WB55 datasheet TSC group table) and update
      `firm/boards/nucleo_wb55rg.overlay`.
- [ ] Pick pump GPIO header pin and MOSFET; document wiring here.
- [ ] Bench measurement: TSC counts, dry vs. water at trigger height.
