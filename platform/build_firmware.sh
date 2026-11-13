#!/bin/bash
# Build one firmware for the GP-5 open platform into ONE flashable image.
#   platform/build_firmware.sh <name> [out.bin]        name = a folder under firmwares/
#   platform/build_firmware.sh <path> [out.bin]        or any folder with dsp/firmware.h and nds32/*.c, e.g. a
#                                                      firmware in its own repository (default out: ./build/)
# Needs: the two compilers (release/setup-toolchain.sh puts them in fw-build/), python3, and on macOS Docker
# (the control processor's compiler is a 32-bit Linux program; Linux and WSL run it directly).
# No Valeton file is needed: the image holds only our two sections, the DSP firmware (dsp) and the
# control-processor app (app). Valeton's other sections on the pedal (DSP data, models, presets) are not
# touched by a flash of this image.
# Flash the result with:  python3 tools/gp5tool.py flash <out.bin> --sections all
set -euo pipefail
ARG=${1:?usage: platform/build_firmware.sh <firmware name | folder> [out.bin]}
if [ -f "$ARG/dsp/firmware.h" ] && [[ "$ARG" == */* || "$ARG" == . || "$ARG" == .. ]]; then      # a folder, anywhere
  FWDIR=$(cd "$ARG" && pwd); NAME=$(basename "$FWDIR")
  OUT=${2:-build/GP-5_$NAME.bin}; mkdir -p "$(dirname "$OUT")"; OUT=$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")
  cd "$(dirname "$0")/.."
else                                                                              # a name under firmwares/
  cd "$(dirname "$0")/.."
  NAME=$ARG; FWDIR=$PWD/firmwares/$NAME; OUT=${2:-build/GP-5_$NAME.bin}
fi
[ -f "$FWDIR/dsp/firmware.h" ] || { echo "no $FWDIR/dsp/firmware.h"; exit 1; }
ls "$FWDIR/nds32/"*.c >/dev/null 2>&1 || { echo "no C files in $FWDIR/nds32/"; exit 1; }
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
mkdir -p "$(dirname "$OUT")"

echo "== DSP firmware"
python3 platform/dsp/build.py "$TMP/dsp.bin" --src responder.c --firmware "$FWDIR" \
    -D AUDIO_PASSTHROUGH -D AUDIO_LEVEL -D RESIDENT_TICK_PATCH
echo "== control processor firmware"
# The Andes compiler is 32-bit Linux: run it directly where it can run (Linux / WSL with the 32-bit C library),
# else in Docker.
AS=fw-build/andes/v3s/nds32le-elf-mculib-v3s/nds32le-elf/bin/as
[ -e "$AS" ] || { echo "control-processor compiler missing: run release/setup-toolchain.sh"; exit 1; }
if [ "$(uname -s)" = Linux ] && "$AS" --version >/dev/null 2>&1; then
  GP5_FIRMWARE_DIR="$FWDIR" bash platform/nds32/build.sh
elif command -v docker >/dev/null 2>&1; then
  if [[ "$FWDIR" == "$PWD"/* ]]; then MNT=(); CFW=/work/${FWDIR#"$PWD"/}       # inside this tree
  else MNT=(-v "$FWDIR":/fw); CFW=/fw; fi                                      # elsewhere: mount it too
  docker run --rm --platform linux/386 -e GP5_FIRMWARE_DIR="$CFW" -e GP5_DEBUG_PEEK="${GP5_DEBUG_PEEK:-}" -v "$PWD":/work ${MNT[@]+"${MNT[@]}"} debian:bookworm-slim \
      bash /work/platform/nds32/build.sh
else
  echo "The control-processor compiler is a 32-bit Linux program and cannot run here."
  echo "  Linux / WSL: install the 32-bit C library (Debian/Ubuntu: sudo apt install libc6-i386,"
  echo "               Arch: sudo pacman -S lib32-glibc, Fedora: sudo dnf install glibc.i686)"
  echo "  macOS:       install Docker Desktop"
  exit 1
fi
python3 platform/nds32/pack.py
echo "== one image"
python3 tools/gp5tool.py pack dsp="$TMP/dsp.bin" app=platform/nds32/out/app_0x62.bin -o "$OUT"
