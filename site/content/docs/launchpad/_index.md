---
title: Launchpad
description: Flash firmware, load sounds, edit presets, and back up supported data from your GP-5.
weight: 20
---

Launchpad is the desktop app for the GP-5. It runs on macOS, Windows and Linux.
Plug the pedal into USB, and Launchpad shows the tools that its firmware supports.

Use it to load Neural Amp Modeler (NAM) captures: models of how an amp or pedal sounds.
It also loads cabinet impulse responses (cab IRs) for the sound of a speaker cab.

| Page | What you can do | Works with |
|---|---|---|
| **Home** | Check the installed build and stored data | Valeton and OpenGP5 firmware |
| **[Flash](flash/)** | Install a bundled build, a catalog build, your own file or Valeton's firmware | A working pedal or a pedal in update mode |
| **[Tones](tones/)** | Load captures and cab IRs; set gate and EQ | NAM Loader |
| **[Presets](presets/)** | Edit 8 Dreamcatcher presets or 100 Valeton presets | Dreamcatcher or Valeton's firmware |
| **[Live](live/)** | Change settings as you play; load and save presets | Firmware with parameter support, added in platform 0.7 |
| **[Backups](backups/)** | Save and restore the data listed on the Backups page | Supported Valeton and OpenGP5 data |
| **[Mods](mods/)** | Apply or undo a change to Valeton's firmware | Valeton V1.0.6 |
| **[Developer](developer/)** | Read the log; check processor load and memory | Platform 0.7 or later |
| **Settings** | Choose a theme; control update checks; read recovery steps | All connections |

NAM Loader and Dreamcatcher have their own pages, so the app hides Live for those builds.
See [API versions](../platform/api/#versions) for the planned 1.0 API and the alpha builds that use it.

## Install

Download the file for your computer from
[Launchpad's releases](https://github.com/OpenGP5/Launchpad/releases/latest):

| System | File |
|---|---|
| macOS, Apple silicon | `OpenGP5 Launchpad-…-arm64.dmg` |
| macOS, Intel | `OpenGP5 Launchpad-….dmg` |
| Windows | `OpenGP5 Launchpad Setup … x64.exe`, or the x64 / ARM64 Windows ZIP |
| Linux | An `.AppImage` or `.deb`; choose `x86_64` / `amd64` for most PCs, or `arm64` for ARM |

Your system may warn you because the app has no code signature.
Use these steps to open it:

- **macOS:** open the disk image and copy the app to **Applications**. Right-click the app and choose **Open**,
  then confirm **Open**. If macOS says the app is damaged, run this in Terminal:

  ```bash
  xattr -cr "/Applications/OpenGP5 Launchpad.app"
  ```

- **Windows:** run the setup file. On **Windows protected your PC**, click **More info**, then **Run anyway**.
  If you chose a ZIP file, unzip it before you open the app.
- **Linux, AppImage:** open a terminal in the download folder. Run `chmod +x` with the file's name, then open the file.
  For example, `chmod +x "OpenGP5 Launchpad-<version>.AppImage"`; replace the name with your download's name.
- **Linux, Debian package:** open the `.deb` with your package installer, then install it.

## Connect

Plug the pedal into the computer and open Launchpad. **Home** shows which firmware it runs.

If the app cannot access MIDI, click **Restart Launchpad** in the banner.
Your MIDI service may not be ready when the app starts.

## Updates

Launchpad checks for a new release when it starts. A banner opens the download page if one is available.
Choose whether to install it. You can turn off the check in **Settings**.

## Valeton's firmware

The app does not include Valeton's firmware or data. When you need it for a flash, model names or a mod,
the app gets V1.0.6 from Valeton's site. It checks the version and keeps the file on your computer.

## Try it without a pedal

You can run Launchpad with a simulated pedal to explore the app. See
[Build from source](https://github.com/OpenGP5/Launchpad#build-from-source).
