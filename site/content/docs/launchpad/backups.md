---
title: Backups
description: Save everything on the pedal to one file, and put it back.
weight: 50
---

A backup is one `.gp5backup` file with the installed firmware's data:

| Firmware | A backup holds |
|---|---|
| NAM Loader | your captures, IRs, and each capture's IR, gate and EQ |
| Dreamcatcher | its presets |
| Any OpenGP5 1.0 firmware | its settings, presets and files |
| Valeton's firmware | the 100 presets. **Not** SnapTones or user IRs: export those in Valeton Suite |

## Back up

On **Backups**, under **What's on the pedal**, click **Back up now**, and save the file. Launchpad also lists the
backups made on this computer under **Backups from this computer**.

## Restore

Pick a backup from the list (or **Open a file**), tick the parts you want back, and click **Restore**. A backup restores onto the firmware it was
made from: if the pedal runs something else, [flash](../flash/) that firmware first.

## Before switching firmwares

Switching to a different firmware wipes the old one's data. When you flash, Launchpad offers a backup first. Take
it.
