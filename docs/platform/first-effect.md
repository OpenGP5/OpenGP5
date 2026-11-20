# Your first effect

This page turns a copy of `firmwares/passthrough`, the smallest firmware, into a tremolo: the guitar's volume rises
and falls with a sine wave. PARA sets how fast, clicking PARA switches it to setting how deep, and the foot switch
turns it on and off. Launchpad gets an editor for it without any extra code. The complete code is below, about 90
lines in all. `firmwares/tremolo` is the same tremolo grown further: presets, a smooth-font screen, more shapes.

The full reference is [the 1.0 API](api.md).

## 1. Copy the passthrough

```
cp -r firmwares/passthrough firmwares/my-tremolo
```

Then give it its own identity in `firmwares/my-tremolo/firmware.json`:

```json
{
  "id": "my-tremolo",
  "name": "My Tremolo",
  "version": "0.1.0",
  "author": "you",
  "platform": "0.8"
}
```

The `id` matters: the pedal's storage belongs to the firmware with that id. Keep it once people use your
firmware. A different id starts with empty storage.

The pedal has two processors, and a firmware has one part for each:

```
firmwares/my-tremolo/firmware.json       who it is
firmwares/my-tremolo/params.h            its settings (you add this one)
firmwares/my-tremolo/dsp/firmware.h      the sound: runs on the DSP, 64 samples at a time
firmwares/my-tremolo/nds32/firmware.c    the knobs, foot switch and screen: runs on the control processor
```

## 2. The settings (`params.h`)

Declare them once; both halves and the computer use the same list:

```c
#define GP5_PARAMS(P, C)                                                                    \
    P(RATE,  "Rate",  "Tremolo", 50, 1500, 500, GP5_U_NONE,    GP5_P_DSP | GP5_P_SAVE)       \
    P(DEPTH, "Depth", "Tremolo", 0, 100, 50,    GP5_U_PERCENT, GP5_P_DSP | GP5_P_SAVE)       \
    C(ON,    "Effect", "Tremolo", 2, "Off|On", 1,              GP5_P_DSP)
```

- `RATE` runs from 50 to 1500 in 1/100 Hz, so 0.5 to 15 Hz. Values are whole numbers; choose the step you need.
- `GP5_P_DSP` means the DSP gets the value. `GP5_P_SAVE` means it is kept across power-off.
- `ON` has no `SAVE`, so the tremolo is on after every power-on.

The platform now sends each change to the DSP, saves `RATE` and `DEPTH` 2 s after you stop turning, and lets a
computer read and set all three. [Parameters and presets](params.md) has the units, flags and presets.

## 3. The sound (`dsp/firmware.h`)

The platform calls:

```c
static void fw_init(void);                                                           /* once, at start-up */
static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right);   /* every 1.45 ms */
static void fw_param(uint32_t id, int32_t value);                                    /* a setting changed */
static int  fw_command(uint32_t cmd, const uint8_t *payload, uint32_t n);           /* commands of your own */
```

- `in` holds `GP5_BLOCK` (64) guitar samples, 32-bit signed, full scale ±2^31. Divide by `GP5_FULL_SCALE` if you
  prefer -1.0 … 1.0. Write 64 samples to each output.
- Write ordinary C. Variables at file level keep their value between calls, and `float` maths runs on the DSP's
  floating-point unit. `gp5_math.h` gives you `sinf`, `cosf`, `expf`, `logf`, `powf`, `tanhf`, `sqrtf` and more.
- Write `0.5f`, not `0.5`. A plain `0.5` is a `double`, which the DSP does slowly in software (the build warns).
- Big buffers (delay lines, loopers) go in the 122 KB block: `static int16_t line[44100] GP5_LARGE;`. Ordinary
  variables have 19,712 bytes. The build prints what you use and refuses what doesn't fit.
- `gp5_param(GP5_P_DEPTH)` reads a setting's current value anywhere. Until the control half sends its values, it
  holds the defaults from `params.h`.

Replace the whole file with this:

```c
/* My tremolo, DSP half */
#include "gp5_math.h"

static float phase;              /* 0 .. 1, one full wave */
static float step;               /* phase per sample = rate / sample rate */
static float depth_now;          /* the depth in use, gliding to the setting so changes never click */

static void set_rate(int32_t centi_hz) { step = (float)centi_hz / 100.0f / (float)GP5_SAMPLE_RATE; }

static void fw_init(void)
{
    phase = 0.0f;
    depth_now = 0.0f;
    set_rate(gp5_param(GP5_P_RATE));
}

static void fw_param(uint32_t id, int32_t value)
{
    if (id == GP5_P_RATE) set_rate(value);           /* DEPTH and ON are read where they are used */
}

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    float target = gp5_param(GP5_P_ON) ? (float)gp5_param(GP5_P_DEPTH) / 100.0f : 0.0f;
    for (uint32_t i = 0; i < GP5_BLOCK; i++) {
        depth_now += 0.001f * (target - depth_now);
        float wave = 0.5f + 0.5f * sinf(2.0f * GP5_PI * phase);   /* 0 .. 1 */
        float gain = 1.0f - depth_now * wave;
        phase += step;
        if (phase >= 1.0f) phase -= 1.0f;
        int32_t y = (int32_t)((float)in[i] * gain);
        out_left[i] = y;
        out_right[i] = y;
    }
}

static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n) { (void)cmd; (void)p; (void)n; return 0; }
```

**Time budget:** one block may take at most 278,000 DSP cycles (64 samples at 44.1 kHz, 191.9 MHz), about 4,300
per sample. With `"audio": {"sample_rate": 48000}` in `firmware.json` a block is 255,900 cycles. Write
`GP5_SAMPLE_RATE` in your maths, never 44100. A `sinf` costs roughly 40 cycles. This tremolo uses a few percent.

## 4. Try the sound on the computer

No pedal needed:

```
python3 tools/run_effect.py my-tremolo guitar.wav out.wav --set Rate=400 --set Depth=100
python3 tools/run_effect.py my-tremolo x out.wav --tone           # no recording? a plucked test tone
```

`--set` sends a setting exactly as the control half would. Once you have the control half too (section 5), the
[simulator](simulator.md) runs both halves with the screen and scripted controls:
`python3 tools/simulate.py my-tremolo`. The program is your `firmware.h` compiled for the
computer with the platform's own maths functions, so it computes what the pedal computes. `gp5_log("...")` in the
DSP code prints to the terminal here.

## 5. The controls and screen (`nds32/firmware.c`)

The platform calls these from its main loop. Each is optional, and none of them may wait:

| function | when | the tremolo |
|---|---|---|
| `fw_init` | start-up (saved settings already loaded) | |
| `fw_para_turn(clicks)` | PARA turned | change the selected setting |
| `fw_para_click` | PARA pushed and let go | select the other setting |
| `fw_foot_down` | foot switch pressed | on / off |
| `fw_param_changed(id)` | a setting changed from the computer | update the LEDs |
| `fw_draw(force)` | every pass | the screen |

The volume knob stays the output level, which is the platform's default. Replace the whole file with this:

```c
/* My tremolo, control half: PARA turn = the selected setting, PARA click = select, foot = on / off */
#include "gp5_firmware.h"
#include "gp5_params.h"

static unsigned sel;                                  /* 0 = rate, 1 = depth */

static void show_on(void) { gp5_led(gp5_param(GP5_P_ON) ? GP5_LED_ON : GP5_LED_BYPASS); }

void fw_init(void) { show_on(); }
void fw_foot_down(void) { gp5_param_set(GP5_P_ON, !gp5_param(GP5_P_ON)); show_on(); }
void fw_param_changed(unsigned id) { if (id == GP5_P_ON) show_on(); }
void fw_para_click(void) { sel = !sel; }

void fw_para_turn(int d)
{
    if (sel == 0) gp5_param_set(GP5_P_RATE, gp5_param(GP5_P_RATE) + 10 * d);    /* 0.1 Hz a click */
    else gp5_param_set(GP5_P_DEPTH, gp5_param(GP5_P_DEPTH) + 5 * d);            /* (clamped to the range) */
}

static void number(char *p, unsigned v, unsigned digits)        /* right-aligned decimal */
{
    for (unsigned i = digits; i-- > 0; v /= 10) p[i] = (char)('0' + v % 10);
}

int fw_draw(int force)
{
    static int32_t shown[3] = { -1, -1, -1 };
    static unsigned shown_sel = 9;
    int32_t rate = gp5_param(GP5_P_RATE), depth = gp5_param(GP5_P_DEPTH), on = gp5_param(GP5_P_ON);
    if (force) { gp5_text(0, "My Tremolo   "); gp5_text(4, "             "); shown[0] = shown[1] = shown[2] = -1; }
    if (rate != shown[0] || sel != shown_sel) {
        char s[14] = " rate 00.00Hz";
        if (sel == 0) s[0] = '>';
        number(s + 6, (unsigned)rate / 100, 2); number(s + 9, (unsigned)rate % 100, 2);
        gp5_text(1, s); shown[0] = rate;
    }
    if (depth != shown[1] || sel != shown_sel) {
        char s[14] = " depth  000 %";
        if (sel == 1) s[0] = '>';
        number(s + 8, (unsigned)depth, 3);
        gp5_text(2, s); shown[1] = depth;
    }
    if (on != shown[2]) { gp5_text(3, on ? " on          " : " off         "); shown[2] = on; }
    shown_sel = sel;
    return 0;
}
```

The screen here is five lines of 13 characters (`gp5_text`). For smooth text in any font and size, see
[Text and fonts](fonts.md); for your own pixels, `gp5_blit`. `firmwares/tremolo` draws its rows that way.

## 6. Build, check, flash

```
python3 platform/manifest.py firmwares/my-tremolo --check   # is firmware.json right?
platform/build_firmware.sh my-tremolo                       # -> build/GP-5_my-tremolo.bin
python3 tools/dsp_emu_check.py build/GP-5_my-tremolo.bin    # boots the built DSP code in an emulator (pip install unicorn)
python3 tools/gp5tool.py flash build/GP-5_my-tremolo.bin --sections all
```

You can also flash it from Launchpad or the web flasher. Both read the name and version from the file.

On the pedal:

```
python3 tools/gp5_link.py params             # your settings and their values
python3 tools/gp5_link.py set Depth 100      # change one from the computer
python3 tools/gp5_link.py log -f             # what gp5_log() prints, on either processor
python3 tools/gp5_link.py status             # how busy both processors are
```

Launchpad's Live page shows your settings as knobs and switches.

If anything goes wrong on the pedal: unplug, hold Foot + PARA, plug in, and flash a firmware that worked
(GETTING-STARTED.md, "Recovery").

## 7. Make it yours

- **Presets:** add `"presets": 4` to `firmware.json`, and `GP5_P_PRESET` instead of `GP5_P_SAVE` on the settings
  that make a sound. `gp5_preset_load(n)` / `gp5_preset_save(n)` from your controls; Launchpad can load, save and
  rename them too. `firmwares/tremolo` uses PARA hold to save ([Parameters and presets](params.md)).
- **A settings page:** the [UI kit](ui.md) draws a page of your parameters and handles PARA for you, in about
  ten lines. `firmwares/tremolo` uses it.
- **Your own text for a value:** `GP5_U_CUSTOM` and `fw_param_text()`, so the screen and Launchpad say "5.00 Hz".
- **More sound:** a drive (`tanhf`), a filter (a biquad is 5 multiplies per sample), an echo (a `GP5_LARGE`
  buffer), a chorus (a short delay whose length follows a slow `sinf`).

## 8. Further

- **`firmwares/tremolo`**: this tremolo finished, with presets, a smooth-font screen that redraws only what changed,
  PARA click and hold, and a screen preview that runs on the computer (`test/screen.sh`).
- **The audio link** (`"audio": {"link": true}`, `gp5_link_start`) exchanges words between the halves after every
  block. Use it for data that changes all the time, or to run sound on the control processor too.
  [NAM Loader](https://github.com/OpenGP5/NAM-Loader) runs most of its model there this way.
- **[Dreamcatcher](https://github.com/OpenGP5/Dreamcatcher)**: a large firmware in its own repository, with a
  looper, delays, reverb, presets and a graphic screen, built on OpenGP5 as a submodule.
