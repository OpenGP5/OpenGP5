---
title: Before you flash
linkTitle: Before you flash
description: Check the known risks and back up your data before a flash.
weight: 10
---

> [!WARNING]
> A flash can fail. Valeton does not make or endorse this project.

## What can go wrong

**A failed flash may need another flash.** The factory loader starts the pedal's USB update mode.
The USB update tools cannot overwrite this loader. If a flash stops or the firmware fails to start,
use the controls to enter update mode and try again. [Recovery](../recovery/) gives the steps.

**Some pedals have a DSP update fault.** The fault can block OpenGP5 and Valeton updates.
These pedals may need an FT232H USB debug board wired to pads inside the case.
You must open the pedal and may need to solder the wires. See [J7 recovery](../../platform/j7-recovery/).

As of November 2026, reports describe this fault on four of about seven pedals that tried OpenGP5.
This small sample does not show how often the fault occurs across all GP-5s. It is a fault in the pedal's
DSP updater, not evidence that an OpenGP5 flash caused it. Read [Known limits](../../platform/known-limits/)
before you decide to flash.

## Back up first

**Switching to a different firmware erases the old firmware's data.** Back up before you start the flash.
Launchpad may clear the data before it writes the new firmware, so a failed flash can still leave the data erased.

- **On OpenGP5:** use [Launchpad's Backups page](../../launchpad/backups/) to save the installed build's
  presets, settings and files. The app also offers a backup when you switch, if it can read the pedal.
- **On Valeton's firmware:** Launchpad can save the 100 presets. Export SnapTones and user IRs in Valeton Suite;
  Launchpad cannot back them up. Global settings stay on the pedal.

An update with the same firmware ID keeps its storage. Check the release notes for any changes to saved settings.
A renamed setting, for example, may return to its default.

## During a flash

- Use a USB cable you trust. Plug it into the computer.
- Keep the pedal plugged in and powered until the tool says it has restarted. A flash takes about 40 seconds.
- Keep the app or browser tab open while it writes. Launchpad asks before it closes during a flash.

To help test a pedal, see [Beta testing](../../help/beta-testing/).
