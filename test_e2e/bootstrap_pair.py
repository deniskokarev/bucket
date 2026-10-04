"""One-time bootstrap: pair the tablet with the board.

This is the ONLY place the harness taps the Android "Pair" prompt. Run it once
(or after a wiped/cleared bond). Afterwards the bond persists on both the phone
(OS) and the board (flash), so the ordered pipeline in test_pipeline.py
reconnects seamlessly and NEVER taps the pairing dialog.

    ./run.sh is for the pipeline;  python bootstrap_pair.py  is this one-time step.
"""
import time

from android import AndroidClient
from board import Board


def main() -> None:
    board = Board().open()
    app = AndroidClient()
    app.grant_permissions()
    app.prepare()
    app.relaunch()

    # Start unbonded + scanning.
    if app.has(contains="Forget Board"):
        app.tap_until(contains="Forget Board", want_regex=r"(?i)^cancel$")
        app.tap_text(regex=r"(?i)^forget$")
        time.sleep(2)
    if app.has(text="Scan"):
        app.tap_until(text="Scan", want_text="Stop Scanning")

    board.clear()
    board.open_bonding_window()  # == pressing SW1
    print("bonding window open; waiting for the OS 'Pair' prompt…")

    # Tap the system pairing dialog's Pair button (button1 next to Cancel).
    deadline = time.time() + 40
    while time.time() < deadline:
        pair = app.find(regex=r"(?i)^pair$")
        if pair is not None and app.find(regex=r"(?i)^cancel$") is not None:
            app.tap_node(pair)
            print("tapped Pair")
            break
        time.sleep(1.5)
    else:
        print("no pairing prompt appeared (already bonded?)")

    board.wait_for(r"Security changed.*level 2", timeout=30)
    app.wait_for(contains="Forget Board", timeout=25)
    board.close()
    print("BONDED OK — the pipeline can now run seamlessly.")


if __name__ == "__main__":
    main()
