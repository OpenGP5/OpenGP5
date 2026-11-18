# Changelog

OpenGP5's platform: what changed for firmware authors. Versions before 1.0 are alpha; the API may still change
before 1.0 final. Firmwares built with OpenGP5 (NAM Loader, Dreamcatcher) have their own changelogs.

## 0.7.0-alpha: the 1.0 API

The first release of the API that 1.0 will have. Everything below is optional per firmware: a firmware without a
`firmware.json` builds as it did on 0.6 (that older API is removed before 1.0 final).

### New
- **`firmware.json`**, a firmware's manifest: id, name, version, author, licence, and what it needs (sample rate,
  audio link, 320 MHz, presets). It replaces the `#define`s in `dsp/firmware.h` and `fw_name()`. The build checks it
  (`platform/manifest.py --check`) and puts it in the image, so Launchpad, the web flasher and
  `gp5tool.py info <file.bin>` know what a `.bin` is before it is flashed. The pedal says what it runs too (USB
  `7E 16`, `gp5_link.py info`).
- **Parameters** (`params.h`, `docs/platform/params.md`): declare the settings once, with name, group, range,
  default, unit and flags. The platform sends changes to the DSP (`fw_param`, `gp5_param`), keeps them across
  power-off, and lets a computer list, read and set them. Saved values survive firmware updates that add, remove or
  reorder parameters.
- **Presets:** `"presets": N` in `firmware.json`. The platform keeps them, with names: `gp5_preset_load`, `_save`,
  `_rename`, `_current`, `_dirty`. USB `7E 17`; `gp5_link.py presets`.
- **New control callbacks:** `fw_foot_down` / `fw_foot_up`, `fw_para_down` / `fw_para_up`, `fw_para_click` and
  `fw_para_hold` (0.6 s), `fw_tick` (every 10 ms), `fw_draw(force)`, `fw_storage_changed`. Platform calls
  `gp5_knob`, `gp5_ms`, `gp5_led`, `gp5_level_own` / `gp5_level_set`.
- **`gp5_log`** on both processors: printf-style lines with the time, read with `gp5_link.py log -f`. DSP lines
  travel over the link. `gp5_fmt.h` is the small printf behind it.
- **`gp5_link.py status`**: cycles per block (last and worst), the main loop's longest pass, late blocks and memory
  in use, for both processors (USB `7E 14`).
- **`run_effect.py --set Name=value`**, choices by name (`--set Shape=Square`); `gp5_log` prints to the terminal.
- **The tremolo** (`firmwares/tremolo`) is the example firmware, on the 1.0 API: parameters, four presets, a
  smooth-font screen, PARA click and hold, a screen preview on the computer.
- **Docs:** `docs/platform/api.md` (everything a firmware can use), `docs/platform/params.md`, the first-effect
  walkthrough rewritten for 1.0, the new USB and link commands in `protocols.md`.

### Changed
- Link command `0x7F10` carries parameters for a firmware with `params.h`; its own commands start at `0x7F11`.
- The DSP's load report (`0x7F04`) also gives memory in use, and can start its worst values again.
- The hand-over to update mode (`11 6F`, flashing without Foot + PARA) always goes through a restart. It failed now
  and then for firmwares without the audio link.

### Removed
- The strobe tuner demo. The screen can't refresh fast enough for a proper strobe.

### Upgrading a 0.6 firmware
1. Add `firmware.json`. Use the name your `fw_name()` returned as `id`, so the pedal keeps its storage. Move
   `GP5_SAMPLE_RATE`, `GP5_CTL_MHZ`, `GP5_AUDIO_LINK`, `GP5_LINK_WORDS` and `GP5_LINK_EARLY` from `dsp/firmware.h`
   into it.
2. Rename the callbacks: `fw_foot` → `fw_foot_down`, `fw_para_push` → `fw_para_down`, `fw_draw(knob, force)` →
   `fw_draw(force)` with `gp5_knob()`. Replace `fw_knob` with `gp5_knob()` or `fw_tick`, and `fw_active` with
   `gp5_led()`. Replace `fw_level` with `gp5_level_own(1)` + `gp5_level_set()`, if you set the level yourself.
3. Optional, recommended: move the settings into `params.h`. `fw_take`, `fw_setting_count`, `fw_dsp_ready` and your
   `0x7F10` handling then go away, and Launchpad gets an editor for your firmware. NAM Loader 1.2 and Dreamcatcher 1.3
   are ported examples.

## 0.6.0-alpha
- 320 MHz on the control processor, for heavy firmwares.
- 48 kHz audio (0.5.6).
- Fixed: slow preset loads and clicks after long flash writes; the DSP no longer stops answering after flash
  writes; LEDs stuck on red.

## 0.5.5-alpha
- Fixed clicks while a computer talks to the pedal: the DSP queues its replies.

## 0.5.4-alpha
- Storage as files, owned by the installed firmware (300 KB).
- Live settings from a computer (`fw_param_get` / `fw_param_set`, USB `7E 10` / `7E 11`).
- The controls over USB (`7E 0F`).
- `gp5_math.h` on both processors.
- Audio link size per firmware.

## 0.2.x-alpha
- Storage across power-off (`gp5_store_save` / `_load`).
- LED colours.
- Switch releases.
- Smooth text (`make_font.py`).
- Firmwares in their own repositories.
- Reading the flash over USB.

## 0.1.0-alpha
- First public release: the platform, the passthrough, the tremolo, the granular delay.
