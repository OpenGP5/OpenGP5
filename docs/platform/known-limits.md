# Known limits

This page lists known faults, tested limits and features the platform does not provide.
Report a problem if it does not appear here.

## The hardware and flashing

### Reported DSP update fault

As of **November 2026**, reports cover about seven GP-5s that ran or tried OpenGP5.
Four reports describe a fault that stalls the DSP updater. This small sample cannot show how often the fault occurs
across all GP-5s. These are reports of a fault in the pedal's updater, not evidence that OpenGP5 caused it.

The fault blocks Valeton updates as well as OpenGP5. The flash stops at the start of a DSP section, before it writes
that section. Tools may say **stopped acknowledging at the erase**; Valeton Suite may remain at **0 %**.
If the pedal still has its old build, unplugging and reconnecting it lets that firmware run.

The fix changes four bytes in the DSP updater. To get an OpenGP5 build onto an affected pedal, you need an FT232H
USB debug adapter wired to J7 inside the case. Then `gp5tool.py tickfix --apply` can write the permanent fix.
Read [J7 recovery](j7-recovery.md) before starting.

### Other flash and backup limits

- **USB recovery has limits.** The factory loader survives a USB update, but the DSP fault above can still block a flash.
- **Use the OpenGP5 tools to go back.** Valeton Suite can fail when returning from OpenGP5.
  Use Launchpad, the web flasher or `gp5tool.py`.
- **Launchpad cannot back up Valeton SnapTones or user IRs.** Export them in Valeton Suite before switching.
  Launchpad can save the 100 presets. Global settings stay on the pedal.
- **The macOS MIDI service may not be ready.** For an `RtMidi -304` error before any data has been sent,
  the tools try up to three times. If a send has started, they stop and report the error instead of starting over.

## What firmware can use

Both chips run C. The DSP, or digital signal processor, handles sound.
The control chip runs the screen, controls, USB and storage. It can share sound work with the DSP.

| Resource | Limit |
|---|---|
| Audio | Mono input, stereo output; 44.1 or 48 kHz; 64-sample blocks |
| DSP variables and fast code | 19,712 bytes shared with `GP5_FAST` code |
| DSP large buffers | 122,880 bytes through `GP5_LARGE` |
| DSP code and constants | 38,920 bytes |
| Control-processor RAM | 258,016 bytes |
| Control-processor code and constants | About 1.4 MB |
| Storage | About 300 KB; store records up to 1 KB, files and up to 64 presets |
| Parameters | Signed whole numbers, −32,768 to 32,767; about 250 in one saved set |
| Audio link | Up to 264 32-bit words each way per block; round trip of three blocks |
| Screen | 160 × 80 pixels |

A round trip takes about 4.4 ms at 44.1 kHz or 4 ms at 48 kHz.
A saved parameter set uses four bytes per entry in a 1 KB record.
The [1.x memory promise](api.md#versions) is part of the planned 1.0 API; `tools/check_budget.py` checks the budgets.

A different firmware starts with no saved data. Back up before you switch.
The code does not use Valeton's amp models or presets.

### Features and processor limits

- **The control chip's fused multiply-add instruction hangs the chip.** The platform build does not use it.
  Its float unit needs about five cycles per operation. See [Performance](performance.md).
- **Screen tools cover text and pixels.** The UI kit adds settings pages and menus.
  The platform has no general line, shape or image drawing helpers.
- **The build uses C with no standard C library.** It has its own code for maths and text.

## The tools

- **The web flasher** uses Web MIDI. Use Chrome, Edge or Opera on a computer. Safari lacks this feature;
  Firefox asks for permission. The page downloads OpenGP5 builds. For other catalog builds, download the image
  and drop it on the page so it can check the file.
- **Your system may warn you when you open Launchpad.** The app has no code signature.
  Use the [install steps](https://opengp5.github.io/docs/launchpad/#install) to open it.
- **The simulator** runs scripts and saves screen images; it has no live window. It does not measure processor speed.
  Check timing on the pedal.
- **NAM Loader** runs the A2-Lite part of A2 captures. It does not support older A1 captures.
