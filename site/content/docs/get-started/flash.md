---
title: Flash a firmware
linkTitle: Flash a firmware
description: Three ways to put a firmware on the pedal, from the easiest to the most flexible.
weight: 20
---

Every way below checks the whole image before it writes anything, restarts the pedal into update mode by itself,
writes, and waits for the pedal to come back. Read [Before you flash](../safety/) first.

## With Launchpad

[Launchpad](../../launchpad/) is the easiest way, and the one to use if you'll load captures or edit presets.

1. [Install Launchpad](../../launchpad/#install) and open it.
2. Plug the pedal in. Launchpad shows what it runs.
3. Open **Flash**, choose a firmware (or **Your own file** for a `.bin`), tick the box, and click **Flash firmware**.

More on the [Flash page](../../launchpad/flash/).

## In your browser

The [web flasher](https://opengp5.github.io/flasher/) needs no installation.

1. Open it in **Chrome, Edge or Opera** on a computer. Safari can't talk to MIDI devices; Firefox asks for a
   permission first.
2. Plug the pedal in, click **Connect**, and allow MIDI access.
3. Pick a firmware (the tremolo or Dreamcatcher), or upload a `.bin` file: one you built, or Valeton's firmware.
4. Tick the box to confirm, and click **Flash**.

## From the command line

Use this for your own builds, or to script it.

1. Download OpenGP5 (**Code › Download ZIP** on [GitHub](https://github.com/OpenGP5/OpenGP5), or a release's
   source zip) and unzip it. It contains the flasher, `tools/gp5tool.py`.
2. Install Python 3.9 or newer, then the MIDI libraries:

   ```bash
   pip install mido python-rtmidi
   ```

   On Windows, use Windows' own Python, not WSL: WSL can't see USB-MIDI devices.
3. Get a firmware image: a firmware's own release (for example
   [Dreamcatcher's](https://github.com/OpenGP5/Dreamcatcher/releases)), or [build one](../../platform/setup/).
4. Plug the pedal in, then from the unzipped folder:

   ```bash
   python3 tools/gp5tool.py status
   python3 tools/gp5tool.py flash GP-5_dreamcatcher.bin --sections all
   ```

   `status` checks that the pedal is found.

### Checking a download

Releases list each file's checksum in `SHA256SUMS`. To compare:

| System | Command |
|---|---|
| macOS, Linux | `shasum -a 256 GP-5_dreamcatcher.bin` |
| Windows | `certutil -hashfile GP-5_dreamcatcher.bin SHA256` |

## What gets written

A firmware image holds only the two parts OpenGP5 replaces: the DSP's firmware and the control processor's app.
The rest of the pedal's flash, including the factory loader, is left alone.
