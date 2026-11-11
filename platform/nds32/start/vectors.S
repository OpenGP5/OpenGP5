/* GP-5 open platform: vector table, image header, reset entry, trap and interrupt entry.
 *
 * What the factory loader requires of an app (docs/platform/hardware.md): the image starts at
 * 0x10000, every 4-byte word in its first 0xA4 bytes is a `j` instruction (41 vectors: reset,
 * 8 exceptions, 32 interrupts), and the word at +0xC0 is 0xB0BEBDC9.
 *
 * Interrupts: every line enters gp5_irq_common with its number in $r0. The common code saves what a C
 * function may change (caller-saved registers, loop and call registers, the interruption registers),
 * lets higher-priority lines interrupt, calls gp5_irq_table[n](n), restores and returns.
 * Exceptions: recorded in gp5_crash[] (type, ITYPE, IPC, stack pointer) and the CPU stops in a loop. */

    .section .vector, "ax"
    .global _start
_start:
    j   gp5_reset
    .irp n, 1, 2, 3, 4, 5, 6, 7, 8
    j   gp5_trap_\n
    .endr
    .irp n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    j   gp5_irq_\n
    .endr

/* Image header, +0xA4 .. +0x105. Same bytes as the stock app's header (platform/nds32/header_a4_ff.bin;
 * pack.py checks this). Known fields: +0xB0 0x168000 and +0xB4 0x1F0000 (flash offsets), +0xC0 the magic
 * the loader checks, +0xC4 0x0706. The rest is unprogrammed (0xFF). Code starts at +0x106. */
    .section .stub_section, "ax"
    .byte 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff      /* +0xA4 */
    .byte 0x00, 0x80, 0x16, 0x00                                                      /* +0xB0 */
    .byte 0x00, 0x00, 0x1f, 0x00                                                      /* +0xB4 */
    .byte 0xb1, 0x00, 0x00, 0x01                                                      /* +0xB8 */
    .byte 0x01, 0x00, 0x00, 0x00                                                      /* +0xBC */
    .byte 0xc9, 0xbd, 0xbe, 0xb0                                                      /* +0xC0 magic */
    .byte 0x06, 0x07, 0x00, 0x00                                                      /* +0xC4 */
    .fill 0x103 - 0xC8, 1, 0xff                                                       /* +0xC8 .. +0x102 */
    .byte 0x00, 0xff, 0xff                                                            /* +0x103 .. +0x105 */
    ret5 $lp                                                                          /* +0x106 */

    .section .text.gp5_start, "ax"
    .align 2

/* ---- reset ---- */
    .global gp5_reset
gp5_reset:
    la      $gp, _SDA_BASE_
    mfsr    $r0, $MSC_CFG               ! bit 28: the core has the EX9 instruction table
    sethi   $r1, hi20(0x10000000)
    and     $r2, $r0, $r1
    beqz    $r2, 1f
    la      $r0, _ITB_BASE_
    mtusr   $r0, $ITB
1:
    mfsr    $r0, $FUCOP_CTL             ! floating-point unit on (the compiler uses its registers)
    ori     $r0, $r0, 1
    mtsr    $r0, $FUCOP_CTL
    dsb
    fmfcsr  $r0
    ori     $r0, $r0, 0x1000            ! denormals flushed to zero
    fmtcsr  $r0
    dsb
    la      $sp, _stack
    movi    $r0, 0
    mtsr    $r0, $MISC_CTL
    jal     gp5_startup                 ! start/startup.c: CPU set-up, caches, memory, .data / .bss
    jal     main
2:  j       2b

/* ---- exceptions ---- */
    .irp n, 1, 2, 3, 4, 5, 6, 7, 8
gp5_trap_\n:
    movi    $r0, \n
    j       gp5_fault
    .endr

gp5_fault:
    la      $r1, gp5_crash
    swi     $r0, [$r1 + 0]
    mfsr    $r2, $ITYPE
    swi     $r2, [$r1 + 4]
    mfsr    $r2, $IPC
    swi     $r2, [$r1 + 8]
    swi     $sp, [$r1 + 12]
3:  j       3b

/* ---- interrupts ---- */
    .irp n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    .global gp5_irq_\n
gp5_irq_\n:
    smw.adm $r0, [$sp], $r1, #0x0       ! push $r0, $r1
    movi    $r0, \n
    j       gp5_irq_common
    .endr

gp5_irq_common:
    /* in: $r0 = line number; $r0 and $r1 of the interrupted code are on the stack */
    mfsr    $r1, $IPSW                  ! PSW bits the CPU cleared on entry: take them over from
    andi    $r1, $r1, 0x20c0            ! the interrupted context
    smw.adm $r2, [$sp], $r5, #0x0       ! push $r2..$r5
    mfsr    $r2, $PSW
    or      $r2, $r2, $r1
    mtsr    $r2, $PSW
    isb
    smw.adm $r15, [$sp], $p1, #0xe      ! push $r15..$p1, $fp, $gp, $lp
    mfusr   $r1, $IFC_LP
    mfusr   $r2, $LB
    mfusr   $r3, $LE
    mfusr   $r4, $LC
    smw.adm $r1, [$sp], $r4, #0x0       ! push call / loop registers
    mfsr    $r1, $IPC
    mfsr    $r2, $IPSW
    smw.adm $r1, [$sp], $r2, #0x0       ! push return address and status
    mfsr    $r1, $PSW                   ! interrupt level 1 -> 0 and interrupts on again:
    addi    $r1, $r1, -1                ! a line with a higher priority may now interrupt this one
    mtsr    $r1, $PSW
    move    $r2, $sp                    ! 8-byte stack alignment for the C code, old $sp kept
    addi    $sp, $sp, -4
    bitci   $sp, $sp, 4
    swi     $r2, [$sp + 0]
    addi    $sp, $sp, -64
    fsdi    $fd0,  [$sp + 0]
    fsdi    $fd1,  [$sp + 8]
    fsdi    $fd2,  [$sp + 16]
    fsdi    $fd11, [$sp + 24]
    fsdi    $fd12, [$sp + 32]
    fsdi    $fd13, [$sp + 40]
    fsdi    $fd14, [$sp + 48]
    fsdi    $fd15, [$sp + 56]
    la      $r1, gp5_irq_table
    lw      $r1, [$r1 + ($r0 << 2)]
    jral    $r1                         ! handler(line number)
    fldi    $fd0,  [$sp + 0]
    fldi    $fd1,  [$sp + 8]
    fldi    $fd2,  [$sp + 16]
    fldi    $fd11, [$sp + 24]
    fldi    $fd12, [$sp + 32]
    fldi    $fd13, [$sp + 40]
    fldi    $fd14, [$sp + 48]
    fldi    $fd15, [$sp + 56]
    addi    $sp, $sp, 64
    lwi     $sp, [$sp + 0]
    setgie.d                            ! no interrupt between here and the return
    dsb
    lmw.bim $r1, [$sp], $r2, #0x0
    mtsr    $r1, $IPC
    mtsr    $r2, $IPSW
    lmw.bim $r1, [$sp], $r4, #0x0
    mtusr   $r1, $IFC_LP
    mtusr   $r2, $LB
    mtusr   $r3, $LE
    mtusr   $r4, $LC
    lmw.bim $r15, [$sp], $p1, #0xe
    lmw.bim $r2, [$sp], $r5, #0x0
    lmw.bim $r0, [$sp], $r1, #0x0
    iret
