# Updating the STM32WB55 BLE coprocessor (M0+) — without CubeMX

> ✅ **Done on this board (2026-10-03).** Updated WS **1.13.2 → 1.24.0 extended**
> (`INFO_STACK_TYPE_BLE_HCI_EXT_ADV`) via **Method B (STM32CubeProgrammer CLI)**,
> install address `0x080DC000`, FUS left at 1.2.0. After re-flashing the app the
> log shows `Bluetooth controller ready` and advertising as `BucketEvac`.
> Method A (psdb) is kept below as the no-ST-tools alternative.

The Nucleo-WB55RG's radio core (Cortex-**M0+**) runs an ST-signed wireless
firmware, separate from our application (Cortex-**M4**). Zephyr's BT host runs on
the M4 and talks **HCI** to a *controller-only* firmware on the M0+. Our board
shipped an old stack, so `bt_enable()` fails:

```
<err> hci_ipm: CPU2 wireless FW mismatch: found v1.13.2, expected v1.24.0
<err> main: bt_enable failed (-19)
```

This doc updates the M0+ coprocessor so BLE comes up. **No CubeMX / CubeIDE
required.** Two routes are given:

- **Method A — `psdb`**: fully open-source, no ST tools at all (needs a small
  from-source patch, see below).
- **Method B — STM32CubeProgrammer CLI**: ST's *standalone* programmer (this is
  **not** CubeMX/CubeIDE); most robust for arbitrary versions.

> ⚠️ This writes the **secure coprocessor region**. It is independent of, and not
> covered by, the mbed app backup in `hard/backups/`. It is re-flashable but
> treat it as irreversible-ish — read the whole doc first.

---

## Verified state of *this* board

Read non-destructively with psdb (see Method A step 2):

| Item | Value | Note |
| ---- | ----- | ---- |
| FUS (Firmware Upgrade Services) | **1.2.0** | Latest FUS for WB5x — **no FUS upgrade needed** |
| Wireless stack (WS) | **1.13.2** | Old; full-stack era. Must be replaced |
| RDP / readout protection | level 0 | Required; ours is already there |
| On-board probe | ST-LINK/V2-1 (`0483:374B`) | psdb + OpenOCD both see it on macOS |

Because FUS is already current, this is a **wireless-stack swap only** (delete
old WS → install new WS). If FUS were older, you'd upgrade FUS first
(`psdb_fus_tool --fus-upgrade --bin-dir ...`, or CubeProgrammer `-fwupgrade` with
the FUS binary) before touching the stack.

---

## Which binary

We want the **HCI-Only, *extended*** controller, matching Zephyr 4.5's expected
version **v1.24.0**:

```
stm32wb5x_BLE_HCILayer_extended_fw.bin   (STM32CubeWB v1.24.0)
```

- **HCI-Only** (not "full stack"): required for Zephyr — the host is on the M4.
  Since Cube 1.13.2, full-stack binaries are *not* Zephyr-compatible.
- **extended**: enables the controller's extended features (LE Extended/periodic
  Advertising, more simultaneous connections, larger filter/resolving lists,
  extra PHY / LE power-control options) vs. the plain `HCILayer` binary. We lose
  nothing we need; only ST's *host-side* ACI vendor API is gone, and Zephyr
  replaces that natively. See the trade-off discussion in the project notes.

### Get the binary from GitHub (no ST account, no CubeProgrammer)

The copro binaries live in the public STM32CubeWB repo:

```sh
mkdir -p ~/gnu/stm32wb-copro && cd ~/gnu/stm32wb-copro
BASE=https://raw.githubusercontent.com/STMicroelectronics/STM32CubeWB/v1.24.0/Projects/STM32WB_Copro_Wireless_Binaries/STM32WB5x
curl -LO $BASE/stm32wb5x_BLE_HCILayer_extended_fw.bin
curl -LO $BASE/Release_Notes.html      # contains the per-flash-size install addresses
md5 stm32wb5x_BLE_HCILayer_extended_fw.bin     # note this MD5 (needed for Method A)
```

> The **install address** is version-specific. For the 1 MB WB55RG, read it from
> `Release_Notes.html` ("Memory mapping" table, column for 1 Mbyte, row
> `BLE_HCILayer_extended`). Do **not** reuse an address from an older version —
> newer (larger) stacks start lower in flash.

---

## Method A — `psdb` (pure open-source, no ST tools)

`psdb` (PyPI, pure-Python, works on macOS without sudo) drives ST's FUS over the
on-board ST-LINK. Installed into `~/venv`:

```sh
~/venv/bin/pip install psdb        # already done in this project
```

### 1. Confirm the probe is seen
```sh
~/venv/bin/psdb_fus_tool -d
```

### 2. Read current FUS / WS versions (non-destructive)
```sh
~/venv/bin/psdb_fus_tool --fus-enter
# -> FUS version: 1.2.0 ;  WS version: 1.13.2
```

### 3. ⚠️ Caveat: patch psdb for v1.24.0 (build from source)

`psdb` recognizes stack binaries by **MD5** against a *hardcoded table* that, as
of psdb 1.2.0, tops out at **v1.13.3** — it does **not** know v1.24.0, so
`--fw-upgrade` would fail with `WS binary with MD5 ... not found`. Add an entry
from source (this is what `~/gnu` is for):

```sh
cd ~/gnu
git clone https://github.com/tgree/psdb
cd psdb
# Edit: psdb/devices/stm32wb55/ipc/binaries.py
# Append to WS_BINARY_UPGRADE_TABLE a new STBinary(...) line:
#
#   STBinary('stm32wb5x_BLE_HCILayer_extended_fw.bin',
#            0x01180000,                 # version 1.24.0 (cosmetic; match is by MD5)
#            '1f67ad15b471b6e23beacf8c20c71b31',   # md5 of v1.24.0 extended bin
#            0x080DC000,                 # addr_1M (v1.24.0 extended, from Release_Notes.html)
#            None, None, None),          # 640K/512K/256K unused for WB55RG (1M)
#
# Install the patched psdb over the pip one in ~/venv:
~/venv/bin/pip install -e ~/gnu/psdb
```
(Only `addr_1M` matters for the 1 MB WB55RG. `find_ws_binary` matches by MD5; the
version field is display-only.)

### 4. Delete the old stack, install the new one, boot to flash
```sh
~/venv/bin/psdb_fus_tool --fw-delete
~/venv/bin/psdb_fus_tool --fw-upgrade ~/gnu/stm32wb-copro/stm32wb5x_BLE_HCILayer_extended_fw.bin
~/venv/bin/psdb_fus_tool --fus-enter       # verify: WS version now 1.24.0
~/venv/bin/psdb_fus_tool --set-flash-boot  # boot back into our M4 app
```
`--fw-delete` is mandatory first: `upgrade_ws_firmware` asserts WS version == 0.
The upgrade reboots the target several times; that's expected.

---

## Method B — STM32CubeProgrammer CLI (standalone; not CubeMX)

If you'd rather not patch psdb, STM32CubeProgrammer handles any version and reads
the address from the binaries' release notes. Install only **STM32CubeProgrammer**
(the standalone tool — *not* CubeMX, *not* CubeIDE); it provides
`STM32_Programmer_CLI`.

```sh
CLI="/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin/STM32_Programmer_CLI"
BIN=~/gnu/stm32wb-copro/stm32wb5x_BLE_HCILayer_extended_fw.bin
ADDR=0x080DC000   # v1.24.0 extended, 1 Mbyte column of Release_Notes.html (verified on this board)

# 1. Check FUS/stack state
$CLI -c port=SWD mode=UR -fusgetstate
# 2. Remove the current wireless stack
$CLI -c port=SWD mode=UR -fwdelete
# 3. Install the new stack (firstinstall=1 since we just deleted it)
$CLI -c port=SWD mode=UR -fwupgrade $BIN $ADDR firstinstall=1
# 4. Start the wireless stack
$CLI -c port=SWD mode=UR -startwirelessstack
```

CubeProgrammer also supports the on-board ST-LINK with no extra wiring.

---

## After the update

1. Re-flash our application (OpenOCD is the default runner):
   ```sh
   cd firm && source ./zephyr.env && west flash
   ```
2. On the shell you should now see `Bluetooth controller ready` and the device
   advertising as `BucketEvac` instead of the `-19` error.
3. To actually *use* the extended-advertising capability the new binary provides,
   enable it in Zephyr later: `CONFIG_BT_EXT_ADV=y` (and use `bt_le_ext_adv_*`).
   Milestone-1 legacy advertising works without it.

---

## Notes

- **J-Link:** not needed and awkward here — the Nucleo-WB55RG has no dedicated
  Cortex-debug header; you'd have to hand-wire SWDIO/SWCLK/GND/NRST off the
  morpho header and isolate the on-board ST-LINK. The on-board ST-LINK/V2-1
  already works with both psdb and OpenOCD, so use it. (SEGGER's J-Flash does
  support WB copro updates if you ever go that route.)
- **FUS upgrade order:** if a future board has an older FUS, upgrade FUS *before*
  the stack; FUS ≤ 0.5.3 cannot jump straight to latest (intermediate hop).
- **Option bytes:** psdb toggles boot option bytes to enter FUS and restores
  flash boot with `--set-flash-boot`; this is normal, not corruption.
- The coprocessor update does **not** touch our M4 application flash
  (0x08000000..), only the secure upper region.
