/*
 * GP-5 DSP "hello" — the first from-scratch DSP firmware (no Valeton code).
 *
 * Boot contract (from the DSP resident boot @0x254, DSP flash 0x0-0x4000, never touched):
 *   - region-c header at DSP flash 0x4000: [5a5a5a5a (written by resident COMMIT), ffffffff,
 *     "V106", vector-table address]; resident checks only the first two words.
 *   - resident does `cpsid i` and calls vectors[1] of that table. It does NOT set MSP or VTOR,
 *     so the reset handler does both. If reset returns, the resident issues SYSRESETREQ.
 *
 * What this does: sets its own stack + VTOR, then writes a RAM status block at 0x20007F00 that
 * survives a DSP reset (not power loss) and is readable over J7 SWD:
 *   [0] magic 0x21355047 "GP5!"   [1] heartbeat (increments forever)
 *   [2] boot count (since power-on) [3] fault code (0 = none; 0xDEAD00xx = exception xx)
 * Interrupts stay disabled (PRIMASK as the resident left it). SWD is never disabled.
 */
#include <stdint.h>

#define STATUS ((volatile uint32_t *)0x20007F00u)
#define MAGIC 0x21355047u
#define SCB_VTOR (*(volatile uint32_t *)0xE000ED08u)
#define SCB_ICSR (*(volatile uint32_t *)0xE000ED04u)

void hello_main(void) __attribute__((noreturn, used));
void default_handler(void) __attribute__((noreturn));

__attribute__((naked, noreturn)) void reset_handler(void) {
    __asm volatile("ldr r0, =0x20008000\n"
                   "msr msp, r0\n"
                   "b hello_main\n");
}

void hello_main(void) {
    SCB_VTOR = 0x4400u;
    uint32_t boots = (STATUS[0] == MAGIC) ? STATUS[2] + 1u : 1u;
    STATUS[3] = 0;
    STATUS[1] = 0;
    STATUS[2] = boots;
    STATUS[0] = MAGIC;
    for (;;) STATUS[1]++;
}

void default_handler(void) {
    STATUS[3] = 0xDEAD0000u | (SCB_ICSR & 0x1FFu);
    for (;;) {}
}

/* Cortex-M4 vector table: SP, reset, then 14 system + 48 IRQ slots -> default_handler. */
__attribute__((section(".vectors"), used))
void (*const vectors[16 + 48])(void) = {
    (void (*)(void))0x20008000u,
    reset_handler,
    [2 ... 16 + 47] = default_handler,
};
