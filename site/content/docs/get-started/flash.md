---
title: Flash a firmware
linkTitle: Flash a firmware
description: Install firmware with Launchpad, a browser, or the command line.
weight: 20
---

Each tool checks the file before it writes to the pedal. It then starts update mode, writes the firmware,
and waits for the pedal to restart. Read [Before you flash](../safety/) and back up your data.

## With Launchpad

Use [Launchpad](../../launchpad/) if you also want to load captures, edit presets or make backups.

1. [Install Launchpad](../../launchpad/#install) and open it.
2. Plug the pedal into the computer. Launchpad shows which firmware it runs.
3. Open **Flash** and choose a build. Use **Your own file** for a `.bin` file.
4. Read the data-loss notice. Tick the box that asks you to keep the pedal connected and accept the risk.
5. Click the **Flash** button for the build you chose. If Launchpad offers **Back up first**, choose it to save your data.

See the [Flash page](../../launchpad/flash/) for the choices and data rules.

## In your browser

The [web flasher](https://opengp5.github.io/flasher/) needs no app installation.

1. Open it in **Chrome, Edge or Opera** on a computer. These browsers support Web MIDI, which the flasher uses.
2. Plug the pedal in, click **Connect**, and allow MIDI access.
3. Choose an available build, or upload a `.bin` file from a release or your own build. You can also upload Valeton's firmware.
4. Read the confirmation beside the checkbox, tick it, and click **Flash**.

Safari lacks Web MIDI. Firefox asks for permission; use one of the browsers listed above for this guide.

## From the command line

Use the command-line tool for scripts or your own builds.

1. Download OpenGP5 from [GitHub](https://github.com/OpenGP5/OpenGP5) with **Code › Download ZIP**, or download a release's source ZIP.
   Unzip it to get `tools/gp5tool.py`.
2. Install Python 3.9 or later, then install the MIDI libraries:

   ```bash
   pip install mido python-rtmidi
   ```

   On Windows, flash with Windows Python. WSL, the Linux shell on Windows, cannot see the pedal's USB-MIDI ports.
3. Download an image from a release, such as [Dreamcatcher's releases](https://github.com/OpenGP5/Dreamcatcher/releases),
   or [build one](../../platform/setup/). Put the `.bin` in the unzipped OpenGP5 folder.
4. Plug the pedal in. Open a terminal in that folder, then run:

   ```bash
   python3 tools/gp5tool.py status
   python3 tools/gp5tool.py flash GP-5_dreamcatcher.bin --sections all
   ```

   `status` checks whether the tool can find the pedal. Use your image's filename in the flash command.
   On Windows, use `py -3` in place of `python3` if that is how you start Python.

### Checking a download

A checksum helps you check that a download matches the release file. Releases list checksums in `SHA256SUMS`.
Run the command for your system and compare its result with the entry for your file:

| System | Command |
|---|---|
| macOS, Linux | `shasum -a 256 GP-5_dreamcatcher.bin` |
| Windows | `certutil -hashfile GP-5_dreamcatcher.bin SHA256` |

## What gets written

An OpenGP5 image holds code for two chips. The digital signal processor (DSP) handles sound.
The control chip runs the screen, controls and USB.

The flash tool keeps the factory loader in place. A full Valeton image also holds its models and presets.
