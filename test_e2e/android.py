"""AndroidClient: the Bucket app on a device, driven over adb.

UI interaction is done by dumping the uiautomator XML, parsing it, and tapping
the center of matched nodes by text/content-desc — so the tests exercise the
real on-screen UI rather than hard-coded coordinates.
"""
import os
import re
import subprocess
import time
import xml.etree.ElementTree as ET

_BOUNDS = re.compile(r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]")


class UiError(AssertionError):
    def __init__(self, msg: str, texts: list[str] | None = None, target: str | None = None):
        detail = f" (looking for: {target})" if target else ""
        shown = "\n  ".join(texts) if texts else "(none)"
        super().__init__(f"{msg}{detail}\n--- on-screen text ---\n  {shown}\n----------------------")


class AndroidClient:
    def __init__(self, package: str = "com.bucketclient", adb: str | None = None,
                 serial: str | None = None):
        self.adb = adb or self._find_adb()
        self.package = package
        self.serial = serial or os.environ.get("BUCKET_ANDROID_SERIAL")

    @staticmethod
    def _find_adb() -> str:
        for cand in (
            os.environ.get("ADB"),
            os.path.expanduser("~/Library/Android/sdk/platform-tools/adb"),
            "adb",
        ):
            if not cand:
                continue
            try:
                subprocess.run([cand, "version"], capture_output=True, timeout=10, check=True)
                return cand
            except Exception:
                continue
        raise RuntimeError("adb not found — set ADB or install platform-tools")

    # ---- raw adb ----

    def _run(self, *args: str, timeout: float = 30, binary: bool = False):
        cmd = [self.adb] + (["-s", self.serial] if self.serial else []) + list(args)
        return subprocess.run(cmd, capture_output=True, timeout=timeout,
                              text=not binary)

    def shell(self, *args: str, timeout: float = 30) -> str:
        return self._run("shell", *args, timeout=timeout).stdout

    # ---- environment / lifecycle ----

    def prepare(self) -> None:
        """Wake the screen, keep it on, and bridge Metro over USB."""
        self.shell("input", "keyevent", "KEYCODE_WAKEUP")
        self.shell("svc", "power", "stayon", "true")
        self._run("reverse", "tcp:8081", "tcp:8081")

    def cleanup(self) -> None:
        self.shell("svc", "power", "stayon", "false")

    def is_installed(self) -> bool:
        return self.package in self.shell("pm", "list", "packages", self.package)

    def install(self, apk: str | None = None) -> None:
        """Install (reinstall) the debug APK so the device runs the current build."""
        if apk is None:
            here = os.path.dirname(os.path.abspath(__file__))
            apk = os.path.join(here, "..", "client", "android", "app", "build",
                               "outputs", "apk", "debug", "app-debug.apk")
        apk = os.path.abspath(apk)
        if not os.path.exists(apk):
            raise RuntimeError(f"apk not built: {apk} (run `npm run android` in client/)")
        r = self._run("install", "-r", apk, timeout=180)
        if r.returncode != 0 or "Success" not in (r.stdout or ""):
            raise RuntimeError(f"adb install failed:\n{r.stdout}\n{r.stderr}")

    def is_running(self) -> bool:
        return bool(self.shell("pidof", self.package).strip())

    def force_stop(self) -> None:
        self.shell("am", "force-stop", self.package)

    def launch(self) -> None:
        self.shell("monkey", "-p", self.package, "-c",
                   "android.intent.category.LAUNCHER", "1")

    def relaunch(self, wait_js: bool = True) -> None:
        self.force_stop()
        time.sleep(1.0)
        self.launch()
        if wait_js:
            self.wait_js_running()

    def home(self) -> None:
        """Background the app (AppState -> background -> disconnect)."""
        self.shell("input", "keyevent", "KEYCODE_HOME")

    def foreground(self) -> None:
        """Bring the app back (AppState -> active -> (re)connect)."""
        self.launch()

    def wait_js_running(self, timeout: float = 45) -> None:
        self._run("logcat", "-c")
        self.launch()
        deadline = time.time() + timeout
        while time.time() < deadline:
            if 'Running "BucketClient"' in self._run("logcat", "-d").stdout:
                time.sleep(2.0)  # let the first render settle
                return
            time.sleep(1.0)
        raise AssertionError("app JS bundle did not start — is Metro running?")

    # ---- UI (uiautomator XML) ----

    def _ui_xml(self) -> str:
        for _ in range(4):
            self._run("shell", "uiautomator", "dump", "/sdcard/ui.xml", timeout=20)
            out = self._run("exec-out", "cat", "/sdcard/ui.xml", timeout=20).stdout
            if out and "<hierarchy" in out:
                return out
            time.sleep(0.5)
        raise AssertionError("uiautomator dump failed")

    def _nodes(self) -> list[dict]:
        root = ET.fromstring(self._ui_xml())
        return [n.attrib for n in root.iter("node")]

    def texts(self) -> list[str]:
        out = []
        for n in self._nodes():
            t = (n.get("text") or "").strip()
            cd = (n.get("content-desc") or "").strip()
            if t:
                out.append(t)
            elif cd:
                out.append(f"[{cd}]")
        return out

    def safe_texts(self) -> list[str]:
        try:
            return self.texts()
        except Exception as e:  # pragma: no cover - diagnostics only
            return [f"<ui dump failed: {e}>"]

    def _match(self, node: dict, text=None, contains=None, regex=None) -> bool:
        label = node.get("text") or ""
        cd = node.get("content-desc") or ""
        if text is not None:
            return label == text or cd == text
        if contains is not None:
            return contains in label or contains in cd
        if regex is not None:
            return bool(re.search(regex, label) or re.search(regex, cd))
        return False

    def find(self, text=None, contains=None, regex=None) -> dict | None:
        for n in self._nodes():
            if self._match(n, text, contains, regex):
                return n
        return None

    def has(self, **kw) -> bool:
        return self.find(**kw) is not None

    def wait_for(self, text=None, contains=None, regex=None, timeout: float = 20) -> dict:
        deadline = time.time() + timeout
        last = []
        while time.time() < deadline:
            try:
                for n in self._nodes():
                    if self._match(n, text, contains, regex):
                        return n
                last = self.texts()
            except Exception:
                pass
            time.sleep(1.0)
        raise UiError("timed out waiting for UI element", last,
                      text or contains or regex)

    @staticmethod
    def _center(node: dict) -> tuple[int, int]:
        m = _BOUNDS.match(node["bounds"])
        x1, y1, x2, y2 = map(int, m.groups())
        return (x1 + x2) // 2, (y1 + y2) // 2

    def tap(self, x: int, y: int) -> None:
        self.shell("input", "tap", str(x), str(y))

    def tap_node(self, node: dict) -> None:
        x, y = self._center(node)
        self.tap(x, y)

    def tap_text(self, text=None, contains=None, regex=None) -> dict:
        n = self.find(text=text, contains=contains, regex=regex)
        if n is None:
            raise UiError("button not found to tap", self.safe_texts(),
                          text or contains or regex)
        self.tap_node(n)
        return n

    def tap_until(self, *, text=None, contains=None, regex=None,
                  want_text=None, want_contains=None, gone_text=None,
                  attempts: int = 4, settle: float = 4) -> None:
        """Tap a button and verify the UI changed, retrying.

        Robust against dropped taps (e.g. Android often eats the first tap
        after an app is brought to the foreground, to focus the window).
        """
        tap_kw = dict(text=text, contains=contains, regex=regex)
        for _ in range(attempts):
            try:
                self.tap_text(**tap_kw)
            except UiError:
                pass  # element gone — maybe we already transitioned
            deadline = time.time() + settle
            while time.time() < deadline:
                if want_text is not None and self.has(text=want_text):
                    return
                if want_contains is not None and self.has(contains=want_contains):
                    return
                if gone_text is not None and not self.has(text=gone_text):
                    return
                time.sleep(1)
        raise UiError("tap had no effect after retries", self.safe_texts(),
                      f"tap={tap_kw} want_text={want_text} "
                      f"want_contains={want_contains} gone_text={gone_text}")

    def confirm_pairing(self, timeout: float = 20) -> bool:
        """Tap the Android system 'PAIR' dialog if/when it appears.

        Returns True if it was tapped, False if no dialog showed (e.g. the OS
        already holds the bond, so no new pairing is needed).
        """
        deadline = time.time() + timeout
        while time.time() < deadline:
            n = self.find(regex=r"(?i)^pair$")
            if n is not None:
                self.tap_node(n)
                return True
            time.sleep(1.0)
        return False

    def screenshot(self, path: str) -> str:
        data = self._run("exec-out", "screencap", "-p", binary=True).stdout
        with open(path, "wb") as f:
            f.write(data)
        return path
