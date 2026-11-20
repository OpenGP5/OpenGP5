---
title: Firmwares
description: The firmwares built on OpenGP5, what they do, and how to play them.
weight: 30
---

A firmware is a complete replacement for the pedal's software. Only one is on the pedal at a time; switching to
another [wipes the first one's data](../get-started/safety/#back-up-first), so back up before you switch.

| Firmware | What it is | Get it |
|---|---|---|
| **[NAM Loader](nam-loader/)** | Plays Neural Amp Modeler captures, with 12 cab IRs, a noise gate and EQ per capture | Launchpad, or [its releases](https://github.com/OpenGP5/NAM-Loader/releases) |
| **[Dreamcatcher](dreamcatcher/)** | Ambient textures: an always-listening micro-looper, a glitch delay that smears into reverb, grains | Launchpad, the web flasher, or [its releases](https://github.com/OpenGP5/Dreamcatcher/releases) |
| **[Tremolo](tremolo/)** | The example firmware: a small tremolo with presets, to learn from | the web flasher, or build it |

How to install one: [Flash a firmware](../get-started/flash/).

## More firmwares

Anyone can write a firmware and list it in [OpenGP5's catalog](https://github.com/OpenGP5/OpenGP5/blob/main/catalog/README.md);
Launchpad and the web flasher then offer it. Firmwares not made by OpenGP5 are marked *Not reviewed*: they are
checked to be what they say, but nobody has read their code for you.

Want to make one? Start with [Build a firmware](../platform/).
