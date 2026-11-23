# J7 recovery

Some GP-5s stop **every** firmware update right at its start, Valeton's own updates included. The flasher says the
pedal "stopped acknowledging at the erase of the dsp section", or Valeton Suite sits at 0 %. This page gets such a
pedal past it, once, with a small USB adapter wired to a header inside the pedal. After that, the pedal flashes over
USB like any other and the adapter isn't needed again.

**Symptoms** (all of them):

- The flash stops at the start of a DSP part (`dsp`, `dsp-data` or `models`), before anything is written to it.
- It happens every time, with every tool: Launchpad, the web flasher, `gp5tool.py`, Valeton Suite.
- The app part (`app`) and the presets (`presets`) on their own do flash.

If your pedal still starts normally after unplugging it and plugging it back in, nothing is broken: it runs what it
ran before. You only need this page to install anything new. If your pedal now always starts on the **BOOTLOAD**
screen, it needs this page to get going again.

**Cause.** Every update goes through a small updater on the pedal's DSP chip. On these pedals, right
after the updater resets its flash memory, a read of that memory comes back wrong for a moment. The updater's timer
then counts in the wrong place, and the updater waits for it forever. The fix moves the counter to an address where
the wrong read doesn't matter: four bytes, written once. OpenGP5's firmwares carry that fix, but to run it, one
OpenGP5 firmware must first get onto the pedal: that one update is what this page gets through.

> **Risk.** This means opening the pedal, soldering three wires (or holding them on with test clips) and running
> a debugger. Done carefully it is safe: nothing on this page erases or rewrites flash by itself, and every step
> checks before it changes anything. But it is your pedal, and the risk is yours.

## What you need

- An **FT232H** USB board (the common "CJMCU-232H" or Adafruit FT232H breakout).
- Three short wires. Soldering them to J7 is the most reliable; small test clips or header pins pressed into the
  holes can work.
- A computer with **Python 3** and **OpenGP5's tools** (the `tools` folder from the OpenGP5 download:
  `pip install mido python-rtmidi`).
- **OpenGP5 firmware** to install: any OpenGP5 firmware image (`.bin`) built on OpenGP5 1.0, for example the
  tremolo from the web flasher, Dreamcatcher or NAM Loader.
- **OpenOCD**, the debugger program:
  - macOS: `brew install open-ocd`
  - Linux: `sudo apt install openocd` (or your distribution's package)
  - Windows: OpenOCD from the xPack project (`xpack-openocd`), and once, the **WinUSB** driver for the FT232H with
    [Zadig](https://zadig.akeo.ie/) (pick the FT232H's "Interface 0", WinUSB, "Replace driver").

## Wiring

J7 is a row of five holes on the main board, next to the chip marked **JAT** (the DSP). Pin 1 is marked with a
triangle.

![J7 beside the JAT chip: from the triangle, GND, RST, SWCLK, SWDIO, SWO](../assets/j7-pinout.webp)

| J7 pin | Signal | FT232H |
|---|---|---|
| 1 (triangle) | GND | GND |
| 2 | RST | not connected |
| 3 | SWCLK | AD0 |
| 4 | SWDIO | AD1 and AD2, joined |
| 5 | SWO | not connected |

Join AD1 and AD2 on the FT232H with a short wire, and run one wire from there to pin 4. No resistors are needed.
Some FT232H boards label the pins D0, D1, D2 instead of AD0, AD1, AD2.

Plug the pedal and the FT232H into the same computer so they share a ground. The pedal can be powered from its USB
cable as usual.

## Steps

Run these in the OpenGP5 folder (where `tools` is).

**1. Put the pedal in update mode.** Unplug it, hold the **foot switch** and the **PARA** knob, plug it in, let go.
The screen shows the update warning. (A pedal that always starts on the BOOTLOAD screen is already there.)

```bash
python3 tools/gp5tool.py status
```

It should say `update mode`.

**2. Check that the debugger sees the stall.** This only reads.

```bash
python3 tools/j7_nudge.py check
```

You want: `The DSP's updater is stalled the known way`. If OpenGP5 can't connect, see [Troubleshooting](#troubleshooting).

**3. Get the updater past it,** for this session only (in the DSP's working memory; unplugging undoes it):

```bash
python3 tools/j7_nudge.py nudge
```

You want: `Done: the updater's timer counts again`.

**4. Flash an OpenGP5 firmware, all of it,** without unplugging the pedal in between:

```bash
python3 tools/gp5tool.py flash GP-5_tremolo.bin --sections all
```

(Any OpenGP5 1.0 firmware image instead of `GP-5_tremolo.bin`.) This now runs through the DSP parts too.

**5. Start the pedal.** Unplug it and plug it in again, without holding anything. It starts the firmware you flashed.

**6. Apply the fix itself.** This writes the four bytes into the DSP's updater (two small writes, no erase):

```bash
python3 tools/gp5tool.py tickfix
python3 tools/gp5tool.py tickfix --apply
```

The first command says `not applied`, the second `applied: updates won't stall`.

**Done.** Take the wires off (or leave them, they do no harm). From now on the pedal updates over USB like any
other: any OpenGP5 firmware, and Valeton's firmware too. `gp5tool.py tickfix` tells you any time whether the fix is
on the pedal.

## Troubleshooting

- **OpenOCD can't connect** ("Error connecting DP", no DAP info): check the three wires first. If they're right,
  swap the wires on pins 3 and 4 once and try again. On Linux, OpenOCD may need permission for USB devices (run it
  once with `sudo`, or add a udev rule for `0403:6014`). On Windows, the FT232H needs the WinUSB driver (Zadig).
- **`check` says it isn't the known stall:**
  - `pc ... isn't in the updater's wait`: the pedal isn't in update mode, or it isn't stalled (it may be a pedal
    that doesn't have this problem). Do step 1 again.
  - `the vector table was already moved`: you already ran `nudge` in this session: go on with step 4.
  - `the DSP's vector table isn't the one this was made for`: your pedal's updater is a different version from
    the one this fix was made on. **Stop here** and report it (below): nothing was changed.
- **`tickfix --apply` says the updater "isn't the known one"**: the same: report it, nothing was written.
- **The flash in step 4 still stops**: unplug the pedal, start again at step 1, and report it with the output.

## Reporting results

Whether it worked or not, please open a [hardware report](https://github.com/OpenGP5/OpenGP5/issues/new/choose) with
what `j7_nudge.py check` and `gp5tool.py tickfix` printed. It tells us how many pedals have this problem and whether
the fix fits them all.
