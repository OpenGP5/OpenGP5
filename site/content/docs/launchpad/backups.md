---
title: Backups
description: Save supported pedal data to a file, then restore it to the same firmware.
weight: 50
---

A backup is one `.gp5backup` file with the installed build's supported data.
It does not include a firmware installer. Check the table to see what it holds:

| Firmware | A backup holds |
|---|---|
| NAM Loader | Captures, cab IRs, IR choices, gate and EQ settings |
| Dreamcatcher | Presets |
| Firmware with the parameter API, added in platform 0.7 | Saved settings, presets and files |
| Valeton's firmware | The 100 presets |

Launchpad cannot back up Valeton SnapTones or user IRs. Export those in Valeton Suite before you switch firmware.
Valeton's global settings stay on the pedal.

## Back up

1. Open **Backups**.
2. Click **Back up now** and save the file.
3. Check **Backups from this computer** to find saved backups.

## Restore

1. Install the firmware that the backup came from. If the pedal runs a different one, [flash it](../flash/) first.
2. Select a backup in the list, or click **Open a file**.
3. Tick the parts you want to restore, then click **Restore** and confirm.
4. Restart the pedal if Launchpad asks you to do so.

Restoring replaces the selected data on the pedal. Launchpad checks that the backup belongs to the installed build.

## Before switching firmware

A different firmware starts with empty storage. If Launchpad can read the pedal, it offers **Back up first**
when you switch. Choose that option before you start the flash if you want to keep the old data.
