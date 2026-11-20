# Sound on both processors: the audio link

After every audio block, the two processors swap a block of 32-bit words: the **audio link**. With it, a firmware
can send the guitar to the control processor, process it there, and play the result on the DSP.

Why bother? The control processor is often the better place for heavy sound code:

- it has more memory (about 270 KB against the DSP's 19.5 + 122 KB);
- it ran the same float code about 1.7 times faster;
- with its integer DSP instructions, fixed-point code is much faster again ([performance](performance.md)).

NAM Loader runs most of its amp model there. Dreamcatcher runs its whole engine there and mixes on the DSP.

The cost: what goes there comes back **3 blocks later** (about 4.4 ms at 44.1 kHz). Keep the dry signal on the DSP so
it isn't delayed.

## The smallest example

The guitar goes to the control processor, gets a soft clip there, and comes back.

`firmware.json`: ask for the link and how many words each way:

```json
{ "id": "link-demo", "name": "Link demo", "version": "0.1.0", "platform": "0.8",
  "audio": { "link_words": 64 } }
```

`dsp/firmware.h`:

```c
static void fw_init(void) {}

static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right)
{
    const int32_t *back = gp5_link_in();       /* the control processor's words: a block from 3 blocks ago, processed */
    int32_t *to = gp5_link_out();              /* words for the control processor */
    for (uint32_t i = 0; i < GP5_BLOCK; i++) {
        to[i] = in[i];
        out_left[i] = out_right[i] = back[i];
    }
}

static int fw_command(uint32_t cmd, const uint8_t *p, uint32_t n) { (void)cmd; (void)p; (void)n; return 0; }
```

`nds32/firmware.c`:

```c
#include "gp5_firmware.h"
#include "gp5_math.c"                          /* sinf, tanhf ... for this processor (once per firmware) */

static void on_block(const int32_t *in, int32_t *out)   /* in an interrupt, once per block: keep it short */
{
    for (unsigned i = 0; i < GP5_BLOCK; i++) {
        float x = (float)in[i] / 2147483648.0f;
        out[i] = (int32_t)(tanhf(4.0f * x) * 0.5f * 2147483647.0f);
    }
}

void fw_init(void) { gp5_link_start(on_block); }
```

Try it: `python3 tools/simulate.py <folder>`. The simulator runs the link with the pedal's timing.

## How it works

- After each block, the DSP sends `link_words` words and receives as many. The platform frames them with a sync
  word and finds them again on the other side; you only see your words.
- On the control processor, `on_block(in, out)` runs **in an interrupt** once per block. `in` holds the DSP's words.
  What you write to `out` goes back to the DSP.
- On the DSP, `gp5_link_out()` takes the words for the control processor during `fw_process()`, and
  `gp5_link_in()` has what came back.
- Words from the control processor reach `fw_process()` 3 blocks after the block they were made from.

## Rules and limits

- **`link_words`:** a multiple of 8, at most 264. The exchange takes time while the next block is already coming
  (about 38 % of a block per 128 words). At 48 kHz, 264 was measured to run reliably and 296 dropped out now and then,
  so the build refuses more than 264. Pack your data if you need more: NAM Loader sends 24-bit samples in block
  floating point (`pack24.h` in its repository).
- **`link_early`:** if `fw_process()` itself runs long (more than about half a block), set
  `"audio": { "link_words": N, "link_early": true }`. The exchange then starts at the beginning of each block, with
  what `fw_process()` wrote the block before, instead of after it. Your firmware also defines
  `static void fw_link_early(const int32_t *in, int32_t *out)` on the DSP. It runs just before the exchange and can put
  words that are ready at once (the new input) straight into `out`. NAM Loader does this.
- **`on_block` must finish within a block** (1.45 ms; 1.33 ms at 48 kHz). It interrupts the main loop, so the longer it
  takes, the less time is left for the screen and USB. `gp5_link.py status` shows its share: keep the worst under
  about 85 %.
- **Don't call the platform from `on_block`.** No drawing, no storage, no `gp5_param_set`. Hand data to the main loop
  through variables; `gp5_log` is allowed but costly.
- **A lost exchange repeats or skips a block.** Rare, but a long flash write (saving, a computer writing files)
  can cause it. If your data has state (a model's history, a sequence), put a counter in a word and check it on the
  other side, as NAM Loader and Dreamcatcher do.

## Who does what: patterns that worked

| | DSP | control processor |
|---|---|---|
| Dreamcatcher | dry signal, final mix, reverb, exact bypass | the whole looper / delay / grain engine (float) |
| NAM Loader | the model's first layers, bypass | the remaining layers, the cab IR (fixed point), gate, EQ |

Measure both sides with `gp5_link.py status` while playing your heaviest settings.
