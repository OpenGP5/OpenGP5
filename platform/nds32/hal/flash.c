#include <nds32_intrinsic.h>
#include "flash.h"

/* No EX9 / IFC compaction in this file: an ex9.it instruction executes an entry of a table that lives in
 * flash, which must not be read while a flash command runs (build.sh compiles this file with
 * -fno-toplevel-reorder so these two lines stay around the code; the markers apply to the section they
 * are in, so they are placed in .ramfunc). */
__asm__(".pushsection .ramfunc,\"ax\",@progbits\n\t.no_ex9_begin\n\t.no_ifc_begin\n\t.popsection");

/* Flash controller registers (as the vendor driver uses them; names are ours). */
#define F(off) (*(volatile uint32_t *)(0x40007000u + (off)))
#define F_GO   (*(volatile uint32_t *)0x40030000u)
#define F_DATA    0x00u     /* read data */
#define F_WDATA   0x04u     /* write data (FIFO) */
#define F_CTRL    0x08u     /* bit 3 cleared after every erase / program */
#define F_LEN     0x0Cu     /* bytes - 1 of a data phase, 0 = none */
#define F_OP      0x10u
#define F_ADDR    0x14u
#define F_PHASES  0x18u
#define F_MODE    0x1Cu
#define F_START   0x20u
#define F_RBUSY   0x24u     /* bit 0: read data pending; bit 3: write FIFO full */
#define F_READ    0x28u
#define F_CBUSY   0x2Cu     /* command in progress */
#define F_CRM     0x54u     /* bit 0: continuous-read mode active */

enum { OP_ID, OP_STATUS, OP_READ, OP_ERASE, OP_PROGRAM };
#define RAMFUNC_INLINE static inline __attribute__((always_inline))

RAMFUNC_INLINE void cmd(uint32_t op, uint32_t a, uint32_t b, uint32_t mode, uint32_t len)
{
    F(F_OP) = op;
    F(F_PHASES) = (a << 6) | b;
    F(F_MODE) = mode;
    F(F_LEN) = len;
    F(F_START) = 1;
    F_GO = 0; F_GO = 1; F_GO = 2;
    if (!len) while (F(F_CBUSY) & 1u) {}
}

RAMFUNC_INLINE void exit_crm(void)
{
    if (!(F(F_CRM) & 1u)) return;
    cmd(0xFF, 7, 0, 0x700, 0);
    F(F_WDATA) = 0xFF;
    cmd(0xFF, 7, 0, 0x500, 0);
}

RAMFUNC_INLINE uint32_t read_reg(uint32_t op, uint32_t len)
{
    uint32_t v;
    exit_crm();
    F(F_READ) = 1;
    cmd(op, 7, 0, 0x600, len);
    while (F(F_RBUSY) & 1u) {}
    v = F(F_DATA);
    F_GO = 0;
    F(F_READ) = 0;
    return v;
}

RAMFUNC_INLINE void wait_ready(void)
{
    volatile unsigned d;
    while (F(F_CBUSY)) {}
    do { for (d = 0; d < 2000u; d++) {} } while (read_reg(0x05, 0) & 1u);
}

/* Everything that talks to the flash, in RAM: while a command runs the flash cannot fetch our code. No calls
 * out of this function (they would land in flash). */
static int __attribute__((section(".ramfunc"), noinline)) flash_op(unsigned op, uint32_t addr, const uint32_t *src,
                                                                     unsigned len, uint32_t *out)
{
    uint32_t psw = __nds32__mfsr(NDS32_SR_PSW);
    unsigned i;
    __nds32__setgie_dis();
    __nds32__dsb();
    if (op == OP_ID) *out = read_reg(0x9F, 2) & 0xFFFFFFu;
    else if (op == OP_STATUS) *out = read_reg(0x05, 0) & 0xFFu;
    else if (op == OP_READ) {                              /* fast read through the controller, not the cache */
        exit_crm();
        F(F_ADDR) = addr;
        F(F_READ) = 1;
        cmd(0x0B, 7, 0x1F, 0x380, len - 1);
        for (i = 0; i < len / 4; i++) {
            while (F(F_RBUSY) & 1u) {}
            out[i] = F(F_DATA);
            F_GO = 0;
        }
        F(F_READ) = 0;
    } else if (op == OP_ERASE) {
        exit_crm();
        F(F_ADDR) = addr;
        cmd(0x06, 7, 0, 0x700, 0);                         /* write enable */
        cmd(0x20, 7, 0x17, 0x300, 0);                      /* 4 KB sector erase */
        wait_ready();
    } else {
        exit_crm();
        cmd(0x06, 7, 0, 0x700, 0);                         /* write enable */
        F(F_ADDR) = addr;
        F(F_WDATA) = src[0];
        cmd(0x02, 7, 0x17, 0x340, len - 1);                /* page program, single line */
        for (i = 1; i < len / 4; i++) {
            while (F(F_RBUSY) & 8u) {}
            F(F_WDATA) = src[i];
            F_GO = 0;
        }
        wait_ready();
    }
    if (op >= OP_ERASE) F(F_CTRL) &= ~8u;
    __nds32__dsb();
    if (psw & 1u) __nds32__setgie_en();
    return 0;
}

/* Called through a pointer: a direct call from flash could not reach RAM. */
static int (*volatile flash_op_ptr)(unsigned, uint32_t, const uint32_t *, unsigned, uint32_t *) = flash_op;

static int in_store(uint32_t addr, unsigned len)
{
    return addr >= GP5_FLASH_STORE && len <= GP5_FLASH_STORE_SIZE && addr - GP5_FLASH_STORE <= GP5_FLASH_STORE_SIZE - len;
}

static void forget_cached(uint32_t addr, unsigned len)       /* the data cache may still hold the old bytes */
{
    uint32_t a;
    for (a = addr & ~31u; a < addr + len; a += 32u) __nds32__cctlva_wbinval_one_lvl(NDS32_CCTL_L1D_VA_INVAL, (void *)a);
    __nds32__dsb();
}

uint32_t gp5_flash_id(void) { uint32_t v = 0; flash_op_ptr(OP_ID, 0, 0, 0, &v); return v; }
uint32_t gp5_flash_status(void) { uint32_t v = 0; flash_op_ptr(OP_STATUS, 0, 0, 0, &v); return v; }

int gp5_flash_read_cmd(uint32_t addr, uint32_t *dst, unsigned len)
{
    if (!len || len > 64u || (len & 3u) || addr >= 0x200000u || 0x200000u - addr < len) return -1;
    flash_op_ptr(OP_READ, addr, 0, len, dst);
    return 0;
}

int gp5_flash_erase(uint32_t addr)
{
    if ((addr & (GP5_FLASH_SECTOR - 1u)) || !in_store(addr, GP5_FLASH_SECTOR)) return -1;
    flash_op_ptr(OP_ERASE, addr, 0, 0, 0);
    forget_cached(addr, GP5_FLASH_SECTOR);
    return 0;
}

int gp5_flash_program(uint32_t addr, const uint32_t *src, unsigned len)
{
    if (!len || (len & 3u) || (addr & 3u) || !in_store(addr, len)) return -1;
    while (len) {
        unsigned n = GP5_FLASH_PAGE - (addr & (GP5_FLASH_PAGE - 1u));    /* never across a page */
        if (n > len) n = len;
        flash_op_ptr(OP_PROGRAM, addr, src, n, 0);
        forget_cached(addr, n);
        addr += n; src += n / 4; len -= n;
    }
    return 0;
}

__asm__(".pushsection .ramfunc,\"ax\",@progbits\n\t.no_ifc_end\n\t.no_ex9_end\n\t.popsection");
