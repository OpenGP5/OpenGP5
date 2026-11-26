---
title: Live
description: Change settings and save presets in firmware that supports the parameter API.
weight: 40
---

Live shows settings that an OpenGP5 build describes through its parameter API, added in **platform 0.7**.
It also works with builds that Launchpad has no built-in editor for. The firmware supplies each setting's name,
range, unit and choices. Launchpad uses that list to show controls.

NAM Loader uses [Tones](../tones/) and Dreamcatcher uses [Presets](../presets/), so Launchpad hides Live for those builds.

## Change a setting

- Drag a knob or scroll over it. The sound changes as you play.
- Double-click a knob to return it to its default value.
- Click a choice button to select that value.

The firmware decides which settings to keep after power-off. Settings marked for saving persist about two seconds
after the last change. Other settings return to their defaults at power-on.

## Load and save presets

If the firmware defines presets, Live shows **Load**, **Save** and name fields.
**Load** recalls a saved sound. **Save** stores the current sound in that slot and replaces any sound it held.
An edit to the live sound does not replace the saved preset until you click **Save**.
Editing a name saves the new name on the pedal.

## For firmware authors

Declare settings in `params.h` to show them here. See [Parameters and presets](../../platform/params/)
for the save flags and [API versions](../../platform/api/#versions) for the 1.0 API.
