---
title: Launchpad
description: The desktop app for the GP-5. Flash firmware, load captures and IRs, edit presets live, and back up the pedal.
weight: 20
---

Launchpad connects to the pedal over USB and shows the tools that fit what the pedal runs: Valeton's firmware or
any firmware built on OpenGP5. It runs on macOS, Windows and Linux.

| Page | What you do there | Works with |
|---|---|---|
| **Home** | See what the pedal runs and what's on it | everything |
| **[Flash](flash/)** | Install a firmware: one that comes with Launchpad, one from the catalog, your own build, or Valeton's | every firmware, and a pedal in update mode |
| **[Tones](tones/)** | Load NAM captures and cab IRs, pair them, set gate and EQ | NAM Loader |
| **[Presets](presets/)** | Edit Dreamcatcher's 8 presets live, or Valeton's 100 | Dreamcatcher, Valeton's firmware |
| **[Live](live/)** | Change any firmware's settings as you play | OpenGP5 1.0 firmwares |
| **[Backups](backups/)** | Save everything on the pedal to one file, and restore it | every firmware |
| **[Mods](mods/)** | Small changes to Valeton's firmware, with undo | Valeton's firmware V1.0.6 |
| **[Developer](developer/)** | The pedal's log, and how busy each processor is | OpenGP5 0.7+ firmwares |
| **Settings** | Theme, update check, recovery steps | |

## Install

Download the file for your computer from
[Launchpad's releases](https://github.com/OpenGP5/Launchpad/releases/latest):

| System | File |
|---|---|
| macOS, Apple silicon | `OpenGP5 Launchpad-…-arm64.dmg` |
| macOS, Intel | `OpenGP5 Launchpad-….dmg` |
| Windows | `OpenGP5 Launchpad Setup … x64.exe` (installer), or `…-win.zip` (x64) / `…-arm64-win.zip` (ARM64) |
| Linux | `.AppImage` or `.deb`: `x86_64` / `amd64` for most PCs, `arm64` for ARM |

Launchpad isn't signed, so the system warns you the first time you open it:

- **macOS:** right-click the app, choose **Open**, then **Open** again. If macOS says the app is damaged, run this
  once in Terminal:

  ```bash
  xattr -cr "/Applications/OpenGP5 Launchpad.app"
  ```

- **Windows:** on "Windows protected your PC", click **More info**, then **Run anyway**.
- **Linux:** make the AppImage executable (`chmod +x`), or install the `.deb`.

## Connect

Plug the pedal in and open Launchpad. It finds the pedal by itself; Home shows what it runs.

If Launchpad says MIDI access isn't available, click **Restart Launchpad** in the banner. The computer's MIDI
service is sometimes not ready when the app starts.

## Updates

Launchpad checks for a newer version when it starts, and shows a banner when there is one. Installing it is your
choice: the banner opens the download. You can turn the check off in **Settings**.

## Valeton's firmware

Nothing of Valeton's comes with Launchpad. When you need Valeton's firmware (to flash it back, for its preset
names, or for a mod), Launchpad downloads it from Valeton's own site, accepts it only if it is exactly version
V1.0.6, and keeps it on your computer.

## Try it without a pedal

Launchpad can run against a simulated pedal, for a look around: see
[Build from source](https://github.com/OpenGP5/Launchpad#build-from-source).
