/* The capture bank and switching (a2.c), for the controls and the screen (firmware.c, ui.c). Main loop only. */
#ifndef NAM_H
#define NAM_H
#include "../split.h"
extern volatile uint32_t nam_bypass, nam_blocks, nam_lost, nam_dsp_cycles, nam_cycles, nam_worst;
void nam_start(int slot);              /* start on this slot (or the first full one, or the built-in capture) */
int nam_poll(void);                    /* every 10 ms; returns the slot a new upload landed in (now playing), else -2 */
void nam_select(int slot);             /* switch to a slot (-1: the built-in capture) */
int nam_slot_ok(int i);                /* slot i holds a good capture */
const char *nam_slot_name(int i);
int nam_current(void);                 /* the slot playing, -1 = built-in */
uint32_t nam_current_crc(void);        /* its checksum: a tone's gate / EQ settings belong to it */
const char *nam_name(void);            /* the capture playing */
int nam_busy(void);                    /* switching */
unsigned nam_progress(void);           /* 0..255 through a switch */
unsigned nam_take_peak(void);          /* the largest input sample since the last call, 0..2^31 */
int nam_ir_ok(int i);                  /* IR i (0..NAM_IRS-1, file "cab-NN") is there */
const char *nam_ir_name(int i);        /* its name ("IR NN" when it has none) */
void nam_ir_select(int i);             /* the IR to play (-1: none); main loop */
int nam_ir_current(void);              /* the IR chosen (-1: none) */
void nam_cab(int on);                  /* the cab IR on / off (live setting 0) */
int nam_cab_is_on(void);
void nam_cab_limit(unsigned taps);     /* at most this many taps of the IR file, 0 = all (live setting 1) */
unsigned nam_cab_get_limit(void);
unsigned nam_cab_len(void);            /* taps in use, 0 = no IR file */
void nam_tone(uint8_t gate, uint8_t bass, uint8_t mid, uint8_t treble);   /* the gate and EQ (../tone.h), main loop */
#define NAM_GATE_MAX 61u                /* gate: 0 off, 1..61 = -90..-30 dBFS (tone.h) */
#define NAM_EQ_MAX 24u                  /* bass / mid / treble: 0..24 = -12..+12 dB */
#define NAM_EQ_FLAT 12u
#endif
