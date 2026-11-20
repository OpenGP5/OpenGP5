---
title: Live
description: Change the settings of any OpenGP5 firmware while you play.
weight: 40
---

The Live page works with **any firmware built on OpenGP5 1.0**, including ones Launchpad has never seen. The
firmware describes its own settings (names, ranges, units, choices), and Launchpad draws a control for each.

- **Turn a control:** drag it or scroll over it. The pedal changes at once.
- **Double-click** a control to put it back to its default.
- If the firmware has presets, you can **load**, **name** and **save** them here.

This is also the quickest way to try a firmware you are writing: declare its settings in `params.h`
([Parameters](../../platform/params/)) and they appear here with no extra code.
