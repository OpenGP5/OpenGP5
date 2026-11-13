# Your first effect

Start from `firmwares/tuner`, a demo firmware built with OpenGP5: the guitar goes straight through, and the
screen is a strobe tuner. It is a complete firmware that uses every part of the platform: audio, both processors, the
audio link between them, the screen, the knobs, the foot switch, the LEDs and storage. Copy it, add your
sound, keep the tuner or replace it.

## 1. A firmware has two halves

The pedal has two processors, and a firmware has one part for each:

```
firmwares/tuner/dsp/firmware.h        the sound: runs on the DSP, 64 samples at a time
firmwares/tuner/dsp/tuner.h           the tuner's maths (note detection, strobe), included by firmware.h
firmwares/tuner/dsp/link_words.h      what the two halves exchange, shared by both
firmwares/tuner/nds32/firmware.c      the knobs, foot switch and screen: runs on the control processor
```

The halves talk in two ways:

- **Settings:** numbered values (`id`, `value`) that the control half hands over when they change and the
  DSP half receives. Right for knob positions and on / off switches.
- **The audio link:** 128 words each way after every audio block (1.45 ms), in both directions. Right for
  anything continuous. The tuner sends its note, cents and strobe positions this way, and gets back the
  reference pitch and the mute switch.

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
- The audio link: put `#define GP5_AUDIO_LINK` in `firmware.h`; in `fw_process`, `gp5_link_in()` holds the
  words the control half sent, and you fill `gp5_link_out()` for it.

In the tuner, the sound is one line: `out_left[i] = out_right[i] = in[i]` (or 0 while muted). Put your
effect there. A tremolo, for example:

```c
static float phase;                                     /* 0 .. 1, one cycle of the wobble */
...
for (uint32_t i = 0; i < GP5_BLOCK; i++) {
    float wave = 0.5f + 0.5f * sinf(2.0f * GP5_PI * phase);
    float gain = 1.0f - 0.5f * wave;                    /* depth 50 % */
    phase += 5.0f / GP5_SAMPLE_RATE; if (phase >= 1.0f) phase -= 1.0f;    /* 5 Hz */
    out_left[i] = out_right[i] = (int32_t)((float)in[i] * gain);
}
```

**Time budget:** one block may take at most 278,000 DSP cycles (64 samples at 44.1 kHz, 191.9 MHz):
about 4,300 per sample. A `sinf` costs roughly 40. The tuner uses about 15 %.

## 3. The controls and screen (`nds32/firmware.c`)

The platform calls these from its main loop (none of them may wait):

| function | when | the tuner |
|---|---|---|
| `fw_init` | start-up | loads the saved reference pitch (`gp5_store_load`), starts the audio link |
| `fw_knob(0..255)` | about every 10 ms | the output level; also its clock for "save after 2 s" |
| `fw_para_turn(clicks)` | PARA turned | reference A4, 1 Hz per click, 430..450 Hz |
| `fw_para_push`, `fw_foot` | pressed | push: back to 440 Hz; foot: mute on / off |
| `fw_level()` | always | output volume 0..255: the volume knob |
| `fw_take(&id, &value)` | always | hands over the next changed setting, or returns 0: none (it uses the link) |
| `fw_dsp_ready` | the DSP (re)started | mark every setting as changed, so all are sent again |
| `fw_setting_count` | start-up | 0: the LEDs start once ids 0..count-1 have arrived |
| `fw_draw(knob, force)` | every pass | note, cents and strobe, drawn as pixels with `gp5_blit` |
| `fw_active` | always | LED colour: mint, dim red while muted (or your own: `return GP5_LED(10, 4, 32);`) |

For settings, the pattern to copy: keep them in an array, set a bit when one changes, and let `fw_take` hand
the changed ones over. For a quick text screen, `gp5_text(line, "13 characters")` writes one of five lines.

## 4. Try it on the computer

No pedal needed for the sound:

```
python3 tools/run_effect.py tuner guitar.wav out.wav
python3 tools/run_effect.py tuner x out.wav --tone              # no recording? a plucked test tone
python3 tools/run_effect.py my-effect guitar.wav out.wav --set 0=400   # send a setting, as the control half would
```

The program is your `firmware.h` compiled for the computer with the platform's own maths functions, so it
computes what the pedal computes. On the computer, words from the audio link arrive as zeros.

The tuner also has its own tests: `firmwares/tuner/test/run.sh` plays test tones at set pitches into the
tuner's maths and checks what it reports; `firmwares/tuner/test/screen.sh` draws its screen into PNG files.

## 5. Build, check, flash

```
platform/build_firmware.sh tuner                     # -> build/GP-5_tuner.bin
python3 tools/dsp_emu_check.py build/GP-5_tuner.bin   # boots the built DSP code in an emulator
python3 tools/gp5tool.py flash build/GP-5_tuner.bin --sections all
python3 tools/gp5_link.py load                        # on the pedal: cycles per block, late blocks
```

If anything goes wrong on the pedal: unplug, hold Foot + PARA, plug in, flash a firmware that worked
(GETTING-STARTED.md, "Recovery").

## 6. Make it yours

```
cp -r firmwares/tuner firmwares/my-effect
```

Change `fw_process`, give your settings ids, show them on the screen. To keep settings across power-off,
save them with `gp5_store_save` (`platform/README.md`). Ideas that fit easily: a drive (`tanhf`), a filter
(a biquad is 5 multiplies per sample), an echo (`GP5_LARGE` buffer), a chorus (a short delay whose length
follows a slow `sinf`). [Dreamcatcher](https://github.com/OpenGP5/Dreamcatcher), in its own repository,
shows a large effect: a looper, delays, reverb, presets and a graphic screen.
