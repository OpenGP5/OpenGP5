---
title: Dreamcatcher
description: Loop your playing and shape it with delay, pitch shifts, grains, and reverb.
weight: 20
---

Dreamcatcher records as you play. Tap the foot switch to loop a short part of your playing.
Add delay, pitch shifts or reverb to the loop or your live sound.

The **dry** signal is your guitar without effects. The **wet** signal has effects.
The delay can break sound into **grains**, which are short slices that can shift pitch, reverse or repeat.
A slow control wave, the **LFO** (low-frequency oscillator), sets the pace of modulation and chance events.

Chase Bliss MOOD MKII and Old Blood Noise Endeavors Parting inspired the effect.
Dreamcatcher has no affiliation with either company and contains none of their code.

[Source and releases on GitHub](https://github.com/OpenGP5/Dreamcatcher)

![A Valeton GP-5 running Dreamcatcher](https://raw.githubusercontent.com/OpenGP5/Dreamcatcher/main/docs/pedal.jpg)

## Controls

| Control | Action |
|---|---|
| **Turn PARA** | Select a setting; the screen shows its section and value |
| **Turn the volume knob** | Edit the selected setting after a small move; gold text shows that the knob has taken control |
| **Tap the foot switch** | Catch a loop, or release it and record new playing |
| **Hold the foot switch** | Add sound to a playing loop (`DUB`); while listening, freeze the wet signal (`FRZ`) |
| **Press PARA** | Save the sound to the active preset, except when choosing a different preset; see below |
| **Hold PARA for 3 s** | Toggle the effect; bypass keeps the dry signal unchanged and retains the caught loop |

**Overdub** (`DUB`) adds your playing to the loop until you release the foot switch.
**Freeze** (`FRZ`) holds the wet sound while you hold the switch.

When you select a setting, it keeps its value until you move the volume knob a small amount.
The knob then sets the value from its own position, which can cause a jump.
It edits the selected setting; select **VOLUME** to set output level.

The LEDs glow violet when the effect is on and dim red when it is off.
The header dot pulses at the LFO rate.

## A good start

1. Play a chord and tap the foot switch to catch it as a loop.
2. Set **ROUTE** to **IN+LOOP** so the effects process both your guitar and the loop.
3. Raise **SMEAR** and set **CHANCE** a little above its middle value.
4. Lower **CLOCK**, then play over the loop.

In the tables, **noon** means the middle of a setting's range. The knob must take control before its position sets the value.

## Settings

Tempo sync needs **Dreamcatcher 1.4 or later**. Other tables describe the controls shared by these builds.

### MAIN

| Setting | What it does |
|---|---|
| PRESET | Select one of 8 saved sounds; press PARA to load the highlighted slot |
| MIX | Blend dry and wet; the middle gives both at full level |
| CLOCK | Set the engine's sample rate, from 1.4 to 44 kHz in musical steps |
| SYNC | Choose FREE timing or SYNC to the BPM tempo |
| BPM | Set tempo from 40 to 240 beats per minute |
| VOLUME | Set output level |

On PRESET, pressing PARA loads a highlighted saved slot or saves into an empty one.
With the active slot highlighted, it saves the current sound. Away from PRESET, pressing PARA also saves to the active slot.
A preset holds all settings, including VOLUME.

`P3` means preset 3 is active. `P3*` means its sound has changed since the last save.
`3 EMPTY` marks an empty slot. `SAVED P3` confirms a save.

A lower CLOCK gives longer loops and delays, with a rougher sound.
Changing CLOCK during a loop shifts its pitch in musical steps.
In SYNC mode, TIME, RATE, LENGTH and GRAIN use note lengths, and the LFO follows the beat.
A dot means a dotted note, `T` means a triplet, and `BAR` means one bar.

### WET

| Setting | What it does |
|---|---|
| TIME | Set delay time: 4 ms to 1.16 s at 44 kHz, or up to 37 s at lower CLOCK values |
| SMEAR | Blend distinct echoes into dense reverb through diffusion and feedback |
| TONE | Darken each repeat; OPEN removes that darkening |
| CHANCE | Set how often sound enters or feeds back through the delay |
| GLITCH | Set delay-clock speed or let the LFO choose it at random |

With SYNC, TIME spans 1/32 note to 1 bar.

**CHANCE** has three regions:

- Below noon, it sets the chance that input enters the delay on each LFO cycle.
- At noon, the delay takes all input (`ALWAYS`).
- Above noon, input keeps entering and feedback becomes more likely (`FB`).

**GLITCH** also changes behavior across its range:

- Below noon, choose HALF, NORMAL or DOUBLE delay-clock speed.
- Above noon, each LFO cycle has a growing chance to choose a new clock speed and crush amount (`RND`).

### GRAIN

| Setting | What it does |
|---|---|
| GRAIN | Set slice length: 6 to 186 ms at 44 kHz; with SYNC, 1/64 to 1/16 note |
| PITCH | Set the chance that a grain shifts to an interval in SCALE |
| SCALE | Choose OCT UP, OCT DN, OCTAVES, 5TH+OCT or HARMONY |
| REVERSE | Set the chance that a grain plays in reverse |
| STUTTER | Set the chance that a grain repeats the one before it |

Feeding pitch-shifted repeats back into the delay builds a shimmer.

### MOD

| Setting | What it does |
|---|---|
| RATE | Set LFO speed: 0.05 to 12 Hz; with SYNC, 1/32 note to 4 bars per cycle |
| DEPTH | Set the amount of modulation |
| SHAPE | Choose SINE, SQUARE, RAMP, SAW, RND SIN, RND SQR or ENV |
| MOD | Apply TREMOLO (volume changes) or VIBRATO (pitch changes) to the wet signal |

RATE also sets how often CHANCE makes a choice.
With ENV, your playing level opens the delay and CHANCE becomes SENSE, the sensitivity control.

### COLOR

| Setting | What it does |
|---|---|
| DISSOLVE | Below noon, reduce sample rate (LOFI); at noon, CLEAN; above noon, reverse at full, ½, ¼ or ⅛ speed |
| FILTER | Below noon, low-pass from 150 Hz to 13 kHz; at noon, OPEN; above noon, high-pass from 20 Hz to 2.5 kHz |

A low-pass filter cuts highs. A high-pass filter cuts lows.

### LOOP

| Setting | What it does |
|---|---|
| MODE | Choose TAPE or STRETCH |
| LENGTH | Set loop length in TAPE mode or slice length in STRETCH mode |
| SPEED / DRIFT | Set tape speed and direction, or how fast the slices move |
| LOOP LV | Set the loop's level and how much it feeds the effects |
| FADE | Set how much old sound remains on each overdub pass: REPLACE to 100 % |
| ROUTE | Process IN (guitar), IN+LOOP or LOOP in the wet channel |

In TAPE mode, LENGTH spans 1/16 of memory to all of it: up to 1.47 seconds at 44 kHz or 47 seconds at 1.4 kHz.
With SYNC, it spans 1/8 note to 4 bars. SPEED spans REV 4X to 4X.

In STRETCH mode, LENGTH sets slice size. DRIFT moves through slices in reverse or forward,
with FROZEN at noon to hold the position.

### REVERB

| Setting | What it does |
|---|---|
| DECAY | Set tail length from 0.3 to 27 s; HOLD sustains the tail and blocks new input |
| MIX | Blend from no reverb, through effect plus reverb at the middle, to reverb alone |
| SIZE | Change room size from a room to a large hall |
| PRE-DLY | Wait 0 to 200 ms before reverb starts |
| MOD | Add slow pitch drift to the tail |
| SHIMMER | Feed an octave-up copy of the tail back into the reverb |

Reverb MIX changes the wet path. Your dry guitar stays dry.

## Presets in Launchpad

Use [Launchpad's Presets page](../../launchpad/presets/#dreamcatcher) to edit sounds as you play and save them.
You can name, import, export and move the 8 preset slots. Use **Write to pedal** to save slot edits.
[Backups](../../launchpad/backups/) save the set of presets to a file.
