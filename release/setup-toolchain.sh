#!/bin/bash
# Fetch the two compilers into fw-build/ (no admin rights needed, nothing installed system-wide):
#   - Andes nds32le-elf-mculib-v3s (gcc 4.9.4) for the control processor, from Andes' GitHub releases, 36 MB.
#     32-bit Linux programs: run directly on Linux / WSL (x86_64), in Docker on macOS.
#   - Arm GNU Toolchain 14.2.rel1 (arm-none-eabi) for the DSP, from developer.arm.com, about 150 MB.
#     Pinned, so every machine builds the same bytes.
# Both downloads are checked against their published SHA-256.
set -euo pipefail
cd "$(dirname "$0")/.."

sha() { (command -v sha256sum >/dev/null && sha256sum "$1" || shasum -a 256 "$1") | cut -d' ' -f1; }
fetch() {   # url file sha256
  echo "downloading $1"
  curl -fL --retry 3 -sS -o "$2" "$1"
  got=$(sha "$2"); [ "$got" = "$3" ] || { echo "checksum mismatch for $2: $got"; rm -f "$2"; exit 1; }
}

# ---- control processor (Andes)
A=fw-build/andes
if [ -x "$A/v3s/nds32le-elf-mculib-v3s/bin/nds32le-elf-gcc" ]; then echo "already there: $A/v3s"
else
  mkdir -p "$A/v3s"
  fetch https://github.com/andestech/Andes-Development-Kit/releases/download/ast-v3_2_5-release-linux/nds32le-elf-mculib-v3s.txz \
        "$A/v3s.txz" ff10eb1fa9dce66f8aeca5267df01ce0353748f2f2364a550eceb10c7953abc3
  tar -xJf "$A/v3s.txz" -C "$A/v3s" && rm -f "$A/v3s.txz"
  echo "ok: $A/v3s/nds32le-elf-mculib-v3s/bin/nds32le-elf-gcc"
fi

# ---- DSP (Arm)
R=fw-build/arm
V=14.2.rel1
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)          H=x86_64;        S=62a63b981fe391a9cbad7ef51b17e49aeaa3e7b0d029b36ca1e9c3b2a9b78823 ;;
  Linux-aarch64)         H=aarch64;       S=87330bab085dd8749d4ed0ad633674b9dc48b237b61069e3b481abd364d0a684 ;;
  Darwin-arm64)          H=darwin-arm64;  S=c7c78ffab9bebfce91d99d3c24da6bf4b81c01e16cf551eb2ff9f25b9e0a3818 ;;
  Darwin-x86_64)         H=darwin-x86_64; S=2d9e717dd4f7751d18936ae1365d25916534105ebcb7583039eff1092b824505 ;;
  *) echo "no Arm toolchain download for $(uname -s) $(uname -m): install arm-none-eabi-gcc yourself"; exit 0 ;;
esac
if [ -x "$R/bin/arm-none-eabi-gcc" ]; then echo "already there: $R"
else
  mkdir -p "$R"
  fetch "https://developer.arm.com/-/media/Files/downloads/gnu/$V/binrel/arm-gnu-toolchain-$V-$H-arm-none-eabi.tar.xz" \
        "$R.tar.xz" "$S"
  tar -xJf "$R.tar.xz" -C "$R" --strip-components=1 && rm -f "$R.tar.xz"
  [ "$(uname -s)" = Darwin ] && xattr -dr com.apple.quarantine "$R" 2>/dev/null || true
  echo "ok: $R/bin/arm-none-eabi-gcc ($("$R/bin/arm-none-eabi-gcc" -dumpversion))"
fi
