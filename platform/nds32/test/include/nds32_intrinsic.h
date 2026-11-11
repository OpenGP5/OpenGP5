#ifndef GP5_TEST_NDS32_INTRINSIC_H
#define GP5_TEST_NDS32_INTRINSIC_H
#include <stdint.h>
enum { NDS32_SR_INT_PEND2, NDS32_SR_INT_MASK2 };
void gp5_test_mtsr(uint32_t value, unsigned reg);
void gp5_test_gie_dis(void);
extern volatile uint32_t gp5_test_timer2_ctrl;
#define TIMER2_CTRL gp5_test_timer2_ctrl
#define __nds32__mtsr(value, reg) gp5_test_mtsr((value), (reg))
#define __nds32__gie_dis() gp5_test_gie_dis()
#endif
