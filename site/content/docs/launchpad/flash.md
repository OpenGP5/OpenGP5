---
title: Flash
description: Install a firmware on the pedal.
weight: 10
---

1. Plug the pedal in. It can run any firmware, or be in update mode.
2. Under **Choose a firmware**, pick one:
   - **The bundled firmwares** come with Launchpad: NAM Loader and Dreamcatcher, at the versions this Launchpad
     was tested with.
   - **More from the catalog** lists firmwares from [OpenGP5's catalog](../../platform/releasing/).
     Launchpad downloads the one you pick and checks it against the catalog. Firmwares not made by OpenGP5 are
     marked *Not reviewed*.
   - **Your own file:** drop a `.bin` on the page, or choose one.
   - **Valeton's firmware:** V1.0.6, downloaded from Valeton's site.
3. Read what it says about the pedal's data, tick the box, and click **Flash firmware**.

The pedal restarts into update mode by itself, the new firmware is written, and the pedal restarts again (about
40 seconds). Keep it plugged in until Launchpad says it's back.

## What's kept

- **The same firmware, a newer version:** its presets, captures and settings stay.
- **A different firmware:** the old one's data is wiped. Launchpad offers a [backup](../backups/) first.
- **Leaving Valeton's firmware:** Launchpad reminds you to export SnapTones and user IRs in Valeton Suite;
  it can back up only the 100 presets.

Every image is checked before anything is written: that it's complete, that it's for the GP-5, and what it is.

If a flash fails, see [Recovery](../../get-started/recovery/).
