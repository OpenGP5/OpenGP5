---
title: Flash
description: Choose a firmware image, back up your data, and flash the pedal.
weight: 10
---

Read [Before you flash](../../get-started/safety/) and back up your data before you switch firmware.

1. Plug the pedal into your computer and open **Flash**. You can also start with the pedal in update mode.
2. Under **Choose a firmware**, choose one of the sources below.
3. Read the notice about stored data. Tick the box that asks you to keep the pedal connected and accept the risk.
4. Click **Flash** followed by the selected build's name. If Launchpad offers **Back up first**, choose it to save your data.
5. Keep the pedal plugged in until Launchpad confirms that it has returned. A flash takes about 40 seconds.

Launchpad starts update mode, writes the image, and waits for the pedal to restart.

## Choose a source

| Source | What it provides |
|---|---|
| **Bundled firmware** | NAM Loader and Dreamcatcher builds included with this Launchpad release |
| **More from the catalog** | Builds listed in [OpenGP5's catalog](../../platform/releasing/) |
| **Your own file** | A `.bin` file you drop on the page or choose from your computer |
| **Valeton's firmware** | V1.0.6 from Valeton's site |

Before it writes, Launchpad checks that the file is whole and made for the GP-5.
For a catalog file, it checks the size, checksum and built-in details against the listing.

Other authors' builds carry the label *Not reviewed*. The file checks do not include a review of the code.

## What's kept

- **An update with the same firmware ID** keeps its storage. Read the release notes for changes to saved settings.
- **A different firmware** starts with empty storage. Launchpad offers a [backup](../backups/) if it can read the installed build.
  It may erase the old data before the flash starts.
- **Leaving Valeton's firmware:** export SnapTones and user IRs in Valeton Suite. Launchpad can back up the 100 presets.
- **Going back to Valeton:** the pedal starts with factory presets. Load your preset backup, then import the files you exported.

If a flash fails, see [Recovery](../../get-started/recovery/).
