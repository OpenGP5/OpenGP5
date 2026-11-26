# Changelog

This changelog tracks the OpenGP5 platform. NAM Loader and Dreamcatcher have their own release notes.
From 1.0, no 1.x release breaks a firmware written for 1.0 (see Versions in the API reference).

Each entry says what changed in that release. Use the current guides to set up the tools or fix a failed flash.

## 1.0.0: USB audio, Bluetooth MIDI, a stable API

Set `"platform": "1.0"` in `firmware.json`. Firmwares written for 0.7 or 0.8 build unchanged; they get USB audio
only with `"usb_audio": true`.

### New

- **The pedal is a USB audio interface.** Class-compliant, two channels each way, 16-bit, at the firmware's sample
  rate, next to USB-MIDI; no driver on macOS or Windows (Linux should need none either: not yet tested). The computer records the firmware's finished output
  and can play through the pedal's outputs. On by default for firmwares written for 1.0; `"usb_audio": false` turns
  it off. A firmware that finishes its sound on the control processor (NAM Loader) uses
  `"usb_audio": {"record_from": "control"}` and `gp5_usb_audio_record()`. See USB audio in the API reference.
- **Direct sound off while the computer plays.** A DAW monitoring the pedal would otherwise be heard twice: once
  straight from the pedal, once back from the DAW. Launchpad's pedal panel, `gp5_link.py direct off`, or the
  firmware (`gp5_usb_audio_set_direct()`) leave the pedal's own sound out of the outputs while the computer plays into
  the pedal. Recording doesn't change; with nothing playing, the pedal is always heard; back on after a restart.
- **Bluetooth MIDI, per firmware.** With `"bluetooth": true` the pedal advertises as OpenGP5 with the BLE-MIDI
  service. Notes, controllers, program changes and real-time messages (MIDI clock, start, stop) reach the new
  `fw_midi()` hook, from USB as well. Launchpad, the phone remote and `gp5_link.py` work over it; flashing needs USB.
  The radio and BLE code are OpenGP5's own.
- **More room for firmwares.** The platform's own buffers moved to RAM the platform didn't use before, so a firmware
  now has 264,160 bytes of control-processor RAM (was 258,016), promised for all of 1.x.
- **Pedals whose updates stall.** `gp5tool.py tickfix` applies the 4-byte fix to the DSP updater;
  `gp5tool.py unstick` gets a pedal out of update mode without J7; `tools/j7_nudge.py` and the J7 recovery guide
  get the first OpenGP5 firmware onto such a pedal.

### Changed

- **The pedal is called OpenGP5** over USB (was GP-5 Open FW), for the MIDI port and the audio device, and over
  Bluetooth (was GP-5). Launchpad 0.4, the web flasher and `gp5tool.py` find both names. Older versions of those tools don't see a pedal running a 1.0
  firmware, except in update mode, which is still called GP-5.

### Fixed

- **The audio link recovers from more stalls.** Restarting it now clears every DMA flag, not only "done"; a latched
  error had kept some links dead through every restart.
- **The crash record survives a watchdog restart.** It was in memory that read as noise on most firmwares.
- **`gp5_ms()` keeps time on heavy firmwares.** It lost milliseconds while an audio-link handler ran for over 1 ms
  (up to 23 % slow on NAM Loader); it now catches up from the cycle counter.

## 0.8.0-alpha: the UI kit, the simulator, the template

Set `"platform": "0.8"` in `firmware.json` when using a feature added in this release.
Code for 0.7 still builds, apart from the older 0.6 API and `GP5_DATA` described under Removed.

### Fixed

- **The audio link restarts after a stall.** Some builds never started an exchange at power-on.
  The fault depended on code layout; its cause was not found. The main loop now rearms the link after 50 ms without an exchange.
  It logs `audio link: no exchange …`. The `sound on` message reports the exchange count.
  A test that stopped the link on purpose recovered within 50 ms.
- **Audio waits for saved settings at start-up.** The platform used to turn sound on when the DSP answered,
  then send settings one every 20 ms. That could play default settings or dry guitar during start-up.
  It now sends settings back to back, with a three-second limit, before fading sound in.
  Output level also glides across its full range over 23 ms to avoid clicks.
- **Flashing waits through macOS MIDI reconnect errors.** The tool keeps waiting if MIDI port listing fails
  while the pedal restarts. It retries the first update-mode query up to four times. Thanks to dk55432, issue #3.

### New

- **MIDI tools retry `RtMidi -304`.** If the macOS MIDI service is not ready, `gp5tool.py`, `gp5_link.py`
  and `gp5_data.py` retry up to three times before sending data. After a send begins, they stop and report the error.
- **The UI kit provides reusable screen tools.** `gp5_ui.h` adds a settings page, value text (`gp5_param_format`),
  messages and knob pickup. Turn PARA to select a row; press it to edit. An optional row selects presets.
  Pickup waits until the knob reaches the saved value before it takes control.
  Unused kit code adds no bytes to an image. See `docs/platform/ui.md`.
- **Launchpad's Developer page shows logs and load.** It reads the pedal's log, processor load and memory
  for builds on platform 0.7 or later.
- **The UI kit adds menus.** `gp5_ui_menu_t` lets PARA select actions and a press run them.
  Items marked for confirmation ask “Sure?” before a second press. Items can show a value, such as the target preset.
- **The tremolo uses the settings page and menu.** Turn PARA to select a row, then press it to edit.
  Earlier controls used a press to change rows and a turn to change values.
- **The tremolo's square wave avoids abrupt edges.** Each edge glides over 5 ms. The tremolo version is 1.1.0,
  and the web flasher offers that build.
- **Run both halves on your computer.** `tools/simulate.py` runs the audio link with normal or early timing.
  A script drives the controls. The tool keeps saved settings and writes screen PNGs and sound WAVs.
  Use `--check` to find memory faults. It runs the tremolo, Dreamcatcher and NAM Loader. See `docs/platform/simulator.md`.
- **Start your own project from a template.** [OpenGP5/firmware-template](https://github.com/OpenGP5/firmware-template)
  is an overdrive with settings, presets and the UI kit. It pins a copy of OpenGP5 as a submodule.
  Use `build.sh` to build it and `test.sh` to run the checks. GitHub Actions tests each push and publishes tagged builds.
  A dev container lets you build in GitHub Codespaces.
- **Continuous integration checks builds and tests.** `.github/workflows/ci.yml` runs `tools/test_all.sh`, builds the examples
  for the pedal and compares the web flasher's tremolo with the source build. Linux and macOS builds match byte for byte.
- **The catalog lists firmware builds and reserves IDs.** Submit one `catalog/` listing per firmware through a pull request.
  CI downloads each release and checks its checksum and metadata. `tools/catalog.py` manages listings;
  `docs/catalog.json` supplies the list to the apps.
- **Launchpad and the web flasher read the catalog.** Each checks file size, checksum and image details against the listing.
  Builds from other authors carry *Not reviewed*, which does not imply a source review.
  Shared catalog code lives in `web/flasher/catalog.js`.
- **New files explain how to take part.** Read CONTRIBUTING.md for ways to help and the code of conduct for the rules.
  Forms cover bugs, hardware reports and feature requests. A pull-request template lists the details to include.
- **New guides explain the shared code.** Read how it works, how the chips share sound work and how to measure load.
  Other guides cover finding bugs, sharing builds, common questions and known limits.
  Use BETA-TESTING.md to test your pedal. The code samples were built and run on a computer.
- **DSP code can run from RAM.** Mark per-sample functions with `GP5_FAST`.
  Use `GP5_CYCLE_COUNT()` to read the cycle counter on either chip.
  Control-chip `on_block` code can use `GP5_BLOCK` for the block size.
- **The link-word limit is 264.** That was the largest tested count without dropouts.
  The build no longer enables fused multiply-add, since that instruction hangs the control chip.
- **Check how much screen data each pass sends.** The simulator reports the largest transfer.
  Long transfers make controls and USB wait.

### Removed (the last planned breaking changes before 1.0)

- **The 0.6 API.** Every firmware folder now needs `firmware.json`.
  Removed callbacks are `fw_foot`, `fw_foot_release`, `fw_para_push`, `fw_para_release`, `fw_knob`, `fw_level`,
  `fw_active`, `fw_draw(knob, force)` and `fw_store_changed`.
  The manifest replaces these settings from `dsp/firmware.h`: `GP5_AUDIO_LINK`, `GP5_LINK_WORDS`, `GP5_SAMPLE_RATE`,
  `GP5_CTL_MHZ` and `GP5_LINK_EARLY`.
- **`GP5_DATA`.** The raw area from before 0.4 is gone. Use files through `gp5_file`.

### Preparing the 1.0 compatibility checks

- `compat/1.0` holds example fixtures from 0.8 sources, pending the final 1.0 freeze.
  `tools/test_compat.sh` runs them in the simulator, and CI builds them for the pedal.
  The parameter test reads the saved fixture `platform/nds32/test/params-1.0.store.hex`.
- `compat/1.0/budget.json` caps the platform's share of memory. CI checks it with `tools/check_budget.py`.
  See `known-limits.md` for the planned 1.x firmware budgets.
- `GP5_DEPRECATED(instead)` marks a call for retirement and warns during a build.
- The planned USB rules add reply fields at the end. Readers skip bytes they do not know. See `protocols.md`.
- The simulator now stops the build if a call has no declared function.
  This catches removed or renamed calls that used to cause a warning.

## 0.7.0-alpha: the 1.0 API

This release introduced the planned 1.0 API. In 0.7, a build without `firmware.json` could still use the 0.6 API.
Platform 0.8 removed that older path.

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
- **`gp5_log`** on both chips: printf-style lines with the time, read with `gp5_link.py log -f`. DSP lines
  travel over the link. `gp5_fmt.h` is the small printf behind it.
- **`gp5_link.py status`**: cycles per block (last and worst), the main loop's longest pass, late blocks and memory
  in use, for both chips (USB `7E 14`).
- **`run_effect.py --set Name=value`**, choices by name (`--set Shape=Square`); `gp5_log` prints to the terminal.
- **The tremolo** (`firmwares/tremolo`) is the example firmware, on the 1.0 API: parameters, four presets, a
  smooth-font screen, PARA click and hold, a screen preview on the computer.
- **Docs:** `docs/platform/api.md` (everything a firmware can use), `docs/platform/params.md`, the first-effect
  walkthrough rewritten for 1.0, the new USB and link commands in `protocols.md`.

### Changed
- Link command `0x7F10` carries parameters for a firmware with `params.h`; its own commands start at `0x7F11`.
- The DSP's load report (`0x7F04`) also gives memory in use, and can clear its peak readings.
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
- 320 MHz on the control chip, for heavy firmwares.
- 48 kHz audio (0.5.6).
- Fixed: slow preset loads and clicks after long flash writes; the DSP no longer stops answering after flash
  writes; LEDs stuck on red.

## 0.5.5-alpha
- Fixed clicks while a computer talks to the pedal: the DSP queues its replies.

## 0.5.4-alpha
- Storage as files, owned by the installed build (300 KB).
- Live settings from a computer (`fw_param_get` / `fw_param_set`, USB `7E 10` / `7E 11`).
- The controls over USB (`7E 0F`).
- `gp5_math.h` on both chips.
- Audio link size per firmware.

## 0.2.x-alpha
- Storage across power-off (`gp5_store_save` / `_load`).
- LED colours.
- Switch releases.
- Smooth text (`make_font.py`).
- Firmwares in their own repositories.
- Reading the flash over USB.

## 0.1.0-alpha
- The first public release includes the shared code, passthrough, tremolo and grain delay.
