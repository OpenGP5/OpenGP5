# DSP updater fault: help wanted

Some GP-5s cannot update their DSP: every flash stops at the start of a DSP section, with every tool, Valeton Suite
included. The cause is known and a four-byte fix exists, but on a pedal that stalls, installing that fix needs the
pedal opened and a debug adapter wired to J7 ([J7 recovery](j7-recovery.md)). This page is for contributors who want
to help find a way to apply the fix **over USB, without opening the pedal**.

Status, November 2026: about seven GP-5s have reported in; four have the fault. The fix is tested on one of them.

## What goes wrong

The DSP's flash starts with Valeton's resident updater (DSP flash `0x0`–`0x3FFF`). The pedal's loader resets the DSP
into it at the start of every update. Its flash set-up resets the flash chip and then waits 12 SysTick ticks.

On affected pedals, memory-mapped flash reads in that window come back with **bit 7 of byte 0 of each 32-bit word
cleared**. The SysTick handler runs from flash and loads the tick counter's address from a literal at DSP flash
`0xB60`: `0x20000DC0` reads as `0x20000D40`. The handler counts in the wrong word; the wait, which runs from a RAM
copy (its literal is at flash `0x16DC`), reads the right one. The wait never ends, and the update stalls before the
first erase. (The wrong word happens to be the command word of the flash-reset descriptor, so a stalled updater also
slowly corrupts that.)

What clears the fault, and why only some pedals have it, is not known.

**The resident updater is not in Valeton's update file.** The file's DSP sections start at DSP flash `0x4000`. The
resident can only be read from a pedal over J7. Nothing from it is published here.

## The fix

Both literals are changed from `0x20000DC0` to `0x20000000`, whose byte 0 has no bit 7, so a faulty read gives the
same value as a good one. `0x20000000` holds dead code in the resident's RAM image, and the wait compares an unsigned
difference, so the counter's starting value doesn't matter. The change only clears bits: two 2-byte page programs,
no erase, safe to repeat after an interruption. SysTick runs only in the update path, so normal start-up never uses
these words.

- [`platform/dsp/tickpatch.h`](../../platform/dsp/tickpatch.h) applies it from OpenGP5's DSP firmware. Its header
  explains how: it copies the resident's RAM image where the resident puts it, then calls the resident's own
  page-program routine from a small RAM stub, with interrupts off. It refuses any resident whose hash isn't the
  known one.
- `gp5tool.py tickfix` reports the state; `--apply` applies it.
- [`tools/dsp_tickfix_emu.py`](../../tools/dsp_tickfix_emu.py) checks the patcher and the stall in an emulator
  (Unicorn), without a pedal. It needs a resident read from a pedal.

The catch: the patcher runs inside OpenGP5's DSP firmware, and getting that firmware onto the DSP is the flash that
stalls. Today, J7 breaks that loop by moving the counter by hand once ([j7_nudge.py](../../tools/j7_nudge.py)).

## What is known not to work

- **A fix inside the `dsp` section.** The loader resets the DSP into the resident for every update session, so
  nothing in the `dsp` section runs at that point.
- **The resident fixing itself.** Its updater accepts a write to its own area, but it stalls before it takes any
  command.
- **Valeton Suite.** It uses the same updater and stalls the same way.
- **The control processor writing DSP flash.** It flashes its own app without the stall, but no path from it to the
  DSP's flash is known.

## The open problem

Find a way to clear those four bytes on a pedal that stalls, using only USB. What still works on such a pedal:

- The control processor's app (`app`) and the presets flash normally, so an OpenGP5 app can be installed.
- Valeton's DSP firmware keeps running, and the app can pass commands to it:
  [Valeton's DSP firmware: link commands](valeton-dsp-link.md) lists what is known.
- If an update left the pedal starting in update mode, `gp5tool.py unstick` gets it running again
  ([Pedal stuck in update mode](j7-recovery.md#pedal-stuck-in-update-mode)).

## Leads

**1. SnapTone uploads.** Valeton Suite uploads SnapTones to a running pedal, with no update session. User IRs are no
help here: they are stored in the control processor's flash (`0x1BE000` on), not the DSP's.

- Where does a SnapTone upload land: DSP flash, DSP RAM, or the control processor's flash?
- If DSP flash: Valeton's DSP firmware has its own flash-write path. Is the target address checked? Could a crafted
  upload clear two bytes at `0xB60` and `0x16DC`?
- The slot-management commands (`0x7001`, `0x7003`, and `0x2001`, `0x2003` for cabinets) are listed with their
  handler addresses but not decoded.

**2. The other DSP commands.** Is there one that writes memory, or one whose length checks let it run code? A small
stub would be enough: the resident's own page-program routine does the writing, as `tickpatch.h` shows.

**3. A wiring check.** J7 carries the DSP's SWD lines. If SWDIO and SWCLK also reach pins of the control processor
(BP1048), OpenGP5 could do the J7 step itself from the app. Needed: a continuity check between each J7 pin and the
BP1048's pins on an opened pedal, with the pedal unplugged. The pinout is in [J7 recovery](j7-recovery.md).

**4. The flash chip itself.** The DSP's flash is a separate SPI NOR chip. A clip-on programmer could clear the four
bytes with the DSP held in reset. This still means opening the pedal, but no debug wiring. Untested; the chip's
position and type aren't documented yet.

## What else would help

- **Reports from more pedals**, with or without the fault, so we learn how common it is.
- **The resident's fingerprint from affected pedals.** The fix refuses any resident but the known one. If the four
  affected pedals have different residents, each needs checking. `j7_nudge.py check` compares the resident's vector
  table with the known one over J7, read-only.

## Caveats

- The read-fault model comes from one pedal. If other pedals lose other bits, the patch may not be enough.
- Two UART interrupt vectors and one more literal in the resident also have bit 7 set in byte 0; the patch can't fix
  them by clearing bits. They matter only if the control processor sends data during the ~12-tick window after the
  flash reset. It doesn't seem to, but that isn't measured.
- After the fix, a J7 nudge must target `0x20000000`; `j7_nudge.py` refuses a patched resident.

## Ground rules

- **No Valeton code or data in the repository**: no disassembly listings, dumps or copied bytes. Work from your
  own copy of Valeton's firmware file (V1.0.6 from Valeton's site) and describe findings in your own words, with
  addresses.
- **Nothing that writes the resident** (`0x0`–`0x3FFF`) goes on a pedal except the four-byte bit-clear above.
  A broken resident takes away the DSP's update path.
- Read-only experiments first. Anything that writes needs a tested way back.
- Share findings in an issue or pull request. Contributors are credited by their GitHub handle.
