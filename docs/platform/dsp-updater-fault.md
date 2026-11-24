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
first erase.

What clears the fault, and why only some pedals have it, is not known.

## The fix

Both literals are changed from `0x20000DC0` to `0x20000000`, whose byte 0 has no bit 7, so a faulty read gives the
same value as a good one. The change only clears bits: two 2-byte page programs, no erase, safe to repeat after an
interruption.

- [`platform/dsp/tickpatch.h`](../../platform/dsp/tickpatch.h) applies it from OpenGP5's DSP firmware. It calls
  the resident's own page-program routine, and refuses any resident whose hash isn't the known one.
- `gp5tool.py tickfix --apply` starts it over USB.

The catch: the patcher runs inside OpenGP5's DSP firmware, and getting that firmware onto the DSP is the flash that
stalls. Today, J7 breaks that loop by moving the counter by hand once ([j7_nudge.py](../../tools/j7_nudge.py)).

## The open problem

Find a way to clear those four bytes on a pedal that stalls, using only USB. What still works on such a pedal:

- The control processor's app (`app`) and the presets flash normally, so any app OpenGP5 can build runs.
- Valeton's DSP firmware keeps running, and the app can send it commands over the link between the processors.
- If an update left the pedal starting in update mode, `gp5tool.py unstick` gets it running again
  ([Pedal stuck in update mode](j7-recovery.md#pedal-stuck-in-update-mode)).

## Leads

**1. Valeton's upload path.** Valeton Suite uploads SnapTones and user IRs to a running pedal, with no update
session. Questions to answer from Valeton's firmware file:

- Where does an upload land: DSP flash, DSP RAM, or the control processor's flash?
- If DSP flash: Valeton's DSP firmware has its own flash-write path. Is the target address checked? Could a
  crafted upload clear two bytes at `0xB60` and `0x16DC`?
- The upload commands between the two processors look like groups 2 and 7 of the link commands. Their format
  isn't decoded yet.

**2. The other DSP commands.** Valeton's DSP firmware answers many more commands from the control processor than
the ones decoded so far (presets, parameters, module lists). Is there one that writes memory, or one whose length
checks let it run code? Running a small stub would be enough:
the resident's own page-program routine does the writing, as `tickpatch.h` shows.

**3. A wiring check.** J7 carries the DSP's SWD lines. If SWDIO and SWCLK also reach pins of the control processor
(BP1048), OpenGP5 could do the J7 step itself from the app. Needed: a continuity check between each J7 pin and the
BP1048's pins on an opened pedal, with the pedal unplugged. The pinout is in [J7 recovery](j7-recovery.md).

Also useful: a report from any pedal, with or without the fault, so we learn how common it is.

## Ground rules

- **No Valeton code or data in the repository**: no disassembly listings, dumps or copied bytes. Work from your
  own copy of Valeton's firmware file (V1.0.6 from Valeton's site) and describe findings in your own words, with
  addresses.
- **Nothing that writes the resident** (`0x0`–`0x3FFF`) goes on a pedal except the four-byte bit-clear above.
  A broken resident takes away the DSP's update path.
- Read-only experiments first. Anything that writes needs a tested way back.
- Share findings in an issue or pull request. Contributors are credited by their GitHub handle.
