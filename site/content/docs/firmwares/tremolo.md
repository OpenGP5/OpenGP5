---
title: Tremolo
description: The example firmware. A tremolo with four presets, small enough to read in one sitting.
weight: 30
---

The tremolo makes the guitar's volume rise and fall with a wave. It's the example firmware of OpenGP5: it shows
the parts most firmwares need (settings, presets, a screen, the controls) and nothing more.
[Your first effect](../../platform/first-effect/) builds a smaller version of it step by step.

## Controls

| Control | Does |
|---|---|
| **Turn PARA** | Pick a row: RATE, DEPTH, SHAPE, PRESET |
| **Click PARA** | Adjust the row: turn to change rate (0.5 … 15 Hz), depth, shape (sine, triangle, square) or the preset (loads it). Click again when done |
| **Hold PARA** | Save the sound into the preset shown |
| **Foot switch** | Tremolo on or off (LEDs mint = on, dim red = off) |
| **Volume knob** | Output level |

## Get it

Flash it from the [web flasher](https://opengp5.github.io/OpenGP5/flasher/), or build it:

```bash
platform/build_firmware.sh tremolo
python3 tools/gp5tool.py flash build/GP-5_tremolo.bin --sections all
```

Its source is in [`firmwares/tremolo`](https://github.com/OpenGP5/OpenGP5/tree/main/firmwares/tremolo). On the
pedal, [Launchpad's Live page](../../launchpad/live/) edits its settings.
