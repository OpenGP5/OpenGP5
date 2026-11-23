---
title: Tones
description: Load captures and cab IRs, choose a cabinet sound, and set the gate and EQ.
weight: 20
---

Use Tones with [NAM Loader](../../firmwares/nam-loader/). Version 1.3 or later holds **30 captures** and
**12 cab IRs**. A capture models an amp or pedal's sound. A cabinet impulse response (cab IR) adds a speaker
cabinet's sound. Each capture can use any stored IR, or none.

The left column lists files on the pedal. Use **NAM / IR** to choose the list.
The right column shows the selected file and controls for adding files.

## Add a capture

1. Choose **NAM** and select an empty slot. If you do not select one, Launchpad uses the first free slot.
2. Drop a `.nam` file on the page, or click **Choose file** under **Add a capture**.
3. Check **Name on the pedal** and click **Send to slot**, followed by the slot number.

NAM Loader supports **A2 captures**, which include an **A2-Lite** model that fits the pedal.
It runs that part of the capture. Older **A1** files use a different model format and do not work.

### Conversion

Open **Conversion** to see two switches. Both start on:

- **Calibrate** sets the input gain to match the level used to make the capture.
  It needs a capture file that includes this level.
- **Normalize** aims for a shared loudness target and limits peaks. Some captures may still sound louder than others.
  With this off, Launchpad keeps the capture's output gain.

Launchpad keeps the A2-Lite model's layers and rounds its weights for fixed-point maths.
That changes their precision. Calibrate and Normalize change gain; they do not retrain the model.

## Add a cab IR

1. Choose **IR** and select an empty slot.
2. Drop a `.wav` file on the page, or click **Choose file** under **Add an IR**.
3. Check the name and click **Send to IR**, followed by the slot number.

Launchpad converts the sample rate to 48 kHz, keeps up to 1,024 samples (21 ms), and adjusts the level.
Use a cab IR with an amp-only, or **DI**, capture. For a full-rig capture that includes the cab, choose **NONE**.

IRs added with NAM Loader 1.1 or 1.2 use an older format that takes twice the space.
**Shrink** converts those IRs to the smaller format without changing their sound.

## Edit a capture or IR

Select a file in the list to see its controls:

| Control | Action |
|---|---|
| **Play on the pedal** | Switch to this capture; needs NAM Loader 1.3.1 or later |
| **Rename** | Change the name on the pedal |
| **Replace** | Load a new file into the same slot; read the reset rules below |
| **Remove** | Clear the slot |
| **IR** | Choose the capture's cab IR, its default IR, or none |
| **Gate and EQ** | Set the noise gate, bass, mid and treble for this capture |

A new capture does not keep the old capture's gate and EQ: it starts with the gate off and EQ flat.
Check its IR choice after replacement. Replacing an IR changes the cabinet sound for every capture that uses that IR.

Gate and EQ affect the sound as you play. The pedal saves them with the capture.
You can also edit a capture that is not playing; the next time you select it, the pedal uses those settings.

## When the pedal is full

To add a file when all its slots are full, replace or remove one.
If old-format IRs use too much storage, Launchpad shrinks them to make room.

## On the pedal

Use Launchpad to load captures and cab IRs. On the pedal, choose which ones to play and set the gate and EQ.
See [NAM Loader's controls](../../firmwares/nam-loader/#controls).
