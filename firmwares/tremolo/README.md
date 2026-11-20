# Tremolo

The example firmware of OpenGP5 1.0: the guitar's volume rises and falls with a wave. It shows the parts most
firmwares need: settings declared once as parameters (`params.h`), four presets, the UI kit's settings page
(`gp5_ui.h`: smooth font, only what changed is redrawn), PARA turn, click and hold, and a screen preview on the computer.
[Your first effect](../../docs/platform/first-effect.md) builds a smaller version of it step by step.

| control | does |
|---|---|
| turn PARA | pick a row: rate, depth, shape, preset |
| click PARA | adjust the row picked: turn to change rate (0.5 … 15 Hz), depth, shape (sine, triangle, square), or which preset (loads it); click again when done |
| hold PARA | save the sound into the preset shown |
| foot switch | tremolo on / off (LEDs mint = on, dim red = off) |
| volume knob | output level |

```
python3 tools/run_effect.py tremolo x out.wav --tone --set Rate=800 --set Shape=Triangle   # hear it without a pedal
firmwares/tremolo/test/screen.sh                                                            # its screen, as a PNG
platform/build_firmware.sh tremolo
python3 tools/gp5tool.py flash build/GP-5_tremolo.bin --sections all
```

On the pedal, `python3 tools/gp5_link.py params` lists its parameters and Launchpad's Live page edits them.
