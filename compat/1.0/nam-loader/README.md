<h1 align="center">NAM Loader</h1>

<p align="center"><b>Neural Amp Modeler captures on the Valeton GP-5, each with its own cab IR, noise gate and EQ.</b></p>

<p align="center">
  <a href="https://github.com/OpenGP5/NAM-Loader/releases/latest"><img alt="Version 1.3.1" src="https://img.shields.io/badge/version-1.3.1-3cc0c1?style=flat-square"></a>
  <a href="https://github.com/OpenGP5/OpenGP5"><img alt="Built on OpenGP5 0.8.0-alpha" src="https://img.shields.io/badge/built%20on-OpenGP5%200.8.0--alpha-5b6b7b?style=flat-square"></a>
  <a href="LICENSE"><img alt="Licence: MIT" src="https://img.shields.io/badge/licence-MIT-3ee8b5?style=flat-square"></a>
  <img alt="Status: alpha" src="https://img.shields.io/badge/status-alpha-e8590c?style=flat-square">
</p>

<p align="center">
  <a href="https://opengp5.github.io/docs/firmwares/nam-loader/"><b>User guide</b></a> ·
  <a href="https://github.com/OpenGP5/NAM-Loader/releases/latest"><b>Download</b></a> ·
  <a href="https://github.com/OpenGP5/NAM-Loader/issues">Report a problem</a>
</p>

---

NAM Loader plays [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) A2 captures on the pedal
itself, at 48 kHz, the rate captures are trained at. It holds **30 captures** and **12 cab IRs**. Any capture can
play any IR, or none, and each capture keeps its own IR choice, noise gate and 3-band EQ.

The pedal runs the capture's own network, every layer and every weight, unchanged. It's computed in fixed point
(16-bit weights, a 32-bit signal through the network), because the pedal's processors are too slow at floating
point to run it in real time. The result is about 65 dB from NAM's own floating-point output on guitar.

> [!NOTE]
> **Alpha.** Flashing custom firmware can go wrong. Read
> [Before you flash](https://opengp5.github.io/docs/get-started/safety/) first. Installing NAM Loader over another
> firmware wipes that firmware's presets and files: back them up first.

## Install

- **With [OpenGP5 Launchpad](https://github.com/OpenGP5/Launchpad):** it comes with NAM Loader. Open **Flash**,
  choose NAM Loader, and flash. Launchpad is also how you put captures and IRs on the pedal.
- **From a release:** download `GP-5_nam-loader.bin` (up to 1.3.1: `GP-5_nam-player.bin`) from [Releases](https://github.com/OpenGP5/NAM-Loader/releases/latest)
  and flash it with Launchpad (**Your own file**) or from the command line:
  ```bash
  python3 opengp5/tools/gp5tool.py flash GP-5_nam-loader.bin --sections all
  ```

Updating from an earlier NAM Loader keeps your captures, IRs and settings.

## Controls

The screen has two pages: **NAM** shows the capture playing, **CAB** shows its IR. Two dots in the header show
which page you're on.

| Control | Does |
|---|---|
| **Turn PARA** | Switch between the NAM and CAB pages. In a list: move through it |
| **Push PARA** | On NAM: the 30 capture slots. On CAB: the IRs, with NONE. In a list: load the highlighted one |
| **Hold PARA** | The TONE page: gate, bass, mid and treble of the capture playing |
| **Foot switch** | Bypass |
| **Volume knob** | Output level |

While you browse a list, the capture playing keeps playing; after 5 seconds without turning, the list closes. The
pedal remembers which capture was playing when you power it off.

## Captures and IRs

Use [Launchpad's Tones page](https://opengp5.github.io/docs/launchpad/tones/): drop a `.nam` file and it's converted
and written into a slot. Cab IRs (`.wav`) go in the same way. With no captures on the pedal, it plays its built-in
demo capture.

- **Captures** must be **A2** (NAM Loader runs their A2-Lite part). Older A1 captures can't be played.
- **Calibrate** (optional, on by default): a capture that records its input level gets matching input gain, so
  it's driven as hard as the original gear. The pedal's input is +7.1 dBu at full scale.
- **Normalize** (optional, on by default): every capture is set to the same loudness. Without it, captures can
  differ by 30 dB or more. Only the capture's input and output gain change, never the network.
- **Cab IRs** add a speaker cabinet to a capture of an amp alone (a "DI" capture). Each is 1,024 samples at 48 kHz
  (21 ms); Launchpad resamples and trims any `.wav`. If the processor ever runs short of time, the IR switches
  itself off rather than interrupting the sound.

## What's new

| Version | Changes |
|---|---|
| 1.3.1 | Launchpad can switch the capture playing (*Play on the pedal*) |
| 1.3.0 | 30 captures and 12 IRs, chosen independently; the NAM and CAB pages |
| 1.2.0 | A noise gate and a 3-band EQ for each capture |
| 1.1.0 | Cab IRs, 48 kHz, Calibrate and Normalize |
| 1.0.0 | First release: 12 capture slots |

Captures added before 1.1 don't have Calibrate and Normalize applied: send them again from Launchpad to get them.

## Build from source

You need what [OpenGP5 needs](https://opengp5.github.io/docs/platform/setup/): Python 3, and Docker Desktop on
macOS or the 32-bit C library on Linux and WSL.

```bash
git clone --recursive https://github.com/OpenGP5/NAM-Loader.git
cd NAM-Loader
opengp5/release/setup-toolchain.sh      # once: the two compilers, into opengp5/fw-build/
./build.sh                              # -> build/GP-5_nam-loader.bin
python3 opengp5/tools/gp5tool.py flash build/GP-5_nam-loader.bin --sections all
```

To rebuild the built-in capture:

```bash
python3 tools/a2r_export.py A2.nam synth default_capture.inc --name "NAM A2 demo"
```

`synth` is a made-up calibration signal (`tools/calib_synth.py`) used to find each layer's range, in place of
NAM's own input recording.

## How it's made

The model is split across both of the pedal's processors: the DSP runs the first layers, the control processor
(at 320 MHz) the rest, the output and the cab IR.

| File | What |
|---|---|
| `kernel/a2r.c`, `a2r.h` | The A2-Lite network in fixed point with a 32-bit residual stream; builds for the control processor, the DSP and the computer |
| `kernel/a2r_blob.c`, `a2r_blob.h` | The capture file format: the quantized weights, their scales and the name |
| `split.h` | Where the model is split, the sizes of the capture and IR banks, and the audio link's words |
| `pack24.h` | The DSP's results packed as 24-bit block floating point, so they fit the audio link at 48 kHz |
| `nds32/a2.c` | The control processor's layers, the output and the cab IR; the capture and IR banks, and switching between them |
| `nds32/firmware.c` | The controls, the screen (NAM and CAB pages, the lists, TONE), and each capture's gate, EQ and IR |
| `dsp/firmware.h` | The DSP's layers, the bypass and the dry signal |
| `tone.h` | The noise gate and the 3-band EQ, in fixed point |
| `cab/cabq.h` | The cab IR: a fixed-point partitioned FFT convolver |
| `firmware.json`, `params.h` | Who it is (OpenGP5 1.0) and its parameters: cab IR, taps, gate, bass, mid, treble, the IR chosen, the capture playing |
| `default_capture.inc` | The built-in capture (see `CAPTURE-LICENSE.txt`) |
| `tools/a2r_export.py` | Converts a `.nam` file into a capture file, with Calibrate and Normalize (`--no-calibration`, `--no-level`); needs numpy and soundfile |
| `tools/make_cab.py` | Converts a `.wav` IR into a cab file (the older float format, which the pedal still plays); needs numpy, soundfile and scipy |
| `opengp5/` | The platform, a git submodule pinned to a tested release (0.8.0-alpha) |

## Licence

MIT (`LICENSE`). The built-in capture is NeuralAmpModelerCore's example A2 model, MIT (`CAPTURE-LICENSE.txt`).

Not affiliated with or endorsed by Valeton or the Neural Amp Modeler project. "Valeton" and "GP-5" are Valeton's
trademarks, used only to say which pedal this runs on.
