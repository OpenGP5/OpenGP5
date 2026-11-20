# Beta testing OpenGP5

Thank you for helping. OpenGP5 has run on only a handful of GP-5s so far, and every pedal that runs this list tells us
something, whether everything works or nothing does. It takes about an hour. Report what happened with the
[hardware report form](https://github.com/OpenGP5/OpenGP5/issues/new?template=hardware.yml).

> **Before you start:** this is alpha software. A bad flash can usually be undone over USB, but in rare cases
> recovery needs a debug adapter soldered to the pedal ([known limits](docs/platform/known-limits.md)). Only take part
> if you accept that.

## You need

- your GP-5, a USB cable, and a desktop or laptop computer;
- [OpenGP5 Launchpad](https://github.com/OpenGP5/Launchpad), or Chrome / Edge for the
  [web flasher](https://opengp5.github.io/OpenGP5/flasher/);
- if you want your Valeton setup back unchanged afterwards: export your SnapTones and user IRs in Valeton Suite
  first (OpenGP5 can't back those up). Launchpad backs up your 100 presets when you flash.

Write down what's on the pedal now: Valeton's firmware version (shown at start-up or in Valeton Suite), and the
hardware version if it's printed anywhere.

## The checklist

Tick what worked; for anything that didn't, write down what you saw.

**1. Flashing**
- [ ] Launchpad (or the web flasher) finds the pedal.
- [ ] Flash the **tremolo**. It finishes, and the pedal restarts into it by itself.
  - If it stops at the start of the DSP part ("stalled", timeout), stop here: unplug and replug the pedal (it
    runs what it ran before) and report it. Your pedal may have the DSP updater fault.

**2. The tremolo**
- [ ] The guitar comes through. The volume knob sets the level.
- [ ] Turn PARA: the highlight moves between RATE, DEPTH, SHAPE and PRESET.
- [ ] Click PARA, turn: the value changes and you hear it. Click again to stop adjusting.
- [ ] Every shape (Sine, Triangle, Square) sounds smooth, without clicks.
- [ ] The foot switch turns it off (dim red LEDs, grey values) and on (mint).
- [ ] On PRESET, hold PARA: "Saved P1" (or the preset shown).
- [ ] Unplug, plug back in: the same settings and the saved preset are still there.

**3. Updating and switching firmwares**
- [ ] Flash the tremolo again: your settings stay.
- [ ] Flash **Dreamcatcher**: it starts with its own defaults. The looper, delay and reverb respond to the controls.
- [ ] Launchpad shows Dreamcatcher's page, and changing a setting there changes the sound while you play.

**4. Recovery** (optional, but very useful to know)
- [ ] Unplug. Hold the foot switch and PARA, plug in: the screen stays dark (update mode).
- [ ] Flash any firmware from update mode: it works.

**5. Going back**
- [ ] Flash Valeton's firmware (Launchpad: *Valeton's firmware*): the pedal is as before, with your presets.

## Reporting

[Open a hardware report](https://github.com/OpenGP5/OpenGP5/issues/new?template=hardware.yml) with:
- the list above (copy it and tick what worked);
- your Valeton firmware version;
- your computer's system;
- if you have Python, the output of `python3 tools/gp5tool.py status` and `python3 tools/gp5_link.py stats`.

Questions on the way: [Discussions](https://github.com/OpenGP5/OpenGP5/discussions).
