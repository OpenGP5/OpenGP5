#!/bin/bash
# Build the GP-5 standalone NDS32 app (linked at 0x10000, where the factory loader jumps).
# Andes toolchain (fw-build/andes) and our own sources only: nothing from the vendor SDK is compiled,
# included or linked. Each driver was checked against the vendor's register behaviour (tools/nds32_drvtest).
# The compiler is a set of 32-bit Linux programs: on Linux / WSL (x86_64) run this script directly,
#   bash platform/nds32/build.sh
# elsewhere (macOS) in a small Linux container, from the repo root:
#   docker run --rm --platform linux/386 -v "$PWD":/work debian:bookworm-slim bash /work/platform/nds32/build.sh
# platform/build_firmware.sh picks the right one.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export PATH=$ROOT/fw-build/andes/v3s/nds32le-elf-mculib-v3s/bin:$PATH
APP=$ROOT/platform/nds32
FW=${GP5_FIRMWARE_DIR:-$ROOT/firmwares/${GP5_FIRMWARE:-granular-delay}}/nds32     # the firmware's own UI / control code
O=$APP/out; mkdir -p "$O"; cd "$O"; rm -f ./*.o app.elf app.bin app.dis app.sym app.map app.ld init-default.c
INC="-I$APP -I$FW"
CF="-std=gnu99 -DUI_PANEL -mcpu=d1088-spu -mext-dsp -mext-zol -mcmodel=medium -Os -g -Wall -fmessage-length=0 -fsingle-precision-constant -ffunction-sections -fdata-sections $INC"

# Platform (start-up, drivers in hal/, USB-MIDI, DSP link, LEDs, screen) + the firmware's own code.
for f in "$APP/main.c" "$APP/led.c" "$APP/usb_midi.c" "$APP/proto.c" "$APP/dsp_link.c" "$APP/dsp_ctl.c" "$APP/hal/gpio.c" "$APP/hal/wdg.c" "$APP/hal/spim.c" "$APP/hal/adc.c" "$APP/hal/uart0.c" "$APP/hal/timer.c" "$APP/hal/tick.c" "$APP/hal/backlight.c" "$APP/hal/clock.c" "$APP/hal/usb.c" "$APP/hal/flash.c" "$APP/gp5_store.c" "$FW"/*.c \
         "$APP/start/vectors.S" "$APP/start/startup.c" "$APP/start/irq.c" "$APP/start/cache.c"; do
  b=$(basename "$f"); X=
  [ "$b" = cache.c ] && X=-mno-ext-zol      # no hardware-loop registers touched on the way to the factory loader
  [ "$b" = flash.c ] && X=-fno-toplevel-reorder   # keeps its no-EX9 / no-IFC brackets around the code
  nds32le-elf-gcc $CF $X -I"$APP/start" -c "$f" -o "${b%.*}.o"
done
nds32le-elf-gcc -Os -nostartfiles -static -mcmodel=medium -mext-dsp -mvh -T "$APP/start/app.ld" -Wl,--gc-sections -Wl,-Map,app.map \
  -o app.elf ./*.o -ldsp
nds32le-elf-objcopy -O binary app.elf app.bin
nds32le-elf-objdump -d app.elf > app.dis
nds32le-elf-nm -n app.elf > app.sym
nds32le-elf-size app.elf
grep -E " (_start|main|gp5_stage|_stack|__data_start|_edata|__bss_start|_end)$" app.sym
