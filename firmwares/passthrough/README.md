# Passthrough: the template

The smallest firmware built with OpenGP5, meant to be copied: the guitar goes to both outputs unchanged and
the volume knob sets the level. [Your first effect](../../docs/platform/first-effect.md) turns a copy of it
into a tremolo, step by step.

- `firmware.json`: who it is. Give your copy its own `id` and `name` first: the pedal's storage belongs to the
  firmware with that id.
- `dsp/firmware.h`: the sound, 3 functions, about 10 lines.
- `nds32/firmware.c`: the screen: two lines of text showing the volume.

```
cp -r firmwares/passthrough firmwares/my-effect          # then edit firmwares/my-effect/firmware.json
platform/build_firmware.sh my-effect
python3 tools/gp5tool.py flash build/GP-5_my-effect.bin --sections all
```

A 64-sample audio block takes about 3,900 of the DSP's 278,000 cycles.
