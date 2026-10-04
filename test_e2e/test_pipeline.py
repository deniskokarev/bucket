"""End-to-end pipeline for the Bucket system.

Ordered sequence (method name prefixes enforce order; run with -f/failfast):

    00 preconditions
    01 forget         -> ensure we start unbonded (scanning)
    02 bond           -> SW1/`bond` + app auto-pairs -> connected
    03 gatt read      -> app shows live Level data
    04 gatt write     -> app pump control reaches the board
    05 disconnect     -> backgrounding the app drops the link
    06 reconnect      -> foregrounding auto-connects (bonded)
    07 gatt read      -> live data again after reconnect
    08 forget         -> back to the unbonded/scanning state

Requires: the Nucleo on USB, an adb device with the Bucket app installed, and
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
            cls.app.cleanup()
        finally:
            cls.board.close()

    # ---- verbose failure helper ----

    def fail_verbose(self, msg: str):
        shot = os.path.join(ARTIFACTS, f"fail_{self._testMethodName}.png")
        try:
            self.app.screenshot(shot)
        except Exception as e:
            shot = f"(screenshot failed: {e})"
        ui = "\n  ".join(self.app.safe_texts())
        self.fail(
            f"{msg}\n"
            f"=== on-screen text ===\n  {ui}\n"
            f"=== board serial (tail) ===\n{self.board.log_tail()}\n"
            f"=== screenshot: {shot} ===\n"
        )

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
        self._ensure_scanning()
        self.board.open_bonding_window()  # SW1: open window + marker (keeps bonds)
        # With a stored bond the app reconnects and encrypts seamlessly. On a
        # first-ever pairing the OS prompt appears; auto-accept it (approved
        # for tests). No-op when it's seamless.
        self.app.confirm_pairing(timeout=30)
        try:
            self.board.wait_for(r"central connected", timeout=60)
        except AssertionError as e:
            self.fail_verbose(f"board did not report a connection: {e}")
        try:
            self.app.wait_for(contains=FORGET_LABEL, timeout=30)
        except UiError as e:
            self.fail_verbose(f"app did not reach the bonded/connected UI: {e}")

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
        self.board.clear()
        try:
            self.app.tap_text(text=PUMP_ON)
            self.board.wait_for(r"pump mode -> 2", timeout=10)
        except AssertionError as e:
            self.fail_verbose(f"GATT write (pump On) did not reach the board: {e}")
        # Restore auto.
        self.board.clear()
        self.app.tap_text(text=PUMP_AUTO)
        self.board.wait_for(r"pump mode -> 0", timeout=10)

    def test_05_disconnect(self):
        self.board.clear()
        self.app.home()  # background -> disconnect
        try:
            self.board.wait_for(r"central disconnected", timeout=15)
        except AssertionError as e:
            self.fail_verbose(f"backgrounding the app did not disconnect: {e}")

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


if __name__ == "__main__":
    unittest.main(verbosity=2, failfast=True)
