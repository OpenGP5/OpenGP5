---
title: Mods
description: Small changes to Valeton's firmware, applied to your own copy, with undo.
weight: 60
---

Mods change one small thing in Valeton's firmware, V1.0.6. Launchpad applies them to your own copy of the
firmware (downloaded from Valeton), checks every byte it changes, and flashes the result. **Undo mods** puts
Valeton's original firmware back; your presets stay.

## Available mods

**USB audio interface, no direct monitoring.** The pedal's outputs play only the audio coming back from the
computer over USB, not the guitar directly. Useful when you record through the pedal and monitor through your
software. USB recording doesn't change (channels 1 and 2: the dry guitar, 3 and 4: the preset). With no audio
coming back, the outputs are silent: undo the mod to use the pedal on its own.

## Mod files

A mod can also come as a `.gp5mod` file: **Open a mod file** on the Mods page. Launchpad checks that it fits your
firmware byte for byte before it applies anything.
