#!/bin/sh
# Host test of the control processor's parameters (gp5_params.c): version A of a firmware, then version B with its
# parameters reordered, one added and one removed, reading what A saved. Then B again on params-1.0.store.hex (as hex text): what version
# A saved with the platform of OpenGP5 1.0, kept as it was, so a later platform still reads 1.0's records.
#   platform/nds32/test/params_test.sh
set -e
D=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cc -std=c99 -Wall -Wno-unused-function -I"$D/params_a" -I"$D/.." -I"$D/../../dsp" "$D/params_test.c" -o "$T/a"
cc -std=c99 -Wall -Wno-unused-function -DVERSION_B -I"$D/params_b" -I"$D/.." -I"$D/../../dsp" "$D/params_test.c" -o "$T/b"
python3 -c "import sys; open(sys.argv[2], 'wb').write(bytes.fromhex(open(sys.argv[1]).read()))" "$D/params-1.0.store.hex" "$T/frozen"
"$T/a" A "$T/store" && "$T/b" B "$T/store" && "$T/b" B "$T/frozen" && echo "ALL OK"
