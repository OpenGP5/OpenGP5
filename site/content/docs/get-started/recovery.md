---
title: Recovery and going back
linkTitle: Recovery
description: Get the pedal back after a failed flash, and return to Valeton's firmware.
weight: 30
---

## If a flash fails or the pedal doesn't start

The factory loader provides USB update mode. USB update tools cannot overwrite it.
Try these steps to flash a working build:

1. **Unplug the pedal.**
2. **Press and hold the foot switch and the PARA knob.** Keep both held as you plug the pedal back in, then release them.
3. **Check update mode.** Launchpad or the web flasher should find the pedal in update mode.
   With the command-line tool, run `python3 tools/gp5tool.py status`. Use the tool's status to confirm the mode;
   the screen alone is not a reliable check.
4. **[Flash again](../flash/).** Choose a build that worked before, another build or Valeton's firmware.

### When USB isn't enough

Some GP-5s have a fault in the DSP updater. The flash stops at the start of the DSP section with each tool.
Valeton Suite may stop at 0 %. If the flash has written no DSP data, unplug the pedal and plug it back in.
It should run its old build. A pedal stuck on **BOOTLOAD** may also need the steps below.

If a stalled flash leaves the pedal starting in update mode every time, you can get it running again over USB,
without J7: `gp5tool.py unstick` installs Valeton's firmware without the DSP section.
See [Pedal stuck in update mode](../../platform/j7-recovery/#pedal-stuck-in-update-mode) for the command and
what it erases. Flashes still stall after that, until the J7 fix below.

These pedals need an FT232H USB debug adapter wired to J7 inside the case to get past the fault.
[J7 recovery](../../platform/j7-recovery/) explains how to confirm the fault, flash OpenGP5, and apply the fix.
Read that guide before opening the pedal.

## Going back to Valeton's firmware

Use Launchpad or the command-line tool to flash Valeton's firmware.
If the DSP fault blocks the flash, use the J7 steps above.

- **With Launchpad:** open **Flash** and choose **Valeton's firmware**. Launchpad downloads V1.0.6 from Valeton's site.
  Read the data notice and confirm the flash. Keep the pedal plugged in until it returns.
- **From the command line:** download the GP-5 update from Valeton and unzip it. From your OpenGP5 folder,
  flash the full image, using the file's path:

  ```bash
  python3 tools/gp5tool.py flash GP-5_USB_V1.0.6_20250626.bin --sections all
  ```

Use the name of the file you downloaded. Use one of the tools above to go back.
Valeton Suite can fail when you try to return from OpenGP5.

The pedal starts with Valeton's **factory presets**. To get your own presets back:

1. Open Launchpad's [Backups page](../../launchpad/backups/) and restore the backup you made before switching.
2. Import any SnapTones and user IRs you exported in Valeton Suite.

Without those backups and exports, flashing Valeton's firmware does not restore your custom data.
