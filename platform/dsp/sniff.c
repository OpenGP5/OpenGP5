/*
 * GP-5 DSP link sniffer — from-scratch DSP firmware that records what the NDS32 sends at boot.
 *
 * Link (reversed from the DSP resident + stock region c): UART0 @0x40005000, 460800 8N1.
 * The resident's pre-boot init (clocks 0x638/0x6a0, GPIO 0x6d0) runs before it jumps here, so
 * UART0 is already clocked and pinned. We program it exactly like the resident's update mode
 * (0x5ca -> 0x762 -> 0xb00), with UART clock 96 MHz, and poll it. No NVIC line is enabled, and
 * PRIMASK stays set, so no interrupts.
 *
 * UART regs: +0x0 data, +0x4 frame cfg (0x703), +0x8 int-enable (0x202e), +0xC status
 * (bits 4..8 = RX FIFO count, 11..15 = TX FIFO count), +0x10 baud divisor, +0x14 FIFO cfg (0x5000).
 *
 * RAM (survives the NDS32's DSP reset, not power loss; read over J7):
 *   0x20007F00 status: [0] magic "GP5!" [1] heartbeat [2] boots [3] fault [4] log count
 *                      [5] rx overflow flag [6] last UART status [7] divisor written
 *   0x20007000 log: uint16 entries (boot << 8 | byte), 1920 entries max, appended across boots.
 */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))
#define STATUS ((volatile uint32_t *)0x20007F00u)
#define LOG ((volatile uint16_t *)0x20007000u)
#define LOG_MAX 1920u
#define MAGIC 0x21355047u
#define UART0 0x40005000u
#define UART_CLK 96000000u
#define BAUD 460800u

void sniff_main(void) __attribute__((noreturn, used));
void default_handler(void) __attribute__((noreturn));

__attribute__((naked, noreturn)) void reset_handler(void) {
    __asm volatile("ldr r0, =0x20008000\n"
                   "msr msp, r0\n"
                   "b sniff_main\n");
}

void sniff_main(void) {
    REG(0xE000ED08u) = 0x4400u; /* VTOR */
    uint32_t boots;
    if (STATUS[0] == MAGIC) {
        boots = STATUS[2] + 1u;
    } else {
        boots = 1u;
        STATUS[4] = 0; /* fresh power-on: empty log */
        STATUS[5] = 0;
    }
    STATUS[1] = 0;
    STATUS[2] = boots;
    STATUS[3] = 0;
    STATUS[0] = MAGIC;

    uint32_t div = (UART_CLK + BAUD * 8u) / (BAUD * 16u) - 1u;
    REG(UART0 + 0x10) = div;
    REG(UART0 + 0x08) = 0x202Eu;
    REG(UART0 + 0x04) = 0x703u;
    REG(UART0 + 0x14) = 0x5000u;
    STATUS[7] = div;

    for (;;) {
        STATUS[1]++;
        uint32_t st = REG(UART0 + 0x0C);
        STATUS[6] = st;
        uint32_t n = (st >> 4) & 0x1Fu;
        while (n--) {
            uint32_t b = REG(UART0 + 0x00) & 0xFFu;
            uint32_t i = STATUS[4];
            if (i < LOG_MAX) {
                LOG[i] = (uint16_t)(((boots & 0xFFu) << 8) | b);
                STATUS[4] = i + 1u;
            } else {
                STATUS[5] = 1u;
            }
        }
    }
}

void default_handler(void) {
    STATUS[3] = 0xDEAD0000u | (REG(0xE000ED04u) & 0x1FFu);
    for (;;) {}
}

__attribute__((section(".vectors"), used))
void (*const vectors[16 + 48])(void) = {
    (void (*)(void))0x20008000u,
    reset_handler,
    [2 ... 16 + 47] = default_handler,
};
