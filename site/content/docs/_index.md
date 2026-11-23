---
title: Documentation
linkTitle: Docs
description: Use OpenGP5 on your pedal, manage your data, or write your own effect.
weight: 1
---

**OpenGP5** is free, open source firmware for the Valeton GP-5 guitar pedal. It runs the pedal's sound,
screen and controls. You can use an effect we provide or write your own in C.

OpenGP5 contains no code or data from Valeton or the chip vendor. To go back to Valeton's firmware, follow
[Recovery](get-started/recovery/). That guide also covers the DSP update fault that can block a USB flash.

## Where to start

| I want to… | Read |
|---|---|
| Try an effect on my pedal | [Get started](get-started/) |
| Load captures, edit presets or back up my data | [Launchpad](launchpad/) |
| Learn an effect's controls | [Firmware](firmwares/) |
| Write my own effect | [Build a firmware](platform/), then [Your first effect](platform/first-effect/) |
| Look up a function or protocol | [API reference](platform/api/), [Protocols](platform/protocols/) |
| Fix a problem | [Recovery](get-started/recovery/), [Help](help/) |

## What each part does

**Firmware** runs on the pedal. [NAM Loader](firmwares/nam-loader/) plays Neural Amp Modeler (NAM) captures:
models of how an amp or pedal sounds. [Dreamcatcher](firmwares/dreamcatcher/) loops and processes your playing.
The [tremolo](firmwares/tremolo/) makes the volume rise and fall. Only one firmware build runs on the pedal at a time.

**Launchpad** is the desktop app. Use it to flash firmware, load captures and edit presets as you play.
It also loads cabinet impulse responses (cab IRs): files that add a speaker cabinet's sound.
Its [backups](launchpad/backups/) save the data each supported firmware exposes. With Valeton's firmware,
a backup holds the 100 presets; export SnapTones and user IRs in Valeton Suite.

**The web flasher** installs firmware from Chrome, Edge or Opera on a computer. You do not need to install an app.

**The platform** is the shared code for firmware authors. It handles both chips, USB, the screen,
controls and storage. Your code defines the sound and interface.

> [!NOTE]
> OpenGP5 is in **alpha**. These docs target **platform 0.8.0-alpha**. These docs also describe the planned
> **1.0 API**, which began in 0.7. The API is the set of functions and commands firmware can use; it may change
> before 1.0 final. See [Beta testing](help/beta-testing/) to report how your pedal works.
