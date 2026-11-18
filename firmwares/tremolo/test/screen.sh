#!/bin/sh
# The tremolo's screen as PNGs in build/tremolo_screen (start, edited, saved, changed, off; then all.png side by side):
#   firmwares/tremolo/test/screen.sh        (needs a C compiler and Python with Pillow)
set -e
D=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$D/../../.." && pwd); OUT=$ROOT/build/tremolo_screen
mkdir -p "$OUT"
cc -std=c99 -O1 -Wall -Wno-unused-function -DGP5_HOST -DGP5_API1 -DGP5_HAS_PARAMS -DGP5_CTL_SIDE -DGP5_PARAMS_HOST_TEST \
   -DGP5_PRESETS=4u -DUI_PANEL -I"$D/.." -I"$ROOT/platform/nds32" -I"$ROOT/platform/dsp" -include "$ROOT/platform/dsp/gp5_math.h" \
   "$D/screen_preview.c" "$ROOT/platform/nds32/text.c" "$ROOT/platform/dsp/gp5_math.c" -lm -o "$OUT/screen"
"$OUT/screen" "$OUT"
python3 - "$OUT" <<'PY'
import sys, glob, os
from PIL import Image
ims = []
for p in sorted(glob.glob(os.path.join(sys.argv[1], "*.ppm"))):
    im = Image.open(p).convert("RGB"); ims.append(im); im.resize((640, 320), Image.NEAREST).save(p[:-4] + ".png"); os.remove(p)
all_ = Image.new("RGB", (168 * len(ims) - 8, 80), (255, 255, 255))
for i, im in enumerate(ims): all_.paste(im, (168 * i, 0))
all_.resize((all_.width * 3, 240), Image.NEAREST).save(os.path.join(sys.argv[1], "all.png"))
PY
echo "screens in $OUT"
