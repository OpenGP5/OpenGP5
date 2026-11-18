# GP-5 open platform

The part every firmware shares. A firmware author does not edit anything here.

## Writing a firmware

A firmware is a folder; it lives under `firmwares/` or in its own repository (then give
`platform/build_firmware.sh` and `tools/run_effect.py` its path instead of a name, and keep OpenGP5 as a git
submodule of it):

```
firmwares/<name>/firmware.json       who it is, what it needs                   (docs/platform/api.md)
firmwares/<name>/params.h            its settings, declared once (optional)     (docs/platform/params.md)
firmwares/<name>/dsp/firmware.h      audio: fw_init, fw_process, fw_param        (platform/dsp/gp5_dsp.h)
firmwares/<name>/nds32/*.c           screen and controls: fw_draw, fw_foot_down, fw_para_turn, ...
                                                                                 (platform/nds32/gp5_firmware.h)
```

Start with `docs/platform/first-effect.md`: it copies `firmwares/passthrough`, the template, and turns it into a
tremolo. `firmwares/tremolo` is that tremolo grown further: four presets, a screen in the smooth font, PARA click and
hold, and a screen preview on the computer. `docs/platform/api.md` lists everything a firmware can use; the
interface headers have the details.

The halves share their settings through parameters: the platform sends each change to the DSP (`fw_param`), keeps
them across power-off, keeps presets, and lets a computer read and set them (Launchpad's Live page,
`tools/gp5_link.py params`). Try the sound on the computer before flashing:
`python3 tools/run_effect.py <name> in.wav out.wav --set Rate=400`.

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
  installed firmware. Updating your firmware keeps it; installing a different one starts empty. The `id` in
  `firmware.json` is how the platform (and the computer) knows it's yours. Parameters and presets are kept there
  for you; the calls below are for anything else.
  - **Settings:** `gp5_store_save(id, data, len)` / `gp5_store_load(id, buf, max)`, up to 1 KB per record.
    Records carry a CRC and alternate between two sectors, so a power cut while saving keeps the previous
    save (`nds32/gp5_store.c`, tested in `nds32/test/store_test.c`).
  - **Files:** bigger things (captures, samples, IRs) the computer writes; read them straight from flash with
    `gp5_file`, `gp5_file_check`, `gp5_file_each`.
  - The flash driver (`nds32/hal/flash.c`) refuses every address outside the area.
- **Live settings from a computer:** every parameter (`params.h`) can be read and set over USB while the pedal
  plays (`7E 10` / `7E 11` / `7E 12`, `docs/platform/protocols.md`); `fw_param_changed` hears of it. The controls
  themselves can be read over USB too (`7E 0F`).
- **LED colour:** `gp5_led(GP5_LED_ON)` (mint, the default), `GP5_LED_BYPASS` (dim red) or `GP5_LED_RGB(r, g, b)`.
  The LEDs show full red while the main loop is stuck for more than 2 s (your colour again once it runs), so a `fw_`
  function that waits shows up.
- **Switches:** `fw_foot_down` / `fw_foot_up`, `fw_para_down` / `fw_para_up`, and `fw_para_click` / `fw_para_hold`
  (0.6 s) for taps versus holds; debounced by the platform.
- **Log and load:** `gp5_log("...")` on either processor (`tools/gp5_link.py log -f`), and `gp5_link.py status`
  for how busy both processors are.
- **Text:** `gp5_text` writes the built-in 5x7 pixel font (5 lines of 13 characters); `gp5_text_at` /
  `gp5_draw_text` (`nds32/gp5_font.h`) write smooth text in the platform's "sans" font or any font you convert
  with `tools/make_font.py`. See `docs/platform/fonts.md`.

**Speed.** The DSP runs code straight from flash; a large per-sample loop can be several times slower than
the same code in RAM, so put hot code in a `.data` section (Dreamcatcher's `DC_HOT` macros, `dsp/dream.h`).
Even from RAM it averages about 4-5 cycles per instruction (measured: 32 cycles for a trivial loop step; its
small and large RAM are equally fast), so budget per-sample work carefully. The control processor (288 MHz,
with its own float unit) ran the same float code about 1.7 times faster: heavy firmwares can run their sound
there over the audio link (`gp5_link_start`; `"audio": {"link_words": N}` sets the words per block) and use
`gp5_math.h` on both processors. A firmware that needs more can run the control processor at 320 MHz, the clock
Valeton's own firmware uses (`"control": {"mhz": 320}` in its `firmware.json`; the core voltage goes to
1.25 V with it, as in stock): 11 % more cycles, a little more power. NAM Loader runs at 320 MHz. Its float unit
takes about 5 cycles per operation with no overlap and has no fused multiply-add, while its integer DSP
instructions (`kmmac`, `kwmmul` and friends) run about one per cycle: fixed-point code can be several times
faster than float there.

A complete example in its own repository: [Dreamcatcher](https://github.com/OpenGP5/Dreamcatcher) (its sound
on the control processor and a reverb on the DSP, parameters and presets, smooth text, its own LED colour,
OpenGP5 as a submodule).

The control-processor build uses only our own code: start-up,
drivers (`nds32/hal/`) and USB, nothing compiled, included or linked from the vendor SDK. The image
holds only the two sections we build; flashing it leaves Valeton's other sections on the pedal as they are.
Flashing and recovery: `GETTING-STARTED.md` (public tree) / `release/GETTING-STARTED.md`.
