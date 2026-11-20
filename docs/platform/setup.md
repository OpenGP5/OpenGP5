# Set up your computer

Everything you need to build a firmware and flash it. It takes about ten minutes, most of it downloading.

## What you need

- A Valeton GP-5 and a USB cable.
- One of these systems:

| System | Install |
|---|---|
| **macOS** | `python3`, the Xcode command line tools (`xcode-select --install`), and **Docker Desktop**. The control processor's compiler is a Linux program; it runs in a small container |
| **Linux** (x86_64) | `python3`, `curl`, a C compiler (`cc`), and the **32-bit C library**: `sudo apt install libc6-i386` (Debian, Ubuntu), `sudo pacman -S lib32-glibc` (Arch), `sudo dnf install glibc.i686` (Fedora) |
| **Windows** | WSL with Ubuntu, then as for Linux. Flash from Windows' own Python, not WSL: WSL can't see USB-MIDI devices |

- For flashing: `pip install mido python-rtmidi`, on the computer the pedal is plugged into.
- Optional, for the checks on the computer: `pip install pillow unicorn` (the simulator's screens as PNG, and the
  DSP emulator).

## Get OpenGP5 and the compilers

```bash
git clone https://github.com/OpenGP5/OpenGP5.git
cd OpenGP5
release/setup-toolchain.sh
```

`setup-toolchain.sh` downloads both compilers (about 190 MB) from Andes' and Arm's own pages into `fw-build/`,
checks their checksums, and installs nothing system-wide. The versions are pinned, so every computer builds the
same bytes.

## Build

```bash
platform/build_firmware.sh tremolo
```

This builds both halves of the [tremolo](../../firmwares/tremolo/README.md) and packs them into one image,
`build/GP-5_tremolo.bin`. It holds only the two parts OpenGP5 replaces: the DSP firmware and the control
processor's app.

## Try it on the computer

```bash
python3 tools/simulate.py tremolo
```

runs the whole firmware on your computer: the sound, the screen and scripted controls. See
[The simulator](simulator.md).

## Flash

Plug the pedal in, then:

```bash
python3 tools/gp5tool.py status
python3 tools/gp5tool.py flash build/GP-5_tremolo.bin --sections all
```

`status` checks that the pedal is found. The flash takes about 40 seconds; the pedal restarts by itself. If it
doesn't come back, see [Recovery](../../GETTING-STARTED.md#recovery).

## Next

[How a firmware works](concepts.md), then [Your first effect](first-effect.md). To keep your firmware in its own
repository from the start, see [Your own repository](template.md).
