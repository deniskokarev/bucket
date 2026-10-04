#!/usr/bin/env bash
# Kick the tires of the whole system: Board (serial) <-> firmware <-> BLE <->
# Android app. Ensures Metro + adb reverse are up, then runs the ordered
# end-to-end pipeline (failfast).
#
# Usage:  ./run.sh            # run the whole pipeline
#         ./run.sh test_02_bond   # run a single step (and later ones won't run)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY="${PYTHON:-$HOME/venv/bin/python}"
ADB="${ADB:-$HOME/Library/Android/sdk/platform-tools/adb}"
CLIENT="$HERE/../client"

# 1. Metro (the debug app loads its JS bundle from it).
if ! curl -s http://localhost:8081/status 2>/dev/null | grep -q packager-status:running; then
  echo "starting Metro…"
  ( cd "$CLIENT" && npx react-native start >/tmp/bucket_metro.log 2>&1 & )
  for _ in $(seq 1 30); do
    curl -s http://localhost:8081/status 2>/dev/null | grep -q packager-status:running && break
    sleep 2
  done
fi

# 2. USB bridge for Metro.
"$ADB" reverse tcp:8081 tcp:8081 || true

# 3. Run the tests.
cd "$HERE"
if [ "$#" -gt 0 ]; then
  exec "$PY" -m unittest -v "test_pipeline.PipelineTest.$1"
else
  exec "$PY" -m unittest -v -f test_pipeline
fi
