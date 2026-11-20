/* The control processor's special registers and barriers, for the simulator (tools/simulate.py): reading a register
 * gives 0 (cycle counters read 0: the simulator doesn't count cycles), writing one and barriers do nothing.
 * The DSP-extension multiplies (kmmac and friends) are not here: code that uses them keeps a portable version for
 * when __NDS32_EXT_DSP__ is not defined, as NAM Loader's kernel and cab convolver do. */
#ifndef GP5_SIM_NDS32_INTRINSIC_H
#define GP5_SIM_NDS32_INTRINSIC_H
#include <stdint.h>
enum { NDS32_SR_PSW, NDS32_SR_PFMC0, NDS32_SR_PFMC1, NDS32_SR_PFMC2, NDS32_SR_PFM_CTL, NDS32_SR_INT_MASK2, NDS32_SR_INT_PEND2 };
static inline uint32_t __nds32__mfsr(int r) { (void)r; return 0; }
static inline void __nds32__mtsr(uint32_t v, int r) { (void)v; (void)r; }
static inline void __nds32__isb(void) {}
static inline void __nds32__dsb(void) {}
static inline void __nds32__setgie_dis(void) {}
static inline void __nds32__setgie_en(void) {}
static inline void __nds32__gie_dis(void) {}
static inline void __nds32__gie_en(void) {}
#endif
