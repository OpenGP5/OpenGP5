# OpenGP5 API reference

This page lists the public calls, callbacks and manifest fields. Read each header's comments for full call details.
An API is the set of calls and commands your effect can use.
These docs target **platform 1.0.0**.
See [Versions](#versions) for the distinction. Start with [How a firmware works](concepts.md), then use these guides:
[parameters](params.md), [UI kit](ui.md), [audio link](audio-link.md), [performance](performance.md),
[debugging](debugging.md), [simulator](simulator.md), [releasing](releasing.md), [storage](storage.md),
[fonts](fonts.md), [questions](faq.md).

| Header | Processor | Purpose |
|---|---|---|
| `platform/nds32/gp5_firmware.h` | control (NDS32) | callbacks, controls, screen, LEDs, storage, log, audio link |
| `platform/nds32/gp5_font.h` | control | smooth text in any font ([Fonts](fonts.md)) |
| `platform/nds32/gp5_ui.h` | control | the UI kit: a settings page, a menu, value text, messages, knob pickup ([UI kit](ui.md)) |
| `platform/dsp/gp5_dsp.h` | DSP (Cortex-M4F) | `fw_init`, `fw_process`, `fw_command`, log, audio link, cycle counters |
| `platform/dsp/gp5_params.h` | both | parameters and presets ([Parameters](params.md)) |
| `platform/dsp/gp5_math.h` | both | `sinf`, `expf`, `logf`, `tanhf`, … (neither processor has a C library) |
| `platform/dsp/gp5_fmt.h` | both | a small printf (`gp5_log` uses it) |

Each firmware folder needs `firmware.json`. The build rejects a folder without it.
Platform 0.8 removed the older 0.6 API, which allowed builds without a manifest.

## A firmware's folder

```
my-effect/
  firmware.json        who it is and what it needs (below)
  params.h             its parameters (optional; params.md)
  dsp/firmware.h       the sound: fw_init, fw_process, fw_command, fw_param      (gp5_dsp.h)
  nds32/*.c            screen and controls: fw_draw, fw_foot_down, ...          (gp5_firmware.h)
```

Keep your effect under `firmwares/`, or put it in your own Git project with a pinned copy of OpenGP5.
Git calls that copy a submodule. In that case, pass your effect folder's path to the build.

The [firmware template](https://github.com/OpenGP5/firmware-template) sets this up.
GitHub Actions tests and builds each push. A tag can publish a release.
You can also build in a browser with GitHub Codespaces. See `firmwares/tremolo` for a full example.

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
  "platform": "1.0",
  "presets": 4
}
```

| Field | Required? | Meaning |
|---|---|---|
| `id` | required | lowercase letters, digits and `-`, up to 31. It names the firmware's storage: a firmware with another id starts with empty storage, so keep it once released |
| `name` | required | shown by Launchpad and the web flasher |
| `version` | required | `1.2.0` or `1.2.0-beta.1` |
| `author`, `url`, `licence`, `description` | optional | shown by Launchpad (Version / By on the Pedal page) |
| `platform` | optional | the API version it was written for, `major.minor`; the build refuses a newer minor or another major |
| `presets` | optional | how many presets the platform keeps for it, 0..64 ([Parameters](params.md)) |
| `audio.sample_rate` | optional | 44100 (default) or 48000 |
| `audio.link` | optional | `true`: the audio link between the processors (below) |
| `audio.link_words` | optional | 32-bit words each way per block: a multiple of 8 from 8 to **264** (200 while USB audio records from the DSP); default 128; enables `link` |
| `audio.link_early` | optional | start the exchange at the beginning of each audio block; define DSP `fw_link_early` (see below) |
| `control.mhz` | optional | 288 (default) or 320: the control chip's clock (11 % more cycles) |
| `bluetooth` | optional | `true`: Bluetooth MIDI (below): the pedal advertises as OpenGP5, MIDI reaches `fw_midi`, the computer's commands work over it |
| `usb_audio` | optional | the pedal as a USB audio interface (below): on by default with `"platform": "1.0"` or later, off for older firmwares unless `true`; `false` turns it off; `{"record_from": "control"}` for a firmware that finishes its sound on the control processor |

Run `python3 platform/manifest.py <folder> --check` to validate the fields. Use your firmware folder's path.
The build creates definitions for both chips: `GP5_FW_ID`, `GP5_SAMPLE_RATE`, `GP5_CTL_MHZ`, `GP5_AUDIO_LINK`,
`GP5_LINK_WORDS`, `GP5_PRESETS`, `GP5_HAS_PARAMS`, `GP5_BLUETOOTH` and `GP5_USB_AUDIO` (with USB audio
also `GP5_LINK_TOTAL`, `GP5_USB_PLAY_AT`, `GP5_USB_REC_AT` or `GP5_USB_RECORD_CONTROL`).

The image stores the manifest with its code, in a metadata block. The tools read it to show the build's name and version
before a flash. Run `gp5tool.py info <file.bin>` to read a file. The pedal sends the same details through USB command `7E 16`.

## Control processor: what the platform calls

You can omit these callbacks. The platform uses an empty function in their place.
They run in the main loop, so do not wait for events inside them.
A blocked loop turns the LEDs full red after two seconds. See [Performance](performance.md) for shorter response-time targets.

| Callback | When it runs |
|---|---|
| `void fw_init(void)` | once at start-up, after the storage claim and the saved parameters are loaded |
| `int fw_draw(int force)` | every pass: draw part of the screen with `gp5_blit`; return non-zero while more is left. `force` = draw everything |
| `void fw_tick(void)` | every 10 ms |
| `void fw_foot_down(void)` / `fw_foot_up` | the foot switch, debounced (30 ms) |
| `void fw_para_down(void)` / `fw_para_up` | the PARA knob pushed / let go |
| `void fw_para_click(void)` | a push let go within 0.6 s (just before `fw_para_up`) |
| `void fw_para_hold(void)` | a push held 0.6 s (once, while still held; no click follows) |
| `void fw_para_turn(int detents)` | PARA turned, + clockwise |
| `void fw_param_changed(unsigned id)` | a parameter changed from outside: the computer, a preset load ([Parameters](params.md)) |
| `int fw_param_text(unsigned id, int32_t v, char *text)` | the text of a `GP5_U_CUSTOM` value, up to 15 characters |
| `void fw_storage_changed(void)` | the computer changed this firmware's files or records: read them again |
| `void fw_midi(const uint8_t *msg, unsigned n)` | a MIDI message from USB or Bluetooth, status byte first, from the main loop: a note, controller, program change, pressure or pitch bend (`n` = 2 or 3), or a real-time byte (`n` = 1): clock `0xF8` (24 a beat), start `0xFA`, continue `0xFB`, stop `0xFC`, active sensing `0xFE`, reset `0xFF`. Check the status byte and `n`. SysEx is the platform's |
| `int fw_param_get/fw_param_set(uint32_t id, ...)` | the firmware's own live ids past its parameters (Dreamcatcher's preset commands) |

## Control processor: what a firmware may call

| Call | Result or action |
|---|---|
| `gp5_knob()` | the volume knob, 0..255, filtered |
| `gp5_ms()` | milliseconds since start |
| `GP5_CYCLE_COUNT()` | the core's cycle counter, for timing code ([performance](performance.md)) |
| `gp5_level_own(1)`, `gp5_level_set(0..255)` | take over the output level (by default the volume knob sets it) |
| `gp5_led(GP5_LED_ON / GP5_LED_BYPASS / GP5_LED_RGB(r, g, b))` | the LEDs: mint (default), dim red, or a colour (0..32 each) |
| `gp5_blit(x, y, w, h, px)` | pixels to the 160 × 80 screen (`gp5_color(0xRRGGBB)` makes one) |
| `gp5_text(line, s)` | the built-in 5×7 font, 5 lines of 13 characters |
| `gp5_draw_text`, `gp5_text_at`, `gp5_text_width` | smooth text (`gp5_font.h`, [Fonts](fonts.md)) |
| `gp5_param`, `gp5_param_set`, `gp5_preset_*` | parameters and presets ([Parameters](params.md)) |
| `gp5_ui_page_*`, `gp5_ui_menu_*`, `gp5_param_format`, `gp5_pickup_*` | the UI kit ([UI kit](ui.md)) |
| `gp5_store_save(id, p, len)` / `gp5_store_load(id, p, max)` | a record of up to 1 KB, kept across power-off ([Storage](storage.md)) |
| `gp5_file`, `gp5_file_check`, `gp5_file_each` | files the computer wrote (captures, IRs): read straight from flash |
| `gp5_log(fmt, ...)` | a line for the log (below) |
| `gp5_link_start(on_block)` | start the audio link (below) |

## Bluetooth

To send MIDI over Bluetooth, set `"bluetooth": true`. The pedal uses Bluetooth Low Energy MIDI (BLE-MIDI).
It shows the name **GP-5** and accepts one host at a time.
Connect with a phone or a BLE-MIDI host on your computer. On a Mac, use **Audio MIDI Setup › Bluetooth**.

The radio starts while the logo shows. Calibration takes about one second and runs before the audio link.
Starting the radio during an active link caused sound stops in testing.

- MIDI messages reach `fw_midi` through the same callback as USB MIDI.
- The computer's commands (SysEx) work over it too: macOS names the port "OpenGP5 Bluetooth" ("GP-5
  Bluetooth" before 1.0); Launchpad and `gp5_link.py` (`GP5_PORT="OpenGP5 Bluetooth"`) use it. Firmware updates need USB.
- Each pedal has its own address (from the chip's efuse).
- The radio uses a short control-chip interrupt per event, 7.5 to 30 ms apart. It also uses 4 KB of RAM outside
  your firmware's budget. The radio code is OpenGP5's own (`platform/nds32/bt/`).

## DSP

One header, `dsp/firmware.h`, compiled into the platform's DSP program:

| Callback | When it runs |
|---|---|
| `static void fw_init(void)` | once, before audio starts |
| `static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)` | every block: 64 samples, mono in, stereo out, full scale ±2³¹. The platform applies the output level afterwards |
| `static void fw_param(uint32_t id, int32_t value)` | a parameter changed, before the next `fw_process` (a firmware with `params.h` defines it, even empty) |
| `static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n)` | the firmware's own link commands from `0x7F11`; return 0 for one it doesn't know |

Use these tools in DSP code:

| Tool | Purpose |
|---|---|
| `gp5_param(id)` | Read the current parameter value |
| `gp5_reply`, `gp5_ack` | Reply to a command |
| `gp5_log` | Send a log line |
| `gp5_link_in()`, `gp5_link_out()` | Read or write audio-link words |
| `GP5_LARGE` | Put a buffer in the 122,880-byte area |
| `GP5_FAST` | Put time-critical code in the 19,712-byte area shared with variables |
| `GP5_CYCLES_LAST`, `GP5_CYCLES_WORST`, `GP5_LATE_BLOCKS` | Read audio timing counters |
| `GP5_CYCLE_COUNT()` | Time a piece of code in cycles |

The DSP budget is about 278,000 cycles per block at 44.1 kHz or 255,900 at 48 kHz.
Code runs from flash unless you mark it for RAM. See [Performance](performance.md).

## The audio link

Set `"audio": {"link": true}` to exchange `link_words` 32-bit values each way per audio block.
On the control chip, call `gp5_link_start(on_block)` in `fw_init`.
The handler runs in an interrupt and must finish within one block.

On the DSP, read received words with `gp5_link_in()` and fill outgoing words with `gp5_link_out()`.
A sample sent from the DSP returns from the control chip **three blocks later**: about 4.4 ms at 44.1 kHz or 4 ms at 48 kHz.

With `link_early`, the exchange starts at the beginning of the block.
Define `static void fw_link_early(const int32_t *in, int32_t *out)` to supply new input before that exchange.
See [The audio link](audio-link.md) for the full example and timing rules.

## USB audio

A firmware written for platform 1.0 or later is a class-compliant USB audio interface unless firmware.json says
`"usb_audio": false`. A firmware written for an older platform (or that doesn't say) gets it only with `"usb_audio": true`.
The pedal then shows up beside its USB-MIDI
port (no driver on macOS, Windows or Linux): 2 channels each way, 16-bit, at the firmware's sample rate.
- **Recording** (pedal → computer) is the firmware's finished stereo output, taken on the DSP after `fw_process`,
  before the computer's playback is mixed in and before the volume knob. It reaches the computer sample for sample
  (the pedal sends 44 or 45 frames a millisecond, one more or fewer as its clock drifts from the computer's).
- **Playback** (computer → pedal) is mixed into the outputs on the DSP, before the volume knob. The control processor
  resamples it from the computer's clock to the DSP's (they differ by tens of ppm).
- **Direct sound.** While the computer plays into the pedal, the pedal's own sound can be left out of the outputs, so a
  DAW that monitors the pedal isn't heard twice: `gp5_usb_audio_set_direct(0)` (`1`, the default, puts it back;
  `gp5_usb_audio_direct()` reads it). The computer can switch it too (Launchpad's pedal panel, `gp5_link.py direct off`,
  `7E 19`). Recording doesn't change, and while nothing plays the pedal is always heard. It is back on after a restart,
  unless the firmware stores it and sets it again in `fw_init`.
- It travels in the audio link: the platform keeps 64 words each way after the firmware's own `link_words`, and starts
  the link for a firmware that has none. So a firmware has at most **200** link words while USB audio is on.
- A firmware that finishes its sound on the control processor (NAM Loader) sets `"usb_audio": {"record_from": "control"}`
  and hands the output over from `on_block`: `gp5_usb_audio_record(left, right)`, 64 samples each, full scale ±2^31
  (a block it doesn't hand over is recorded as silence, so the recording keeps its place).
  No words come back from the DSP for it then: `link_words` may be up to 264, but its own words towards the DSP must
  stay below 200 (the platform's playback words are 200..263).
- Cost: on the control processor the USB interrupt (0.9 %) and the resampler in the link's interrupt (about 3 % of a
  block, 6 % at worst); on the DSP a copy and a mix of 64 frames. `"usb_audio": false` gives all of it back.

## Log

`gp5_log("rate %ld, depth %d %%", rate, depth)` on either processor. printf-style (`%d %u %x %s %c %f`, width,
precision; `gp5_fmt.h`), at most about 110 characters a line, stamped with the time. The control chip keeps
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

The command shows current and peak block load, the longest main-loop pass, late blocks and memory use.
Add `--reset` to clear peak readings. Test with the settings that use the most time before a release.

## Trying things without a pedal

```
python3 tools/simulate.py tremolo --script controls.txt --in guitar.wav
```

The [simulator](simulator.md) runs both halves and the audio link with a control script.
It saves screen images as PNGs and sound as a WAV file.
To run the DSP half alone, use `python3 tools/run_effect.py tremolo in.wav out.wav --set Rate=800 --set Shape=Square`.

## Talking to a running firmware

| Command | Result or action |
|---|---|
| `gp5_link.py info` | the firmware's name, version and author |
| `gp5_link.py params` | its parameters, values and ranges |
| `gp5_link.py set Rate 800` | set one (a choice by name: `set Shape Square`) |
| `gp5_link.py presets`, `preset load / save / name <n>` | its presets |
| `gp5_link.py log [-f]`, `status` | above |

Launchpad's Live page uses these commands to read and edit settings.
NAM Loader and Dreamcatcher have their own pages. See [Protocols](protocols.md) for the USB messages.

## Versions

These docs target **platform 1.0.0**. Its API was introduced in 0.7, so firmwares written for 0.7 or 0.8 build on
1.x unchanged; they get USB audio only if they ask for it (`"usb_audio": true`).

Read the running platform version from `GP5_VERSION_TEXT` in `gp5_firmware.h`.
Set `firmware.json`'s `platform` field to the major and minor release your code needs, such as `"1.0"`.
The build rejects a newer minor version or a different major version.

### 1.x guarantees

Every 1.x release preserves these parts:

- Public calls, callbacks, their timing and manifest fields keep their meaning.
- Saved parameters, presets, store records and files remain readable.
- USB commands keep their numbers and existing fields. New reply fields go at the end.
- The platform stays within its memory share, leaving at least the documented firmware budgets.

See [Known limits](known-limits.md) for those budgets and [Protocols](protocols.md) for reply rules.
Continuous integration (CI) runs build and test checks on changes.
`tools/check_budget.py` checks memory use, and `tools/test_compat.sh` checks the fixtures in `compat/1.0`.
The fixtures are the passthrough, the tremolo and the template as written for 1.0, and NAM Loader 1.3.1 as written
for 0.8.

In 1.x, `GP5_DEPRECATED` warns that a call will go away.
The warning must stay for at least one full minor release. A later major release can then remove the call.
The guarantees do not cover code inside the platform or UI-kit struct layout beyond the fields you set.
Limits such as link size and preset count may grow.
