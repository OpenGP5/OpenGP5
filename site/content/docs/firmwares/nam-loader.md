---
title: NAM Loader
description: Play Neural Amp Modeler captures on the pedal, each with its own cab IR, noise gate and EQ.
weight: 10
---

NAM Loader plays [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) (NAM) captures on the GP-5.
A capture models the sound of an amp or pedal. NAM Loader runs at **48 kHz** and supports **A2 captures**.
It uses their A2-Lite model; older A1 captures do not work.

Version **1.3 or later** holds **30 captures** and **12 cabinet impulse responses (cab IRs)**.
An IR adds a speaker cabinet's sound. Each capture keeps its own IR choice, noise gate and 3-band EQ.
You can choose any stored IR, or none.

[Source and releases on GitHub](https://github.com/OpenGP5/NAM-Loader)

## Controls

The screen has two main pages. **NAM** shows the playing capture; **CAB** shows its IR.
Two dots in the header mark the current page.

| Control | Action |
|---|---|
| **Turn PARA** | Switch NAM / CAB pages; in a list, move the highlight |
| **Press PARA** | Open the capture list on NAM or the IR list on CAB; press again to load the highlighted item |
| **Hold PARA for 0.6 s** | Open or close TONE to edit the playing capture's gate and EQ |
| **Press the foot switch** | Toggle bypass |
| **Turn the volume knob** | Set the output level |

The playing capture keeps sounding while you browse. The list closes after five seconds with no knob movement.
The screen shows the capture or IR name, slot number, input level and output volume.
Long names scroll. The pedal recalls the last capture at power-on.

### Set the gate and EQ

1. Hold PARA for 0.6 seconds to open **TONE**.
2. Turn PARA to select **GATE**, **BASS**, **MID** or **TREBLE**.
3. Press PARA, then turn it to change the value. Press again to select another row.
4. Hold PARA to leave TONE, or wait ten seconds without using the control.

The pedal saves changes about two seconds after the last edit. Keep it powered until then.

## Getting captures onto the pedal

Use [Launchpad's Tones page](../../launchpad/tones/). Drop a `.nam` file, check its name and click **Send to slot**.
Launchpad converts the model and sends it to the pedal. Use the same page to send `.wav` cab IRs.
With no stored captures, NAM Loader plays its built-in demo.

## Cab IRs

Use an IR with an amp-only, or **DI**, capture. A full-rig capture includes the cab sound, so choose **NONE** for that capture.

- Launchpad converts each IR to 48 kHz and keeps up to 1,024 samples (21 ms). It also adjusts the level.
- A capture uses the IR with the same slot number by default, if that IR exists. You can choose another or **NONE**.
- If the processor runs out of time, NAM Loader turns off the IR to keep the audio running.

## How the model runs

NAM Loader keeps the A2-Lite model's layers. It converts weights to 16-bit fixed-point values and uses a
32-bit signal through the network. Rounding changes the precision of the weights; it does not retrain the model.

Fixed-point maths lets the model fit the pedal's time budget. Its output differs from NAM's floating-point output.
The size of that difference depends on the capture and input signal.

Both chips share the work. The digital signal processor (DSP) runs the first layers.
The control chip runs the rest, plus the output stage and cab IR.

## Input level and loudness

Launchpad offers two gain controls when it converts a capture. Both start on:

- **Calibrate** sets input gain to match the level used to make the capture. The file must include that level.
  The pedal reaches full scale at an input of +7.1 dBu.
- **Normalize** sets output gain with a test sound. It aims for a level of −20 dB below full scale.
  It uses K-weighting to account for the ear's response to low and high sounds.
  It also keeps peaks in that test no higher than −3 dBFS.

The peak limit can keep a capture below the loudness target. Your playing can also produce different levels from the test signal.
With Normalize off, Launchpad keeps the capture's output gain.
These two switches change gain, not the model's layers or training.

## Updating

An update keeps your captures, IRs and settings. Files sent before version 1.1 did not use Calibrate or Normalize.
To use those options, send the source `.nam` file again. Then check its gate, EQ and IR choice.

## Versions

| Version | Changes |
|---|---|
| 1.3.1 | Launchpad can select the playing capture with **Play on the pedal** |
| 1.3.0 | 30 captures, a separate bank of 12 IRs, and NAM / CAB pages |
| 1.2.0 | Noise gate and 3-band EQ per capture |
| 1.1.0 | Cab IRs, 48 kHz, Calibrate and Normalize |
| 1.0.0 | First release with 12 capture slots |
