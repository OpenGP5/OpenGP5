#!/bin/bash
# The 1.0 promise, checked: every firmware in compat/1.0 (written for OpenGP5 1.0 and kept exactly as it was) still
# builds against this platform and runs in the simulator for a second, its controls used, without a crash or a memory
# error. (CI also builds them for the pedal; platform/nds32/test/params_test.sh reads settings saved by 1.0.)
#   tools/test_compat.sh
set -euo pipefail
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
printf '0.2 para turn 1\n0.3 para click\n0.4 para turn 2\n0.5 para click\n0.6 foot\n0.8 foot\n0.9 para hold\n' > "$T/script"
for f in compat/*/*/; do
  python3 tools/simulate.py "$f" --check --seconds 1 --script "$T/script" --out "$T/out.wav" --shots "$T/shots" > "$T/log" 2>&1 \
    || { echo "FAIL $f"; tail -20 "$T/log"; exit 1; }
  echo "ok   $f"
done
