---
title: Documentation
linkTitle: Docs
description: Everything about OpenGP5, from flashing your first firmware to writing your own.
weight: 1
---

**OpenGP5** is open firmware for the Valeton GP-5 guitar pedal. It replaces the pedal's software on both of its
processors with code written from scratch, and gives you a framework for writing your own: your audio code, your
screen, your controls, flashed over USB.

Nothing in it comes from Valeton or the chip vendor. You can always go back to Valeton's firmware.

## Where to start

| I want to… | Read |
|---|---|
| Try a firmware on my pedal | [Get started](get-started/) |
| Load NAM captures, edit presets, back up my pedal | [Launchpad](launchpad/) |
| Learn what a firmware does and how to use it | [Firmwares](firmwares/) |
| Write my own effect | [Build a firmware](platform/), starting with [Your first effect](platform/first-effect/) |
| Look up a function or a protocol | [API reference](platform/api/), [Protocols](platform/protocols/) |
| Fix a problem | [Recovery](get-started/recovery/), [Help](help/) |

## The parts

**Firmwares** run on the pedal. Each one is a complete replacement for the pedal's software:
[NAM Loader](firmwares/nam-loader/) plays Neural Amp Modeler captures, [Dreamcatcher](firmwares/dreamcatcher/) is
an ambient looper and glitch delay, and the [tremolo](firmwares/tremolo/) is the example to learn from. Only one
firmware is on the pedal at a time.

**Launchpad** is the desktop app. It flashes firmwares, loads captures and IRs, edits settings and presets while
you play, and backs up what's on the pedal. It works with Valeton's own firmware too.

**The web flasher** flashes a firmware from Chrome, Edge or Opera, with nothing to install.

**The platform** is what every firmware is built on. It runs both processors, the USB connection, the screen,
the controls and the storage, so a firmware is only its sound and its interface.

> [!NOTE]
> OpenGP5 is in **alpha**: expect rough edges, and expect interfaces to change before 1.0. Reports from more
> pedals are very welcome ([Beta testing](help/beta-testing/)).
