/* Chip set-up and clock scenarios: the calls the platform makes, with its real arguments, one per scenario
 * and the whole start-up sequence. Builds against the vendor library or (-DOURS) against
 * platform/nds32/hal/clock.c.
 * The simulator's peripherals are plain memory, so every status bit the code waits for (crystal running,
 * PLL locked, efuse done) is set before the call, for both sides alike; everything else is random. */
#include <stdint.h>
typedef struct { const char *name; void (*fn)(void); } scenario_t;
const uint32_t windows[][2] = {
    { 0x40010000u, 0x40010100u },   /* analogue / pads */
    { 0x40020000u, 0x40020010u },   /* reset mask */
    { 0x40021000u, 0x40021080u },   /* clock control */
    { 0x40023000u, 0x40023020u },   /* efuse */
    { 0x40026000u, 0x40026050u },   /* main PLL */
    { 0x4002E040u, 0x4002E060u },   /* VMID */
    { 0x40030070u, 0x40030080u },   /* memory set-up */
    { 0x40032000u, 0x40032040u },   /* audio PLL */
};
const unsigned n_windows = 8;
#define REG(a) (*(volatile uint32_t *)(a))
#define OUT(v) (REG(0x400FFFF0u) = (uint32_t)(v))

static void ready(void)
{
    REG(0x40021078u) |= 1u;         /* crystal running */
    REG(0x40026034u) |= 1u;         /* PLL locked */
    REG(0x40032034u) |= 1u;         /* APLL locked */
    REG(0x40023010u) |= 2u;         /* efuse read done */
}

#ifdef OURS
#include "hal/clock.h"
#define CHIP_INIT()  gp5_chip_init()
#define CLOCKS()     gp5_clock_init()
#define MEMINIT()    gp5_chip_mem_init()
#define RESETMASK()  gp5_chip_reset_mask()
#define EFUSE(a)     gp5_efuse_read(a)
#else
#include "type.h"
#include "clk.h"
#include "chip_info.h"
#include "efuse.h"
extern void Chip_MemInit(void);
extern void GPIO_RegisterResetMask(void);
static void clocks(void)
{
    Clock_Module1Enable(ALL_MODULE1_CLK_SWITCH);
    Clock_Module2Enable(ALL_MODULE2_CLK_SWITCH);
    Clock_Module3Enable(ALL_MODULE3_CLK_SWITCH);
    Clock_Config(1, 24000000);
    Clock_PllLock(288000);
    Clock_APllLock(240000);
    Clock_SysClkSelect(PLL_CLK_MODE);
    Clock_USBClkDivSet(4);
    Clock_USBClkSelect(APLL_CLK_MODE);
    Clock_UARTClkSelect(APLL_CLK_MODE);
}
#define CHIP_INIT()  Chip_Init(1)
#define CLOCKS()     clocks()
#define MEMINIT()    Chip_MemInit()
#define RESETMASK()  GPIO_RegisterResetMask()
#define EFUSE(a)     Efuse_ReadData(a)
#endif

static void chip_init(void) { ready(); CHIP_INIT(); }
static void clock_all(void) { ready(); CLOCKS(); }
static void meminit(void) { MEMINIT(); }
static void resetmask(void) { RESETMASK(); }
static void efuse_e(void) { ready(); OUT(EFUSE(0x0E)); }
static void efuse_4(void) { ready(); OUT(EFUSE(0x04)); }
static void efuse_big(void) { ready(); OUT(EFUSE(0x3B)); }
/* the whole sequence as the platform runs it */
static void all(void) { ready(); MEMINIT(); CHIP_INIT(); CLOCKS(); }

const scenario_t scenarios[] = {
    { "chip_init", chip_init }, { "clocks", clock_all }, { "meminit", meminit }, { "resetmask", resetmask },
    { "efuse_0e", efuse_e }, { "efuse_04", efuse_4 }, { "efuse_3b", efuse_big }, { "all", all },
};
const unsigned n_scenarios = sizeof scenarios / sizeof scenarios[0];
