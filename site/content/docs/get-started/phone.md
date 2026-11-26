---
title: Control the pedal from your phone
linkTitle: Phone remote
description: Change a firmware's settings and switch its presets from a phone, over Bluetooth, in the browser.
weight: 25
---

The [phone remote](https://opengp5.github.io/remote/) is a web page: nothing to install. It connects to the pedal
over Bluetooth and shows what the running firmware describes: its name, its settings by group, and its presets.
Changes play at once; turning the pedal's own knob shows up on the phone a moment later.

It needs a firmware built with Bluetooth on (`"bluetooth": true` in its `firmware.json`, OpenGP5 1.0 or later),
such as Dreamcatcher.

## Which browser

| Phone or computer | Use |
|---|---|
| iPhone, iPad | a free Web Bluetooth browser from the App Store, such as **Bluefy** or **BLE Link**. Safari can't connect to Bluetooth devices. |
| Android | **Chrome** |
| Mac, Windows, Linux | **Chrome** or **Edge** |

## Connect

1. Plug the pedal in and switch it on.
2. The pedal takes one Bluetooth connection at a time. If a computer is connected to it (Launchpad, or the
   computer's Bluetooth settings), disconnect it there first.
3. Open the [phone remote](https://opengp5.github.io/remote/), tap **Connect** and pick **OpenGP5** (**GP-5** on firmwares built before OpenGP5 1.0).

The pedal doesn't appear in the phone's Bluetooth settings: phones list Bluetooth MIDI devices only inside the apps
that use them. Connect from the page.

If the pedal doesn't show up, something else is probably still connected to it: phones reconnect to a Bluetooth
device in the background after an app has used it. Turn Bluetooth off on the other phone or computer.

## Presets

**Load** switches to a preset. **Save** writes the current sound into a slot on the pedal; the page asks first.
Firmwares that keep preset names can be renamed from the pencil button.

To look around without a pedal, open the page with `?simulate` at the end of its address: it then talks to a
simulated Dreamcatcher.
