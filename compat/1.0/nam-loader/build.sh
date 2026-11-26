#!/bin/bash
# Build NAM Loader into one flashable image: build/GP-5_nam-loader.bin
# First time: git submodule update --init && opengp5/release/setup-toolchain.sh   (see README.md)
set -euo pipefail
cd "$(dirname "$0")"
[ -f opengp5/platform/build_firmware.sh ] || { echo "OpenGP5 missing: run  git submodule update --init"; exit 1; }
opengp5/platform/build_firmware.sh . build/GP-5_nam-loader.bin
