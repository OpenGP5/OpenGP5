---
title: Recovery and going back
linkTitle: Recovery
description: Get the pedal back after a failed flash, and return to Valeton's firmware.
weight: 30
---

## If a flash fails or the pedal doesn't start

The pedal's factory loader can't be overwritten over USB, so this almost always works:

1. **Unplug the pedal.**
2. **Hold the foot switch and press the PARA knob in**, and keep both held while you plug the pedal back in.
   The screen stays dark: the pedal is in update mode.
3. **Flash again,** with [any of the usual ways](../flash/): the firmware you had, another one, or Valeton's.

Launchpad and the web flasher both see a pedal in update mode and offer to flash it.

> [!NOTE]
> If the pedal stops at the start of the DSP part of **every** flash, it may have a fault in Valeton's DSP
> updater. Unplug and plug it back in: it runs what it ran before. Please
> [report it](https://github.com/OpenGP5/OpenGP5/issues).

### When USB isn't enough

In rare cases a pedal can't be recovered over USB. Getting it back then needs an FT232H USB adapter wired to the
pedal's internal debug pads, which takes some soldering experience. Ask in the
[issues](https://github.com/OpenGP5/OpenGP5/issues) first; someone may be able to help.

## Going back to Valeton's firmware

You can return to Valeton's firmware at any time.

- **With Launchpad:** on the **Flash** page, choose **Valeton's firmware**. Launchpad downloads version V1.0.6 from
  Valeton's own site, checks it, and flashes it.
- **From the command line:** download the GP-5 update from Valeton, unzip it, and flash all of it:

  ```bash
  python3 tools/gp5tool.py flash GP-5_USB_V1.0.6_20250626.bin --sections all
  ```

Valeton's own updater, Valeton Suite, is not a reliable way back. Use one of the ways above.

Valeton's firmware starts with its factory presets. If you backed up your presets before leaving it, restore them
on Launchpad's [Backups page](../../launchpad/backups/).
