# Debugging

Use the simulator to find bugs before you flash the pedal. On the pedal, use the log and load report to find faults.

## On the computer

| Tool | What to check |
|---|---|
| `python3 tools/simulate.py <fw> --script s.txt --check` | Run both halves with checks for memory errors and undefined behavior |
| `python3 tools/run_effect.py <fw> in.wav out.wav` | Run the DSP sound code and listen to its output |
| `python3 tools/dsp_emu_check.py build/GP-5_<id>.bin` | Boot the built DSP image in an emulator and check replies, output level and sound |
| `gp5_log("...")` | Print log lines from either half while the simulation runs |

Replace `<fw>` with the firmware folder or name, and `<id>` with its manifest ID.
Use a control script that exercises the changed behavior. See [The simulator](simulator.md).
Run `--check` after changes to memory use or control flow. The checks can point to the source line of a fault.
They do not measure whether the code meets the pedal's timing budget.

## On the pedal

```
python3 tools/gp5_link.py log -f        # gp5_log lines from both processors as they come (the DSP's marked "dsp:")
python3 tools/gp5_link.py status        # how busy both processors are, late blocks, memory
python3 tools/gp5_link.py info          # which firmware and platform it runs
python3 tools/gp5_link.py params        # every parameter's value
python3 tools/gp5_link.py stats         # the link to the DSP: frames, errors
```

Launchpad's **Developer** page shows the log, processor load and memory for platform 0.7 or later.
Use **Reset worst** to clear peak readings before a test. Copy or save the log to include it in a bug report.

`gp5_log` uses `printf`-style formatting. It accepts `%d %u %x %s %c %f`, widths and precision such as `%.2f`.
Print `uint32_t` as `%lu` with an `unsigned long` cast.
The pedal keeps the last 4 KB of log text; new text overwrites old text if the reader falls behind.

Log once per second or on an event. A log line in every block adds load and can affect the test.
The platform also logs its start-up: the build name, clock and DSP memory.

## Symptoms

| Symptom | Check | Next action |
|---|---|---|
| **Full red LEDs** | A main-loop callback may have blocked for more than two seconds | Remove waiting loops; read `status` for the longest pass |
| **Silence** | DSP reply, output level and audio-link start | Read `status`, `log` and `params`; check the volume knob |
| **Clicks or crackles** | Late blocks, lost audio-link exchanges or abrupt gain changes | Measure load while playing; smooth gain changes |
| **Zipper noise** when turning a knob | A value may jump once per block | Smooth the value per sample |
| **Slow controls or screen** | A long main-loop pass | Draw less per pass; see [Performance](performance.md) |
| **Settings lost after power-off** | Save flags and firmware ID | Check `GP5_P_SAVE` / `GP5_P_PRESET` and `firmware.json` |
| **Black screen and no USB device** | A start-up crash | Follow recovery below, then use the simulator's `--check` |
| **RtMidi -304** on macOS | MIDI service readiness | Wait, reconnect the pedal and retry if the tools' three retries fail |

For silence, check whether `status` says **DSP did not answer**. If the firmware owns output level,
`gp5_level_set(0)` mutes it even when the knob is up. An audio-link effect also needs `gp5_link_start()`
to run its control-processor handler.

For zipper noise, a per-sample glide such as `x += 0.002f * (target - x)` can smooth a changing value.
Choose the smoothing time to suit the setting. The tremolo's square-wave code is another example.

After a MIDI error, tools retry if they have sent no data. Once a send has started, they stop and report the fault.

## Recovery

USB update tools leave the factory loader in place, so a crashed app does not erase that recovery path.

1. Unplug the pedal.
2. Press and hold the foot switch and PARA as you plug it back in, then release both.
3. Confirm update mode with Launchpad, the web flasher or `gp5tool.py status`.
4. Flash a build that worked before.

See [Recovery](../../GETTING-STARTED.md#recovery) for the full steps.
The known DSP updater fault can still block a USB flash; [J7 recovery](j7-recovery.md) covers that case.

The same firmware ID keeps storage. A different ID starts with empty storage, so back up before switching.

## Asking for help

Open a [Discussion](https://github.com/OpenGP5/OpenGP5/discussions), or an issue for a fault in OpenGP5.
Include:

- the steps that trigger the problem;
- what you expected and what happened;
- output from `gp5_link.py info`, `status` and `log`;
- a control script that reproduces it, if you can make one.
