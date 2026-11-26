# Performance: making it fit

The DSP must finish `fw_process()` within each 64-sample block. A block lasts 1.45 ms at 44.1 kHz or 1.33 ms at 48 kHz.
If you use [the audio link](audio-link.md), the control chip must also finish `on_block()` within that time.
These figures come from measurements on the pedal. Measure your effect under load to check that it fits.

## The budgets

| Processor | Cycles per block at 44.1 kHz | At 48 kHz |
|---|---|---|
| DSP (191.9 MHz) | about 278,000 | about 255,900 |
| control processor at 288 MHz | 417,920 | 384,000 |
| control processor at 320 MHz (`"control": {"mhz": 320}`) | 464,384 | 426,624 |

Keep peak load below about **85 %** of a block on each chip. Peak load is the most time used by any measured block.
Test with the settings that use the most time so the spare time covers changes in load.

## Measuring

- **Run `python3 tools/gp5_link.py status`** with the pedal connected. It shows current and peak block load, late blocks,
  the longest main-loop pass, and memory. Late blocks can cause audible glitches.
  Add `--reset` to clear the peak readings. Launchpad's Developer page shows the same readings.
- **Time a piece of code** with `GP5_CYCLE_COUNT()` on either processor. Keep its peak cycle count.
  Log that value once per second to limit the log's own time cost:

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

  This code shares the 19,712-byte variable area. Use it for loops that run often and cost the most time.
  Measured 2026-11-25 (cycles per float multiply-add): a small loop takes 13 from flash and 24 with `GP5_FAST`; 5 KB
  of straight-line code takes 10 from flash and 12 with `GP5_FAST`. So keep small loops in flash, and use
  `GP5_FAST` for large loops with branches, which miss the flash cache.
  The build may then warn about a "LOAD segment with RWX permissions" because the section holds code in writable RAM.
- **`float`, never `double`.** The float unit is single precision; `double` runs in software and is many times
  slower. Write `0.5f`, not `0.5` (the build warns about doubles).
- **Maths functions cost tens of cycles** (`sinf` about 40; `powf`, `expf` and `tanhf` more). Work out what you can
  in `fw_param()`, when a setting changes, not per sample: a filter's coefficients, a delay length, a gain in
  decibels turned into a factor. For a waveform, a phase accumulator and `sinf`, or a small table, are both fine.
- **Put large buffers in the 122,880-byte area** with `GP5_LARGE`. The measured access speed is the same for both RAM areas.

## The control chip

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
- **Choose 320 MHz** with `"control": {"mhz": 320}`. This matches Valeton's clock and gives about 11 % more cycles per block.

## The main loop

Your callbacks (`fw_draw`, `fw_tick` and controls) share the main loop with USB and saving.
A long pass delays controls and USB. Audio runs in interrupts, but long flash writes can still disrupt the audio link.
`gp5_link.py status` shows the longest pass. Aim for one or two blocks per pass:

- draw the screen in strips (`fw_draw` returns non-zero while more is left), not all at once. The simulator says how
  much your firmware sends in one pass: NAM Loader's passes of 13-18 ms came from sending 14 KB at once, and strips
  of a quarter of the screen fixed them;
- move heavy work out of callbacks, or spread it over several passes;
- saving to flash takes about 1 ms, and a few hundred ms now and then (an erase). The platform saves parameters
  2 s after the last change for you.

## When it doesn't fit

In this order:
1. Measure first: which part is slow?
2. Move calculations that depend on a setting, such as filter coefficients, into `fw_param()`.
3. Put the most time-consuming DSP loops in RAM (`GP5_FAST`).
4. Move work to the other processor over the [audio link](audio-link.md).
5. On the control chip, rewrite the most time-consuming loop in fixed point.
6. Run part of it at half rate: Dreamcatcher's reverb runs at half rate on the DSP, in about 36 % of a block.
