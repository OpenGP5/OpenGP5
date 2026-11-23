---
title: Tremolo
description: Learn the platform with a tremolo, four presets, and controls on the pedal.
weight: 30
---

The tremolo makes the guitar's volume rise and fall with a wave.
Use it to learn how an OpenGP5 effect handles settings, presets, the screen and controls.
[Your first effect](../../platform/first-effect/) builds a smaller version step by step.

## Controls

| Control | Action |
|---|---|
| **Turn PARA** | Select RATE, DEPTH, SHAPE or PRESET |
| **Press PARA** | Start editing the selected row; press again to finish |
| **Turn PARA while editing** | Change the value; on PRESET, load the selected slot |
| **Hold PARA for 0.6 s** | Save the sound in the displayed preset slot |
| **Press the foot switch** | Toggle the effect; mint LEDs mean on, dim red means off |
| **Turn the volume knob** | Set the output level |

RATE runs from 0.5 to 15 Hz. DEPTH sets how far the volume falls.
SHAPE offers sine, triangle and square waves. There are four preset slots.

## Get it

Install the tremolo with the [web flasher](https://opengp5.github.io/flasher/).
To build it, [set up your computer](../../platform/setup/), then run these commands from the OpenGP5 folder:

```bash
platform/build_firmware.sh tremolo
python3 tools/gp5tool.py flash build/GP-5_tremolo.bin --sections all
```

Read [Before you flash](../../get-started/safety/) before writing to the pedal.
The source is in [`firmwares/tremolo`](https://github.com/OpenGP5/OpenGP5/tree/main/firmwares/tremolo).
Use [Launchpad's Live page](../../launchpad/live/) to edit the installed tremolo's settings.
