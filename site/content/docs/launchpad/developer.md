---
title: Developer
description: The pedal's log, and how busy each processor is. For firmware authors.
weight: 70
---

The Developer page helps while you write a firmware. It works with firmwares built on OpenGP5 0.7 or later.

## The pedal's log

Lines a firmware writes with `gp5_log` (on either processor) appear here as they come. **Pause** stops the list
from scrolling; **Save the log** keeps it in a file. See [Debugging](../../platform/debugging/).

## Load and memory

How much of each audio block each processor uses, now and at its worst:

| | |
|---|---|
| **Audio block** | the DSP's share of a 64-sample block |
| **Audio-link handler** | the control processor's audio work, if the firmware uses the [audio link](../../platform/audio-link/) |
| **Main loop, longest pass** | the longest the control processor's main loop took (screen, controls, USB) |
| **Variables** | memory in use on each processor |

Keep the worst values under about 85 % of a block while playing your heaviest settings. **Reset worst** starts the
worst values again. See [Performance](../../platform/performance/).
