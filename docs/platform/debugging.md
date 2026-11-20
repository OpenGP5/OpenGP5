# Debugging

Find problems on the computer first, where nothing can go wrong, then on the pedal with the log and the load
report.

## On the computer

| tool | finds |
|---|---|
| `python3 tools/simulate.py <fw> --script s.txt --check` | most bugs: the whole firmware runs with checkers that stop at the file and line of a crash, a bad memory access or undefined behaviour ([simulator](simulator.md)) |
| `python3 tools/run_effect.py <fw> in.wav out.wav` | the DSP half alone, quickly: listen to what it does |
| `python3 tools/dsp_emu_check.py build/GP-5_<id>.bin` | the DSP part of the built image boots, answers, takes the output level, makes sound and doesn't fault, in an emulator of the DSP |
| `gp5_log("...")` | on the computer, both halves' log lines print as the simulation runs |

Run `--check` after every bigger change: a memory bug on the pedal shows up as something odd much later, while on
the computer it stops at the line that did it.

## On the pedal

```
python3 tools/gp5_link.py log -f        # gp5_log lines from both processors as they come (the DSP's marked "dsp:")
python3 tools/gp5_link.py status        # how busy both processors are, late blocks, memory
python3 tools/gp5_link.py info          # which firmware and platform it runs
python3 tools/gp5_link.py params        # every parameter's value
python3 tools/gp5_link.py stats         # the link to the DSP: frames, errors
```

Or in OpenGP5 Launchpad: its **Developer** page (for firmwares on OpenGP5 0.7 and later) shows the log as it comes and
the load and memory of both processors, with buttons to copy or save the log and to start the worst values again.

`gp5_log` works like `printf` (`%d %u %x %s %c %f`, widths, `%.2f`; print `uint32_t` as `%lu` with a cast). It keeps
the last 4 KB of lines; older ones are overwritten if nobody reads them. A line costs time, so log once a second or
on an event, not every block. The platform logs its own start-up too: which firmware, the clock, the DSP's memory.

## Symptoms

| you see | likely cause | look at |
|---|---|---|
| The LEDs turn **full red** | the main loop has been stuck for more than 2 s: a callback that waits or loops. Your colour comes back once it runs again | what your callbacks wait for; `status` (longest pass) |
| **Silence** | the DSP isn't answering (`status`: "DSP did not answer"), the output level is 0 (the volume knob, or `gp5_level_set(0)` after `gp5_level_own(1)`), the effect is bypassed, or a link firmware's `on_block` isn't running (`gp5_link_start` not called) | `status`, `log`, `params` |
| **Clicks or crackles** | late blocks (`status`), a lost exchange on the audio link, or a sudden jump in level: smooth every change in gain or a parameter over a few ms | `status` while playing; the tremolo's square wave for smoothing |
| **Zipper noise** when turning a knob | the setting jumps per block: glide to it per sample (`x += 0.002f * (target - x)`) | |
| Controls and screen **sluggish** | a long main-loop pass: drawing too much at once, or slow work in a callback | `status` (longest pass); [performance](performance.md) |
| Settings **not kept** after power-off | the parameter has neither `GP5_P_PRESET` nor `GP5_P_SAVE`, or the firmware's `id` changed (another id starts with empty storage) | `params.h`, `firmware.json` |
| **Black screen**, the pedal doesn't show up on USB | the firmware crashed at start-up | recovery below; then `--check` in the simulator |
| "the computer's MIDI system wasn't ready (**RtMidi -304**)" on macOS | the MIDI system wasn't ready yet; nothing was sent. The tools try again by themselves, up to 3 times | if all 3 fail: wait a moment, or unplug and replug the pedal, and run it again |

## Recovery

A firmware that crashes usually can't take the pedal with it: the factory loader that flashes it can't be
overwritten over USB.

1. Unplug the pedal.
2. Hold the foot switch and the PARA knob, and plug it back in. The screen stays dark: that's update mode.
3. Flash a firmware that worked (`gp5tool.py flash`, Launchpad or the web flasher).

Flashing the same firmware (the same `id`) keeps its storage; another firmware starts with empty storage. For the rare
cases where USB recovery isn't enough, see [Known limits](known-limits.md).

## Asking for help

Open a [Discussion](https://github.com/OpenGP5/OpenGP5/discussions), or an issue if it looks like a bug in OpenGP5.
Include:
- what you did;
- what you expected and what happened;
- the output of `gp5_link.py info`, `status` and `log`;
- if you can, a simulator script that shows it.
