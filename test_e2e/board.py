"""Board: the Bucket firmware, driven over the ST-LINK virtual COM port.

A background thread continuously drains the serial port into a text buffer so
tests can `wait_for(<regex>)` against the Zephyr shell/log output while also
sending shell commands (`bond`, `tsc`, ...).
"""
import glob
import os
import re
import subprocess
import threading
import time

import serial  # pyserial (installed in ~/venv)

_HERE = os.path.dirname(os.path.abspath(__file__))
_DEFAULT_HEX = os.path.join(_HERE, "..", "firm", "build", "zephyr", "zephyr.hex")
_DEFAULT_DTS = os.path.join(_HERE, "..", "firm", "build", "zephyr", "zephyr.dts")

_ANSI = re.compile(r"\x1b\[[0-9;]*m")


def _strip(s: str) -> str:
    return _ANSI.sub("", s)


class BoardTimeout(AssertionError):
    def __init__(self, pattern: str, log_tail: str):
        super().__init__(
            f"board: timed out waiting for /{pattern}/\n"
            f"--- last serial output ---\n{log_tail}\n--------------------------"
        )


class Board:
    def __init__(self, port: str | None = None, baud: int = 115200):
        self.port = port or self._autodetect()
        self.baud = baud
        self.ser: serial.Serial | None = None
        self._buf = ""
        self._log: list[str] = []  # full transcript; clear() doesn't touch it
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._reader: threading.Thread | None = None

    @staticmethod
    def _autodetect() -> str:
        env = os.environ.get("BUCKET_SERIAL")
        if env:
            return env
        ports = sorted(glob.glob("/dev/cu.usbmodem*"))
        if not ports:
            raise RuntimeError(
                "no /dev/cu.usbmodem* found — plug in the Nucleo or set BUCKET_SERIAL"
            )
        return ports[0]

    # ---- install (flash) ----

    @staticmethod
    def is_built(hex_path: str | None = None) -> bool:
        return os.path.exists(hex_path or _DEFAULT_HEX)

    def install(self, hex_path: str | None = None,
                board_cfg: str = "board/st_nucleo_wb55.cfg") -> None:
        """Flash the firmware via OpenOCD so the board runs the current build.

        Uses the SWD endpoint of the ST-LINK, which coexists with the serial
        VCP; if the port is open the reader thread keeps running across the
        reset.
        """
        hex_path = os.path.abspath(hex_path or _DEFAULT_HEX)
        if not os.path.exists(hex_path):
            raise RuntimeError(f"firmware not built: {hex_path} (build firm/ first)")
        openocd = os.environ.get("OPENOCD", "openocd")
        r = subprocess.run(
            [openocd, "-f", board_cfg, "-c", f"program {hex_path} verify reset exit"],
            capture_output=True, text=True, timeout=120,
        )
        if r.returncode != 0:
            raise RuntimeError(f"openocd flash failed:\n{r.stdout}\n{r.stderr}")
        time.sleep(2.0)
        if self.ser is not None:
            self.ping()  # confirm the new firmware booted

    # ---- connection ----

    def open(self) -> "Board":
        self.ser = serial.Serial(self.port, self.baud, timeout=0.2)
        self._stop.clear()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        return self

    def close(self) -> None:
        self._stop.set()
        if self._reader:
            self._reader.join(timeout=1.0)
        if self.ser:
            self.ser.close()

    def _read_loop(self) -> None:
        while not self._stop.is_set():
            try:
                data = self.ser.read(4096)
            except Exception:
                break
            if data:
                text = _strip(data.decode(errors="replace"))
                with self._lock:
                    self._buf += text
                    self._log.append(text)

    # ---- buffer ----

    def clear(self) -> None:
        with self._lock:
            self._buf = ""

    def snapshot(self) -> str:
        with self._lock:
            return self._buf

    def log_tail(self, n: int = 1500) -> str:
        return self.snapshot()[-n:]

    def transcript(self) -> str:
        """Everything read from the board since open()."""
        with self._lock:
            return "".join(self._log)

    # ---- commands / assertions ----

    def send(self, cmd: str) -> None:
        assert self.ser is not None, "board not open"
        self.ser.write((cmd + "\r\n").encode())

    def wait_for(self, pattern: str, timeout: float = 10.0) -> re.Match:
        """Block until `pattern` (regex) appears in the serial output."""
        rx = re.compile(pattern)
        deadline = time.time() + timeout
        while time.time() < deadline:
            m = rx.search(self.snapshot())
            if m:
                return m
            time.sleep(0.1)
        raise BoardTimeout(pattern, self.log_tail())

    # ---- high-level helpers ----

    def ping(self) -> None:
        """Confirm the shell is alive."""
        self.clear()
        self.send("kernel version")
        self.wait_for(r"Zephyr version", timeout=5)

    def open_bonding_window(self) -> None:
        """SW1 short-short-long: clear the board's bonds, open the pairing window."""
        self.clear()
        self.morse("..-")
        self.wait_for(r"bonding mode", timeout=10)
        self.wait_for(r"led: pairing", timeout=5)

    # Press timing for morse(): well inside the firmware's 2 s long-press and
    # 1.5 s idle-reset thresholds (boards/nucleo_wb55rg.overlay, sw1-morse).
    DOT_S, DASH_S, GAP_S = 0.3, 2.6, 0.4

    def morse(self, code: str) -> None:
        """Enter a SW1 code ("." short, "-" long) by injecting key events with
        real timing through the input shell, so the board's decoder runs as
        for a physical press."""
        for i, sym in enumerate(code):
            if i:
                time.sleep(self.GAP_S)
            self.send("input report 1 11 1")  # INPUT_EV_KEY, INPUT_KEY_0, pressed
            time.sleep(self.DOT_S if sym == "." else self.DASH_S)
            self.send("input report 1 11 0")

    def tsc_count(self) -> int:
        self.clear()
        self.send("tsc")
        return int(self.wait_for(r"g1 count = (\d+)", timeout=5).group(1))

    def bond_count(self) -> int:
        self.clear()
        self.send("bt bonds")
        return int(self.wait_for(r"Total (\d+)", timeout=5).group(1))

    @staticmethod
    def _storage_partition(dts_path: str | None = None) -> tuple[str, int, int]:
        """(flash controller node, offset, size) of `storage_partition` in the build."""
        with open(dts_path or _DEFAULT_DTS) as f:
            dts = f.read()
        ctrl = re.search(r"\b(flash-controller@[0-9a-f]+)\s*\{", dts)
        part = re.search(r"storage_partition:\s*partition@[0-9a-f]+\s*\{[^}]*?"
                         r"reg\s*=\s*<\s*(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s*>", dts)
        if not ctrl or not part:
            raise RuntimeError("storage_partition not found in zephyr.dts (build firm/ first)")
        return ctrl.group(1), int(part.group(1), 16), int(part.group(2), 16)

    def wipe_settings(self) -> None:
        """Erase the settings (bonds) partition and cold-reboot the board."""
        ctrl, off, size = self._storage_partition()
        self.clear()
        self.send(f"flash erase {ctrl} {off:#x} {size:#x}")
        self.wait_for(r"Erase success", timeout=10)
        self.clear()
        self.send("kernel reboot cold")
        self.wait_for(r"led: no-bond", timeout=15)  # boot status of a wiped board
        self.wait_for(r"advertising as", timeout=15)
        self.ping()
        assert self.bond_count() == 0, "bonds survived the settings wipe"
