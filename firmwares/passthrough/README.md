# Passthrough

The smallest firmware for the GP-5 open platform, meant to be copied. The guitar goes to both outputs
unchanged and the big knob is the volume.

- `dsp/firmware.h` — 3 functions, about 10 lines.
- `nds32/firmware.c` — the control half: volume from the knob, two lines of text on the screen.

```
platform/build_firmware.sh passthrough
python3 tools/gp5tool.py flash build/GP-5_passthrough.bin --sections all
```

Tested on the pedal: the guitar comes through at full level once the knob is up, and a 64-sample audio
block takes about 3,900 of the DSP's 278,000 cycles.
