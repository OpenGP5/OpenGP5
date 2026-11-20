# Known limits

What OpenGP5 can't do, or hasn't been shown to do yet. If you hit something that isn't here, please report it.

## The hardware and flashing

- **Run on a handful of pedals.** At least four GP-5s run OpenGP5 (users' reports, November 2026), Dreamcatcher and
  NAM Loader among the firmwares, with no problems reported. The first pedal, though, had a fault in Valeton's DSP
  updater, the part of the DSP that every update goes through, Valeton's own included: every update stalled at its
  start. It was fixed on that unit with a 4-byte patch, applied once (`platform/dsp/tickpatch.h`). None of the other
  pedals has reported it, and Valeton ships its own updates through the same updater, so it is most likely limited to
  some units.

  **If your pedal has it,** the flash stops at the start of the DSP part, before anything is written ("stalled" or
  a timeout). Unplug the pedal and plug it back in: it runs what it ran before. Please report it, because such pedals
  need a debug adapter for the one-time patch.
- **Recovery over USB covers almost everything, not everything.** In rare cases a pedal needs an FT232H USB adapter
  wired to its internal debug pads, which takes some soldering. Only flash if you accept that risk.
- **Valeton's own updater (Valeton Suite) is not a reliable way back.** Use Launchpad, the web flasher or `gp5tool.py`
  to flash Valeton's firmware.
- **SnapTones and user IRs from Valeton's firmware can't be backed up** by OpenGP5's tools. Export them in Valeton
  Suite before leaving Valeton's firmware. Launchpad backs up the 100 presets. Global settings stay on the pedal.
- **On macOS the MIDI system is sometimes not ready** ("RtMidi -304"), most often right after a flash. The tools
  then try again by themselves, up to 3 times, as long as nothing was sent to the pedal yet. If it happens in the
  middle of talking to the pedal, they stop and say so instead of starting over.

## What firmwares can use

- **Audio:** mono in (the guitar), stereo out, 44.1 or 48 kHz, 64-sample blocks. Valeton's amp models, presets and
  Bluetooth aren't used.
- **Memory** a firmware has, promised for all of 1.x (`tools/check_budget.py` keeps the platform within its share):
  - DSP: 19,712 bytes for variables (and `GP5_FAST` code), 122,880 bytes for buffers (`GP5_LARGE`), 38,920 bytes for
    code and constants.
  - Control processor: 258,016 bytes of RAM, about 1.4 MB for code and constants.
- **Storage:** about 300 KB for the installed firmware: store records of up to 1 KB, files, parameters, and up to 64
  presets. Installing another firmware starts empty.
- **Parameters** are whole numbers from -32,768 to 32,767, at most about 250 per firmware (a saved set holds 4 bytes
  for each, in a 1 KB record).
- **The audio link:** at most 264 words each way per block, arriving 3 blocks (about 4.4 ms) later.
- **The control processor's float unit** takes about 5 cycles per operation, and its fused multiply-add hangs it:
  [performance](performance.md).
- **The screen** is 160 × 80, drawn from code: text (the built-in font, smooth fonts), pixels, and the UI kit's
  settings page and menu. There are no other drawing helpers yet (lines, shapes, images).
- **Languages:** C only, without a C library.

## The tools

- **The web flasher** needs Web MIDI: Chrome, Edge or Opera on a desktop computer. Safari doesn't have it; Firefox
  asks for permission. It can download OpenGP5's own firmwares itself. Others from the catalog you download first,
  and the page checks them when you drop them in.
- **Launchpad isn't signed.** macOS needs a one-time command to open it (its README says which); Windows shows a
  SmartScreen warning.
- **The simulator** doesn't count cycles, and has no window: it runs a script and saves screens as pictures.
  Speed is measured on the pedal.
- **NAM Loader** plays the A2-Lite part of NAM A2 captures; older A1 captures can't be played.
