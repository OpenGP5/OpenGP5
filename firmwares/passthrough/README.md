# Passthrough: the template

The smallest firmware built with OpenGP5, meant to be copied: the guitar goes to both outputs unchanged and
the volume knob sets the level. [Your first effect](../../docs/platform/first-effect.md) turns a copy of it
into a tremolo, step by step.

- `dsp/firmware.h`: the sound, 3 functions, about 10 lines.
- `nds32/firmware.c`: the controls and screen: volume from the knob, two lines of text.

```
cp -r firmwares/passthrough firmwares/my-effect
platform/build_firmware.sh my-effect
python3 tools/gp5tool.py flash build/GP-5_my-effect.bin --sections all
```

A 64-sample audio block takes about 3,900 of the DSP's 278,000 cycles.
