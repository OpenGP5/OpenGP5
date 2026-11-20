# How an OpenGP5 firmware works

The big picture, before the details. Read this first if the pedal's insides are new to you.

## Two processors

The GP-5 has two processors, and a firmware has a part for each.

| | the DSP | the control processor |
|---|---|---|
| chip | Arm Cortex-M4F, 192 MHz | Andes NDS32 (BP1048), 288 or 320 MHz |
| does | the audio: guitar in, two outputs | the screen, knobs, foot switch, LEDs, USB, storage |
| your code | `dsp/firmware.h` | `nds32/*.c` |
| runs | `fw_process()` every 1.45 ms, for 64 samples | a main loop that calls your callbacks; `fw_tick()` every 10 ms |
| memory (the firmware's, promised for 1.x) | 19,712 bytes for variables, 122,880 for big buffers | 258,016 bytes of RAM for the firmware's data (code runs from flash) |

The DSP is where sound happens. The control processor is the "brain" around it, and it can do sound work too
(below).

## The platform

OpenGP5 is the code both processors share: start-up, drivers, USB, the screen, the link between the processors,
storage. You never edit it. Your firmware is a few callbacks and calls into it:

```
                 ┌─────────── control processor ───────────┐        ┌──────────── DSP ────────────┐
  knobs, foot ──►│ platform ──► fw_para_turn, fw_foot_down │        │ platform ──► fw_process      │◄── guitar
  screen     ◄───│ gp5_blit ◄── fw_draw                    │        │ fw_param ◄── (parameters)    │──► out L / R
  USB        ◄──►│ parameters, presets, storage, log ──────┼─ link ─┼─► gp5_param()                │
                 │ on_block (optional) ◄───────────────────┼─ audio ┼─► gp5_link_in / out          │
                 └─────────────────────────────────────────┘  link  └──────────────────────────────┘
```

**Your callbacks run when the platform calls them, and must return quickly.** Nothing in a firmware waits. Even
the screen is drawn a strip at a time (`fw_draw` returns "more to do"), so the controls and USB stay responsive.

## Settings: parameters

A firmware's settings are **parameters**, declared once in `params.h`. The platform takes care of the rest:

- it sends each change to the DSP (`fw_param()`);
- it keeps them across power-off, and keeps presets;
- it lets a computer list, read and change them.

Launchpad's Live page edits them, with no code of its own for your firmware. See [parameters](params.md).

## Sound on both processors: the audio link

The two processors exchange a block of words after every audio block: the **audio link**. A firmware can send the
guitar to the control processor, process it there, and play the result on the DSP, 3 blocks (about 4.4 ms) later.
NAM Loader and Dreamcatcher do this: some sound code is faster on the control processor, and it has more memory. See
[the audio link](audio-link.md).

## Storage

The pedal's storage (about 300 KB) belongs to the firmware that is installed. The firmware's `id` (in
`firmware.json`) says whose it is:
- an update to the same firmware keeps it;
- installing another firmware starts empty;
- going back to Valeton's firmware gives Valeton's presets back.

Parameters are stored there automatically. For anything else there are store records (up to 1 KB each) and files
(bigger things that a computer writes, such as captures). See [storage](storage.md).

## What happens at power-on

1. The control processor starts the platform, checks who owns the storage, loads saved parameters, and calls your
   `fw_init()`.
2. It starts the DSP. The DSP starts the platform there and calls its `fw_init()`. The DSP has your `params.h`
   defaults until the control processor sends the saved values, a moment later.
3. From then on: the DSP runs `fw_process()` for every block. The control processor runs its main loop: your
   callbacks, parameter changes to the DSP, saving, USB, and the screen (`fw_draw`).

## From code to pedal

```
firmware.json + params.h + dsp/firmware.h + nds32/*.c
        │  tools/simulate.py       on the computer: both processors, the screen, the sound
        │  platform/build_firmware.sh
        ▼
build/GP-5_<id>.bin   (both parts and a description of itself: name, version, author)
        │  Launchpad, the web flasher, or gp5tool.py flash
        ▼
the pedal (Foot + PARA at power-on brings back update mode if something goes wrong)
```

Next: [your first effect](first-effect.md), or the [firmware template](https://github.com/OpenGP5/firmware-template)
for a repository of your own.
