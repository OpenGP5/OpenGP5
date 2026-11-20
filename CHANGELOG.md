# Changelog

OpenGP5's platform: what changed for firmware authors. Versions before 1.0 are alpha; the API may still change
before 1.0 final. Firmwares built with OpenGP5 (NAM Loader, Dreamcatcher) have their own changelogs.

## 0.8.0-alpha: the UI kit, the simulator, the template

Build firmwares for 0.8 with `"platform": "0.8"` in `firmware.json` when they use anything new below; firmwares
written for 0.7 build unchanged (apart from the 0.6 API and `GP5_DATA`, removed below).

### Fixed
- **The audio link restarts itself.** Some builds of a firmware with the audio link never got an exchange after
  start-up, every start (the cause depends on where code lands in memory and wasn't found): only the DSP's own sound
  came out. The main loop now arms the link again after 50 ms without an exchange, and logs it ("audio link: no
  exchange …"); "sound on" says how many exchanges there were so far. Checked on the pedal by stopping the link on
  purpose: back in 50 ms.
- **No dry guitar at start-up.** The platform turned the sound on as soon as the DSP answered, then sent the saved
  settings one every 20 ms, so the first moments played the DSP's defaults (or, for firmwares with sound on the control
  processor, the dry guitar). Now the sound stays off until the DSP has every setting the firmware starts with (sent
  back to back meanwhile, at most 3 s), and then fades in. The output level now glides to each new value (23 ms for
  the whole range) instead of jumping, so a level change makes no click either.
- **Flashing on macOS while the pedal re-connects** (thanks, dk55432, #3): the tool keeps waiting when listing the MIDI
  ports fails during the pedal's restart, and asks the update mode's first question up to 4 times.

### New
- **The tools survive macOS's "RtMidi -304".** When the computer's MIDI system isn't ready (it used to stop
  `gp5tool.py`, `gp5_link.py` and `gp5_data.py` at once, most often right after a flash), they try again by themselves,
  up to 3 times, as long as nothing was sent to the pedal yet; after a send they stop and say what to check.
- **The UI kit** (`gp5_ui.h`, `docs/platform/ui.md`): a settings page built from parameters (PARA turns to pick a
  row, a click adjusts it, an optional presets row), value text as Launchpad shows it (`gp5_param_format`), a
  message line, and knob pickup (the volume knob takes over a setting without a jump). Optional; firmwares that
  don't use it build byte-identical.
- **Launchpad's Developer page:** the pedal's log as it comes, and the load and memory of both processors, for any
  firmware on OpenGP5 0.7 or later.
- **A menu** in the UI kit (`gp5_ui_menu_t`): actions picked with PARA and run with a click; risky ones ask "Sure?"
  first; items can show a value (the preset "Save" writes to).
- The tremolo uses it: turn PARA to pick a row, click to adjust (before: click for the next row, turn to change).
- The tremolo's square wave no longer clicks: each edge glides over 5 ms. The tremolo is now 1.1.0 (the web flasher
  offers it).
- **The simulator** (`tools/simulate.py`, `docs/platform/simulator.md`): a whole firmware on the computer, both
  processors and the audio link (normal and early exchanges), the screen as PNGs, controls from a script, WAV in and
  out, saved settings between runs, and `--check` for memory bugs. Runs the tremolo, Dreamcatcher and NAM Loader.
- **The firmware template** (github.com/OpenGP5/firmware-template): a firmware repository to start from (an
  overdrive with parameters, presets and the UI kit), OpenGP5 as a submodule, `build.sh`, `test.sh` (the simulator),
  a GitHub Actions workflow that tests and builds every push and publishes a release from a tag, and a dev container
  for GitHub Codespaces.
- **CI for OpenGP5** (`.github/workflows/ci.yml`): every computer-side test (`tools/test_all.sh`, which you can run
  too), the examples built for the pedal, and the web flasher's tremolo checked byte for byte against the source.
  Builds are byte-identical on Linux and macOS.
- **The firmware catalog** (`catalog/`, `tools/catalog.py`): a firmware's listing in one file, by pull request; it
  is also the registry of firmware ids. CI checks each listing and downloads each release to check its checksum and
  built-in metadata; `docs/catalog.json` is the list Launchpad and the web flasher will read.
- **Launchpad and the web flasher offer the catalog's firmwares** ("More from the catalog"). Every download is
  checked against the catalog (size, checksum) and against what the image says it is before it can be flashed;
  firmwares not from OpenGP5 are marked *Not reviewed*. The shared code is `web/flasher/catalog.js`.
- **Community files:** CONTRIBUTING.md, a code of conduct, issue forms (bug, hardware report, platform feature) and a
  pull-request template.
- **Guides:** how a firmware works (concepts), the audio link, performance, debugging, releasing your firmware,
  questions, known limits, and a beta-testing checklist (BETA-TESTING.md). Every code example in them was built and
  run in the simulator.
- `GP5_FAST` (DSP): a function in RAM, for code that runs for every sample. `GP5_CYCLE_COUNT()` on the control
  processor too. `GP5_BLOCK` on the control processor (the samples in `on_block`).
- `audio.link_words` at most 264, the most measured to run reliably; the build's fused-multiply-add option is gone
  (it hangs the control processor).
- The simulator reports the largest screen update sent in one pass (a long one makes the controls and USB wait).
- `GP5_CYCLE_COUNT()` on the DSP: the cycle counter, instead of reading its hardware address.

### Removed (the last breaking changes before 1.0)
- **The 0.6 API.** Every firmware needs a `firmware.json`; the build refuses a folder without one. Gone with it:
  `fw_foot`, `fw_foot_release`, `fw_para_push`, `fw_para_release`, `fw_knob`, `fw_level`, `fw_active`,
  `fw_draw(knob, force)`, `fw_store_changed`, and the settings read from `dsp/firmware.h` (`GP5_AUDIO_LINK`,
  `GP5_LINK_WORDS`, `GP5_SAMPLE_RATE`, `GP5_CTL_MHZ`, `GP5_LINK_EARLY`): `firmware.json` sets them.
- **`GP5_DATA`**, the raw data area from before 0.4: use files (`gp5_file`).

### The 1.0 promise, checked
- `compat/1.0`: the examples as released, never edited; `tools/test_compat.sh` (in `test_all.sh`) builds and runs
  them in the simulator, CI builds them for the pedal, and the parameters test reads settings saved by 1.0
  (`platform/nds32/test/params-1.0.store.hex`).
- The memory a firmware has is promised for 1.x (`known-limits.md`): `compat/1.0/budget.json` caps what the
  platform itself takes, checked in CI by `tools/check_budget.py`.
- `GP5_DEPRECATED(instead)` marks what 1.x will retire, with a warning when you build.
- The USB commands only grow at their end; readers ignore what they don't know (`protocols.md`).
- The simulator refuses calls to functions that aren't declared (a removed or renamed one), instead of a warning.

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
