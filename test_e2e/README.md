# test_e2e — end-to-end tires-kicking

Drives the whole stack — **firmware** (over the ST-LINK serial port) and the
**Android app** (over adb, parsing the on-screen UI XML) — and asserts the full
bonding/control pipeline works.

## Objects

- **`Board`** (`board.py`) — the firmware over serial. A reader thread buffers
  the Zephyr shell/log so tests can `wait_for(<regex>)` while sending shell
  commands, and keeps a full `transcript()`. Helpers: `install()` (flash the
  current build via OpenOCD), `ping()`, `morse(code)` (enter a SW1 code by
  injecting key events with real timing via `input report`),
  `open_bonding_window()` (SW1 `..-`), `bond_count()`, `wipe_settings()` (erase
  the bonds partition + cold reboot), `tsc_count()`.
- **`AndroidClient`** (`android.py`) — the app over adb. Dumps and parses the
  uiautomator XML to **find buttons by text and tap their center** (no hard-coded
  coordinates), so UI behavior is validated too. Helpers: `install()` /
  `uninstall()`, `is_installed()`, `relaunch()`, `home()`/`foreground()`
  (background/restore), `tap_text()`, `wait_for()`, `confirm_pairing()` (taps the
  Android system PAIR dialog), `unpair_os()` / `is_os_bonded()` (Settings >
  Bluetooth), `screenshot()`, `dump_logcat()`.

## Pipeline (`test_pipeline.py`, ordered, failfast)

```
00 preconditions   board shell alive, app installed + running
01 forget          ensure we start unbonded (scanning)
02 bond            SW1 "..-" (board clears bonds, LED glows) -> app auto-pairs
                   -> secure connection, LED solid
03 gatt read       app shows live Level data ("raw TSC")
04 gatt write      app pump On/Auto reaches the board (pump mode -> N)
04b no accidental  ".", "..", "-", ".-", "-..-" keep the bond and the link
04c thresholds     shell `level set 300 200` shows in the app's Settings form
    shell->app     (notify); `level set 200 250` (off <= on) is rejected
04d thresholds     form 280/230 + Save -> board `level get`, persisted in
    app->shell     settings; form 200/250 shows a validation error, no write
05 background      backgrounding stops notifications but keeps the link (held
                   30 s); then the board drops it (`bt disconnect`)
06 reconnect       foregrounding auto-connects (bonded)
07 gatt read       live data again after reconnect
08 forget          back to the unbonded/scanning state
09 re-pair         after app Forget: "..-" clears the board's keys; the phone's
                   stale keys fail ("key missing"), Android drops them, pairs again
10 OS unpair       Settings > Bluetooth > Unpair, app Forget, "..-" -> pairs again
```

The app never disconnects on its own when backgrounded: React Native pauses JS
timers in the background, so a timed disconnect can't fire. Background power
drain is deferred.

Each step is its own `test_NN_*` method; ordering comes from the numeric
prefixes and `-f` (failfast) stops the run at the first failure. On failure the
test prints the **on-screen text** and the **board serial tail**, and saves to
`artifacts/`: a **screenshot**, the full **board transcript**, and the phone's
**logcat** (the app logs its BLE flow with a `[ble]` tag).

**Standard outcome:** `tearDownClass` (even after a failure) unpairs the phone
in Settings, uninstalls the app, erases the board's bonds partition and
cold-reboots it (the board must then show `led: no-bond`). Every run therefore
starts from a first-time pairing. `BUCKET_KEEP_STATE=1` skips this for
debugging.

Don't edit `client/src` while a run is in progress: Metro hot-applies the
change, which creates a second BLE service instance inside the running app.

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
- An adb device (the debug APK is installed if missing; build it once with
  `cd ../client && npm run android`), screen lock set to None/Swipe.
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

On the board, SW1 short-short-long (`..-`, or shell `bond`) **clears all bonds**
and opens a 60 s pairing window; the tests enter it via `Board.morse()`. A
returning, still-bonded phone reconnects at any time with its stored keys,
without the window.

Every pairing in the pipeline is therefore a fresh one, and Android shows a
"Pair" prompt; the tests **auto-accept it via adb**
(`AndroidClient.confirm_pairing`). Auto-confirming the pairing is fine for a
dedicated e2e test device.

`bond clear` drops the board's bonds without opening the window.

## Notes

- `tsc_count()` returns ~1 with no electrode wired; see `../hard/tsc-validation.md`.
