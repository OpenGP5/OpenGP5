/* GPIO sweep: every GPIO driver call the platform makes, ONE call per scenario, to get each call's exact effect.
 * Generated; vendor-only (used to derive platform/nds32/hal/gpio.c, see tools/nds32_drvtest/derive_gpio.py). */
#include <stdint.h>
#include "type.h"
#include "gpio.h"

typedef struct { const char *name; void (*fn)(void); } scenario_t;
const uint32_t windows[][2] = { { 0x40010000u, 0x40010200u }, { 0x40020000u, 0x40024000u } };
const unsigned n_windows = 2;

static void a7_mode0(void) { GPIO_PortAModeSet((GPIO_PortA)GPIO_INDEX7, 0); }
static void a7_ds2(void) { GPIO_PortAOutDsSet(GPIO_INDEX7, 2); }
static void a7_pd0(void) { GPIO_PortAPulldownSet((GPIO_PortA)GPIO_INDEX7, 0); }
static void a9_mode0(void) { GPIO_PortAModeSet((GPIO_PortA)GPIO_INDEX9, 0); }
static void a9_ds2(void) { GPIO_PortAOutDsSet(GPIO_INDEX9, 2); }
static void a9_pd0(void) { GPIO_PortAPulldownSet((GPIO_PortA)GPIO_INDEX9, 0); }
static void a15_mode0(void) { GPIO_PortAModeSet((GPIO_PortA)GPIO_INDEX15, 0); }
static void a15_ds2(void) { GPIO_PortAOutDsSet(GPIO_INDEX15, 2); }
static void a15_pd0(void) { GPIO_PortAPulldownSet((GPIO_PortA)GPIO_INDEX15, 0); }
static void a16_mode0(void) { GPIO_PortAModeSet((GPIO_PortA)GPIO_INDEX16, 0); }
static void a16_ds2(void) { GPIO_PortAOutDsSet(GPIO_INDEX16, 2); }
static void a16_pd0(void) { GPIO_PortAPulldownSet((GPIO_PortA)GPIO_INDEX16, 0); }
static void a22_mode0(void) { GPIO_PortAModeSet((GPIO_PortA)GPIO_INDEX22, 0); }
static void a22_ds2(void) { GPIO_PortAOutDsSet(GPIO_INDEX22, 2); }
static void a22_pd0(void) { GPIO_PortAPulldownSet((GPIO_PortA)GPIO_INDEX22, 0); }
static void a24_mode0(void) { GPIO_PortAModeSet((GPIO_PortA)GPIO_INDEX24, 0); }
static void a24_ds2(void) { GPIO_PortAOutDsSet(GPIO_INDEX24, 2); }
static void a24_pd0(void) { GPIO_PortAPulldownSet((GPIO_PortA)GPIO_INDEX24, 0); }
static void a29_mode0(void) { GPIO_PortAModeSet((GPIO_PortA)GPIO_INDEX29, 0); }
static void a29_ds2(void) { GPIO_PortAOutDsSet(GPIO_INDEX29, 2); }
static void a29_pd0(void) { GPIO_PortAPulldownSet((GPIO_PortA)GPIO_INDEX29, 0); }
static void a30_mode0(void) { GPIO_PortAModeSet((GPIO_PortA)GPIO_INDEX30, 0); }
static void a30_ds2(void) { GPIO_PortAOutDsSet(GPIO_INDEX30, 2); }
static void a30_pd0(void) { GPIO_PortAPulldownSet((GPIO_PortA)GPIO_INDEX30, 0); }
static void a31_mode0(void) { GPIO_PortAModeSet((GPIO_PortA)GPIO_INDEX31, 0); }
static void a31_ds2(void) { GPIO_PortAOutDsSet(GPIO_INDEX31, 2); }
static void a31_pd0(void) { GPIO_PortAPulldownSet((GPIO_PortA)GPIO_INDEX31, 0); }
static void b5_mode0(void) { GPIO_PortBModeSet(GPIOB5, 0); }
static void b5_ds2(void) { GPIO_PortBOutDsSet(GPIO_INDEX5, 2); }
static void b5_pd0(void) { GPIO_PortBPulldownSet(GPIOB5, 0); }
static void a0_mode2(void) { GPIO_PortAModeSet(GPIOA0, 2); }
static void a1_modec(void) { GPIO_PortAModeSet(GPIOA1, 0xc); }
static void reg_A_IN(void) { GPIO_RegSet(GPIO_A_IN, GPIO_RegGet(GPIO_A_IN) ^ 0xA5C3F00Fu); }
static void reg_A_OUT(void) { GPIO_RegSet(GPIO_A_OUT, GPIO_RegGet(GPIO_A_OUT) ^ 0xA5C3F00Fu); }
static void reg_A_SET(void) { GPIO_RegSet(GPIO_A_SET, GPIO_RegGet(GPIO_A_SET) ^ 0xA5C3F00Fu); }
static void reg_A_CLR(void) { GPIO_RegSet(GPIO_A_CLR, GPIO_RegGet(GPIO_A_CLR) ^ 0xA5C3F00Fu); }
static void reg_A_IE(void) { GPIO_RegSet(GPIO_A_IE, GPIO_RegGet(GPIO_A_IE) ^ 0xA5C3F00Fu); }
static void reg_A_OE(void) { GPIO_RegSet(GPIO_A_OE, GPIO_RegGet(GPIO_A_OE) ^ 0xA5C3F00Fu); }
static void reg_A_PU(void) { GPIO_RegSet(GPIO_A_PU, GPIO_RegGet(GPIO_A_PU) ^ 0xA5C3F00Fu); }
static void reg_A_PD(void) { GPIO_RegSet(GPIO_A_PD, GPIO_RegGet(GPIO_A_PD) ^ 0xA5C3F00Fu); }
static void reg_A_ANA_EN(void) { GPIO_RegSet(GPIO_A_ANA_EN, GPIO_RegGet(GPIO_A_ANA_EN) ^ 0xA5C3F00Fu); }
static void reg_B_IN(void) { GPIO_RegSet(GPIO_B_IN, GPIO_RegGet(GPIO_B_IN) ^ 0xA5C3F00Fu); }
static void reg_B_OUT(void) { GPIO_RegSet(GPIO_B_OUT, GPIO_RegGet(GPIO_B_OUT) ^ 0xA5C3F00Fu); }
static void reg_B_IE(void) { GPIO_RegSet(GPIO_B_IE, GPIO_RegGet(GPIO_B_IE) ^ 0xA5C3F00Fu); }
static void reg_B_OE(void) { GPIO_RegSet(GPIO_B_OE, GPIO_RegGet(GPIO_B_OE) ^ 0xA5C3F00Fu); }
static void reg_B_PU(void) { GPIO_RegSet(GPIO_B_PU, GPIO_RegGet(GPIO_B_PU) ^ 0xA5C3F00Fu); }
static void reg_B_PD(void) { GPIO_RegSet(GPIO_B_PD, GPIO_RegGet(GPIO_B_PD) ^ 0xA5C3F00Fu); }
static void bitsset_A_PU(void) { GPIO_RegBitsSet(GPIO_A_PU, 0x00F0000Fu); }
static void bitsclr_A_PU(void) { GPIO_RegBitsClear(GPIO_A_PU, 0x00F0000Fu); }
extern void GPIO_RegisterResetMask(void);
static void reset_mask(void) { GPIO_RegisterResetMask(); }

const scenario_t scenarios[] = {
    { "a7_mode0", a7_mode0 }, { "a7_ds2", a7_ds2 }, { "a7_pd0", a7_pd0 }, { "a9_mode0", a9_mode0 }, { "a9_ds2", a9_ds2 }, { "a9_pd0", a9_pd0 }, { "a15_mode0", a15_mode0 }, { "a15_ds2", a15_ds2 }, { "a15_pd0", a15_pd0 }, { "a16_mode0", a16_mode0 }, { "a16_ds2", a16_ds2 }, { "a16_pd0", a16_pd0 }, { "a22_mode0", a22_mode0 }, { "a22_ds2", a22_ds2 }, { "a22_pd0", a22_pd0 }, { "a24_mode0", a24_mode0 }, { "a24_ds2", a24_ds2 }, { "a24_pd0", a24_pd0 }, { "a29_mode0", a29_mode0 }, { "a29_ds2", a29_ds2 }, { "a29_pd0", a29_pd0 }, { "a30_mode0", a30_mode0 }, { "a30_ds2", a30_ds2 }, { "a30_pd0", a30_pd0 }, { "a31_mode0", a31_mode0 }, { "a31_ds2", a31_ds2 }, { "a31_pd0", a31_pd0 }, { "b5_mode0", b5_mode0 }, { "b5_ds2", b5_ds2 }, { "b5_pd0", b5_pd0 }, { "a0_mode2", a0_mode2 }, { "a1_modec", a1_modec }, { "reg_A_IN", reg_A_IN }, { "reg_A_OUT", reg_A_OUT }, { "reg_A_SET", reg_A_SET }, { "reg_A_CLR", reg_A_CLR }, { "reg_A_IE", reg_A_IE }, { "reg_A_OE", reg_A_OE }, { "reg_A_PU", reg_A_PU }, { "reg_A_PD", reg_A_PD }, { "reg_A_ANA_EN", reg_A_ANA_EN }, { "reg_B_IN", reg_B_IN }, { "reg_B_OUT", reg_B_OUT }, { "reg_B_IE", reg_B_IE }, { "reg_B_OE", reg_B_OE }, { "reg_B_PU", reg_B_PU }, { "reg_B_PD", reg_B_PD }, { "bitsset_A_PU", bitsset_A_PU }, { "bitsclr_A_PU", bitsclr_A_PU }, { "reset_mask", reset_mask },
};
const unsigned n_scenarios = sizeof scenarios / sizeof scenarios[0];
