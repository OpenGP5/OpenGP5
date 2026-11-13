#!/bin/sh
# Build and run the tuner test on the computer:  firmwares/tuner/test/run.sh [wav FILE.wav]
set -e
D=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$D/../../.." && pwd)
mkdir -p "$ROOT/build"
cc -std=c99 -O2 -Wall -DGP5_HOST -I"$ROOT/platform/dsp" -include "$ROOT/platform/dsp/gp5_math.h" "$D/tuner_test.c" "$ROOT/platform/dsp/gp5_math.c" -lm -o "$ROOT/build/tuner_test"
"$ROOT/build/tuner_test" "${@:-tones}"
