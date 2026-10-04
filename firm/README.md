# bucket firmware

Zephyr RTOS firmware for the dehumidifier bucket auto-evacuator, targeting the
ST **Nucleo-WB55RG** (`nucleo_wb55rg`).

Built & verified against **Zephyr 4.5.0-rc1** with **Zephyr SDK 1.0.1**.

## Milestone 1 (done): bring-up

Boot the WB55 with the **TSC** capacitive sensor and the **Bluetooth**
controller initialized, plus an interactive **shell**. Verified on hardware:

```
- tsc@40024000 (READY)          <- TSC peripheral up
- water_key (READY)             <- tsc-keys input device up
hci_ipm: CPU2 wireless FW: v1.24.0 (INFO_STACK_TYPE_BLE_HCI_EXT_ADV)
main: Bluetooth controller ready
main: advertising as "BucketEvac"
```

> **BT note (resolved):** the board originally shipped an old M0+ stack (v1.13.2)
> and `bt_enable()` returned `-19`. We updated the coprocessor to the **HCI-Only
> extended v1.24.0** stack — see [*Wireless coprocessor*](#wireless-coprocessor-for-ble).
> BLE now comes up and advertises.

## Toolchain

Nothing is vendored here. The Zephyr tree, SDK and Python venv live under
`$HOME` and are referenced via `zephyr.env`:

| Thing      | Location          |
| ---------- | ----------------- |
| Zephyr     | `~/zephyrproject` |
| Zephyr SDK | `~/zephyr-sdk`    |
| Python venv (`west` + reqs) | `~/venv` |

To (re)create them, follow the Zephyr
[Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html),
or the equivalent, which is what this project used:

```sh
python3 -m venv ~/venv
~/venv/bin/pip install west
~/venv/bin/west init ~/zephyrproject && (cd ~/zephyrproject && ~/venv/bin/west update)
~/venv/bin/west zephyr-export
~/venv/bin/pip install -r ~/zephyrproject/zephyr/scripts/requirements.txt
~/venv/bin/west sdk install -d ~/zephyr-sdk -t arm-zephyr-eabi -H
```

## Build

```sh
cd firm
source ./zephyr.env          # ZEPHYR_BASE, ZEPHYR_SDK_INSTALL_DIR, venv, aliases
west build -b nucleo_wb55rg .   # or just: b
```

## Flash & debug (OpenOCD)

OpenOCD is the **default runner** for this app (set via `BOARD_FLASH_RUNNER` in
`CMakeLists.txt`), so no `-r` is needed:

```sh
west flash                   # or: f
west debug                   # gdb via OpenOCD
```

The on-board ST-LINK/V2-1 is recognized out of the box by Homebrew OpenOCD
0.12.0. Manual attach / flash is also possible:

```sh
openocd -f ../openocd/nucleo_wb55rg.cfg
openocd -f ../openocd/nucleo_wb55rg.cfg \
        -c "program build/zephyr/zephyr.hex verify reset exit"
```

> If OpenOCD can't see the probe, `brew install stlink` gives `st-info --probe`
> as a quick cross-check. OpenOCD is the preferred path.

## Console / shell

Shell + log share the ST-LINK virtual COM port at **115200 8N1**:

```sh
picocom -b 115200 /dev/cu.usbmodem*       # exit: C-a C-x
# or:  screen /dev/cu.usbmodem* 115200
```

Useful shell commands during bring-up: `device list`, `kernel uptime`,
`bt` (BT commands), and watch the `<inf> level:` lines for raw TSC input events.

## CLion

This builds as a normal CMake project. The firm `CMakeLists.txt` defaults
`BOARD=nucleo_wb55rg`, so CLion just needs the environment:

1. Settings → Build, Execution, Deployment → CMake → *Environment*: add
   `ZEPHYR_BASE=$HOME/zephyrproject/zephyr` and
   `ZEPHYR_SDK_INSTALL_DIR=$HOME/zephyr-sdk`.
2. Set the CMake *generator* to Ninja and point the Python interpreter at
   `~/venv` if prompted.
3. Reload the CMake project. Zephyr's CMake package pulls in the SDK toolchain;
   CLion indexes from the generated `build/compile_commands.json`.

`.clang-format` in this dir drives CLion's formatter and `clangd`.

## Source map

| File | Role |
| ---- | ---- |
| `src/main.c`  | init pump + level + BT, advertise (re-advertise on disconnect), control loop |
| `src/tsc.c`   | raw TSC Group-1 acquisition; `tsc_last_count()` + `tsc` shell command |
| `src/level.c` | raw count → "water present" with threshold + hysteresis |
| `src/pump.c`  | pump GPIO with min-run / max-run guards + control mode |
| `src/gatt.c`  | custom GATT service: Level (read/notify) + secure Pump (read/write) |
| `src/bond.c`  | pairing window (`bond` shell cmd + SW1/EXTI), secure-access gating |
| `boards/nucleo_wb55rg.overlay` | TSC node (G1: PB12 Cs / PB13 electrode), `pump` GPIO, SW1 bond button |
| `boards/nucleo_wb55rg.conf`    | board Kconfig fragment |

### BLE GATT service

The board advertises service `d2f2a658-2605-4993-9794-01961b93a091` (name
`BucketEvac`) with two characteristics — the contract shared with the mobile
app in [`../client/src/utils/ble.ts`](../client/src/utils/ble.ts):

| Characteristic | UUID | Props | Payload |
| --- | --- | --- | --- |
| Level | `3ec2dc2f-…a88003` | Read + Notify | `[water:u8, pump:u8, rawTSC:u16 LE]` |
| Pump  | `4feebce2-…756af1` | Read + Write (**encrypted**) | `u8` mode: `0=auto, 1=off, 2=on` |

`rawTSC` is the live raw Group-1 acquisition count. Verified on-device with
`gatt show-db` (handles 0x0012 Level, 0x0015 Pump).

### Pairing / bonding

The Pump characteristic requires an **encrypted (bonded)** link, so pump control
is only available after pairing; Level/notify stay open. New pairings are
rejected except during a 60 s **bonding window**, opened by:

- the **`bond`** shell command, or
- pressing **SW1** (PC4, wired as an EXTI GPIO interrupt).

During the window a client bonds by **reading the secure Pump attribute**, which
triggers OS pairing (LE Secure Connections, Just Works). Bonds persist in the
`storage` flash partition (NVS); bonded centrals reconnect encrypted with no
re-pairing. Verified end-to-end on Android (`pairing complete (bonded=1)`,
secure pump write succeeds).

## Wireless coprocessor (for BLE)

The WB55 BLE controller runs on the Cortex-M0+ as an ST-signed binary. This
board shipped an old stack (v1.13.2) so `bt_enable()` returns `-19`; Zephyr 4.5
wants the **HCI-Only** controller **v1.24.0** (we use the *extended* variant).

Full step-by-step update instructions — **without CubeMX**, via the open-source
`psdb` tool *or* standalone STM32CubeProgrammer — are in
[`../docs/ble-coprocessor-update.md`](../docs/ble-coprocessor-update.md). The
update does not touch our M4 application flash.
