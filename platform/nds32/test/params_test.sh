#!/bin/sh
# Host test of the control processor's parameters (gp5_params.c): version A of a firmware, then version B with its
# parameters reordered, one added and one removed, reading what A saved.   platform/nds32/test/params_test.sh
set -e
D=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cc -std=c99 -Wall -Wno-unused-function -I"$D/params_a" -I"$D/.." -I"$D/../../dsp" "$D/params_test.c" -o "$T/a"
cc -std=c99 -Wall -Wno-unused-function -DVERSION_B -I"$D/params_b" -I"$D/.." -I"$D/../../dsp" "$D/params_test.c" -o "$T/b"
"$T/a" A "$T/store" && "$T/b" B "$T/store" && echo "ALL OK"
