/* Watchdog, SPI master and ADC scenarios: every call of these the platform makes, with its real
 * arguments. Builds against the vendor library or (-DOURS) against platform/nds32/hal.
 * Status bits that hardware sets by itself are put in place by the scenario before the call (and so
 * show in both logs): SPI "room for a byte" set and "busy" clear, ADC "done" set. */
#include <stdint.h>
typedef struct { const char *name; void (*fn)(void); } scenario_t;
const uint32_t windows[][2] = { { 0x40003000u, 0x40003020u }, { 0x40010000u, 0x40010200u }, { 0x40020000u, 0x40020020u },
                                { 0x40022000u, 0x40022008u }, { 0x40028000u, 0x40028020u } };
const unsigned n_windows = 5;
#define REG(a) (*(volatile uint32_t *)(a))
static uint32_t sink;

#ifdef OURS
#include "hal/wdg.h"
#include "hal/spim.h"
#include "hal/adc.h"
#define WDG_OFF()        gp5_wdg_disable()
#define WDG_ON()         gp5_wdg_enable_1s()
#define SPI_PINS()       gp5_spim_pins_port0()
#define SPI_CLRINT()     gp5_spim_clear_int()
#define SPI_INIT()       gp5_spim_init()
#define SPI_SEND(b, n)   gp5_spim_send(b, n)
#define ADC_INIT()       gp5_adc_init()
#define ADC_READ()       gp5_adc_read(9)
#else
#include "type.h"
#include "watchdog.h"
#include "spim.h"
#include "adc.h"
#define WDG_OFF()        WDG_Disable()
#define WDG_ON()         WDG_Enable(WDG_STEP_1S)
#define SPI_PINS()       SPIM_IoConfig(SPIM_PORT0_A5_A6_A7)
#define SPI_CLRINT()     SPIM_ClrIntFlag()
#define SPI_INIT()       SPIM_Init(0, SPIM_CLK_DIV_24M)
#define SPI_SEND(b, n)   SPIM_Send(b, n)
#define ADC_INIT()       do { ADC_Enable(); ADC_ClockDivSet(CLK_DIV_128); ADC_VrefSet(ADC_VREF_VDDA); ADC_ModeSet(0); ADC_Calibration(); } while (0)
#define ADC_READ()       ADC_SingleModeDataGet(ADC_CHANNEL_GPIOA28)
#endif

static void spi_ready(void) { REG(0x40003014u) = (REG(0x40003014u) | 0x4000u) & ~0x10u; }
static void adc_done(void) { REG(0x4002800Cu) |= 1u; }

static void wdg_off(void) { WDG_OFF(); }
static void wdg_on(void) { WDG_ON(); }
static void spi_pins(void) { SPI_PINS(); }
static void spi_clrint(void) { SPI_CLRINT(); }
static void spi_init(void) { SPI_INIT(); }
static void spi_send1(void) { static uint8_t b[1] = { 0x2C }; spi_ready(); SPI_SEND(b, 1); }
static void spi_send320(void)
{
    static uint8_t b[320];
    unsigned i;
    for (i = 0; i < sizeof b; i++) b[i] = (uint8_t)(i * 7 + 1);
    spi_ready(); SPI_SEND(b, sizeof b);
}
static void adc_init(void) { adc_done(); ADC_INIT(); }
/* the value read back is part of the behaviour: store it where the log shows it (an unused GPIO word) */
static void adc_read(void) { adc_done(); sink = (uint32_t)ADC_READ(); REG(0x400101F0u) = sink; }

const scenario_t scenarios[] = {
    { "wdg_off", wdg_off }, { "wdg_on", wdg_on }, { "spi_pins", spi_pins }, { "spi_clrint", spi_clrint }, { "spi_init", spi_init },
    { "spi_send1", spi_send1 }, { "spi_send320", spi_send320 }, { "adc_init", adc_init }, { "adc_read", adc_read },
};
const unsigned n_scenarios = sizeof scenarios / sizeof scenarios[0];
