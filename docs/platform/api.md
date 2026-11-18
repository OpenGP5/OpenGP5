# OpenGP5 1.0 API

Everything a firmware can use, in one place. The headers are the full reference (every function has its comment
there); this page is the map.

| header | processor | what |
|---|---|---|
| `platform/nds32/gp5_firmware.h` | control (NDS32) | callbacks, controls, screen, LEDs, storage, log, audio link |
| `platform/nds32/gp5_font.h` | control | smooth text in any font (`fonts.md`) |
| `platform/dsp/gp5_dsp.h` | DSP (Cortex-M4F) | `fw_init`, `fw_process`, `fw_command`, log, audio link, cycle counters |
| `platform/dsp/gp5_params.h` | both | parameters and presets (`params.md`) |
| `platform/dsp/gp5_math.h` | both | `sinf`, `expf`, `logf`, `tanhf`, … (neither processor has a C library) |
| `platform/dsp/gp5_fmt.h` | both | a small printf (`gp5_log` uses it) |

A firmware is "1.0" when its folder has a `firmware.json`. Firmwares without one build as before (the 0.6 API, kept
for now, removed before 1.0 final).

## A firmware's folder

```
my-effect/
  firmware.json        who it is and what it needs (below)
  params.h             its parameters (optional; params.md)
  dsp/firmware.h       the sound: fw_init, fw_process, fw_command, fw_param      (gp5_dsp.h)
  nds32/*.c            screen and controls: fw_draw, fw_foot_down, ...          (gp5_firmware.h)
```

The folder can live under `firmwares/` or in its own repository with OpenGP5 as a git submodule; then give the
build its path instead of a name. `firmwares/tremolo` is the example that uses all of it.

## firmware.json

```json
{
  "id": "tremolo",
  "name": "Tremolo",
  "version": "1.0.0",
  "author": "OpenGP5",
  "url": "https://github.com/OpenGP5/OpenGP5",
  "licence": "MIT",
  "description": "The volume rises and falls.",
  "platform": "0.7",
  "presets": 4
}
```

| field | | |
|---|---|---|
| `id` | required | lowercase letters, digits and `-`, up to 31. It names the firmware's storage: a firmware with another id starts with empty storage, so keep it once released |
| `name` | required | shown by Launchpad and the web flasher |
| `version` | required | `1.2.0` or `1.2.0-beta.1` |
| `author`, `url`, `licence`, `description` | optional | shown by Launchpad (Version / By on the Pedal page) |
| `platform` | optional | the API version it was written for, `major.minor`; the build refuses a newer minor or another major |
| `presets` | optional | how many presets the platform keeps for it, 0..64 (`params.md`) |
| `audio.sample_rate` | optional | 44100 (default) or 48000 |
| `audio.link` | optional | `true`: the audio link between the processors (below) |
| `audio.link_words` | optional | words each way per block, a multiple of 8 up to 512 (default 128; sets `link`) |
| `audio.link_early` | optional | start the link at boot, before the firmware's `fw_init` |
| `control.mhz` | optional | 288 (default) or 320: the control processor's clock (11 % more cycles) |

`python3 platform/manifest.py <folder> --check` checks it and prints what it says. The build turns it into
defines both processors see (`GP5_FW_ID`, `GP5_SAMPLE_RATE`, `GP5_CTL_MHZ`, `GP5_AUDIO_LINK`, `GP5_LINK_WORDS`,
`GP5_PRESETS`, `GP5_HAS_PARAMS`), and puts the manifest in the image as a metadata block, so Launchpad, the web
flasher and `gp5tool.py info <file.bin>` know what a `.bin` is before it is flashed, and the pedal says what it
runs (USB `7E 16`).

## Control processor: what the platform calls

All optional: the platform has an empty default for each. All run in the main loop, one after the other, so none
of them may wait (a loop that waits more than 2 s turns the LEDs full red).

| callback | when |
|---|---|
| `void fw_init(void)` | once at start-up, after the storage claim and the saved parameters are loaded |
| `int fw_draw(int force)` | every pass: draw part of the screen with `gp5_blit`; return non-zero while more is left. `force` = draw everything |
| `void fw_tick(void)` | every 10 ms |
| `void fw_foot_down(void)` / `fw_foot_up` | the foot switch, debounced (30 ms) |
| `void fw_para_down(void)` / `fw_para_up` | the PARA knob pushed / let go |
| `void fw_para_click(void)` | a push let go within 0.6 s (just before `fw_para_up`) |
| `void fw_para_hold(void)` | a push held 0.6 s (once, while still held; no click follows) |
| `void fw_para_turn(int detents)` | PARA turned, + clockwise |
| `void fw_param_changed(unsigned id)` | a parameter changed from outside: the computer, a preset load (`params.md`) |
| `int fw_param_text(unsigned id, int32_t v, char *text)` | the text of a `GP5_U_CUSTOM` value, up to 15 characters |
| `void fw_storage_changed(void)` | the computer changed this firmware's files or records: read them again |
| `int fw_param_get/fw_param_set(uint32_t id, ...)` | the firmware's own live ids past its parameters (Dreamcatcher's preset commands) |

## Control processor: what a firmware may call

| call | |
|---|---|
| `gp5_knob()` | the volume knob, 0..255, filtered |
| `gp5_ms()` | milliseconds since start |
| `gp5_level_own(1)`, `gp5_level_set(0..255)` | take over the output level (by default the volume knob sets it) |
| `gp5_led(GP5_LED_ON / GP5_LED_BYPASS / GP5_LED_RGB(r, g, b))` | the LEDs: mint (default), dim red, or a colour (0..32 each) |
| `gp5_blit(x, y, w, h, px)` | pixels to the 160 × 80 screen (`gp5_color(0xRRGGBB)` makes one) |
| `gp5_text(line, s)` | the built-in 5×7 font, 5 lines of 13 characters |
| `gp5_draw_text`, `gp5_text_at`, `gp5_text_width` | smooth text (`gp5_font.h`, `fonts.md`) |
| `gp5_param`, `gp5_param_set`, `gp5_preset_*` | parameters and presets (`params.md`) |
| `gp5_store_save(id, p, len)` / `gp5_store_load(id, p, max)` | a record of up to 1 KB, kept across power-off (`storage.md`) |
| `gp5_file`, `gp5_file_check`, `gp5_file_each` | files the computer wrote (captures, IRs): read straight from flash |
| `gp5_log(fmt, ...)` | a line for the log (below) |
| `gp5_link_start(on_block)` | start the audio link (below) |

## DSP

One header, `dsp/firmware.h`, compiled into the platform's DSP program:

| | |
|---|---|
| `static void fw_init(void)` | once, before audio starts |
| `static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)` | every block: 64 samples, mono in, stereo out, full scale ±2³¹. The platform applies the output level afterwards |
| `static void fw_param(uint32_t id, int32_t value)` | a parameter changed, before the next `fw_process` (a firmware with `params.h` defines it, even empty) |
| `static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n)` | the firmware's own link commands from `0x7F11`; return 0 for one it doesn't know |

It may use `gp5_param(id)` (the current value), `gp5_reply` / `gp5_ack` (answer a command), `gp5_log`,
`gp5_link_in()` / `gp5_link_out()`, `GP5_LARGE` (put a buffer in the 122 KB block; ordinary variables have
19.5 KB), and `GP5_CYCLES_LAST`, `GP5_CYCLES_WORST`, `GP5_LATE_BLOCKS`.

Budget: about 278,000 cycles per block at 44.1 kHz (255,900 at 48 kHz). Code runs from flash unless you put it
in RAM; see "Speed" in `platform/README.md`.

## The audio link

With `"audio": {"link": true}` both processors exchange `link_words` 32-bit words after every block. On the control
processor, `gp5_link_start(on_block)` (in `fw_init`) makes `on_block(in, out)` run in an interrupt once per block:
keep it shorter than a block. On the DSP, `gp5_link_in()` holds what arrived and `gp5_link_out()` takes what to
send. Words take two exchanges to arrive. This is how NAM Loader and Dreamcatcher run part of their sound on the
control processor.

## Log

`gp5_log("rate %ld, depth %d %%", rate, depth)` on either processor. printf-style (`%d %u %x %s %c %f`, width,
precision; `gp5_fmt.h`), at most about 110 characters a line, stamped with the time. The control processor keeps
the last 4 KB; the DSP's lines travel over the link and are marked `dsp:`. `uint32_t` is a `long` on both
processors: print it with `%lu` / `%lx`. A DSP line costs a few thousand cycles: not every block.

```
python3 tools/gp5_link.py log -f        # follow it while you play
```

On the computer (`tools/run_effect.py`) the DSP's `gp5_log` prints to the terminal.

## Load and memory

```
python3 tools/gp5_link.py status [--reset]
```

shows, for both processors, the cycles per block used (last and worst), the main loop's longest pass, late
blocks and memory in use. `--reset` starts the worst values again. Check it with the heaviest settings before
a release.

## Trying things without a pedal

```
python3 tools/run_effect.py tremolo in.wav out.wav --set Rate=800 --set Shape=Square
```

runs the DSP half on the computer with the same parameters (by name or id; choices by name).
`firmwares/tremolo/test/screen.sh` draws the control half's screen to a PNG.

## Talking to a running firmware

| | |
|---|---|
| `gp5_link.py info` | the firmware's name, version and author |
| `gp5_link.py params` | its parameters, values and ranges |
| `gp5_link.py set Rate 800` | set one (a choice by name: `set Shape Square`) |
| `gp5_link.py presets`, `preset load / save / name <n>` | its presets |
| `gp5_link.py log [-f]`, `status` | above |

Launchpad's Live page shows the same for any firmware with parameters. The USB commands behind them are in
`protocols.md`.

## Versions

The platform's version is `GP5_VERSION_TEXT` in `gp5_firmware.h`; `firmware.json`'s `platform` says which API a
firmware needs. Until 1.0 final the API may still change; after it, a minor version only adds.
