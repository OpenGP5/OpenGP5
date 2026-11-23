# Set up your computer

Install the tools, build the tremolo, and try it on your computer. You need a pedal to flash and play the build.
Download time depends on your connection.

## What you need

- Git, Python **3.9 or later**, and a C compiler (`cc`) for tests on your computer.
- A Valeton GP-5 and USB cable for flashing.
- The tools for your system below.

| System | Tools |
|---|---|
| **macOS** | Xcode command line tools (`xcode-select --install`) and Docker Desktop; start Docker before building |
| **Linux** (x86_64) | `curl` and a 32-bit C runtime; see the commands below |
| **Windows** | Windows Subsystem for Linux (WSL) with Ubuntu for builds; Windows Python for USB flashing |

The tool that builds code for the control chip runs on Linux. On macOS, Docker runs that tool.
On Linux, add the 32-bit C runtime for your system:

| Linux system | Command |
|---|---|
| Debian, Ubuntu | `sudo apt install libc6-i386` |
| Arch | `sudo pacman -S lib32-glibc` |
| Fedora | `sudo dnf install glibc.i686` |

For flashing, add these MIDI tools to the Python setup that can see the pedal:

```bash
pip install mido python-rtmidi
```

For screen images in the simulator, install Pillow with `pip install pillow`.
For the DSP emulator, install Unicorn with `pip install unicorn`.
These tools are optional if you do not use those checks.

## Get OpenGP5 and the compilers

Run the build commands in Terminal on macOS, a Linux shell, or an Ubuntu WSL shell on Windows:

```bash
git clone https://github.com/OpenGP5/OpenGP5.git
cd OpenGP5
release/setup-toolchain.sh
```

The setup script gets both build tools from Andes and Arm, about 190 MB in total.
It checks each file's checksum and keeps the tools in `fw-build/`. It makes no system-wide changes.
Each computer uses the same pinned tool versions.

## Build

From the OpenGP5 folder, run:

```bash
platform/build_firmware.sh tremolo
```

The build combines the [tremolo's](../../firmwares/tremolo/README.md) sound and control code in
`build/GP-5_tremolo.bin`. This image holds the DSP firmware and the control chip's app.
The DSP is the digital signal processor that handles sound.

## Try it on the computer

```bash
python3 tools/simulate.py tremolo
```

The simulator runs the sound, screen and scripted controls. It saves the results as files and has no live window.
See [The simulator](simulator.md) for input audio and control scripts.

## Flash

Read [Before you flash](../../README.md#safety) and back up your data.
Plug the pedal into the computer. On macOS or Linux, run these commands from the OpenGP5 folder:

```bash
python3 tools/gp5tool.py status
python3 tools/gp5tool.py flash build/GP-5_tremolo.bin --sections all
```

On Windows, flash with **Windows Python**. WSL cannot see the pedal's USB-MIDI ports.
Copy the built `.bin` to a Windows copy of the OpenGP5 folder, beside `tools`. Open PowerShell in that folder:

```powershell
py -3 tools/gp5tool.py status
py -3 tools/gp5tool.py flash GP-5_tremolo.bin --sections all
```

You can also choose the built file in Launchpad or the web flasher.
`status` checks that the tool can find the pedal. A flash takes about 40 seconds.
If the pedal does not return, see [Recovery](../../GETTING-STARTED.md#recovery).

## Next

Read [How a firmware works](concepts.md), then build [Your first effect](first-effect.md).
To keep the code in your own project, use [Your own repository](template.md).
