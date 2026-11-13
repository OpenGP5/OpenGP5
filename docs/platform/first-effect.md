# Your first effect

Start from `firmwares/passthrough`, the smallest firmware: the guitar goes straight through and the volume
knob sets the level. This page turns a copy of it into a tremolo, step by step: the guitar's volume rises and
falls with a sine wave, the volume knob sets how deep, the PARA knob how fast, the foot switch turns it on and
off. The complete code of both files is below; about 130 lines in all.

## 1. Copy the passthrough

```
cp -r firmwares/passthrough firmwares/my-tremolo
```

The pedal has two processors, and a firmware has one part for each:

```
firmwares/my-tremolo/dsp/firmware.h      the sound: runs on the DSP, 64 samples at a time
firmwares/my-tremolo/nds32/firmware.c    the knobs, foot switch and screen: runs on the control processor
```

They talk through **settings**: numbered values (`id`, `value`) that the control half hands over when they
change and the DSP half receives. The platform carries them across; you do not deal with the wire. (For data
that changes all the time, there is also an audio link between the halves; see section 7.)

## 2. The sound (`dsp/firmware.h`)

Three functions, called by the platform:

```c
static void fw_init(void);                                           /* once, at start-up */
static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right);   /* every 1.45 ms */
static int  fw_command(uint32_t cmd, const uint8_t *payload, uint32_t n);           /* a setting arrived */
```

- `in` holds `GP5_BLOCK` (64) guitar samples, 32-bit signed, full scale ±2^31. Divide by `GP5_FULL_SCALE`
  for -1.0 … 1.0 if you prefer to think in floats. Write 64 samples to each output.
- Write ordinary C: variables at file level keep their value between calls (`static float phase;`),
  `float` maths runs on the DSP's floating-point unit, and `gp5_math.h` gives you `sinf`, `cosf`, `expf`,
  `logf`, `powf`, `tanhf`, `sqrtf` and friends. Write `0.5f`, not `0.5`: plain `0.5` is a `double`,
  which the DSP does in software, slowly (the build warns).
- Big buffers (delay lines, loopers) go in the 122 KB block: `static int16_t line[44100] GP5_LARGE;`.
  Ordinary variables have 19.5 KB. The build prints what you use and refuses what does not fit.
- `fw_command` gets every setting as link command `0x7F10` with `[id u32][value u32]` at `payload + 4`.
  Read them with `gp5_get32`, answer with `gp5_ack()` and return 1.

The passthrough's `fw_process` copies `in` to both outputs. Replace the whole file with this:

```c
/* Tremolo, DSP half. Settings from the control half:
 *   0  rate    in 1/100 Hz, 50 .. 1500 (0.5 .. 15 Hz)
 *   1  depth   0 .. 255
 *   2  on      0 = straight through, 1 = tremolo */
#include "gp5_math.h"

static float phase;              /* 0 .. 1, one full wave */
static float step;               /* phase per sample = rate / sample rate */
static float depth, depth_now;   /* target and smoothed depth, 0 .. 1 */
static int on;

static void set_rate(uint32_t centi_hz)
{
    if (centi_hz < 50) centi_hz = 50;
    if (centi_hz > 1500) centi_hz = 1500;
    step = (float)centi_hz / 100.0f / (float)GP5_SAMPLE_RATE;
}

static void fw_init(void)
{
    phase = 0.0f;
    set_rate(500);               /* 5 Hz */
    depth = depth_now = 0.5f;
    on = 1;
}

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    float target = on ? depth : 0.0f;
    for (uint32_t i = 0; i < GP5_BLOCK; i++) {
        depth_now += 0.001f * (target - depth_now);              /* glide, so changes never click */
        float wave = 0.5f + 0.5f * sinf(2.0f * GP5_PI * phase);   /* 0 .. 1 */
        float gain = 1.0f - depth_now * wave;
        phase += step;
        if (phase >= 1.0f) phase -= 1.0f;
        int32_t y = (int32_t)((float)in[i] * gain);
        out_left[i] = y;
        out_right[i] = y;
    }
}

static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n)
{
    if (cmd != 0x7F10 || n < 12) return 0;                       /* not a setting: the platform answers */
    uint32_t id = gp5_get32(p + 4), value = gp5_get32(p + 8);
    if (id == 0) set_rate(value);
    else if (id == 1) depth = (float)(value > 255 ? 255 : value) / 255.0f;
    else if (id == 2) on = value != 0;
    gp5_ack();
    return 1;
}
```

**Time budget:** one block may take at most 278,000 DSP cycles (64 samples at 44.1 kHz, 191.9 MHz):
about 4,300 per sample. A `sinf` costs roughly 40. This tremolo uses a few percent.

## 3. Try the sound on the computer

No pedal needed:

```
python3 tools/run_effect.py my-tremolo guitar.wav out.wav --set 0=400 --set 1=255
python3 tools/run_effect.py my-tremolo x out.wav --tone            # no recording? a plucked test tone
```

`--set ID=VALUE` sends a setting exactly as the control half would. The program is your `firmware.h`
compiled for the computer with the platform's own maths functions, so it computes what the pedal computes.

## 4. The controls and screen (`nds32/firmware.c`)

The platform calls these from its main loop (none of them may wait):

| function | when | the tremolo |
|---|---|---|
| `fw_init` | start-up | rate 5 Hz, depth 50 %, on |
| `fw_knob(0..255)` | about every 10 ms | depth |
| `fw_para_turn(clicks)` | PARA turned | rate, 0.25 Hz per click |
| `fw_para_push`, `fw_foot` | pressed (debounced) | foot = on / off |
| `fw_level()` | always | output volume 0..255: the tremolo keeps it at 255 (the knob is the depth) |
| `fw_take(&id, &value)` | always | hands over the next changed setting, or returns 0 |
| `fw_dsp_ready` | the DSP (re)started | mark every setting as changed, so all are sent again |
| `fw_setting_count` | start-up | 3: the LEDs start once ids 0..2 have arrived |
| `fw_draw(knob, force)` | every pass | text lines with `gp5_text(line, "13 characters")` |
| `fw_active` | always | LED colour: mint when on, dim red when off (or your own: `return GP5_LED(10, 4, 32);`) |

The pattern: keep the settings in an array, set a bit when one changes, and let `fw_take` hand the changed
ones over. Replace the whole file with this:

```c
/* Tremolo, control half: volume knob = depth, PARA = rate, foot switch = on / off. */
#include "gp5_firmware.h"

enum { ID_RATE, ID_DEPTH, ID_ON, N_IDS };

static uint32_t value[N_IDS];
static uint32_t dirty;                  /* one bit per id: changed, not sent yet */
static unsigned knob_seen = 1000, shown_rate, shown_depth, shown_on = 2;

static void set(unsigned id, uint32_t v) { if (value[id] != v) { value[id] = v; dirty |= 1u << id; } }

void fw_init(void)
{
    value[ID_RATE] = 500; value[ID_DEPTH] = 128; value[ID_ON] = 1;
    dirty = 0; knob_seen = 1000; shown_on = 2;
}

void fw_dsp_ready(void) { dirty = (1u << N_IDS) - 1; }        /* the DSP starts with its defaults: send all */
unsigned fw_setting_count(void) { return N_IDS; }
unsigned fw_level(void) { return 255; }
unsigned fw_active(void) { return value[ID_ON]; }

void fw_foot(void) { set(ID_ON, !value[ID_ON]); }
void fw_para_push(void) {}

void fw_para_turn(int detents)
{
    int r = (int)value[ID_RATE] + 25 * detents;
    set(ID_RATE, (uint32_t)(r < 50 ? 50 : r > 1500 ? 1500 : r));
}

void fw_knob(unsigned knob)
{
    /* the knob reading wobbles by a step or two: only follow real moves */
    if (knob_seen != 1000 && knob + 2 >= knob_seen && knob <= knob_seen + 2) return;
    knob_seen = knob;
    set(ID_DEPTH, knob);
}

int fw_take(uint32_t *id, uint32_t *v)
{
    for (unsigned i = 0; i < N_IDS; i++)
        if (dirty & (1u << i)) { dirty &= ~(1u << i); *id = i; *v = value[i]; return 1; }
    return 0;
}

static void number(char *p, unsigned v, unsigned digits)        /* right-aligned decimal */
{
    for (unsigned i = digits; i-- > 0; v /= 10) p[i] = (char)('0' + v % 10);
}

int fw_draw(unsigned knob, int force)
{
    (void)knob;
    if (force) { gp5_text(0, "Tremolo      "); gp5_text(4, "             "); shown_on = 2; shown_rate = shown_depth = 100000; }
    if (value[ID_RATE] != shown_rate) {
        char s[14] = "rate 00.00 Hz";
        number(s + 5, value[ID_RATE] / 100, 2); number(s + 8, value[ID_RATE] % 100, 2);
        gp5_text(1, s); shown_rate = value[ID_RATE];
    }
    if (value[ID_DEPTH] != shown_depth) {
        char s[14] = "depth   000 %";
        number(s + 8, value[ID_DEPTH] * 100 / 255, 3);
        gp5_text(2, s); shown_depth = value[ID_DEPTH];
    }
    if (value[ID_ON] != shown_on) {
        gp5_text(3, value[ID_ON] ? "on           " : "off          "); shown_on = value[ID_ON];
    }
    return 0;
}
```

The screen is five lines of 13 characters (`gp5_text`). For smooth text in any font and size, see
[Text and fonts](fonts.md); for pixels of your own, `gp5_blit` (`platform/nds32/gp5_firmware.h`).

## 5. Build, check, flash

```
platform/build_firmware.sh my-tremolo                       # -> build/GP-5_my-tremolo.bin
python3 tools/dsp_emu_check.py build/GP-5_my-tremolo.bin    # boots the built DSP code in an emulator
python3 tools/gp5tool.py flash build/GP-5_my-tremolo.bin --sections all
python3 tools/gp5_link.py load                              # on the pedal: cycles per block, late blocks
```

If anything goes wrong on the pedal: unplug, hold Foot + PARA, plug in, flash a firmware that worked
(GETTING-STARTED.md, "Recovery").

## 6. Make it yours

Change `fw_process`, give your settings ids, show them on the screen. To keep settings across power-off,
save them with `gp5_store_save` (`platform/README.md`). Ideas that fit easily: a drive (`tanhf`), a filter
(a biquad is 5 multiplies per sample), an echo (`GP5_LARGE` buffer), a chorus (a short delay whose length
follows a slow `sinf`).

## 7. Further

- **`firmwares/tuner`**, a strobe tuner (demo): the **audio link** (`gp5_link_start` /
  `#define GP5_AUDIO_LINK`), which exchanges 128 words each way between the halves after every block, for data
  that changes all the time; a graphic screen drawn pixel by pixel with smooth lettering ([Text and fonts](fonts.md)); settings kept across power-off; and tests that run on the computer.
- **[Dreamcatcher](https://github.com/OpenGP5/Dreamcatcher)**, in its own repository: a large firmware with a
  looper, delays, reverb, presets and a graphic screen, built on OpenGP5 as a submodule.
