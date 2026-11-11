/*
 * Resident tick-literal patch (-D RESIDENT_TICK_PATCH). See "Known limits" in GETTING-STARTED.md.
 *
 * Why: in update mode the DSP resident (DSP flash 0x0..0x3FFF) resets the flash chip and then waits
 * 12 ticks. On the test unit, flash reads in that window return byte 0 of each 32-bit word with
 * bit 7 cleared. The SysTick handler is the only handler that runs from flash (0xb54); its literal
 * 0x20000dc0 reads as 0x20000d40, the tick counter never moves, the wait never ends, and the update
 * hangs until the counter is nudged over J7.
 *
 * Fix: move the tick counter to an address whose byte 0 has no bit 7: 0x20000000 (a dead `b .` stub
 * pair in the resident's RAM image). Both literals that hold the address change:
 *     flash 0x0b60 (SysTick handler)        c0 0d 00 20 -> 00 00 00 20
 *     flash 0x16dc (get-tick / delay, RAM)  c0 0d 00 20 -> 00 00 00 20
 * Only bits are cleared, so no erase is needed: two 2-byte page programs. SysTick runs only in the
 * resident's update path, so normal boot does not use these words at all.
 *
 * How: the resident's own page-program routine is used, in RAM, exactly as the resident runs it
 * (scatter-load copy flash 0xe50 -> 0x20000000, 0x1be4 -> 0x20000d94; routine at 0x20000184, argument
 * {addr, len, buf}). A small RAM stub makes the two calls and checks that memory-mapped flash reads
 * are back before it returns to this (XIP) firmware.
 *
 * Link commands (payload [cmd u16][len u16][fields]):
 *     0x7F01                       status, read-only
 *     0x7F02 "TICK" "FIX!"         apply (8 magic bytes), then status
 * Reply: [cmd][len=36] u32 state, word@0xb60, word@0x16dc, resident hash, SFI+0x30 before,
 *        program result 1, program result 2, stage | restore flag << 8 | SFI+0x10 before << 16 | SFI+0x10 now << 24.
 * state: 0 not patched (can be applied)  1 patched  2 partly patched (apply again)
 *        0x10 resident is not the known one (hash)  0x11 literals hold something else
 *        0x20 a program call returned an error  0x21 words wrong after programming
 *        0x22 other resident bytes changed (hash)  0x30 bad magic
 * ST[27] = last state, ST[28] = apply runs.
 */
#define TP_LIT_A   0x0b60u
#define TP_LIT_B   0x16dcu
#define TP_OLD     0x20000dc0u
#define TP_NEW     0x20000000u
#define TP_RES_END 0x1c14u            /* end of the resident's load image (0x1be4 + 0x30) */
#define TP_HASH    0xb0ef62ecu        /* FNV-1a of the known resident 0..TP_RES_END without the 4 patch bytes */
#define TP_PROG    0x20000185u        /* resident page program, RAM copy, Thumb */
#define TP_CTX     ((volatile uint32_t *)0x20006E00u)
#define TP_ZERO    0x20006E40u        /* two zero bytes to program */
#define TP_STUB    0x20006E80u
#define SFI        0x40050000u

/* ctx: [0] program fn  [1..3] {addr,len,buf} A  [4..6] {addr,len,buf} B
 *      [7] probe addr 1 [8] expected  [9] probe addr 2 [10] expected  [11] SFI+0x30 before
 *      [12] result A [13] result B [14] stage (1 A, 2 B, 3 probe, 4 done, 0xEE flash unreadable)
 *      [15] 1 = had to put SFI+0x30 back */
__asm(
    ".section .text.tp_stub,\"ax\",%progbits\n"
    ".syntax unified\n"
    ".thumb\n"
    ".balign 4\n"
    ".global tp_stub_start\n"
    ".global tp_stub_end\n"
    "tp_stub_start:\n"
    "    push {r4, r5, r6, lr}\n"
    "    mov r4, r0\n"
    /* as the resident's flash init does before its first command (RAM 0x200002fa): SFI+0x34 = 0x400,
     * SFI+0x10 = 0. In normal mode SFI+0x10 reads 0x81 (measured 2026-11-10); the program routine has
     * only ever run with 0. It stays 0 afterwards, as in the resident's update mode. */
    "    movw r5, #0x0000\n"
    "    movt r5, #0x4005\n"
    "    mov.w r0, #0x400\n"
    "    str r0, [r5, #0x34]\n"
    "    movs r0, #0\n"
    "    str r0, [r5, #0x10]\n"
    "    movs r0, #1\n"
    "    str r0, [r4, #56]\n"
    "    adds r0, r4, #4\n"
    "    ldr r3, [r4, #0]\n"
    "    blx r3\n"
    "    str r0, [r4, #48]\n"
    "    cbnz r0, 1f\n"
    "    movs r0, #2\n"
    "    str r0, [r4, #56]\n"
    "    add.w r0, r4, #16\n"
    "    ldr r3, [r4, #0]\n"
    "    blx r3\n"
    "    str r0, [r4, #52]\n"
    "1:  movs r0, #3\n"
    "    str r0, [r4, #56]\n"
    "    movs r6, #0\n"
    "2:  ldr r1, [r4, #28]\n"
    "    ldr r1, [r1]\n"
    "    ldr r2, [r4, #32]\n"
    "    cmp r1, r2\n"
    "    bne 3f\n"
    "    ldr r1, [r4, #36]\n"
    "    ldr r1, [r1]\n"
    "    ldr r2, [r4, #40]\n"
    "    cmp r1, r2\n"
    "    beq 5f\n"
    "3:  cbnz r6, 4f\n"
    "    movs r6, #1\n"
    "    str r6, [r4, #60]\n"
    "    movw r5, #0x0000\n"
    "    movt r5, #0x4005\n"
    "    mov.w r0, #0x400\n"
    "    str r0, [r5, #0x34]\n"
    "    ldr r0, [r4, #44]\n"
    "    str r0, [r5, #0x30]\n"
    "    b 2b\n"
    "4:  movs r0, #0xEE\n"
    "    str r0, [r4, #56]\n"
    "    b .\n"                       /* flash unreadable: stay in RAM, state is readable over J7 */
    "5:  movs r0, #4\n"
    "    str r0, [r4, #56]\n"
    "    pop {r4, r5, r6, pc}\n"
    "    .balign 4\n"
    "tp_stub_end:\n"
    ".text\n"
);
extern const uint8_t tp_stub_start[], tp_stub_end[];

/* Resident flash starts at address 0: keep the compiler from reasoning about null pointers. */
static uint32_t tp_addr(uint32_t a) { __asm volatile("" : "+r"(a)); return a; }
static uint32_t tp_word(uint32_t a) { return *(volatile const uint32_t *)tp_addr(a); }

static uint32_t tp_hash(void) {
    uint32_t h = 0x811C9DC5u;
    const volatile uint8_t *f = (const volatile uint8_t *)tp_addr(0);
    for (uint32_t i = 0; i < TP_RES_END; i++) {
        if (i == TP_LIT_A || i == TP_LIT_A + 1 || i == TP_LIT_B || i == TP_LIT_B + 1) continue;
        h = (h ^ f[i]) * 0x01000193u;
    }
    return h;
}

/* A word that can still be turned into TP_NEW by clearing bits (covers a half-done earlier run). */
static int tp_clearable(uint32_t w) { return (w | (TP_OLD ^ TP_NEW)) == TP_OLD; }

static uint32_t tp_state(uint32_t hash) {
    uint32_t a = tp_word(TP_LIT_A), b = tp_word(TP_LIT_B);
    if (hash != TP_HASH) return 0x10;
    if (!tp_clearable(a) || !tp_clearable(b)) return 0x11;
    if (a == TP_NEW && b == TP_NEW) return 1;
    if (a == TP_OLD && b == TP_OLD) return 0;
    return 2;
}

static void tp_copy(uint32_t dst, uint32_t src, uint32_t n) {
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)tp_addr(src);
    while (n--) *d++ = *s++;
}

#define tp_sfi10 (*(volatile uint32_t *)0x20006E44u)   /* SFI+0x10 as found when the command arrived (no .bss in this image) */

static void tp_reply(uint32_t cmd, uint32_t state, uint32_t hash, uint32_t sfi30) {
    uint8_t r[36];
    uint32_t v[8] = {state, tp_word(TP_LIT_A), tp_word(TP_LIT_B), hash, sfi30,
                     TP_CTX[12], TP_CTX[13],
                     (TP_CTX[14] & 0xFFu) | ((TP_CTX[15] & 0xFFu) << 8) | (tp_sfi10 << 16) | ((REG(SFI + 0x10) & 0xFFu) << 24)};
    r[0] = (uint8_t)cmd; r[1] = (uint8_t)(cmd >> 8); r[2] = 36; r[3] = 0;
    for (uint32_t i = 0; i < 8; i++)
        for (uint32_t k = 0; k < 4; k++) r[4 + 4 * i + k] = (uint8_t)(v[i] >> (8 * k));
    ST[27] = state;
    send_frame(r, sizeof r);
}

/* Returns 1 when the command was one of ours. */
static int tp_handle(uint32_t cmd, const uint8_t *p, uint32_t n) {
    static const uint8_t magic[8] = {'T', 'I', 'C', 'K', 'F', 'I', 'X', '!'};
    if (cmd != 0x7F01u && cmd != 0x7F02u) return 0;
    uint32_t hash = tp_hash(), sfi30 = REG(SFI + 0x30), state = tp_state(hash);
    tp_sfi10 = REG(SFI + 0x10) & 0xFFu;
    for (uint32_t i = 12; i < 16; i++) TP_CTX[i] = 0;
    if (cmd == 0x7F02u) {
        int ok = n >= 12;
        for (uint32_t i = 0; ok && i < 8; i++) ok = p[4 + i] == magic[i];
        if (!ok) { tp_reply(cmd, 0x30, hash, sfi30); return 1; }
        if (state == 0 || state == 2) {
            ST[28]++;
            /* the resident's RAM image, as its own scatter load makes it */
            tp_copy(0x20000000u, 0x0e50u, 0x0d94u);
            tp_copy(0x20000d94u, 0x1be4u, 0x30u);
            tp_copy(TP_STUB, (uint32_t)tp_stub_start & ~1u, (uint32_t)(tp_stub_end - tp_stub_start));
            *(volatile uint32_t *)TP_ZERO = 0;
            TP_CTX[0] = TP_PROG;
            TP_CTX[1] = TP_LIT_A; TP_CTX[2] = 2; TP_CTX[3] = TP_ZERO;
            TP_CTX[4] = TP_LIT_B; TP_CTX[5] = 2; TP_CTX[6] = TP_ZERO;
            TP_CTX[7] = 0x4400u; TP_CTX[8] = tp_word(0x4400u);
            TP_CTX[9] = 0x0004u; TP_CTX[10] = tp_word(0x0004u);
            TP_CTX[11] = sfi30;
            __asm volatile("cpsid i");
            ((void (*)(volatile uint32_t *))(TP_STUB | 1u))(TP_CTX);
            __asm volatile("cpsie i");
            hash = tp_hash();
            state = tp_state(hash);
            if (TP_CTX[12] || TP_CTX[13]) state = 0x20;
            else if (hash != TP_HASH) state = 0x22;
            else if (state != 1) state = 0x21;
        }
    }
    tp_reply(cmd, state, hash, sfi30);
    return 1;
}
