#!/bin/bash
# Every test that runs on the computer (no pedal, no cross-compilers): the platform's host tests, the tools' tests,
# the simulator, the tremolo's screens, and the web flasher's tests when Node is there. CI runs this; so can you:
#   tools/test_all.sh
set -uo pipefail
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fails=0
run() {   # name, command...
  local name=$1; shift
  if out=$("$@" 2>&1); then echo "ok   $name"; else echo "FAIL $name"; echo "$out" | tail -20; fails=$((fails + 1)); fi
}
CC="cc -std=c99 -Wall -Wno-unused-function"
run "store (power cuts, two sectors)" sh -c "$CC -I platform/nds32 platform/nds32/test/store_test.c -o $T/store && $T/store"
run "storage claim"                   sh -c "$CC -I platform/nds32 platform/nds32/test/claim_test.c -o $T/claim && $T/claim"
run "log"                             sh -c "$CC -I platform/nds32/test/include -I platform/nds32 -I platform/dsp platform/nds32/test/log_test.c -o $T/log && $T/log"
run "printf (gp5_fmt)"                sh -c "$CC -I platform/dsp platform/dsp/test/fmt_test.c -o $T/fmt && $T/fmt"
run "parameters and presets"          platform/nds32/test/params_test.sh
run "UI kit"                          platform/nds32/test/ui_test.sh
run "manifest"                        python3 tools/test_manifest.py
run "gp5tool"                         python3 tools/test_gp5tool.py
run "maths (gp5_math)"                python3 tools/test_gp5_math.py
run "simulator"                       python3 tools/test_simulate.py
run "catalog rules"                   python3 tools/test_catalog.py
run "catalog listings"                python3 tools/catalog.py check
run "catalog index up to date"        python3 tools/catalog.py index --check
run "tremolo screens"                 firmwares/tremolo/test/screen.sh
run "1.0 firmwares still build and run" tools/test_compat.sh
FL=$( [ -d docs/flasher/test ] && echo docs/flasher || echo web/flasher )
if command -v node >/dev/null; then run "web flasher" sh -c "cd $FL && node --test test/flasher.test.mjs test/catalog.test.mjs"; else echo "skip web flasher (no node)"; fi
[ $fails = 0 ] && echo "ALL OK" || echo "$fails FAILED"
exit $((fails > 0))
