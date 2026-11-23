# Known limits

What OpenGP5 can't do, or hasn't been shown to do yet. If you hit something that isn't here, please report it.

## The hardware and flashing

- **Run on a handful of pedals.** At least seven GP-5s have run or tried OpenGP5 (users' reports, November 2026),
  Dreamcatcher and NAM Loader among the firmwares.
- **Some pedals stall every update** at its start, Valeton's own updates too: a fault in the DSP's updater, the part
  of the DSP that every update goes through. Four pedals so far, out of about seven: not rare. The flash stops at the
  start of the DSP part, before anything is written to it ("stopped acknowledging at the erase", or Valeton Suite at
  0 %). Unplug the pedal and plug it back in: it runs what it ran before. The fix is four bytes in that updater,
  written once by OpenGP5's firmwares (`gp5tool.py tickfix`), but getting the first OpenGP5 firmware onto such a
  pedal needs a USB debug adapter (an FT232H) wired to the header J7 inside it, once:
  [J7 recovery](j7-recovery.md).
- **Recovery over USB covers almost everything, not everything.** A pedal whose updates stall (above) needs an
  FT232H USB adapter wired to J7 inside it once, which takes some soldering. Only flash if you accept that risk.
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
