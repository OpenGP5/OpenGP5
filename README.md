# OpenGP5

An open firmware platform for the Valeton GP-5.

> **Beta.** It works on the pedal it was built on; we need reports from other GP-5s (see "Known limits" in
> GETTING-STARTED.md). Interfaces may still change.

Open firmware for the **Valeton GP-5** guitar pedal, written from scratch for both of its processors, and a
small framework for writing your own: your audio code, your screen, your controls, flashed over USB.

> Not affiliated with or endorsed by Valeton. Flashing your own firmware is at your own risk.
> Recovery is always possible over USB (see below), and Valeton's own firmware can be put back the same way.

## What you get

- **A platform** (`platform/`) that runs on both chips of the pedal:
  - the **control processor** (MVsilicon BP1048, Andes NDS32): start-up, drivers for every part we use,
    USB-MIDI, the colour screen, foot switch, PARA knob, big knob, LEDs, link to the DSP;
  - the **DSP** (ARM Cortex-M4F): start-up, audio in/out, link to the control processor.
  None of it comes from the chip vendor or from Valeton: no vendor SDK code, headers or libraries, no
  Valeton code or data. It was written from what the hardware does, observed on our own pedal.
- **Firmwares** (`firmwares/`) built on it:
  - `tremolo`: the worked example, about 130 lines; start here.
  - `passthrough`: the smallest one. Guitar in, guitar out, big knob = volume.
  - `granular-delay`: a granular delay / micro-looper with 13 settings and its own screen.
- **Tools to work without the pedal**: `tools/run_effect.py` runs your audio code on a WAV file,
  `tools/dsp_emu_check.py` boots the built DSP firmware in an emulator.
- **A flasher** (`tools/gp5tool.py`): flashes your build, or Valeton's own firmware, over USB-MIDI on
  macOS, Linux and Windows. No debug probe, no Valeton software.

## Quick start

See [GETTING-STARTED.md](GETTING-STARTED.md): set up the two compilers, build `passthrough`, flash it,
and put Valeton's firmware back when you want to.

## Writing a firmware

Read `docs/platform/first-effect.md` and copy `firmwares/tremolo`. The two interface headers are the
reference: `platform/dsp/gp5_dsp.h` (audio half) and `platform/nds32/gp5_firmware.h` (controls and screen).
Audio code is plain C with `float` maths on the DSP's floating-point unit; maths functions come from
`platform/dsp/gp5_math.h`.

## Status

Tested on one GP-5 (hardware `GP-5_MB_V01_241115`). See "Known limits" in GETTING-STARTED.md before you
flash.

## Documentation

- `GETTING-STARTED.md`: set up, build, flash, recover.
- `docs/platform/first-effect.md`: your first effect, step by step.
- `platform/README.md`: how a firmware plugs into the platform.
- `docs/platform/protocols.md`: USB-MIDI, firmware update, image format, the link between the processors.
- `docs/platform/hardware.md`: memory, pins, clocks of both processors.

## Licence

MIT, see `LICENSE`. "Valeton" and "GP-5" are Valeton's trademarks, used here only to say which pedal this
runs on.
