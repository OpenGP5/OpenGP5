/* See irq_entry_test.S. Prints PASS / FAIL lines; exit code 0 only if everything held. */
#include <stdio.h>
#include <stdint.h>

extern void irq_entry_run(void);
extern void irq_clobber(uint32_t *psw, uint32_t *sp);

uint32_t irq_saved_sp, irq_after[34], irq_ipc_seen;
extern char irq_resume_addr_marker[];
double irq_fp_in[16], irq_fp_out[16], irq_fp_junk = -12345.678;
volatile uint32_t gp5_crash[4];
static unsigned calls, seen_line, in_psw, in_sp;

static void handler(unsigned line) { calls++; seen_line = line; irq_clobber((uint32_t *)&in_psw, (uint32_t *)&in_sp); }
static void other(unsigned line) { (void)line; calls += 100; }

typedef void (*fn)(unsigned);
const fn gp5_irq_table[32] = { [0 ... 31] = other, [9] = handler };

void gp5_startup(void) {}

int main(void)
{
    unsigned i, bad = 0;
    uint32_t sp_before;

    for (i = 0; i < 16; i++) irq_fp_in[i] = 1000.5 + i;
    irq_entry_run();
    sp_before = irq_saved_sp;

    if (calls != 1 || seen_line != 9) { printf("FAIL handler calls %u line %u\n", calls, seen_line); bad++; }
    if ((in_psw & 7) != 1) { printf("FAIL PSW inside the handler %08x (want interrupts on, level 0)\n", (unsigned)in_psw); bad++; }
    if (!(in_psw & 0x2000)) { printf("FAIL PSW bit 13 not taken over inside the handler\n"); bad++; }
    if (in_sp & 7) { printf("FAIL handler stack not 8-byte aligned: %08x\n", (unsigned)in_sp); bad++; }
    for (i = 0; i < 28; i++)
        if (irq_after[i] != 0x11110000u + i) { printf("FAIL $r%u = %08x after return\n", i, (unsigned)irq_after[i]); bad++; }
    if (irq_after[28] != sp_before) { printf("FAIL $sp %08x, was %08x\n", (unsigned)irq_after[28], (unsigned)sp_before); bad++; }
    if (irq_after[29] != 0x22220001u || irq_after[30] != 0x22220003u || irq_after[31] != 0x22220007u || irq_after[32] != 0x2222000Fu) {
        printf("FAIL loop / call registers %08x %08x %08x %08x\n", (unsigned)irq_after[29], (unsigned)irq_after[30], (unsigned)irq_after[31], (unsigned)irq_after[32]); bad++; }
    if ((irq_after[33] & 0x2007) != 0x2001) { printf("FAIL PSW after return %08x\n", (unsigned)irq_after[33]); bad++; }
    if (irq_ipc_seen != (uint32_t)irq_resume_addr_marker) { printf("FAIL return address %08x\n", (unsigned)irq_ipc_seen); bad++; }
    for (i = 0; i < 16; i++)
        if (irq_fp_out[i] != irq_fp_in[i]) { printf("FAIL $fd%u changed\n", i); bad++; }
    printf(bad ? "%u FAILED\n" : "PASS: interrupt entry/exit keeps every register, aligns the stack, re-enables interrupts for the handler\n", bad);
    return bad ? 1 : 0;
}
