# test_e2e — end-to-end tires-kicking

Drives the whole stack — **firmware** (over the ST-LINK serial port) and the
**Android app** (over adb, parsing the on-screen UI XML) — and asserts the full
bonding/control pipeline works.

## Objects

- **`Board`** (`board.py`) — the firmware over serial. A reader thread buffers
  the Zephyr shell/log so tests can `wait_for(<regex>)` while sending shell
  commands. Helpers: `install()` (flash the current build via OpenOCD), `ping()`,
  `open_bonding_window()` (== pressing SW1), `tsc_count()`.
- **`AndroidClient`** (`android.py`) — the app over adb. Dumps and parses the
  uiautomator XML to **find buttons by text and tap their center** (no hard-coded
  coordinates), so UI behavior is validated too. Helpers: `install()` (reinstall
  the debug APK), `is_installed()`, `relaunch()`, `home()`/`foreground()`
  (background/restore), `tap_text()`, `wait_for()`, `confirm_pairing()` (taps the
  Android system PAIR dialog), `screenshot()`.

## Pipeline (`test_pipeline.py`, ordered, failfast)

```
00 preconditions   board shell alive, app installed + running
01 forget          ensure we start unbonded (scanning)
02 bond            board SW1/`bond` -> app auto-pairs -> secure connection
03 gatt read       app shows live Level data ("raw TSC")
04 gatt write      app pump On/Auto reaches the board (pump mode -> N)
05 disconnect      backgrounding the app drops the link
06 reconnect       foregrounding auto-connects (bonded)
07 gatt read       live data again after reconnect
08 forget          back to the unbonded/scanning state
```

Each step is its own `test_NN_*` method; ordering comes from the numeric
prefixes and `-f` (failfast) stops the run at the first failure. On failure the
test prints the **on-screen text**, the **board serial tail**, and saves a
**screenshot** to `artifacts/`.

## Run

```sh
./run.sh                 # whole pipeline
./run.sh test_02_bond    # a single step
BUCKET_INSTALL=1 ./run.sh   # flash firmware + reinstall app first
```

`run.sh` starts Metro (the debug app needs it) and sets `adb reverse 8081`.
With `BUCKET_INSTALL=1`, setup calls `Board.install()` (OpenOCD flash) and
`AndroidClient.install()` (adb install -r) so both run the current build.

### Prerequisites

- Nucleo-WB55RG on USB (flashed with the Bucket firmware).
- An adb device with the `com.bucketclient` app installed (`cd ../client &&
  npm run android`), screen unlocked.
- `~/venv` Python with `pyserial`.

### Env overrides

| Var | Default |
| --- | --- |
| `BUCKET_SERIAL` | first `/dev/cu.usbmodem*` |
| `BUCKET_ANDROID_SERIAL` | the only adb device |
| `BUCKET_PKG` | `com.bucketclient` |
| `PYTHON` | `~/venv/bin/python` |
| `ADB` | `~/Library/Android/sdk/platform-tools/adb` |

## Pairing

`bond`/SW1 opens the pairing window but does **not** clear existing bonds, so a
returning phone reconnects seamlessly (encryption via stored keys, no prompt).
The app-side "forget" only drops the app's record, so `test_02` normally
re-connects with no dialog.

When a fresh pairing *is* required (e.g. after `bond clear` or a new phone),
Android shows a "Pair" prompt — `test_02` **auto-accepts it via adb**
(`AndroidClient.confirm_pairing`). Auto-confirming the pairing is fine for a
dedicated e2e test device.

To switch to a different phone / recover a mismatched bond: shell `bond clear`.

## Notes

- `tsc_count()` returns ~1 with no electrode wired; see `../hard/tsc-validation.md`.
