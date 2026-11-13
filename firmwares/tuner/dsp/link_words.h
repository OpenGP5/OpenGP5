/* What the tuner's two halves exchange over the audio link (word numbers in GP5_LINK_WORDS). Shared by the DSP
 * (dsp/firmware.h) and the control processor (nds32/firmware.c). */
#ifndef LINK_WORDS_H
#define LINK_WORDS_H
#define LW_MAGIC    0
#define LW_MAGIC_D  0x54554E31u    /* "TUN1": DSP -> control processor */
#define LW_MAGIC_N  0x43544C31u    /* "CTL1": control processor -> DSP */
/* DSP -> control processor */
#define LW_NOTE     1              /* MIDI note, -1 = none */
#define LW_CENTS    2              /* cents x 10 */
#define LW_PHASE    3              /* the fundamental's phase against the note, 0..65535 = one turn */
#define LW_AMP      6              /* how strong the fundamental is, 0..65536 */
#define LW_LEVEL    9              /* input level, dBFS x 10 */
#define LW_FREQ     10             /* detected frequency, Hz x 100 (0 = none) */
#define LW_SEG      16             /* 48 words: the strobe, 3 bands x 64 segments, one byte each (0 dark .. 255
                                      lit), band 0 first, four segments per word, lowest byte first */
#define LW_SEG_N    64
/* control processor -> DSP */
#define LW_A4       1              /* reference A4, Hz x 10 */
#define LW_MUTE     2              /* 1 = outputs silent (tuning quietly) */
#endif
