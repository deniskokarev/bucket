# CLAUDE.md — bucket

Guidance for Claude Code (and humans) working in this repository.

## What this project is

Firmware for a **dehumidifier bucket auto-evacuator**: an STM32WB55 BLE MCU
senses the water level in a dehumidifier bucket with a **capacitive electrode**
driven by the STM32 **TSC** (Touch Sensing Controller), and switches an
**evacuation pump** on/off with hysteresis. BLE reports state and is used for
live tuning of the capacitance thresholds.

This repo follows the same layout as the sibling `thermo` and `geiger` Zephyr
projects in `../`: a top-level split into `firm/` (the Zephyr app), `hard/`
(hardware), and tooling dirs.

## Target

- **Board:** `nucleo_wb55rg` (ST Nucleo-WB55RG, STM32WB55RG).
- **Zephyr:** installed at `~/zephyrproject` (not vendored in this repo).
- **SDK:** `~/zephyr-sdk` (arm-zephyr-eabi toolchain).
- **Python venv:** `~/venv` (holds `west` and Zephyr's Python requirements).
- Source `firm/zephyr.env` to set `ZEPHYR_BASE`, `ZEPHYR_SDK_INSTALL_DIR`, and
  activate the venv in one shot.

## Build / flash / debug

Always work from `firm/` with the environment sourced:

```sh
cd firm
source ./zephyr.env
west build -b nucleo_wb55rg .        # configure + build
west build                           # incremental rebuild
west flash                           # flash via on-board ST-LINK (OpenOCD)
```

Handy aliases (set by `zephyr.env`): `b` = build, `f` = flash.

### OpenOCD (preferred flasher/debugger)

The board is recognized out of the box by Homebrew OpenOCD 0.12.0 — no udev /
driver work was needed on this machine. Verified:

```
STLINK V2J39M27 (API v2) VID:PID 0483:374B
[stm32wbx.cpu] Cortex-M4 r0p1 processor detected
```

- Sanity check / attach: `openocd -f ../openocd/nucleo_wb55rg.cfg`
- `west flash` / `west debug` use OpenOCD by default. The board's upstream
  default is `stm32cubeprogrammer`; we override it to `openocd` via
  `BOARD_FLASH_RUNNER`/`BOARD_DEBUG_RUNNER` in `firm/CMakeLists.txt` (no
  upstream edits). Confirm with `grep runner firm/build/zephyr/runners.yaml`.
- GDB server listens on `:3333`; RTT/semihosting can be added later.
- **Before flashing, back up the existing image** (we must preserve the stock
  mbed OS): the non-secure user flash is 0x08000000–0x080CF000. Dump with
  `openocd -f ../openocd/nucleo_wb55rg.cfg -c "init; reset halt; dump_image hard/backups/mbed.bin 0x08000000 0x100000; shutdown"`
  (the read stops at the secure wireless-stack boundary, which is correct).

### st-link tools (fallback only)

If OpenOCD ever fails to see the probe, `brew install stlink` provides
`st-info --probe` (quick connectivity check) and `st-flash`. OpenOCD is the
preferred path; keep flashing on one tool to avoid confusion.

## Firmware structure (`firm/src/`)

- `main.c` — wiring: init pump + level sensor, run the control loop.
- `level.{c,h}` — capacitive level sensing. Consumes TSC acquisitions / input
  events, applies hysteresis, exposes "water above trigger?".
- `pump.{c,h}` — pump GPIO actuation with a minimum run-time / safety guard.
- `bond.{c,h}` — bonding window. SW1 `..-` / `bond` **clears all bonds first**,
  then accepts one new pairing for 60 s; reports bond events.
- `ui.{c,h}` — SW1 morse codes → actions, bond state → status LED pattern.
- `firm/drivers/` — out-of-tree Zephyr module (vendor prefix `dkv`, hooked in via
  `ZEPHYR_EXTRA_MODULES` in `firm/CMakeLists.txt`):
  - `dkv,morse-button` — input-subsystem filter decoding one key into
    dot/dash codes listed as child DT nodes; handlers bound per entry at boot,
    run on the module's own `dkv_wq` workqueue (never the system workqueue,
    which the BT host uses — don't block it).
  - `dkv,signal-led` — off/solid/blink/sine-glow LED patterns with a duration,
    on `pwms` or a plain GPIO (software PWM).
- Devicetree:
  - `boards/nucleo_wb55rg.overlay` — TSC node config (sampling caps + sensing
    channel for the electrode), the `pump` GPIO alias, the SW1 morse codes
    (`sw1-morse`) and the status LED (`status-led`, blue LED1/PB5 — no timer PWM
    on that pin, hence software PWM). **Pin assignments
    here are experimental and must be checked against the actual wiring and the
    STM32WB datasheet TSC group map.**
  - `boards/nucleo_wb55rg.conf` — board-specific Kconfig fragment.

## Conventions

- C (not C++) for this project; C11. 4-space indent via `.clang-format`
  (`UseTab: ForIndentation`, matching the sibling projects).
- Prefer devicetree + Kconfig over hard-coded pins/magic numbers.
- Keep `prj.conf` minimal; put board-specific config in `boards/*.conf`.
- Never commit `build/`, `cmake-build*/`, or editor scratch (see `.gitignore`).

## Gotchas

- `~/venv` is **Python 3.14** — very new. `west` works; if a Zephyr Python
  dependency ever fails to install, that's the first suspect.
- STM32WB is **dual-core**. We only build/flash the Cortex-M4 application core.
  The BLE controller runs on the M0+ via a pre-flashed ST wireless coprocessor
  binary. **Resolved on this board (2026-10-03):** it shipped WS v1.13.2 →
  `bt_enable()` returned `-19`; updated to **HCI-Only extended v1.24.0**
  (`INFO_STACK_TYPE_BLE_HCI_EXT_ADV`) at `0x080DC000` via STM32CubeProgrammer CLI
  (FUS stayed 1.2.0). BLE now advertises. Procedure (CubeProgrammer CLI used;
  open-source `psdb` alternative, whose bundled binary DB stops at ~1.13.3 so it
  needs a from-source patch under `~/gnu`) is in `docs/ble-coprocessor-update.md`.
  Caveat: any CubeProgrammer FUS op flashes an 8 KB `FUS_Operator` to
  `0x08000000`, erasing app sectors [0 1] — **re-flash the app afterward**.
- TSC acquisition values drift with temperature/humidity — hence hysteresis and
  (eventually) a baseline-tracking filter rather than a fixed threshold.
