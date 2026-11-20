---
title: Before you flash
linkTitle: Before you flash
description: What can go wrong, how likely it is, and what to back up first.
weight: 10
---

> [!WARNING]
> Flashing your own firmware is at your own risk. OpenGP5 is not affiliated with or endorsed by Valeton.

## What can go wrong

**Usually nothing that can't be undone.** The pedal has a factory loader that can't be overwritten over USB. If a
flash is interrupted or a firmware doesn't start, you can put the pedal into update mode with the controls and
flash again. [Recovery](../recovery/) shows how.

**Rarely, USB recovery isn't enough.** For example, some pedals may have a fault in Valeton's own DSP updater.
Getting those back needs an FT232H USB adapter wired to debug pads inside the pedal, which takes some soldering
experience. Don't flash unless you're comfortable with that small risk.

OpenGP5 has run on several GP-5s so far, with no problem reported. More reports help; see
[Known limits](../../platform/known-limits/) and [Beta testing](../../help/beta-testing/).

## Back up first

**Installing a different firmware wipes the old one's data.** Each firmware has its own storage on the pedal: its
presets, captures, IRs and settings. When you switch firmwares, the new one starts empty.

- **On an OpenGP5 firmware** (NAM Loader, Dreamcatcher, …): back up with [Launchpad's Backups page](../../launchpad/backups/).
  Launchpad also offers a backup before every flash that would wipe something.
- **On Valeton's firmware:** Launchpad backs up the 100 presets. It can't back up SnapTones or user IRs: export
  those in Valeton Suite first.

Updating a firmware to a newer version of the same firmware keeps its data.

## During a flash

- Use a USB cable you trust, plugged straight into the computer.
- Keep the pedal connected and powered until it restarts by itself (about 40 seconds).
- Don't close the app or the browser tab while it writes. Launchpad asks before it closes during a flash.
