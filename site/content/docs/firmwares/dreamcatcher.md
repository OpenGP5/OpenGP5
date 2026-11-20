---
title: Dreamcatcher
description: Ambient textures. An always-listening micro-looper, a glitch delay that smears into reverb, and grains.
weight: 20
---

Dreamcatcher records your playing all the time. One press catches the last moments as a loop that plays back like
tape, or stretches into slow-moving slices. Alongside it, a wet channel lets bits of sound into a delay "by
chance", smears them into reverb, jumps their clock in octaves, reverses or crushes them. Grains read the delay
and can jump in pitch, play backwards or stutter, and a hall reverb holds it all.

It was inspired by the Chase Bliss MOOD MKII and the Old Blood Noise Endeavors Parting. It is an independent
project, not affiliated with either, and contains none of their code.

[Source and releases on GitHub](https://github.com/OpenGP5/Dreamcatcher)

![A Valeton GP-5 running Dreamcatcher](https://raw.githubusercontent.com/OpenGP5/Dreamcatcher/main/docs/pedal.jpg)

## Controls

| Control | Does |
|---|---|
| **Turn PARA** | Choose a setting. The screen shows its section and value |
| **Volume knob** | Changes the chosen setting once you turn it a little. The value turns gold while it follows |
| **Tap the foot switch** | Catch a loop, or let it go and listen again |
| **Hold the foot switch** | While a loop plays: **overdub** into it until you let go (`DUB`). While listening: **freeze** the wet channel (`FRZ`) |
| **Click PARA** | Save the sound into the active preset (`SAVED P3`). On PRESET with another slot highlighted: load it, or save there if it's empty |
| **Hold PARA 3 s** | Effect on or off. A caught loop stays caught. While off, the output is the exact dry signal |

The LEDs glow violet when Dreamcatcher is on and dim red when it is off. The dot in the screen's header pulses
with the LFO, like a "moment of chance" light.

## A good start

Catch a chord. Set ROUTE to IN+LOOP, turn SMEAR up and CHANCE a little past noon, then roll CLOCK down and play
over it.

## Settings

### MAIN

| Setting | Range |
|---|---|
| PRESET | Slots 1–8. Highlight one with the knob, load it with a click of PARA. `P3` is active, `P3*` changed since saved, `3 EMPTY` nothing saved. A preset holds every setting and VOLUME |
| MIX | dry only … both full (middle) … wet only |
| CLOCK | 1.4 kHz … 44 kHz in musical steps. The engine's sample rate: lower means longer loops and delays, and more grit. Changing it while a loop plays shifts the loop's pitch in harmonised steps |
| SYNC | `FREE`, or `SYNC`: TIME, RATE, LENGTH and GRAIN become note values of BPM (`1/8.`, `1/4T`, `1 BAR` …), and the LFO keeps to the beat |
| BPM | 40 … 240, the tempo SYNC follows |
| VOLUME | Output level |

### WET

| Setting | Range |
|---|---|
| TIME | Delay time, 4 ms … 1.16 s at 44 kHz (up to 37 s at a lower CLOCK). With SYNC: 1/32 … 1 bar |
| SMEAR | Diffusion and feedback: clean echoes … dense reverb |
| TONE | Darkens the repeats each time round; `OPEN` at the top |
| CHANCE | Below noon: the chance per LFO cycle that something enters the delay. Noon: input always enters (`ALWAYS`). Above noon: plus a growing chance of feeding the old output back (`FB`) |
| GLITCH | Below noon: the delay's clock at HALF, NORMAL or DOUBLE speed. Above noon: a growing chance (`RND`) that each LFO cycle picks a new clock and crush at random |

### GRAIN

| Setting | Range |
|---|---|
| GRAIN | Grain size, 6 ms … 186 ms at 44 kHz. With SYNC: 1/64 … 1/16 |
| PITCH | Chance a grain is pitched to an interval of SCALE. Fed back, pitched repeats stack into a shimmer |
| SCALE | OCT UP, OCT DN, OCTAVES, 5TH+OCT, HARMONY |
| REVERSE | Chance a grain plays backwards |
| STUTTER | Chance a grain repeats the one before it |

### MOD

| Setting | Range |
|---|---|
| RATE | LFO rate, 0.05 … 12 Hz; also how often CHANCE decides. With SYNC: 1/32 … 4 bars per cycle |
| DEPTH | Modulation depth |
| SHAPE | SINE, SQUARE, RAMP, SAW, RND SIN, RND SQR, ENV. With ENV, your playing's loudness opens the delay and CHANCE becomes SENSE |
| MOD | TREMOLO or VIBRATO on the wet channel |

### COLOR

| Setting | Range |
|---|---|
| DISSOLVE | Below noon: sample-rate crush (LOFI). Noon: CLEAN. Above noon: the delay plays backwards, then at ½, ¼, ⅛ speed |
| FILTER | Below noon: low-pass 150 Hz … 13 kHz. Noon: OPEN. Above noon: high-pass 20 Hz … 2.5 kHz |

### LOOP

| Setting | Range |
|---|---|
| MODE | TAPE or STRETCH |
| LENGTH | TAPE: loop length, 1/16 … all of the memory (1.47 s at 44 kHz, 47 s at 1.4 kHz); with SYNC 1/8 … 4 bars. STRETCH: slice size |
| SPEED / DRIFT | TAPE: REV 4X … 4X. STRETCH: how fast the slices move, backwards … FROZEN (noon) … forwards |
| LOOP LV | Level of the loop, and what it feeds the effects |
| FADE | Overdub: how much of the loop stays each pass, `REPLACE` … 100 % |
| ROUTE | What the wet channel processes: IN (the guitar), IN+LOOP, or LOOP |

### REVERB

| Setting | Range |
|---|---|
| DECAY | 0.3 s … 27 s; at the top, `HOLD` sustains the tail and keeps new playing out |
| MIX | None … on top of the effect (middle) … the reverb alone. The dry guitar stays dry |
| SIZE | Room … big hall |
| PRE-DLY | 0 … 200 ms before the reverb starts |
| MOD | Slow drift in the tail |
| SHIMMER | An octave-up copy of the tail, fed back in |

## Presets in Launchpad

[Launchpad's Presets page](../../launchpad/presets/#dreamcatcher) edits all 8 presets while you play, names them,
and exports and imports them. Its [Backups](../../launchpad/backups/) save them all.
