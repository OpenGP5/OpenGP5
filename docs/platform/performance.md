# Performance: making it fit

Every 64 samples (1.45 ms at 44.1 kHz), the DSP must finish `fw_process()` and the control processor must finish
`on_block()` if you use [the audio link](audio-link.md). This page is what was learnt the hard way, measured on the
pedal.

## The budgets

| | cycles per block at 44.1 kHz | at 48 kHz |
|---|---|---|
| DSP (191.9 MHz) | about 278,000 | about 255,900 |
| control processor at 288 MHz | 417,920 | 384,000 |
| control processor at 320 MHz (`"control": {"mhz": 320}`) | 464,384 | 426,624 |

Leave room. Aim for a worst case under about 85 % of a block on both processors, measured while playing the heaviest
settings.

## Measuring

- **`python3 tools/gp5_link.py status`** on the pedal: both processors' share of a block (last and worst), the DSP's
  late blocks (a late block is an audible glitch), the control processor's longest main-loop pass, and memory.
  `--reset` starts the worst values again. Launchpad's Developer page shows the same, as bars, while you play.
- **Time a piece of code** with `GP5_CYCLE_COUNT()` (both processors). Keep the worst and log it now and then, not
  every block:

  ```c
  static volatile uint32_t worst;                 /* the slowest block so far, in cycles */

  static void on_block(const int32_t *in, int32_t *out)
  {
      uint32_t t = GP5_CYCLE_COUNT();
      /* ... the work ... */
      t = GP5_CYCLE_COUNT() - t;
      if (t > worst) worst = t;
  }
  void fw_tick(void)                              /* every 10 ms: report once a second */
  {
      static unsigned n;
      if (++n % 100 == 0) gp5_log("worst %lu cycles of %lu", (unsigned long)worst,
                                  (unsigned long)(GP5_CTL_MHZ * 1000000u / GP5_SAMPLE_RATE * GP5_BLOCK));
  }
  ```

  Read it with `gp5_link.py log -f`.
- The simulator and `run_effect.py` don't count cycles: they check what your code computes, not how fast it is.

## The DSP

- **Average about 4 to 5 cycles per instruction**, even from RAM. A trivial loop step measured 32 cycles. Budget
  per sample, not per block: 278,000 cycles per block is about 4,300 per sample.
- **Code runs from flash unless you say otherwise**, and big loops miss its small fetch cache. Dreamcatcher's audio
  loop took 110 % of a block from flash and 42 % from RAM. Mark the functions that run for every sample with
  `GP5_FAST`:

  ```c
  GP5_FAST static void fw_process(const int32_t *in, int32_t *out_left, int32_t *out_right) { ... }
  ```

  The code then takes room from the 19.5 KB of variables, so use it for the hot parts only. The build's warning
  about a "LOAD segment with RWX permissions" is expected then.
- **`float`, never `double`.** The float unit is single precision; `double` runs in software and is many times
  slower. Write `0.5f`, not `0.5` (the build warns about doubles).
- **Maths functions cost tens of cycles** (`sinf` about 40; `powf`, `expf` and `tanhf` more). Work out what you can
  in `fw_param()`, when a setting changes, not per sample: a filter's coefficients, a delay length, a gain in
  decibels turned into a factor. For a waveform, a phase accumulator and `sinf`, or a small table, are both fine.
- **Big buffers go in the 122 KB block** with `GP5_LARGE`. Both memories are equally fast.

## The control processor

- **Its float unit takes about 5 cycles per operation, with no overlap.** Even so, the same float code ran about
  1.7 times faster than on the DSP, and it has more memory.
- **It has no working fused multiply-add.** The instruction exists, but it hangs the processor. The platform's build
  never uses it; don't turn it on with your own compiler flags.
- **Its integer DSP instructions run about one per cycle:** `__nds32__kmmac_u`, `__nds32__kmmawb_u` /
  `_kmmawt_u` (32 × 16 bits), `__nds32__kwmmul_u`, from `<nds32_intrinsic.h>`. Fixed-point code built on them is
  several times faster than float.
  - NAM Loader's float cab IR used about 5 cycles per float operation: 256 taps took 9 % of a 48 kHz block, and
    1,024 taps didn't fit.
  - Rewritten in fixed point with these instructions, 1,024 taps fit with the whole amp model (worst 79 % of a
    block at 320 MHz).

  Keep a portable version of such code for when `__NDS32_EXT_DSP__` isn't defined, so the simulator can run it.
- **Code runs from flash through a cache**, built for small size (`-Os`). A file with hot code can ask for speed with
  `#pragma GCC optimize("O2")` at its top, as NAM Loader's `a2.c` does.
- **A faster clock for the asking:** `"control": {"mhz": 320}` runs it at Valeton's own clock, 11 % more
  cycles.

## The main loop

Your callbacks (`fw_draw`, `fw_tick`, the controls) share one loop with USB and saving. A long pass makes the
controls and USB sluggish, but it never affects the sound, which runs in interrupts. `gp5_link.py status` shows the
longest pass. Keep it under a block or two:

- draw the screen in strips (`fw_draw` returns non-zero while more is left), not all at once. The simulator says how
  much your firmware sends in one pass: NAM Loader's passes of 13-18 ms came from sending 14 KB at once, and strips
  of a quarter of the screen fixed them;
- move heavy work out of callbacks, or spread it over several passes;
- saving to flash takes about 1 ms, and a few hundred ms now and then (an erase). The platform saves parameters
  2 s after the last change for you.

## When it doesn't fit

In this order:
1. Measure first: which part is slow?
2. Move per-sample work into `fw_param()`.
3. Put the hot DSP code in RAM (`GP5_FAST`).
4. Move work to the other processor over the [audio link](audio-link.md).
5. On the control processor, rewrite the hot loop in fixed point.
6. Run part of it at half rate: Dreamcatcher's reverb runs at half rate on the DSP, in about 36 % of a block.
