---
title: Mods
description: Apply or undo a patch to Valeton V1.0.6 firmware.
weight: 60
---

A mod changes part of Valeton's **V1.0.6** firmware. Launchpad downloads a copy from Valeton,
checks the bytes the mod will change, and flashes the result.

## Install or undo a mod

1. With Valeton V1.0.6 on the pedal, open **Mods**.
2. Read the mod's description and click **Install**.
3. Let Launchpad download and check Valeton's firmware, then read and confirm the change.
4. Keep the pedal connected until the flash finishes.

To undo the mods, click **Restore Valeton's original** under **Undo mods**.
When the pedal runs Valeton's firmware, a mod or undo keeps its presets.
If you switch from OpenGP5, [back up first](../backups/). That switch clears the old data.

## USB audio interface, no direct monitoring

This mod sends the sound from your recording app to the pedal's outputs.
Your app must send the guitar sound back over USB for you to hear it from those outputs.
If the app sends no sound back, the outputs stay silent.

The USB recording channels stay the same:

| Channels | Signal sent to the computer |
|---|---|
| 1 and 2 | Dry guitar |
| 3 and 4 | The processed preset |

Undo the mod to use the pedal on its own.

## Load a mod file

Click **Open a mod file** and choose a `.gp5mod` file. Read its description, then click **Install**.
Launchpad checks the original bytes before it applies each change. Those checks confirm that the patch fits;
they do not review what the mod does.
