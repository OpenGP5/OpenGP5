# Tremolo

The worked example of `docs/platform/first-effect.md`: the guitar's volume rises and falls with a sine wave.
Float maths and `sinf` on the DSP, ordinary variables, three settings passed from the controls to the sound.

| control | does |
|---|---|
| big knob | depth: none … the volume dips to silence |
| turn PARA | rate, 0.25 Hz per click, 0.5 … 15 Hz |
| foot switch | tremolo on / off (LEDs mint = on, dim red = off) |

```
python3 tools/run_effect.py tremolo x out.wav --tone --set 0=400 --set 1=255    # hear it without a pedal
platform/build_firmware.sh tremolo
python3 tools/gp5tool.py flash build/GP-5_tremolo.bin --sections all
```

Settings (link command `0x7F10`): 0 = rate in 1/100 Hz, 1 = depth 0..255, 2 = on.
