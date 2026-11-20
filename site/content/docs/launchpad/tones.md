---
title: Tones
description: Load NAM captures and cab IRs onto the pedal, pair them, and set each capture's gate and EQ.
weight: 20
---

The Tones page is for [NAM Loader](../../firmwares/nam-loader/). The pedal holds **30 captures** and **12 cab
IRs**, and any capture can play any IR, or none.

The page has two columns. On the left, the list of what's on the pedal, with a **NAM / IR** switch at the top.
On the right, adding new files, and the details of the one you picked.

## Add a capture

1. Switch the list to **NAM** and pick an empty slot (or Launchpad uses the first free one).
2. Drop a `.nam` file anywhere on the page, or click **Choose file** under **Add a capture**.
3. Check the name it will have on the pedal, and send it.

NAM Loader plays **A2** captures (it runs their A2-Lite part). Older A1 captures can't be played.

### Conversion

Two switches under **Conversion**, both on by default:

- **Calibrate** matches the pedal's input to the level the capture was recorded at, so it reacts to your playing
  like the original gear. It applies only to captures that include their recording level.
- **Normalize** sets every capture to the same volume. Off, each one is as loud as the gear it captured.

The network itself is never changed: every layer and weight of the capture is kept.

## Add a cab IR

Switch the list to **IR**, then drop a `.wav` impulse response (or use **Add an IR**). Any sample rate and length
works: Launchpad resamples it to 48 kHz, keeps the first 1,024 samples (21 ms), and sets it to the same loudness
as your other IRs.

Cab IRs are for captures of an amp without its cabinet (a "DI" capture). A capture of a full rig already has its
cab.

IRs added with NAM Loader 1.1 or 1.2 use an older format, twice the size. **Shrink** converts them, with the same
sound.

## The one you picked

Pick a capture or an IR in the list to see its details:

| | |
|---|---|
| **Play on the pedal** | Switch the pedal to this capture (NAM Loader 1.3.1 or later) |
| **Rename** | Change the name shown on the pedal |
| **Replace** | Send a new file into the same slot. Its IR choice, gate and EQ stay |
| **Remove** | Clear the slot. A capture's IR, gate and EQ go back to none, off and flat |
| **IR** | Which cab IR this capture plays: its own default, another, or none |
| **Gate and EQ** | A noise gate and bass, mid and treble, kept with the capture |

Gate and EQ change the sound live when the capture is playing. When it isn't, they're saved on the pedal for the
next time it plays.

## When the pedal is full

30 captures and 12 IRs fill the pedal's storage. To add one, replace or remove another first. If IRs in the older
format take up room, Launchpad shrinks them to make space.

## On the pedal

Everything here can also be done on the pedal itself: see [NAM Loader's controls](../../firmwares/nam-loader/#controls).
