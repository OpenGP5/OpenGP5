# Your first effect

This walks through `firmwares/tremolo`, a complete firmware of about 140 lines: the guitar's volume rises
and falls with a sine wave, the volume knob sets how deep, the PARA knob sets how fast, the foot switch turns it
on and off. Read it next to the two files; then copy the folder and change it.

## 1. A firmware has two halves

The pedal has two processors, and a firmware has one part for each:

```
firmwares/tremolo/dsp/firmware.h      the sound: runs on the DSP, 64 samples at a time
firmwares/tremolo/nds32/firmware.c    the knobs, foot switch and screen: runs on the control processor
```

They talk through **settings**: numbered values (`id`, `value`) that the control half hands over and the
DSP half receives. The platform carries them across; you do not deal with the wire.

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

The tremolo's core is four lines per sample:

```c
float wave = 0.5f + 0.5f * sinf(2.0f * GP5_PI * phase);   /* 0 .. 1 */
float gain = 1.0f - depth_now * wave;
phase += step; if (phase >= 1.0f) phase -= 1.0f;
out_left[i] = out_right[i] = (int32_t)((float)in[i] * gain);
```

`depth_now` glides towards the set depth a little every sample, so turning the knob never clicks.

**Time budget:** one block may take at most 278,000 DSP cycles (64 samples at 44.1 kHz, 191.9 MHz):
about 4,300 per sample. A `sinf` costs roughly 40. The tremolo uses a few percent; the granular delay about
20 %.

## 3. The controls and screen (`nds32/firmware.c`)

The platform calls these from its main loop (none of them may wait):

| function | when | the tremolo |
|---|---|---|
| `fw_init` | start-up | default rate 5 Hz, depth 50 %, on |
| `fw_knob(0..255)` | about every 10 ms | depth |
| `fw_para_turn(clicks)` | PARA turned | rate, 0.25 Hz per click |
| `fw_para_push`, `fw_foot` | pressed | foot = on / off |
| `fw_level()` | always | output volume 0..255: the tremolo keeps it at 255 |
| `fw_take(&id, &value)` | always | hands over the next changed setting, or returns 0 |
| `fw_dsp_ready` | the DSP (re)started | mark every setting as changed, so all are sent |
| `fw_setting_count` | start-up | 3: the LEDs start once ids 0..2 have arrived |
| `fw_draw(knob, force)` | every pass | four text lines with `gp5_text(line, "13 characters")` |
| `fw_active` | always | LED colour: mint when on, dim red when off (or your own: `return GP5_LED(10, 4, 32);`) |

The pattern to copy: keep the settings in an array, set a bit when one changes, and let `fw_take` hand the
changed ones over.

## 4. Try it on the computer

No pedal needed for the sound:

```
python3 tools/run_effect.py tremolo guitar.wav out.wav --set 0=400 --set 1=255
python3 tools/run_effect.py tremolo x out.wav --tone            # no recording? a plucked test tone
```

`--set ID=VALUE` sends a setting exactly as the control half would. The program is your `firmware.h`
compiled for the computer with the platform's own maths functions, so it computes what the pedal computes.

## 5. Build, check, flash

```
platform/build_firmware.sh tremolo                     # -> build/GP-5_tremolo.bin
python3 tools/dsp_emu_check.py build/GP-5_tremolo.bin   # boots the built DSP code in an emulator
python3 tools/gp5tool.py flash build/GP-5_tremolo.bin --sections all
python3 tools/gp5_link.py load                          # on the pedal: cycles per block, late blocks
```

If anything goes wrong on the pedal: unplug, hold Foot + PARA, plug in, flash a firmware that worked
(GETTING-STARTED.md, "Recovery").

## 6. Make it yours

```
cp -r firmwares/tremolo firmwares/my-effect
```

Change `fw_process`, give your settings ids, show them on the screen. Ideas that fit easily: a drive
(`tanhf`), a filter (a biquad is 5 multiplies per sample), an echo (`GP5_LARGE` buffer), a chorus (a short
delay whose length follows a slow `sinf`). `firmwares/granular-delay` shows a large effect with 13 settings
and a graphic screen.
