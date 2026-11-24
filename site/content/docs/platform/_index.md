---
title: Build a firmware
linkTitle: Build a firmware
weight: 40
description: >-
  Write your own effect for the GP-5 in plain C, try it on the computer, and flash it.
cascade:
  - target: {path: /docs/platform/setup}
    title: "Set up your computer"
    linkTitle: "Set up"
    weight: 10
    description: "Install the compilers and tools, build a firmware, flash it."
  - target: {path: /docs/platform/concepts}
    title: "How a firmware works"
    linkTitle: "How it works"
    weight: 20
    description: "The two processors, the platform, settings, storage, and what happens at power-on."
  - target: {path: /docs/platform/first-effect}
    title: "Your first effect"
    linkTitle: "Your first effect"
    weight: 30
    description: "Build a tremolo step by step: settings, sound, screen and controls."
  - target: {path: /docs/platform/template}
    title: "Your own repository"
    linkTitle: "Your own repository"
    weight: 40
    description: "Start a firmware from the template: its own repository, built and tested on every push."
  - target: {path: /docs/platform/simulator}
    title: "The simulator"
    linkTitle: "Simulator"
    weight: 50
    description: "Run both halves on your computer and save sound and screen files."
  - target: {path: /docs/platform/params}
    title: "Parameters and presets"
    linkTitle: "Parameters"
    weight: 60
    description: "Define settings, choose save flags, and use them in presets and Launchpad."
  - target: {path: /docs/platform/ui}
    title: "The UI kit"
    linkTitle: "UI kit"
    weight: 70
    description: "A settings page, a menu, value text, messages and knob pickup."
  - target: {path: /docs/platform/fonts}
    title: "Text and fonts"
    linkTitle: "Fonts"
    weight: 80
    description: "The built-in pixel font, smooth text in any font, and making your own."
  - target: {path: /docs/platform/storage}
    title: "Storage"
    linkTitle: "Storage"
    weight: 90
    description: "Keeping settings and files on the pedal."
  - target: {path: /docs/platform/audio-link}
    title: "The audio link"
    linkTitle: "Audio link"
    weight: 100
    description: "Sound on both chips."
  - target: {path: /docs/platform/performance}
    title: "Performance"
    linkTitle: "Performance"
    weight: 110
    description: "Measure load and keep sound code within each block's time budget."
  - target: {path: /docs/platform/debugging}
    title: "Debugging"
    linkTitle: "Debugging"
    weight: 120
    description: "The log, the load report, symptoms and their causes."
  - target: {path: /docs/platform/releasing}
    title: "Releasing your firmware"
    linkTitle: "Releasing"
    weight: 130
    description: "Versions, release notes, and getting listed in Launchpad and the web flasher."
  - target: {path: /docs/platform/api}
    title: "API reference"
    linkTitle: "API reference"
    weight: 140
    description: "Everything a firmware can use: manifest, callbacks, platform calls, log, load."
  - target: {path: /docs/platform/protocols}
    title: "Protocols"
    linkTitle: "Protocols"
    weight: 150
    description: "USB-MIDI, the firmware update, the image format, and the link between the processors."
  - target: {path: /docs/platform/hardware}
    title: "Hardware"
    linkTitle: "Hardware"
    weight: 160
    description: "Memory, pins and clocks of both chips."
  - target: {path: /docs/platform/known-limits}
    title: "Known limits"
    linkTitle: "Known limits"
    weight: 170
    description: "What OpenGP5 can't do (yet)."
  - target: {path: /docs/platform/j7-recovery}
    title: "J7 recovery"
    linkTitle: "J7 recovery"
    weight: 175
    description: "Recovering a pedal whose DSP updater stalls, with an FT232H on the debug header J7."
  - target: {path: /docs/platform/dsp-updater-fault}
    title: "DSP updater fault: help wanted"
    linkTitle: "Updater fault: help wanted"
    weight: 176
    description: "What causes the DSP update stall, and leads for a fix that doesn't need J7."
  - target: {path: /docs/platform/palette}
    title: "Colour palette"
    linkTitle: "Colour palette"
    weight: 178
    description: "OpenGP5's colours for the website, Launchpad and firmware pages."
  - target: {path: /docs/platform/faq}
    title: "Questions"
    linkTitle: "FAQ"
    weight: 180
    description: "Common questions about using OpenGP5 and writing firmware."
---


Write an effect in C, test it on your computer, then flash it over USB.
You need basic C knowledge and the tools in the setup guide. You can start without a pedal.

These guides target **platform 0.8.0-alpha** and the planned **1.0 API** introduced in 0.7.
See [API versions](api/#versions) for what each number means.

## Learn

1. [Set up your computer](setup/): install tools, build the tremolo, and flash it.
2. [How a firmware works](concepts/): learn the two processors, shared code and storage.
3. [Your first effect](first-effect/): build a tremolo step by step.
4. [Your own repository](template/): use the template and its build and test workflow.

## Guides

| Guide | Task |
|---|---|
| [The simulator](simulator/) | Run code on your computer; save sound and screen files |
| [Parameters and presets](params/) | Define settings and choose which values to save |
| [The UI kit](ui/) | Add a settings page, menu and value text |
| [Text and fonts](fonts/) | Draw text or convert a font |
| [Storage](storage/) | Keep records and files on the pedal |
| [The audio link](audio-link/) | Share sound work between the two processors |
| [Performance](performance/) | Measure load and stay within the time budget |
| [Debugging](debugging/) | Find faults with checks, logs and load reports |
| [Releasing your firmware](releasing/) | Publish a build and list it in the catalog |

## Reference

| Reference | Contents |
|---|---|
| [API reference](api/) | Public calls, callbacks and manifest fields |
| [Protocols](protocols/) | USB messages, image files and the processor link |
| [Hardware](hardware/) | Memory, pins and clocks |
| [Known limits](known-limits/) | Faults, tested limits and missing features |
| [J7 recovery](j7-recovery/) | Steps for the confirmed DSP update fault |
| [DSP updater fault: help wanted](dsp-updater-fault/) | The cause, the fix, and open leads for a USB-only route |
| [Colour palette](palette/) | OpenGP5's colours |
| [FAQ](faq/) | Questions about using OpenGP5 and writing effects |
