"""End-to-end pipeline for the Bucket system.

Ordered sequence (method name prefixes enforce order; run with -f/failfast):

    00 preconditions
    01 forget         -> ensure we start unbonded (scanning)
    02 bond           -> SW1 "..-" (clears board bonds, LED glows) + app
                         auto-pairs -> connected, LED solid
    03 gatt read      -> app shows live Level data
    04 gatt write     -> app pump control reaches the board
    04b no accidental -> other SW1 presses (".", "..", "-", ".-", "-..-")
        unbond           keep the bond and the link
    04c thresholds    -> shell `level set <off> <on>` shows in the app (notify);
        shell->app       off <= on is rejected
    04d thresholds    -> app Settings form write reaches the board + persists;
        app->shell       off <= on is blocked by the form
    05 background     -> backgrounding silences notifications but keeps the
                         link; then the board drops it (`bt disconnect`)
    06 reconnect      -> foregrounding auto-connects (bonded)
    07 gatt read      -> live data again after reconnect
    08 forget         -> back to the unbonded/scanning state
    09 re-pair        -> after the app's Forget (the phone keeps its OS bond):
                         "..-" clears the board's keys -> pairs again ->
                         encrypted write works
    10 OS unpair      -> Settings > Bluetooth > Unpair, app Forget, then
                         "..-" -> fresh pairing prompt -> encrypted write works

SW1 presses are injected over the board shell (`input report`) with real
timing, so the firmware's morse decoder runs as for a physical press.

Standard outcome (tearDownClass, even on failure): the phone is unpaired in
Settings and the app uninstalled; the board's settings partition (bonds) is
erased and the board cold-rebooted. The next run therefore always starts from a
first-time pairing. Set BUCKET_KEEP_STATE=1 to skip this when debugging.

Requires: the Nucleo on USB, an adb device (the app is installed if missing), and
(for the debug build) Metro running + `adb reverse 8081`. `run.sh` sets that up.
"""
import os
import time
import unittest

from android import AndroidClient, UiError
from board import Board

ARTIFACTS = os.path.join(os.path.dirname(__file__), "artifacts")

# App UI labels (what the user sees).
SCAN = "Scan"
STOP_SCANNING = "Stop Scanning"
FORGET_LABEL = "Forget Board"        # substring of the varying button label
CONFIRM_FORGET = r"(?i)^forget$"     # confirmation dialog's destructive button
CANCEL = r"(?i)^cancel$"             # confirmation dialog's cancel button
PUMP_ON = "On"
PUMP_AUTO = "Auto"
DEVICE_NAME = "BucketEvac"          # advertised name / OS paired-devices entry
THR_OFF = "threshold-off"           # Settings form fields (accessibilityLabel)
THR_ON = "threshold-on"
SETTINGS_TOGGLE = "settings-toggle"
SAVE_THRESHOLDS = "Save thresholds"

# How long a backgrounded app must hold the link without dropping it.
BACKGROUND_HOLD_S = 30


class PipelineTest(unittest.TestCase):
    board: Board
    app: AndroidClient

    @classmethod
    def setUpClass(cls):
        os.makedirs(ARTIFACTS, exist_ok=True)
        cls.board = Board().open()
        cls.app = AndroidClient()
        # Set BUCKET_INSTALL=1 to (re)flash the firmware and reinstall the app
        # so the devices run the current build before testing.
        if os.environ.get("BUCKET_INSTALL"):
            cls.board.install()
            cls.app.install()
        elif not cls.app.is_installed():
            cls.app.install()  # app must be present to test the UI
        cls.app.grant_permissions()  # don't block on the location/BLE prompt
        cls.app.prepare()
        cls.app.relaunch()

    @classmethod
    def tearDownClass(cls):
        try:
            if not os.environ.get("BUCKET_KEEP_STATE"):
                cls._reset_both_sides()
        finally:
            try:
                cls.app.cleanup()
            finally:
                cls.board.close()

    @classmethod
    def _reset_both_sides(cls):
        """Standard outcome: no bond anywhere, no app on the phone."""
        try:
            cls.app.unpair_os(DEVICE_NAME)
        finally:
            try:
                cls.app.uninstall()
            finally:
                cls.board.wipe_settings()

    # ---- verbose failure helper ----

    def fail_verbose(self, msg: str):
        base = os.path.join(ARTIFACTS, f"fail_{self._testMethodName}")
        saved = []
        for what, save in (("screenshot", lambda: self.app.screenshot(base + ".png")),
                           ("logcat", lambda: self.app.dump_logcat(base + "_logcat.log")),
                           ("board", lambda: self._write(base + "_board.log",
                                                         self.board.transcript()))):
            try:
                saved.append(save())
            except Exception as e:
                saved.append(f"({what} failed: {e})")
        ui = "\n  ".join(self.app.safe_texts())
        self.fail(
            f"{msg}\n"
            f"=== on-screen text ===\n  {ui}\n"
            f"=== board serial (tail) ===\n{self.board.log_tail()}\n"
            f"=== artifacts ===\n  " + "\n  ".join(saved) + "\n"
        )

    @staticmethod
    def _write(path: str, text: str) -> str:
        with open(path, "w") as f:
            f.write(text)
        return path

    def _unbonded(self) -> bool:
        return self.app.has(text=SCAN) or self.app.has(text=STOP_SCANNING)

    def _ensure_scanning(self):
        if self.app.has(text=SCAN):  # idle -> start a scan
            self.app.tap_until(text=SCAN, want_text=STOP_SCANNING)
        else:
            self.app.wait_for(text=STOP_SCANNING, timeout=15)

    def _forget(self):
        """Tap the Forget button, confirm the dialog, -> scanning.

        Retry-driven: the app re-renders on every Level notification, which can
        drop a tap, so re-open/confirm until it actually unbonds.
        """
        saw_dialog = False
        for _ in range(5):
            if not self.app.has(regex=CANCEL):  # dialog not up -> open it
                try:
                    self.app.tap_text(contains=FORGET_LABEL)
                except UiError:
                    pass
                time.sleep(1.5)
            if self.app.has(regex=CANCEL):  # confirmation dialog is up
                saw_dialog = True
                try:
                    self.app.tap_text(regex=CONFIRM_FORGET)
                except UiError:
                    pass
                time.sleep(2)
            if self.app.has(text=STOP_SCANNING):
                # A confirmation step must have appeared before unbonding.
                self.assertTrue(saw_dialog, "unbonded without a confirmation dialog")
                return
        raise UiError("forget did not return to scanning", self.app.safe_texts())

    def _bond_via_sw1(self):
        """Scan, enter SW1 "..-" (board clears bonds), accept the OS prompt."""
        self._ensure_scanning()
        self.board.open_bonding_window()
        self.app.confirm_pairing(timeout=30)
        try:
            self.board.wait_for(r"central connected", timeout=60)
            self.board.wait_for(r"pairing complete", timeout=60)
            self.board.wait_for(r"led: bonded", timeout=5)
        except AssertionError as e:
            self.fail_verbose(f"board did not pair: {e}")
        try:
            self.app.wait_for(contains="Disconnect & Forget Board", timeout=45)
        except UiError as e:
            self.fail_verbose(f"app did not reach the bonded/connected UI: {e}")
        try:
            self.app.wait_for(contains="Disconnect & Forget Board", timeout=45)
        except UiError as e:
            self.fail_verbose(f"app did not reach the bonded/connected UI: {e}")

    def _assert_secure_write(self):
        """Pump is write-encrypted: reaching the board proves an encrypted link."""
        self.board.clear()
        try:
            self.app.tap_text(text=PUMP_ON)
            self.board.wait_for(r"pump mode -> 2", timeout=10)
            self.board.clear()
            self.app.tap_text(text=PUMP_AUTO)
            self.board.wait_for(r"pump mode -> 0", timeout=10)
        except AssertionError as e:
            self.fail_verbose(f"encrypted GATT write (pump) did not reach the board: {e}")

    # ---- the pipeline ----

    def test_00_preconditions(self):
        try:
            self.board.ping()
        except AssertionError as e:
            self.fail(f"firmware not responding — flash it (or BUCKET_INSTALL=1): {e}")
        self.assertTrue(self.app.is_installed(), "Bucket app not installed (BUCKET_INSTALL=1)")
        self.assertTrue(self.app.is_running(), "Bucket app not running — is Metro up?")

    def test_01_forget(self):
        # Start from a known unbonded state. (test_02's `bond`/SW1 also clears
        # any stale board bond, so pairing always starts from a clean slate.)
        try:
            if self.app.has(contains=FORGET_LABEL):
                self._forget()
            self._ensure_scanning()
        except (UiError, AssertionError) as e:
            self.fail_verbose(f"expected unbonded/scanning state after forget: {e}")

    def test_02_bond(self):
        # With a stored bond the app reconnects and encrypts seamlessly. On a
        # first-ever pairing the OS prompt appears; auto-accept it (approved
        # for tests). No-op when it's seamless.
        self._bond_via_sw1()

    def test_03_gatt_read(self):
        # The Level characteristic is read + notified; the app shows "raw TSC".
        try:
            self.app.wait_for(contains="raw TSC", timeout=15)
        except UiError as e:
            self.fail_verbose(f"no live GATT Level data shown: {e}")
        # While connected the forget button reads "Disconnect & Forget Board".
        if not self.app.has(contains="Disconnect & Forget Board"):
            self.fail_verbose("connected forget label should be 'Disconnect & Forget Board'")

    def test_04_gatt_write(self):
        self._assert_secure_write()

    def test_04b_no_accidental_unbond(self):
        # Only short-short-long re-bonds. A tap shows the status; anything else
        # is unmatched. None of it may touch the bond or the link.
        self.assertEqual(self.board.bond_count(), 1, "expected exactly one bond")
        for code, outcome in ((".", r"led: bonded"),
                              ("..", r'unmatched "\.\."'),
                              ("-", r'unmatched "-"'),
                              (".-", r'unmatched "\.-"'),
                              ("-..-", r'unmatched "-\.\.-"')):
            self.board.clear()
            self.board.morse(code)
            try:
                self.board.wait_for(outcome, timeout=6)
            except AssertionError as e:
                self.fail_verbose(f"SW1 {code!r}: expected /{outcome}/: {e}")
            log = self.board.snapshot()
            for bad in ("cleared all bonds", "bonding mode", "central disconnected"):
                if bad in log:
                    self.fail_verbose(f"SW1 {code!r} must not cause {bad!r}")
        self.assertEqual(self.board.bond_count(), 1, "bond lost to a non-rebond code")
        self._assert_secure_write()

    def _settings(self, open_: bool):
        """Expand/collapse the (collapsed by default) Settings card."""
        if self.app.has(text=THR_OFF) == open_:
            return
        if open_:
            self.app.tap_until(text=SETTINGS_TOGGLE, want_text=THR_OFF)
        else:
            self.app.hide_keyboard()
            self.app.tap_until(text=SETTINGS_TOGGLE, gone_text=THR_OFF)

    def _wait_app_thresholds(self, off: int, on: int, timeout: float = 15):
        deadline = time.time() + timeout
        seen = None
        while time.time() < deadline:
            try:
                seen = (self.app.field_text(THR_OFF), self.app.field_text(THR_ON))
            except UiError:
                pass
            if seen == (str(off), str(on)):
                return
            time.sleep(1)
        self.fail_verbose(f"app thresholds {seen} != expected ({off}, {on})")

    def test_04c_thresholds_shell_to_app(self):
        # Shell `level set <off> <on>` is notified to the connected app.
        self._settings(True)
        self.assertTrue(self.board.set_thresholds(300, 200), "board rejected 300/200")
        self._wait_app_thresholds(300, 200)
        # Invalid (off <= on) is rejected by the shell; nothing changes.
        self.assertFalse(self.board.set_thresholds(200, 250), "board accepted off < on")
        self.assertEqual(self.board.thresholds(), (300, 200))
        self._wait_app_thresholds(300, 200, timeout=3)

    def test_04d_thresholds_app_to_shell(self):
        # The app's Settings form writes the encrypted characteristic.
        self._settings(True)
        self.board.clear()
        self.app.set_field(THR_OFF, "280")
        self.app.set_field(THR_ON, "230")
        self.app.hide_keyboard()
        self.app.tap_text(text=SAVE_THRESHOLDS)
        try:
            self.board.wait_for(r"level thresholds -> off 280 on 230", timeout=10)
        except AssertionError as e:
            self.fail_verbose(f"app thresholds write did not reach the board: {e}")
        self.assertEqual(self.board.thresholds(), (280, 230))
        self.board.clear()
        self.board.send("settings read level/thr")
        self.board.wait_for(r"18 01 e6 00", timeout=5)  # persisted (u16 LE)

        # Minimal UI validation: OFF must exceed ON; Save does nothing.
        self.app.set_field(THR_OFF, "200")
        self.app.set_field(THR_ON, "250")
        self.app.hide_keyboard()
        try:
            self.app.wait_for(contains="OFF count must be greater than ON count", timeout=5)
        except UiError as e:
            self.fail_verbose(f"no validation message for off < on: {e}")
        self.board.clear()
        self.app.tap_text(text=SAVE_THRESHOLDS)
        time.sleep(3)
        if "level thresholds ->" in self.board.snapshot():
            self.fail_verbose("invalid thresholds were written to the board")
        self.assertEqual(self.board.thresholds(), (280, 230))
        self._settings(False)  # later steps expect the main screen layout

    def test_05_background(self):
        # Backgrounding silences notifications but keeps the link (the Android
        # pairing prompt backgrounds the app too). No JS timers are involved:
        # RN pauses them in the background.
        self.board.clear()
        self.app.home()
        try:
            self.board.wait_for(r"level notifications disabled", timeout=15)
        except AssertionError as e:
            self.fail_verbose(f"backgrounding did not stop Level notifications: {e}")
        time.sleep(BACKGROUND_HOLD_S)
        if "central disconnected" in self.board.snapshot():
            self.fail_verbose("link dropped while the app was in the background")
        self.app.foreground()
        try:
            self.board.wait_for(r"level notifications enabled", timeout=15)
        except AssertionError as e:
            self.fail_verbose(f"foregrounding did not resume notifications: {e}")
        if "central disconnected" in self.board.snapshot():
            self.fail_verbose("link dropped across a trip to the background")

        # Board-side link loss while backgrounded (e.g. out of range): the app
        # goes idle and reconnects when foregrounded (test_06).
        self.app.home()
        self.board.wait_for(r"level notifications disabled", timeout=15)
        self.board.clear()
        self.board.send("bt disconnect")
        try:
            self.board.wait_for(r"central disconnected", timeout=15)
        except AssertionError as e:
            self.fail_verbose(f"board-side disconnect did not drop the link: {e}")

    def test_06_reconnect(self):
        self.board.clear()
        self.app.foreground()  # -> bonded auto-connect
        try:
            self.board.wait_for(r"central connected", timeout=30)
        except AssertionError as e:
            self.fail_verbose(f"app did not auto-reconnect: {e}")
        try:
            self.app.wait_for(contains=FORGET_LABEL, timeout=20)
        except UiError as e:
            self.fail_verbose(f"app UI not connected after reconnect: {e}")

    def test_07_gatt_read_after_reconnect(self):
        try:
            self.app.wait_for(contains="raw TSC", timeout=15)
        except UiError as e:
            self.fail_verbose(f"no live GATT data after reconnect: {e}")

    def test_08_forget(self):
        try:
            self._forget()
        except (UiError, AssertionError) as e:
            self.fail_verbose(f"did not return to unbonded/scanning after forget: {e}")

    def test_09_repair_after_app_forget(self):
        # The app's Forget drops only its own record; the phone keeps its OS
        # bond. "..-" clears the board's keys, so the phone's stored keys go
        # stale and it has to pair again.
        if not self.app.has(text=STOP_SCANNING) and not self.app.has(text=SCAN):
            self._forget()
        self.assertTrue(self.app.is_os_bonded(DEVICE_NAME), "OS bond should survive app Forget")
        self._bond_via_sw1()
        self._assert_secure_write()

    def test_10_repair_after_os_unpair(self):
        # Unpair from the OS side (user removes it in Settings): the phone loses
        # its keys. The app's record is stale too, so Forget it, then "..-"
        # (which clears the board's keys) and pair from scratch.
        self.app.unpair_os(DEVICE_NAME)
        self.assertFalse(self.app.is_os_bonded(DEVICE_NAME), "OS unpair did not take")
        self.app.foreground()
        try:
            self.app.wait_for(contains=FORGET_LABEL, timeout=15)
            self._forget()
        except (UiError, AssertionError) as e:
            self.fail_verbose(f"could not Forget the stale board in the app: {e}")
        self._bond_via_sw1()
        self.assertTrue(self.app.is_os_bonded(DEVICE_NAME), "OS bond not re-created")
        self._assert_secure_write()


if __name__ == "__main__":
    unittest.main(verbosity=2, failfast=True)
