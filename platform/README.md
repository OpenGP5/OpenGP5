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

Start with `docs/platform/first-effect.md`, which walks through `firmwares/tuner` (a demo firmware, a strobe tuner: float
maths, the audio link between the processors, a graphic screen, storage), then copy that folder.
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
- `dsp/` — audio processor (Cortex-M4F): start-up, audio in/out (64-sample blocks, 44.1 kHz), link
  responder, output level, cycle counters, resident tick patcher.

What a firmware can use besides settings and the screen (all in `nds32/gp5_firmware.h`):

- **Storage that survives power-off and firmware updates:** `gp5_store_save(id, data, len)` /
  `gp5_store_load(id, buf, max)`, up to 1 KB per firmware, tagged with the firmware's own id. It lives in an
  8 KB area of the control processor's flash that no update writes (`0x160000`, `docs/platform/hardware.md`);
  records carry a CRC and alternate between two sectors, so a power cut while saving keeps the previous save
  (`nds32/gp5_store.c`, tested in `nds32/test/store_test.c`). The flash driver (`nds32/hal/flash.c`) refuses
  every address outside that area.
- **LED colour:** `fw_active` returns 0 (dim red), 1 (mint) or `GP5_LED(r, g, b)`.
- **Switch releases:** optional `fw_para_release` / `fw_foot_release` (the platform has empty defaults), for
  taps versus holds. The edges are not debounced; time them with `fw_knob`'s 10 ms.
- **Large, smooth text:** `gp5_text` is the platform's 5x7 font; `tools/make_font.py` turns any TrueType font
  into anti-aliased glyph tables for `gp5_blit` (keep the font's licence notice it writes).

On the DSP, code runs from flash: a large per-sample loop can be several times slower than the same code in
RAM. Put hot code in a `.data` section to run it from RAM (Dreamcatcher's `DC_HOT` macros, `dsp/dream.h`).

A complete example in its own repository: [Dreamcatcher](https://github.com/OpenGP5/Dreamcatcher) (presets,
anti-aliased text, its own LED colour, OpenGP5 as a submodule).

The control-processor build uses only our own code: start-up,
drivers (`nds32/hal/`) and USB, nothing compiled, included or linked from the vendor SDK. The image
holds only the two sections we build; flashing it leaves Valeton's other sections on the pedal as they are.
Flashing and recovery: `GETTING-STARTED.md` (public tree) / `release/GETTING-STARTED.md`.
