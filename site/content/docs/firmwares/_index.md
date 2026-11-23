---
title: Firmware
description: Choose an OpenGP5 firmware build and learn its controls.
weight: 30
---

Each build runs the pedal's sound and controls. You can keep one build on the pedal at a time.
Switching to another firmware [erases the old data](../get-started/safety/#back-up-first). Back up before you start.

| Firmware | What it does | Get it |
|---|---|---|
| **[NAM Loader](nam-loader/)** | Plays amp captures with cab IRs, a noise gate and EQ | Launchpad or [releases](https://github.com/OpenGP5/NAM-Loader/releases) |
| **[Dreamcatcher](dreamcatcher/)** | Loops your playing and adds delay, grains and reverb | Launchpad, the web flasher or [releases](https://github.com/OpenGP5/Dreamcatcher/releases) |
| **[Tremolo](tremolo/)** | Makes the volume rise and fall; serves as an example for authors | The web flasher or a local build |

A Neural Amp Modeler (NAM) capture models the sound of an amp or pedal.
A cabinet impulse response (cab IR) adds the sound of a speaker cab.
A grain is a short slice of sound. Dreamcatcher can shift its pitch or play it in reverse.

See [Flash a firmware](../get-started/flash/) for the install steps.

## More firmware

You can list a build in [OpenGP5's catalog](https://github.com/OpenGP5/OpenGP5/blob/main/catalog/README.md).
Use Launchpad to get listed builds. To use another author's build in the web flasher, download the file and drop it on the page.

The tools check the file's size, checksum and built-in details against its listing.
Other authors' builds carry *Not reviewed*. This means the file checks do not include a review of its code.

To write an effect, start with [Build a firmware](../platform/).
