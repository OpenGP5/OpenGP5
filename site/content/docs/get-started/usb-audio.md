---
title: Use the pedal as an audio interface
linkTitle: USB audio
description: Record the pedal's sound on a computer and play the computer's sound through the pedal, over the USB cable.
weight: 26
---

Firmwares built on OpenGP5 1.0 make the pedal a USB audio interface, beside its MIDI port. There's no driver to
install. It works with any recording app on macOS and Windows. Linux should work too, since the device is
class-compliant, but it hasn't been tested yet.

It comes with Dreamcatcher 1.5, NAM Loader 1.4, the tremolo 1.2 and other firmwares built on 1.0, unless the
firmware turns it off.

## What the computer sees

The pedal shows up as an audio device called **OpenGP5**, with 2 inputs and 2 outputs, 16-bit, at the firmware's
sample rate:

| Firmware | Sample rate |
|---|---|
| Dreamcatcher, tremolo | 44.1 kHz |
| NAM Loader | 48 kHz |

Set your recording app to the same rate. If the rate is different, the computer converts it, or the app refuses
the device.

## Record

The inputs carry the firmware's finished stereo sound: what you hear from the pedal, taken before the volume
knob. Turning the knob changes what you hear, not what is recorded.

## Play through the pedal

The computer's sound, a backing track or your DAW's output, comes out of the pedal's outputs, mixed with the
pedal's own sound. The volume knob sets the level of both.

## Monitoring in a DAW

If your DAW plays back the pedal's input while it records (input monitoring), you hear the pedal twice: once
straight from the pedal, and again a moment later from the DAW. To hear only the DAW:

- In Launchpad, untick **Pedal sound while the computer plays** in the pedal panel, or
- run `python3 tools/gp5_link.py direct off`.

This turns off only the pedal's own sound at its outputs, and only while the computer is sending sound to the
pedal. When the computer stops playing, you hear the pedal again. Recording doesn't change. The setting goes back
on when the pedal restarts.

## If the device doesn't show up or can't be opened

- Check that the firmware is built on OpenGP5 1.0 or later. Launchpad's pedal panel shows the platform.
- On Windows, if the device shows up but can't be opened (Windows and your recording app both refuse it), Windows
  is using details it saved for an earlier firmware. Open Device Manager, choose View › Show hidden devices, and
  under Sound, video and game controllers uninstall every **OpenGP5** and **GP-5 Open FW** entry (leave "delete the
  driver" unticked). Then unplug the pedal and plug it in again.
- Firmwares built before 1.0 have no USB audio. Their MIDI port is called **GP-5 Open FW** instead of OpenGP5.
