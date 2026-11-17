# GP-5 open platform

The part every firmware shares. A firmware author does not edit anything here.

## Writing a firmware

A firmware is a folder with two halves, one per processor; it lives under `firmwares/` or in its own
repository (then give `platform/build_firmware.sh` and `tools/run_effect.py` its path instead of a name,
and keep OpenGP5 as a git submodule of it):

```
firmwares/<name>/dsp/firmware.h      audio: fw_init, fw_process, fw_command       (interface: platform/dsp/gp5_dsp.h)
firmwares/<name>/nds32/*.c           screen and controls: fw_draw, fw_knob, ...   (interface: platform/nds32/gp5_firmware.h)
```

Start with `docs/platform/first-effect.md`: it copies `firmwares/passthrough`, the template, and turns it into a
tremolo (float maths, settings, a text screen). `firmwares/tuner`, a strobe tuner demo, shows the rest: the
audio link between the processors, a graphic screen, storage.
Try the sound on the computer before flashing: `python3 tools/run_effect.py <name> in.wav out.wav`. The two interface headers are the documentation: what the platform calls, when, and what
a firmware may use (audio format, memory, screen, link commands to its other half).

How the halves talk: the control processor hands each changed setting to the platform (`fw_take`), which
sends it to the DSP as link command `0x7F10 [id][value]`; the DSP half receives it in `fw_command`.

## Building and flashing

```
release/setup-toolchain.sh                                  # once: the Andes compiler into fw-build/andes
platform/build_firmware.sh <name> [out.bin]                 # one flashable image with both halves
python3 tools/gp5tool.py flash <out.bin> --sections all     # over USB
```

Needs `arm-none-eabi-gcc`, Docker (the control processor is built with the Andes toolchain under
`fw-build/andes`, in a linux/386 container) and `python3`. No Valeton file is needed: the image holds only
our two sections (`dsp`, `app`); Valeton's other sections on the pedal are not touched.

## What is in here

- `nds32/` — control processor (BP1048, NDS32): start-up, display driver, foot switch, PARA knob, volume knob,
  LEDs, USB-MIDI, hand-over to the factory loader (`11 6F`), link to the DSP, start-up sync, the audio link
  (SPI slave + DMA, `gp5_link_start`).
- `dsp/` — audio processor (Cortex-M4F): start-up, audio in/out (64-sample blocks, 44.1 kHz or 48 kHz), link
  responder, output level, cycle counters, resident tick patcher.

What a firmware can use besides settings and the screen (all in `nds32/gp5_firmware.h`):

- **Storage** (`docs/platform/storage.md`): a 300 KB area of the control processor's flash that belongs to the
  installed firmware. Updating your firmware keeps it; installing a different one starts empty. Give your
  firmware a name with `fw_name()`: that's how the platform (and the computer) knows it's yours.
  - **Settings:** `gp5_store_save(id, data, len)` / `gp5_store_load(id, buf, max)`, up to 1 KB per record.
    Records carry a CRC and alternate between two sectors, so a power cut while saving keeps the previous
    save (`nds32/gp5_store.c`, tested in `nds32/test/store_test.c`).
  - **Files:** bigger things (captures, samples, IRs) the computer writes; read them straight from flash with
    `gp5_file`, `gp5_file_check`, `gp5_file_each`.
  - The flash driver (`nds32/hal/flash.c`) refuses every address outside the area.
- **Live settings from a computer:** optional `fw_param_set(id, value)` / `fw_param_get(id, &value)` let a
  computer change and read your settings while the pedal plays (USB `7E 10` / `7E 11`,
  `docs/platform/protocols.md`). The controls themselves can be read over USB too (`7E 0F`).
- **LED colour:** `fw_active` returns 0 (dim red), 1 (mint) or `GP5_LED(r, g, b)`.
- **Switch releases:** optional `fw_para_release` / `fw_foot_release` (the platform has empty defaults), for
  taps versus holds. The edges are not debounced; time them with `fw_knob`'s 10 ms.
- **Text:** `gp5_text` writes the built-in 5x7 pixel font (5 lines of 13 characters); `gp5_text_at` /
  `gp5_draw_text` (`nds32/gp5_font.h`) write smooth text in the platform's "sans" font or any font you convert
  with `tools/make_font.py`. See `docs/platform/fonts.md`.

**Speed.** The DSP runs code straight from flash; a large per-sample loop can be several times slower than
the same code in RAM, so put hot code in a `.data` section (Dreamcatcher's `DC_HOT` macros, `dsp/dream.h`).
Even from RAM it averages about 4-5 cycles per instruction (measured: 32 cycles for a trivial loop step; its
small and large RAM are equally fast), so budget per-sample work carefully. The control processor (288 MHz,
with its own float unit) ran the same float code about 1.7 times faster: heavy firmwares can run their sound
there over the audio link (`gp5_link_start`; `#define GP5_LINK_WORDS` sets the words per block) and use
`gp5_math.h` on both processors.

A complete example in its own repository: [Dreamcatcher](https://github.com/OpenGP5/Dreamcatcher) (its sound
on the control processor and a reverb on the DSP, presets, live settings, smooth text, its own LED colour,
OpenGP5 as a submodule).

The control-processor build uses only our own code: start-up,
drivers (`nds32/hal/`) and USB, nothing compiled, included or linked from the vendor SDK. The image
holds only the two sections we build; flashing it leaves Valeton's other sections on the pedal as they are.
Flashing and recovery: `GETTING-STARTED.md` (public tree) / `release/GETTING-STARTED.md`.
