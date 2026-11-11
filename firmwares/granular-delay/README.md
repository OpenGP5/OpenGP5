# Granular delay / micro-looper

A granular delay with a frozen micro-loop on top, for ambient textures: the guitar is recorded all the time,
short grains are played back from it with random placement, pitch and direction, and a press of the foot
switch freezes a loop that keeps playing under whatever you play next.

## Controls

| control | does |
|---|---|
| turn PARA | choose a setting (shown on the screen) |
| big knob | changes the chosen setting over its whole range, once turned a little |
| foot switch | freeze / unfreeze the loop (also switches the effect on) |
| push PARA | effect on / off (off also lets go of the loop) |

| setting | range |
|---|---|
| MIX | dry only … both at full level (middle) … effect only |
| DELAY | 0 … 510 ms |
| GRAIN | grain length 5 … 185 ms |
| SPEED | grain pitch / direction: reverse 2x, reverse 1x, reverse 0.5x, 0.5x, 0.75x, 1x, 1.5x, 2x, 4x |
| SPRAY | random grain placement, 0 … 250 ms |
| FEEDBK | feedback, 0 … 98 % |
| CHAOS | per grain: chance of octave up / down / reverse, length ±50 %, left out |
| CLOCK | 1 … 8: divides the engine's sample rate (longer times, lo-fi sound) |
| LOOP | loop length 10 … 810 ms (times CLOCK) |
| LP SPD | loop playback speed, same steps as SPEED |
| LP LVL | loop level |
| LEVEL | output level |

## Files

- `dsp/firmware.h`: the DSP half's three platform functions. The effect itself is `dsp/granular.h`
  (integer, shared with the host test in `dsp/test/`).
- `nds32/firmware.c`: the control half's platform functions, connecting
  `nds32/fx_ui.c` (the controls above) and `nds32/ui.c` (the screen; mock-ups without a pedal:
  `python3 tools/ui_mock.py out.png`).

## Build and flash

```
platform/build_firmware.sh granular-delay
python3 tools/gp5tool.py flash build/GP-5_granular-delay.bin --sections all
```

Checks without a pedal: `python3 tools/granular_sim.py` (the engine on the computer, can write demo WAVs)
and `python3 tools/dsp_granular_emu.py build/GP-5_granular-delay.bin` (the built DSP code in an emulator;
needs `pip install unicorn`).

Settings can also be read and changed from the computer while it runs: `python3 tools/gp5_granular.py get`.
