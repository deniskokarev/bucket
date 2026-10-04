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
                with self._lock:
                    self._buf += _strip(data.decode(errors="replace"))

    # ---- buffer ----

    def clear(self) -> None:
        with self._lock:
            self._buf = ""

    def snapshot(self) -> str:
        with self._lock:
            return self._buf

    def log_tail(self, n: int = 1500) -> str:
        return self.snapshot()[-n:]

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
        """Equivalent to pressing SW1 on the board."""
        self.clear()
        self.send("bond")
        self.wait_for(r"bonding mode ON", timeout=5)

    def tsc_count(self) -> int:
        self.clear()
        self.send("tsc")
        return int(self.wait_for(r"g1 count = (\d+)", timeout=5).group(1))
