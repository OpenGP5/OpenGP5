# Getting started

## Just want to try it? Flash a prebuilt release

No compilers needed. Read the [Safety](README.md#safety) warning first.

**In your browser (easiest):** open the [web flasher](https://opengp5.github.io/OpenGP5/flasher/) in Chrome, Edge or Opera on a computer
(Safari can't talk to MIDI devices; Firefox asks for a permission first). Plug the pedal in, click
Connect, allow MIDI access, pick a firmware, tick the box and click Flash. It can also flash a `.bin` file
you have, including Valeton's firmware.

**From the command line:**

1. From the [Releases](https://github.com/OpenGP5/OpenGP5/releases) page, download a firmware image
   (`GP-5_tremolo.bin`, `GP-5_granular-delay.bin` or `GP-5_passthrough.bin`) and the release's
   **Source code (zip)**, which contains the flasher. Unzip the source.
2. Install Python 3.9 or newer, then: `pip install mido python-rtmidi`
3. Plug the pedal in and, from the unzipped folder:

   ```
   python3 tools/gp5tool.py status
   python3 tools/gp5tool.py flash GP-5_tremolo.bin --sections all
   ```

Optional: check the download against the release's `SHA256SUMS` (`shasum -a 256 GP-5_tremolo.bin` on
macOS / Linux, `certutil -hashfile GP-5_tremolo.bin SHA256` on Windows).

## 1. What you need

- A Valeton GP-5 and a USB cable.
- For building, one of:
  - **Linux** (x86_64) or **Windows with WSL** (Ubuntu): `python3`, `curl`, a C compiler (`cc`, for the
    computer-side tools) and the **32-bit C library** the control processor's compiler needs:
    Debian / Ubuntu / WSL `sudo apt install libc6-i386`, Arch `sudo pacman -S lib32-glibc`,
    Fedora `sudo dnf install glibc.i686`.
  - **macOS**: `python3`, the Xcode command line tools (`xcode-select --install`, for `cc`) and **Docker
    Desktop** (the control processor's compiler is a Linux program; it
    runs in a small container).
- For flashing: `python3` with `pip install mido python-rtmidi`, on the computer the pedal is plugged into
  (on Windows: Windows' own Python, not WSL, which cannot see USB-MIDI devices).

Then fetch both compilers into `fw-build/` (about 190 MB, from Andes' and Arm's own download pages,
checksum-checked, nothing installed system-wide). The versions are pinned, so every machine builds the same
bytes:

```
release/setup-toolchain.sh
```

## 2. Build

```
platform/build_firmware.sh tremolo
```

This builds both halves and packs them into one image, `build/GP-5_tremolo.bin`, which holds only the
two parts we replace: the DSP firmware and the control-processor app. Valeton's other data on the pedal
(audio models, presets) is not touched.

## 3. Flash

Plug the pedal in, then:

```
python3 tools/gp5tool.py status                                        # finds the pedal
python3 tools/gp5tool.py flash build/GP-5_tremolo.bin --sections all
```

The tool checks the whole image before it sends anything, restarts the pedal into its update mode by
itself, writes, and waits for the pedal to come back (about 40 s). Keep it connected and powered.

## 4. Recovery, and going back to Valeton's firmware

The pedal's factory loader cannot be overwritten over USB, so in the usual case a bad flash is
recoverable:

1. Unplug the pedal. Hold the **foot switch** and the **PARA knob** pressed, plug it back in. The screen
   stays dark: that is update mode.
2. Flash again: your previous image, or Valeton's firmware.

To go back to Valeton's firmware, download the GP-5 update from Valeton and flash all of it with this
tool (Valeton's own updater, Valeton Suite, is not reliable for this):

```
python3 tools/gp5tool.py flash <Valeton GP-5 firmware .bin> --sections all
```

> [!WARNING]
> **In some circumstances USB recovery is not enough** (for example a pedal whose DSP updater stalls, see
> Known limits). Recovering those needs an FT232H USB adapter wired to the pedal's internal debug pads,
> which takes some soldering experience. Only flash if you are comfortable with the possibility of a
> bricked pedal.

## 5. Write your own

`docs/platform/first-effect.md`. Try your audio code on the computer first:
`python3 tools/run_effect.py <name> in.wav out.wav`.

## Known limits

- **Tested on one pedal.** That pedal had a fault in Valeton's DSP updater (the part of the DSP that every
  update goes through, Valeton's own included): every update stalled at its start. It was fixed on that
  unit with a 4-byte patch, applied once (`platform/dsp/tickpatch.h`). Valeton ships updates to every
  customer through the same updater, so the fault is most likely limited to some units, but no second pedal
  has been tried yet. **If your pedal has it,** the flash stops at the start of the DSP part, before
  anything is written ("stalled" / timeout from `gp5tool.py`): unplug the pedal, plug it back in, and it
  runs what it ran before. Please report it; such pedals need a debug probe for the one-time patch.
- Bluetooth, the audio models and Valeton's presets are not used by these firmwares. Audio is mono in,
  stereo out, 44.1 kHz.
- To report: your pedal's hardware version if you know it, whether `--sections all` completed, and
  the output of `python3 tools/gp5tool.py status` and `python3 tools/gp5_link.py stats`.
- One screen font, no drawing helpers beyond text and raw pixels yet.
