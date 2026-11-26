/* usb_audio_test.c: the control processor's intrinsics, as no-ops on the computer */
#ifndef GP5_TEST_USB_INTRINSIC_H
#define GP5_TEST_USB_INTRINSIC_H
#include <stdint.h>
#define NDS32_SR_PFMC0 0
#define NDS32_SR_PSW 0
extern uint32_t gp5_test_cycles;                 /* the test's clock, as the cycle counter */
static inline uint32_t __nds32__mfsr(int r) { (void)r; return gp5_test_cycles; }
static inline void __nds32__mtsr(uint32_t v, int r) { (void)v; (void)r; }
static inline void __nds32__setgie_dis(void) {}
static inline void __nds32__setgie_en(void) {}
static inline void __nds32__gie_dis(void) {}
static inline void __nds32__dsb(void) {}
static inline void __nds32__isb(void) {}
/* the DSP extension's kmada: r + a.lo * b.lo + a.hi * b.hi (16-bit halves, saturated to 32 bits) */
static inline int32_t __nds32__kmada(int32_t r, uint32_t a, uint32_t b)
{ int64_t v = (int64_t)r + (int16_t)a * (int16_t)b + (int16_t)(a >> 16) * (int16_t)(b >> 16);
  return v > 0x7FFFFFFF ? 0x7FFFFFFF : v < -0x7FFFFFFF - 1 ? -0x7FFFFFFF - 1 : (int32_t)v; }
#endif
