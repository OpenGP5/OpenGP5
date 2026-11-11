/* GP-5 open platform: chip set-up and clocks of the BP1048 control processor.
 *
 * One fixed sequence, the one the platform has always run (with the vendor library):
 *   24 MHz crystal, main PLL at 288 MHz (system clock), audio PLL at 240 MHz (USB at 240 / 4 = 60 MHz,
 *   and the UART that carries the DSP link), every module clock on.
 * Register for register the same accesses, widths and order as the vendor's Chip_Init(1),
 * Clock_Module1..3Enable(all), Clock_Config(1, 24000000), Clock_PllLock(288000), Clock_APllLock(240000),
 * Clock_SysClkSelect(PLL), Clock_USBClkDivSet(4), Clock_USBClkSelect(APLL), Clock_UARTClkSelect(APLL),
 * Chip_MemInit() and GPIO_RegisterResetMask(); checked by difference in the simulator
 * (tools/nds32_drvtest/clock_cases.c). Only the branches these arguments take are kept. */
#include <stdint.h>
#include "clock.h"

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R8(a)  (*(volatile uint8_t *)(a))

#define CLK      0x40021000u   /* clock switches, dividers, module clock gates */
#define PLL      0x40026000u   /* main PLL (system clock) */
#define APLL     0x40032000u   /* audio PLL (USB, UART) */
#define EFUSE    0x40023000u
#define ANA      0x40010000u   /* analogue / pad set-up touched by chip init */

#define OSC_HZ   24000000u

#include "tick.h"

uint8_t gp5_efuse_read(unsigned addr)
{
    uint8_t v;

    R32(EFUSE + 0x18) = 1;
    if (addr >= 0x3B) return 0;                     /* (the vendor returns here without closing again) */
    R16(EFUSE + 0x08) |= 0x2000;
    gp5_delay_us(1);
    R16(EFUSE + 0x08) = (uint16_t)((R16(EFUSE + 0x08) & ~0x1FFu) | addr);
    R16(EFUSE + 0x08) |= 0x4000;
    R16(EFUSE + 0x08) &= ~0x4000u;
    gp5_delay_us(1);
    while (!(R32(EFUSE + 0x10) & 2u)) { }
    v = (uint8_t)R32(EFUSE + 0x0C);
    return v;
}

void gp5_chip_init(void)
{
    uint8_t trim;

    R16(0x4002E05Cu) &= ~0x8u;                      /* VMID detection off */
    R32(ANA + 0x94) |= 0x3CFC001Fu;
    R32(ANA + 0x9C) |= 0xCFu;
    R8(ANA + 0xBC) |= 0x01u;
    R8(ANA + 0xC0) |= 0x01u;
    R8(ANA + 0xC4) |= 0x01u;
    R8(ANA + 0xC8) |= 0x40u;
    R8(ANA + 0xCC) |= 0x40u;
    if ((gp5_efuse_read(0x0E) >> 4) & 1u) {         /* factory trim present: copy it in */
        trim = gp5_efuse_read(0x0E) & 0xFu;
        R16(CLK + 0x74) = (uint16_t)((R16(CLK + 0x74) & ~0xF0u) | (trim << 4));
    }
}

static void wait_oscillator(void)
{
    if (R32(CLK + 0x70) & (1u << 21))
        while (!R32(CLK + 0x78)) { }
}

/* Lock one PLL to khz (both run in the 120..360 MHz band, range setting 0x400). */
static void pll_lock(uint32_t base, uint32_t zero_reg, uint32_t khz)
{
    uint32_t ref;

    wait_oscillator();
    R16(base) &= ~0x1u;
    R32(base + zero_reg) = 0;
    R32(base + 0x30) = 1;
    ref = OSC_HZ / (R32(base + 0x04) + 1u);         /* reference after the input divider (31.25 kHz) */
    R32(base + 0x08) = (khz * 1000u / ref) << 8;    /* multiplier, 8 fraction bits */
    R32(base + 0x1C) = 0x40B;
    R16(base) |= 0x6u;
    R32(base + 0x0C) = 0x1C0;
    R16(base) = (uint16_t)((R16(base) & ~0xC00u) | 0x400u);
    R16(base) |= 0x1u;
    while (!(R32(base + 0x34) & 1u)) { }            /* locked */
}

void gp5_clock_init(void)
{
    R32(CLK + 0x30) |= 0xFFFFFFFFu;                 /* module clock gates: all on */
    R32(CLK + 0x34) |= 0x1Fu;
    R32(CLK + 0x38) |= 0x7Fu;

    R32(CLK + 0x70) |= 1u << 20;                    /* 24 MHz crystal */
    R32(CLK + 0x70) |= 1u << 21;
    R32(APLL + 0x04) = OSC_HZ / 31250u - 1u;        /* both PLL input dividers: 31.25 kHz reference */
    R32(PLL + 0x04) = OSC_HZ / 31250u - 1u;
    R32(CLK + 0x20) = (uint16_t)(OSC_HZ / 32000u - 1u);

    pll_lock(PLL, 0x48, 288000);                    /* main PLL 288 MHz */
    pll_lock(APLL, 0x3C, 240000);                   /* audio PLL 240 MHz */

    if (R32(PLL + 0x34) & 1u) {                     /* system clock from the main PLL */
        R32(CLK + 0x24) &= ~0x2u;
        R32(CLK + 0x24) |= 0x1u;
    }
    R32(CLK + 0x04) = 4u - 1u;                      /* USB clock = APLL / 4 */
    R32(CLK + 0x24) |= 0x80u;                       /* USB clock from the APLL */
    R32(CLK + 0x24) |= 0x80u;                       /* UART clock from the APLL */
    R32(CLK + 0x24) |= 0x100u;
}

void gp5_chip_mem_init(void)
{
    R32(0x4003007Cu) = 0;
}

void gp5_chip_reset_mask(void)
{
    R32(0x40020000u) = 1;
}
