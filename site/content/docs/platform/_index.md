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
    description: "Run a whole firmware on the computer: sound, screen and scripted controls."
  - target: {path: /docs/platform/params}
    title: "Parameters and presets"
    linkTitle: "Parameters"
    weight: 60
    description: "Declare settings once; the platform saves them, keeps presets and gives a computer an editor."
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
    description: "Sound on both processors."
  - target: {path: /docs/platform/performance}
    title: "Performance"
    linkTitle: "Performance"
    weight: 110
    description: "Budgets, measuring, and what's fast on each processor."
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
    description: "Memory, pins and clocks of both processors."
  - target: {path: /docs/platform/known-limits}
    title: "Known limits"
    linkTitle: "Known limits"
    weight: 170
    description: "What OpenGP5 can't do (yet)."
  - target: {path: /docs/platform/faq}
    title: "Questions"
    linkTitle: "FAQ"
    weight: 180
    description: "Common questions about using OpenGP5 and writing firmware."
---
Write your own effect in plain C: your sound, your screen, your controls. Try it on your computer first, then
flash it over USB.

## Learn

1. [Set up your computer](setup/): the compilers, a first build, a first flash.
2. [How a firmware works](concepts/): the two processors, the platform, settings and storage.
3. [Your first effect](first-effect/): a tremolo, step by step.
4. [Your own repository](template/): start from the template, built and tested by GitHub.

## Guides

| | |
|---|---|
| [The simulator](simulator/) | the whole firmware on the computer: sound, screen, scripted controls |
| [Parameters and presets](params/) | settings declared once, saved, and editable from a computer |
| [The UI kit](ui/) | a settings page, a menu, value text, messages |
| [Text and fonts](fonts/) | the built-in pixel font, smooth text, your own font |
| [Storage](storage/) | keeping settings and files on the pedal |
| [The audio link](audio-link/) | sound on both processors |
| [Performance](performance/) | budgets, measuring, what's fast where |
| [Debugging](debugging/) | the log, the load report, symptoms and causes |
| [Releasing your firmware](releasing/) | versions, release notes, getting listed |

## Reference

| | |
|---|---|
| [API reference](api/) | everything a firmware can use |
| [Protocols](protocols/) | USB-MIDI, the firmware update, the image format, the processor link |
| [Hardware](hardware/) | memory, pins and clocks of both processors |
| [Known limits](known-limits/) | what it can't do yet |
| [FAQ](faq/) | common questions |
