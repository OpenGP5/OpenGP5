# The simulator

`tools/simulate.py` runs a whole firmware on the computer:

- both processors' code;
- the audio link between them;
- the screen, saved as pictures;
- the controls, from a script;
- guitar in and sound out as WAV files.

No pedal and no cross-compilers needed: only a C compiler (`cc`) and Python. It runs any firmware with a
`firmware.json`.

```
python3 tools/simulate.py tremolo --script controls.txt --in guitar.wav --out out.wav
```

Without `--in` it plays a plucked test tone for 4 s. Screens go to `build/sim/<id>/` (`end.png` is the screen at the
end) and the sound to `build/sim/<id>.wav`.

## A script

One line per action, the time in seconds first:

```
# the tremolo: faster, saved into preset 2, then bypassed
0.2  shot start
0.5  para click            # adjust RATE
0.6  para turn 10          # +1.0 Hz
0.8  para click
1.0  para turn 3           # down to PRESET
1.1  para click
1.2  para turn 1           # P2
1.4  para hold             # held 0.8 s: saves at 2.0 s
2.1  shot saved
2.5  foot
3.0  set Shape Square      # as a computer sets it
3.2  knob 128
```

| line | does |
|---|---|
| `foot` | press and let go of the foot switch (`foot down` / `foot up`: one edge) |
| `para click`, `para hold` | push PARA and let go after 0.1 s / 0.8 s (`para down` / `para up`: one edge) |
| `para turn N` | turn PARA N clicks (negative: back) |
| `knob N` | the volume knob, 0..255 |
| `set NAME VALUE` | a parameter, as Launchpad sets it: by the name shown (`Loop_level` for "Loop level") or the code's (`LOOPLV`); a choice by its name |
| `preset load N`, `preset save N` | presets, numbered from 1 as on the pedal |
| `shot NAME` | the screen now, as `NAME.png` |
| `storage changed` | as when a computer changed the firmware's files |

While it runs, it prints the log (`gp5_log` on either processor, the DSP's lines marked `dsp:`) and every LED change.
At the end it says how much was sent to the screen in one `fw_draw` call at most. On the pedal that is one main-loop
pass, and sending takes time, so keep it to a strip of the screen (OpenGP5's firmwares send at most about
8 KB of the 25.6 KB screen in a pass). The first full draw is reported on its own.

## More options

| option | |
|---|---|
| `--seconds S` | how long (default: the input's length) |
| `--knob N` | where the volume knob starts (default 255) |
| `--store FILE` | keep saved settings and presets in `FILE` between runs, as the pedal keeps them across power-off |
| `--files DIR` | the firmware's files (`gp5_file`): every file in `DIR`, by its name |
| `--check` | build with the address and undefined-behaviour checkers: a crash or a bad memory access stops with the file and line it happened on |
| `--shots DIR`, `--out WAV` | where screens and sound go |

## How close is it to the pedal?

**The same:**
- The firmware's own code, both halves.
- The platform's parameters, presets, UI kit, text drawing and maths (`gp5_math.c`), so the arithmetic is the pedal's.
- The timing: 64-sample blocks, `fw_tick` every 10 ms, the PARA hold at 0.6 s, the order of the main loop, and the
  audio link's words arriving three blocks on (both normal and `link_early` exchanges).

**Different:**
- **Speed is not simulated.** Code that is too slow for the pedal runs fine here: measure with
  `gp5_link.py status` on the pedal. `GP5_CYCLE_COUNT()` and the control processor's cycle counters read 0.
- The DSP-extension instructions of the control processor (`kmmac` and friends) don't exist on a computer. Code that
  uses them needs a portable version for when `__NDS32_EXT_DSP__` isn't defined (NAM Loader's kernel has one).
- Saved data lives in memory or the `--store` file, not flash, so flash timing and wear don't apply.
- Controls are exact: no bounce, no noise on the knob.

Use it to try the sound and the screen while you write the code, to check a firmware after a change, and in CI.
Then check timing and feel on the pedal.

## How it works

`platform/host/sim_dsp.c` builds the DSP half as a library, and `platform/host/sim.c` is the control half's main
program with the platform's calls. They are kept apart because each half has its own `fw_init`, `sinf` and so on.
`tools/test_simulate.py` checks the simulator itself.
