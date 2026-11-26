/* NAM A2-Lite on both processors: the DSP runs layers 0..A2_SPLIT-1 on the guitar input, the control processor the
 * rest, the output and the cab IR. Shared by both sides. Kernel: kernel/a2r.c (32-bit residual stream), model: any
 * A2 capture (A2-Lite part), loaded at run time (below). Cab IR: cab/cabconv.h on the control processor.
 *
 * Why this way round (since the cab IR): the IR needs the finished output, and only the control processor has the
 * time for it (about 10 % of a block at 1024 taps). With the output finished there, the audio crosses the link
 * once each way, as before: one block more latency than with the DSP finishing (it has to run its layers before
 * it can send), instead of three for an extra round trip.
 *
 * Link words (GP5_LINK_WORDS 264, set in dsp/firmware.h: measured on the pedal at 48 kHz, 296 already loses sync
 * now and then). The DSP's three blocks go over in 24-bit block floating point (pack24.h, 107 dB or better from
 * unpacked even for a high-gain capture at full level):
 *   DSP -> control processor, for input block n (sent at the start of block n+1):
 *     [0] MAGIC_D, [1] tag n, [2] the checksum of the capture the DSP holds (0 = none), [3] 1 when the blocks below
 *     are valid for it, [4] DSP cycles, [5] shifts: u | x channel 0 << 4 | 1 << 8 | 2 << 12 | y << 16,
 *     [6..53] guitar input u(n) (64 values; every layer mixes it in), [54..197] the residual stream out of layer
 *     A2_SPLIT-1 (192 values, rows of 3), [198..245] the DSP's share of the output (64 values)
 *   control processor -> DSP:
 *     [0] MAGIC_N, [1..64] the output (Q31, cab IR applied), [65] the tag of the input it belongs to,
 *     [66] flags: bit 0 bypass, bit 1 valid, [67] sequence number, [68] its cycles for the block,
 *     [69] the checksum of the capture it runs, [70] output gain (Q16; the DSP ramps to it over the block),
 *     [71..75] the cab IR's cycles and state (LW_CAB)
 *
 * The capture (a2r_blob.h file) is in a slot of the bank (the control processor's storage files "tone-NN"), or the
 * built-in default. The DSP gets its own copy over the link: while the checksum it reports is not the control
 * processor's, the control processor sends the file instead of audio: [0] MAGIC_W, [1] word offset, [2] word count
 * (<= LW_W_MAX), [3] words in all, [4] checksum, [5..] the words. */
#ifndef NAM_A2SPLIT_H
#define NAM_A2SPLIT_H
#define A2_SPLIT   5             /* first layer on the control processor (layers 0..4 on the DSP: 5 of 23; one more since the IR, 2026-11-16) */
#define MAGIC_D    0x4E414D33u   /* "NAM3" (the layout since the cab IR) */
#define MAGIC_N    0x4E414D34u   /* "NAM4" */
#define MAGIC_W    0x4E414D57u   /* "NAMW": a piece of the capture file */
/* DSP -> control processor */
#define LW_TAG     1
#define LW_DSPCRC  2
#define LW_DVALID  3
#define LW_DSPCYC  4
#define LW_SHIFTS  5
#define LW_U       6             /* 48 words packed */
#define LW_X       54            /* 144 words packed */
#define LW_YD      198           /* 48 words packed */
/* control processor -> DSP */
#define LW_Y       1
#define LW_RTAG    65
#define LW_FLAGS   66
#define LW_SEQ     67
#define LW_CTLCYC  68
#define LW_CAPCRC  69
#define LW_GAIN    70
#define LW_CAB     71            /* [71] IR cycles, [72] forward FFT, [73] products, [74] inverse FFT, [75] taps | on << 16 | auto-off << 17 */
#define LW_W_DATA  5
#define LW_W_MAX   256

/* The capture bank: NAM_SLOTS storage files "tone-01".. (2 sectors each: a capture is kept exactly as converted),
 * and NAM_IRS cab IRs "cab-01".. (1 sector each in the 16-bit format), chosen per capture (1.3; before: 12 slots,
 * each with its own IR). 30 x 2 + 12 = 72 of the 73 storage sectors. NAM_SLOT_BYTES: the largest capture. */
#define NAM_SLOTS      30
#define NAM_IRS        12
#define NAM_SLOT_BYTES 4608u
#endif
