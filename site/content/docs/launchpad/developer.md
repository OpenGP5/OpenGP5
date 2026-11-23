---
title: Developer
description: Read the log and check processor load and memory while you write firmware.
weight: 70
---

Use Developer to read the pedal's log and check the work each chip does.
It needs firmware built on **platform 0.7 or later**.

## The pedal's log

Messages from `gp5_log` on either processor appear as the pedal sends them.
**Pause** stops the display from scrolling. **Save** writes the displayed log to a file.
See [Debugging](../../platform/debugging/) for ways to use the log.

## Load and memory

Load is the share of one 64-sample audio block that a task takes to run.
**Last** shows the most recent block; **worst** shows the peak since the last reset.
The DSP is the digital signal processor that handles the audio.

| Display | Meaning |
|---|---|
| **Audio block** | Time the DSP takes to process a block |
| **Audio-link handler** | Time the control chip spends on audio, if the build uses the [audio link](../../platform/audio-link/) |
| **Main loop, longest pass** | Longest pass through the control chip's screen, controls and USB work, shown as a share of a block |
| **RAM**, **Variables**, **GP5_LARGE buffers** | Memory in use and the limit for each area |

Keep peak audio load below about **85 %** on both chips while using the settings that use the most time.
The main-loop bar measures response time for controls; it is not an extra audio task to add to those loads.

Click **Reset worst** to clear the peak readings and start a new measurement.
See [Performance](../../platform/performance/) for timing limits and ways to reduce load.
