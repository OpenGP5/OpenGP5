#!/bin/sh
# Host test of the UI kit (gp5_ui.c).   platform/nds32/test/ui_test.sh
set -e
D=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cc -std=c99 -Wall -Wno-unused-function -DGP5_HOST -I"$D/ui_params" -I"$D/include" -I"$D/.." -I"$D/../../dsp" "$D/ui_test.c" "$D/../text.c" -o "$T/ui"
"$T/ui"
