---
title: NAM Loader
description: Play Neural Amp Modeler captures on the pedal, each with its own cab IR, noise gate and EQ.
weight: 10
---

NAM Loader plays [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) captures on the GP-5
itself, at 48 kHz, the rate captures are trained at. The pedal holds **30 captures** and **12 cab IRs**. Any
capture can play any IR, or none, and each capture keeps its own IR choice, noise gate and 3-band EQ.

[Source and releases on GitHub](https://github.com/OpenGP5/NAM-Loader)

## Controls

The screen has two pages: **NAM** shows the capture playing, **CAB** shows its IR. Two dots in the header show
which page you're on.

| Control | Does |
|---|---|
| **Turn PARA** | Switch between the NAM and CAB pages. In a list: move through it |
| **Push PARA** | On NAM: open the 30 capture slots. On CAB: open the IRs, with NONE. In a list: load the highlighted one |
| **Hold PARA** | The TONE page: gate, bass, mid and treble of the capture playing |
| **Foot switch** | Bypass |
| **Volume knob** | Output level |

While you browse a list, the capture playing keeps playing. After 5 seconds without turning, the list closes.

The screen shows the name of the capture or IR (a long name slides to its end and back), its number, an input
meter and the volume. The pedal remembers which capture was playing when you power it off.

## Getting captures onto the pedal

Use [Launchpad's Tones page](../../launchpad/tones/): drop a `.nam` file, and it's converted and written into a
slot. Cab IRs (`.wav`) go in the same way. With no captures on the pedal, it plays its built-in demo capture.

NAM Loader plays **A2** captures. Older A1 captures can't be played.

## Cab IRs

An impulse response adds a speaker cabinet to a capture of an amp alone (a "DI" capture). Captures of a full rig
already have their cab, so choose NONE for those.

- Each IR is 1,024 samples at 48 kHz (21 ms). Launchpad resamples and trims any `.wav`, and sets every IR to the
  same loudness.
- A capture plays its own default IR (the one with the same number) until you choose another.
- If the processor ever runs short of time, the IR switches itself off rather than interrupting the sound.

## How the model runs

The pedal runs the capture's own network: every layer and every weight, unchanged. It's computed in fixed point
(16-bit weights, a 32-bit signal through the network), because the pedal's processors are too slow at floating
point to run it in real time. The result is about 65 dB from NAM's own floating-point output on guitar.

The model is split across both of the pedal's processors: the DSP runs the first layers, the control processor the
rest, the output and the cab IR.

## Input level and loudness

When Launchpad converts a capture, two optional steps happen (both on by default):

- **Calibrate:** the pedal's input is +7.1 dBu at full scale. A capture that records the level it was made at gets
  the difference as input gain, so it's driven as hard as the original gear.
- **Normalize:** every capture is set to the same loudness (−20 dB, K-weighted, for hard playing), with peaks at
  most −3 dBFS. Without it, captures can differ in loudness by 30 dB or more.

Only the capture's input and output gain change. The network itself is never altered.

## Updating

Updating NAM Loader keeps your captures, IRs and settings. Captures added before version 1.1 don't have Calibrate
and Normalize applied; send them again from Launchpad to get them.

## Versions

| Version | Changes |
|---|---|
| 1.3.1 | Launchpad can switch the capture playing (*Play on the pedal*) |
| 1.3.0 | 30 captures and 12 IRs, chosen independently; NAM and CAB pages |
| 1.2.0 | Noise gate and 3-band EQ per capture |
| 1.1.0 | Cab IRs, 48 kHz, Calibrate and Normalize |
| 1.0.0 | First release: 12 slots |
