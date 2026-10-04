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
import unittest

from android import AndroidClient, UiError
from board import Board

ARTIFACTS = os.path.join(os.path.dirname(__file__), "artifacts")

# App UI labels (what the user sees).
SCAN = "Scan"
STOP_SCANNING = "Stop Scanning"
FORGET = "Forget Board"
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

    # ---- the pipeline ----

    def test_00_preconditions(self):
        try:
            self.board.ping()
        except AssertionError as e:
            self.fail(f"firmware not responding — flash it (or BUCKET_INSTALL=1): {e}")
        self.assertTrue(self.app.is_installed(), "Bucket app not installed (BUCKET_INSTALL=1)")
        self.assertTrue(self.app.is_running(), "Bucket app not running — is Metro up?")

    def test_01_forget(self):
        # Start from a known unbonded state.
        try:
            if self.app.has(text=FORGET):
                self.app.tap_until(text=FORGET, want_text=STOP_SCANNING)
            self._ensure_scanning()
        except UiError as e:
            self.fail_verbose(f"expected unbonded/scanning state after forget: {e}")

    def test_02_bond(self):
        self._ensure_scanning()
        self.board.open_bonding_window()  # == pressing SW1
        # App should detect the marker, connect, and pair. Confirm the OS
        # prompt if it shows (no prompt when the OS already holds the bond).
        self.app.confirm_pairing(timeout=20)
        try:
            self.board.wait_for(r"central connected", timeout=30)
            self.board.wait_for(r"Security changed.*level 2", timeout=10)
        except AssertionError as e:
            self.fail_verbose(f"board did not reach a secure connection: {e}")
        try:
            self.app.wait_for(text=FORGET, timeout=20)  # bonded UI
        except UiError as e:
            self.fail_verbose(f"app did not reach the bonded/connected UI: {e}")

    def test_03_gatt_read(self):
        # The Level characteristic is read + notified; the app shows "raw TSC".
        try:
            self.app.wait_for(contains="raw TSC", timeout=15)
        except UiError as e:
            self.fail_verbose(f"no live GATT Level data shown: {e}")

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
            self.app.wait_for(text=FORGET, timeout=20)
        except UiError as e:
            self.fail_verbose(f"app UI not connected after reconnect: {e}")

    def test_07_gatt_read_after_reconnect(self):
        try:
            self.app.wait_for(contains="raw TSC", timeout=15)
        except UiError as e:
            self.fail_verbose(f"no live GATT data after reconnect: {e}")

    def test_08_forget(self):
        try:
            self.app.tap_until(text=FORGET, want_text=STOP_SCANNING)
        except (UiError, AssertionError) as e:
            self.fail_verbose(f"did not return to unbonded/scanning after forget: {e}")


if __name__ == "__main__":
    unittest.main(verbosity=2, failfast=True)
