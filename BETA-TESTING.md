# Beta testing OpenGP5

Use this checklist to test your GP-5 and report the results. Allow about an hour.
Reports of passed checks and faults both help. Send yours with the
[hardware report form](https://github.com/OpenGP5/OpenGP5/issues/new?template=hardware.yml).

> **Before you start:** OpenGP5 is alpha software. Some pedals have a DSP updater fault that blocks USB updates,
> including Valeton's updates. Those pedals may need a USB debug board wired inside the case.
> Read [Known limits](docs/platform/known-limits.md) and the backup steps before you take part.

## You need

- A GP-5, USB cable and desktop or laptop computer.
- [Launchpad](https://github.com/OpenGP5/Launchpad), or Chrome / Edge for the
  [web flasher](https://opengp5.github.io/flasher/).
- A backup of the data you want to keep. With Valeton's firmware, save the 100 presets in Launchpad.
  Export SnapTones and user IRs in Valeton Suite; Launchpad cannot back them up.

Write down the name and version of the installed build. Note any version printed on the board or case.
If Launchpad offers **Back up first**, choose it. The app does not make that choice for you.

## The checklist

Tick each check that passes. For a failure, write down the steps and what you saw.

**1. Flashing**

- [ ] Launchpad or the web flasher finds the pedal.
- [ ] Flash the **tremolo**. The flash completes and the pedal restarts into it.
  If the DSP section stalls at its start, stop testing and report the error.
  If no DSP data was written, unplugging and reconnecting lets the old build run.
  See [J7 recovery](docs/platform/j7-recovery.md) for the known updater fault.

**2. The tremolo**

- [ ] The guitar comes through and the volume knob sets output level.
- [ ] Turn PARA: the highlight moves through RATE, DEPTH, SHAPE and PRESET.
- [ ] Press PARA and turn it: the value changes and you hear it. Press again to finish editing.
- [ ] Sine, Triangle and Square shapes sound smooth, without clicks.
- [ ] The foot switch turns the effect off (dim red LEDs, grey values) and on (mint LEDs).
- [ ] On PRESET, hold PARA for 0.6 seconds. The screen confirms the saved slot, such as **Saved P1**.
- [ ] After a save completes, unplug and reconnect. The saved settings and preset return.

**3. Updating and switching firmware**

- [ ] Flash the tremolo again. Its settings stay.
- [ ] Flash **Dreamcatcher**. It starts with its own defaults. The looper, delay and reverb respond to the controls.
- [ ] Open Launchpad's **Presets** page. Changes in the live editor change the sound.
- [ ] Use **Save** to store the current sound, then reload it to check the result.

**4. Recovery** (optional)

- [ ] Unplug the pedal. Press and hold the foot switch and PARA as you reconnect it, then release both.
- [ ] Launchpad or the web flasher reports update mode.
- [ ] Flash a working build from update mode.

**5. Going back**

- [ ] Flash **Valeton's firmware** from Launchpad. The pedal starts with factory presets.
- [ ] Restore the Valeton preset backup from Launchpad's **Backups** page. Your saved presets return.
- [ ] Import exported SnapTones and user IRs in Valeton Suite, if you had any.

## Reporting

[Open a hardware report](https://github.com/OpenGP5/OpenGP5/issues/new?template=hardware.yml) and include:

- this checklist with passed and failed checks;
- your original Valeton firmware version;
- the tested effect, platform and Launchpad versions;
- your computer's operating system;
- error text and logs;
- if you have Python, output from `python3 tools/gp5tool.py status` and `python3 tools/gp5_link.py stats`.

Ask questions in [Discussions](https://github.com/OpenGP5/OpenGP5/discussions).
