# bucket

Firmware for a **dehumidifier bucket auto-evacuator**.

A small STM32 BLE MCU watches the water level in a dehumidifier's collection
bucket with a **capacitive sensor** and, when the water reaches the trigger
level, runs an **evacuation pump** until the bucket is drained again.

The twist versus a classic float switch: there are no moving parts in the
sensor. We use the STM32 **TSC (Touch Sensing Controller)** with a capacitive
electrode taped to the outside of the bucket wall. Water rising past the
electrode changes the measured capacitance, which the firmware turns into a
level decision with hysteresis.

## Hardware

- **MCU board:** ST **Nucleo-WB55RG** (`nucleo_wb55rg`) — STM32WB55RG, Cortex-M4
  application core + Cortex-M0+ radio core, **Bluetooth LE**, and a hardware
  **TSC** peripheral. BLE is used to report level / pump state and for tuning.
- **Sensor:** copper-tape / PCB capacitive electrode on the bucket wall, driven
  by the TSC. (Experimental — see [`hard/`](hard/) and
  [`firm/README.md`](firm/README.md).)
- **Actuator:** low-voltage diaphragm pump driven through a MOSFET / relay from
  a GPIO (`pump` alias in the devicetree overlay).
- **Debug / flash:** on-board ST-LINK/V2-1 over USB.

## Repository layout

```
bucket/
├── README.md            this file
├── CLAUDE.md            guidance for Claude Code working in this repo
├── firm/                Zephyr RTOS firmware (the application)
├── hard/                hardware notes, electrode geometry, pump wiring
└── openocd/             OpenOCD helpers for flashing/debugging the WB55
```

## Status

Early scaffold. The firmware builds for `nucleo_wb55rg`, drives the pump GPIO,
and reads the TSC. Capacitive water detection thresholds are still being
characterized against a real bucket — this is the "experiment" phase.

## Quick start

See [`firm/README.md`](firm/README.md) for toolchain setup, building, and
flashing. In short:

```sh
cd firm
source ./zephyr.env      # points at ~/zephyrproject + ~/venv + ~/zephyr-sdk
west build -b nucleo_wb55rg .
west flash               # or: openocd -f ../openocd/nucleo_wb55rg.cfg
```
