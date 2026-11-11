#!/bin/bash
# run.sh <cases.c> vendor|ours [our driver .c files...]   -> prints the register-difference log
# Run inside the linux/386 container from the repo root (/work).
set -eu
export PATH=/work/fw-build/andes/v3s/nds32le-elf-mculib-v3s/bin:$PATH
SDK=/work/vendor/bp1048_sdk/MVsB1_Base_SDK
T=/work/tools/nds32_drvtest
CASES=$1; MODE=$2; shift 2
D=$(mktemp -d); cd "$D"
CF="-mcpu=d1088-spu -mcmodel=medium -Os -Wall -I$SDK/driver/driver/inc -I$SDK/driver/driver_api/inc -I$SDK/middleware/mv_utils/inc"
if [ "$MODE" = vendor ]; then
  cat > stubs.c <<'S'
/* what libDriver objects expect from the SDK startup / other objects, as no-ops for the simulator */
/* weak: a cases file may supply its own (counting) versions */
__attribute__((weak)) void GIE_ENABLE(void){} __attribute__((weak)) void GIE_DISABLE(void){}
__attribute__((weak)) int GIE_STATE_GET(void){return 0;}
__attribute__((weak)) void NVIC_EnableIRQ(int n){(void)n;} __attribute__((weak)) void NVIC_DisableIRQ(int n){(void)n;}
S
  nds32le-elf-gcc $CF "$T/harness.c" "$CASES" stubs.c -L"$SDK/driver/driver" -lDriver -o t.elf 2>&1 | grep -v 'warning' || true
else
  # our drivers: no SDK include path, no SDK library
  nds32le-elf-gcc -mcpu=d1088-spu -mcmodel=medium -Os -Wall -Wextra -DOURS -I/work/platform/nds32 "$T/harness.c" "$CASES" "$@" -o t.elf
fi
nds32le-elf-run --memory-region 0x0,0x3000000 --memory-region 0x40000000,0x100000 t.elf
