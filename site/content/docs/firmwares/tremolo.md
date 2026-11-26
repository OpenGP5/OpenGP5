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

## Bluetooth and MIDI

Pair **GP-5** over Bluetooth MIDI from a phone, a tablet or a Mac, or use the USB cable: the same MIDI works on
both, on any channel.

| MIDI | Does |
|---|---|
| **Program Change 0–3** | Loads presets 1–4 (an empty one is ignored) |
| **CC 20** | Rate (0.5 … 15 Hz) |
| **CC 21** | Depth |
| **CC 22** | Shape (sine, triangle, square) |
| **CC 23** | Tremolo off (0–63) or on (64–127) |

[Launchpad](../../launchpad/) works over Bluetooth too. Flashing needs the USB cable.

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
