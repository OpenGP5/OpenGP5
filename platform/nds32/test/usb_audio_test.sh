#!/bin/sh
# Host test of USB audio's clock following and resampler (usb_audio.c).   platform/nds32/test/usb_audio_test.sh
set -e
D=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cc -std=gnu99 -O1 -Wall -Wno-unused-function -Wno-unknown-attributes -DGP5_USB_AUDIO -DGP5_USB_PLAY_AT=0u -DGP5_USB_REC_AT=0u -DGP5_LINK_TOTAL=64u \
   -DGP5_SAMPLE_RATE=44100u -DGP5_CTL_MHZ=288u -I"$D/usb_include" -I"$D/.." -I"$D/../../dsp" "$D/usb_audio_test.c" -lm -o "$T/t"
# clock differences: realistic ones (crystals are good to tens of ppm; macOS ran 23 ppm off its USB frames), and far off
# (far off with the computer slower than the DSP, beyond ~-1,200 ppm, the fill dips below the floor while the loop
# settles: the buffer has less room below its target than above. Real clocks are within a few hundred ppm.)
for c in "0 0" "25 0" "80 -40" "-150 120" "300 -300" "-500 500" "800 -800"; do "$T/t" $c; done
# the stream pausing (91 ms, then 43 ms: Windows when a program opens the device): no dry-outs once it runs again
for c in "0 0" "50 -30" "-80 40" "300 -300"; do GAPS=1 "$T/t" $c; done
# a long link handler (over 1 ms, NAM Loader): USB packets arrive after it started
for c in "0 0" "50 -30" "-80 40"; do LATE=1 "$T/t" $c; GAPS=1 LATE=1 "$T/t" $c; done
