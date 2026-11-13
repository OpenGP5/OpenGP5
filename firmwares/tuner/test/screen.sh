#!/bin/sh
# The tuner's screen as PNGs (4x enlarged) in build/tuner_screen:  firmwares/tuner/test/screen.sh
set -e
D=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$D/../../.." && pwd); OUT=$ROOT/build/tuner_screen
mkdir -p "$OUT"
cc -std=c99 -O1 -Wall -Wno-unused-function -DGP5_HOST -I"$ROOT/platform/nds32" -I"$ROOT/platform/dsp" -include "$ROOT/platform/dsp/gp5_math.h" "$D/screen_preview.c" "$ROOT/platform/dsp/gp5_math.c" -lm -o "$OUT/screen"
"$OUT/screen" "$OUT"
python3 - "$OUT" <<'PY'
import sys, glob, os
from PIL import Image
for p in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    im = Image.open(p); im.resize((640, 320), Image.NEAREST).save(p[:-4] + ".png"); os.remove(p)
PY
echo "screens in $OUT"
