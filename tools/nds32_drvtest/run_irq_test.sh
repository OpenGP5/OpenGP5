#!/bin/bash
# Simulator test of platform/nds32/start/vectors.S interrupt entry. Run in the linux/386 container:
#   docker run --rm --platform linux/386 -v "$PWD":/work debian:bookworm-slim bash /work/tools/nds32_drvtest/run_irq_test.sh
set -eu
export PATH=/work/fw-build/andes/v3s/nds32le-elf-mculib-v3s/bin:$PATH
T=/work/tools/nds32_drvtest; D=$(mktemp -d); cd "$D"
CF="-mcpu=d1088-spu -mcmodel=medium -O1 -Wall"
nds32le-elf-gcc $CF -D_start=gp5_image_start "-Diret=j irq_iret_shim" -Wl,--defsym,_stack=0x2f00000 "$T/irq_entry_main.c" "$T/irq_entry_test.S" /work/platform/nds32/start/vectors.S -o t.elf
nds32le-elf-run --memory-region 0x0,0x3000000 t.elf
