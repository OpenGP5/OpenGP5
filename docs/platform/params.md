# Parameters and presets

A firmware's settings, declared once in `params.h`. From that one list the platform:

- sends each value to the DSP when it changes, and all of them again when the DSP restarts;
- keeps them across power-off;
- keeps presets, with names, if the manifest asks for them;
- lets a computer list, read and set them: Launchpad's Live page builds its editor from them, with no code of
  its own for your firmware.

The firmware's own code only reads values and sets them from the pedal's controls.

## Declaring them

`params.h`, at the top of the firmware's folder (next to `firmware.json`):

```c
#define GP5_PARAMS(P, C)                                                                                     \
    P(RATE,  "Rate",  "Tremolo", 50, 1500, 500, GP5_U_CUSTOM,  GP5_P_DSP | GP5_P_PRESET)  /* 1/100 Hz */      \
    P(DEPTH, "Depth", "Tremolo", 0, 100, 50, GP5_U_PERCENT,     GP5_P_DSP | GP5_P_PRESET)                    \
    C(SHAPE, "Shape", "Tremolo", 3, "Sine|Triangle|Square", 0,  GP5_P_DSP | GP5_P_PRESET)                    \
    C(ON,    "Effect", "Tremolo", 2, "Off|On", 1,               GP5_P_DSP)
```

```
P(id, name, group, min, max, default, unit, flags)     a number, min..max, within -32768..32767
C(id, name, group, count, "A|B|C", default, flags)     a choice, 0..count-1
```

Each line gives an id, `GP5_P_RATE`, `GP5_P_DEPTH` and so on (0, 1, 2 …, in order), and `GP5_NPARAMS` counts them.
Names and groups have at most 15 characters; a choice list at most 47. Launchpad shows each group as a card.

Values are whole numbers. Pick the step the user needs and say it in the unit: 1/100 Hz for a rate, ms for a time,
tenths of a dB for a gain.

### Units

How a computer shows a value:

| unit | shown as |
|---|---|
| `GP5_U_NONE` | the number |
| `GP5_U_PERCENT` | `50 %` |
| `GP5_U_DB10` | tenths of a dB: `-60` → `−6 dB` |
| `GP5_U_MS` | `350 ms`, `1.5 s` |
| `GP5_U_HZ` | `440 Hz` |
| `GP5_U_SEMITONES` | `+7 st` |
| `GP5_U_CENTS10` | tenths of a cent |
| `GP5_U_CUSTOM` | your own text: `fw_param_text()` on the control processor writes it (below) |

### Flags

| flag | |
|---|---|
| `GP5_P_DSP` | the DSP gets it: `fw_param(id, value)` there, and `gp5_param(id)` |
| `GP5_P_PRESET` | part of a preset; also kept across power-off as part of the current sound |
| `GP5_P_SAVE` | kept across power-off on its own: a global setting, not part of presets |
| `GP5_P_READONLY` | the computer may read it, not set it: a state such as "looping" or the note played |
| `GP5_P_HIDDEN` | not shown in Launchpad's editor |

A parameter with neither `PRESET` nor `SAVE` starts at its default after every power-on (the tremolo's `ON`).

## Control processor

```c
#include "gp5_params.h"

void fw_para_turn(int d) { gp5_param_set(GP5_P_DEPTH, gp5_param(GP5_P_DEPTH) + 5 * d); }
void fw_foot_down(void)  { gp5_param_set(GP5_P_ON, !gp5_param(GP5_P_ON)); }
```

- `gp5_param(id)`: the current value. By `fw_init()` the saved values are already in place.
- `gp5_param_set(id, value)`: from the pedal's own controls. It clamps to the range, sends the value to the DSP if it
  is a DSP parameter, and saves 2 s after the last change, so turning a knob doesn't wear the flash. Returns 1 if
  the value changed.
- `fw_param_changed(id)` (optional): a value changed from outside, either from the computer or from a preset load
  (once per parameter that changed). Redraw, or update the LEDs. It is not called for your own
  `gp5_param_set`.
- `fw_param_text(id, value, text)` (optional): for `GP5_U_CUSTOM`, write up to 15 characters and return 1. The
  computer asks for it (USB `7E 15`), so your screen and Launchpad show the same thing:

```c
int fw_param_text(unsigned id, int32_t v, char *t)    /* RATE: 500 -> "5.00 Hz" */
{
    if (id != GP5_P_RATE) return 0;
    ...
    return 1;
}
```

## DSP

```c
static void fw_param(uint32_t id, int32_t value)
{
    if (id == GP5_P_RATE) step = (float)value / 100.0f / (float)GP5_SAMPLE_RATE;
}
```

The platform keeps every DSP parameter's value; `gp5_param(id)` reads it anywhere, including in `fw_process`.
`fw_param` hears each change before the next block: use it to work out what is costly to compute (a coefficient, a
delay length), and read simple values where they are used. Until the control processor sends its values, the DSP
has the defaults from `params.h`.

Link command `0x7F10` carries parameters; a firmware's own link commands start at `0x7F11`.

## Presets

`"presets": N` in `firmware.json` (up to 64). A preset holds every `GP5_P_PRESET` parameter and a name of up to
12 characters.

| | |
|---|---|
| `gp5_preset_count()` | N |
| `gp5_preset_load(n)` | 0 if preset n is empty (nothing changes); otherwise every value, then `fw_param_changed` for each one that changed |
| `gp5_preset_save(n)` | the current sound into n, keeping its name |
| `gp5_preset_current()` | the preset loaded last, -1 if none |
| `gp5_preset_dirty()` | 1 once the sound differs from that preset (show a `*`) |
| `gp5_preset_used(n)`, `gp5_preset_name(n)`, `gp5_preset_rename(n, name)` | |

Which preset is loaded survives power-off with the current sound. How the pedal chooses presets is up to the
firmware: the tremolo turns PARA on its PRESET row to load and holds PARA to save. Launchpad loads, saves and renames
them for any firmware.

## How they are stored

In the firmware's own storage (`storage.md`), as store records whose ids come from the firmware's `id`: one for
the current sound, one for each preset. A record keeps pairs of (a hash of the parameter's name, value), not
positions, so an update that adds, removes or reorders parameters keeps what was saved:

- a new parameter starts at its default;
- a removed one is dropped;
- a renamed one starts at its default (keep names once released).

Ranges are applied when values are loaded.

Launchpad's backups include these records, so a backup restores the presets and the current sound exactly.

## Firmware ids past the parameters

A firmware can answer more live ids, starting from `GP5_NPARAMS`, with `fw_param_get` / `fw_param_set`: commands
or states that aren't settings. Dreamcatcher does this for its own preset commands. The platform passes it every id
it doesn't own.

## Trying it

```
python3 tools/run_effect.py tremolo x out.wav --tone --set Rate=800 --set Shape=Square
python3 tools/gp5_link.py params
python3 tools/gp5_link.py set Depth 80
python3 tools/gp5_link.py presets
```
