/* Simulator test of the interrupt entry / exit code in platform/nds32/start/vectors.S.
 * irq_entry_run(): loads every register a C handler may change with a known pattern, puts the CPU in
 * the state it is in right after taking an interrupt (IPC / IPSW set, PSW: interrupts off, level 1),
 * jumps to the entry of line 9, and after the return stores every register into irq_after[].
 * The handler (irq_entry_main.c) overwrites all caller-saved registers. */
    .text
    .align 2
    .global irq_entry_run
irq_entry_run:
    smw.adm $r6, [$sp], $r14, #0xe          ! keep callee-saved registers, $fp, $gp, $lp of the caller
    la      $r0, irq_saved_sp
    swi     $sp, [$r0 + 0]
    /* patterns: $rN = 0x11110000 + N */
    .irp n, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27
    sethi   $r\n, hi20(0x11110000)
    ori     $r\n, $r\n, \n
    .endr
    sethi   $r0, hi20(0x22220000)
    ori     $r0, $r0, 1
    mtusr   $r0, $LB
    ori     $r0, $r0, 3
    mtusr   $r0, $LE
    ori     $r0, $r0, 7
    mtusr   $r0, $LC
    ori     $r0, $r0, 15
    mtusr   $r0, $IFC_LP
    la      $r0, irq_fp_in
    .irp n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    fldi    $fd\n, [$r0 + (\n * 8)]
    .endr
    la      $r0, irq_resume_addr_marker
    mtsr    $r0, $IPC
    mfsr    $r0, $PSW
    bitci   $r0, $r0, 7
    ori     $r0, $r0, 0x2001                ! interrupted context: interrupts on, level 0, bit 13 set
    mtsr    $r0, $IPSW
    bitci   $r0, $r0, 0x2007
    ori     $r0, $r0, 2                     ! as after interrupt entry: interrupts off, level 1, bit 13 clear
    mtsr    $r0, $PSW
    isb
    sethi   $r15, hi20(0x11110000)
    ori     $r15, $r15, 15
    sethi   $r0, hi20(0x11110000)
    j       gp5_irq_9

/* The simulator has no `iret`. The test build replaces it with a jump here, which does what the CPU
 * does: PSW = IPSW, continue at IPC (recorded for the check; the continuation is always irq_resume). */
    .global irq_iret_shim
irq_iret_shim:
    smw.adm $r0, [$sp], $r1, #0x0
    la      $r1, irq_ipc_seen
    mfsr    $r0, $IPC
    swi     $r0, [$r1 + 0]
    mfsr    $r0, $IPSW
    mtsr    $r0, $PSW
    isb
    lmw.bim $r0, [$sp], $r1, #0x0
    j       irq_resume

irq_resume:
    smw.adm $r0, [$sp], $r0, #0x0           ! free $r0, then store everything
    la      $r0, irq_after
    .irp n, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27
    swi     $r\n, [$r0 + (\n * 4)]
    .endr
    lmw.bim $r1, [$sp], $r1, #0x0           ! the interrupted $r0
    swi     $r1, [$r0 + 0]
    swi     $sp, [$r0 + 112]
    mfusr   $r1, $LB
    swi     $r1, [$r0 + 116]
    mfusr   $r1, $LE
    swi     $r1, [$r0 + 120]
    mfusr   $r1, $LC
    swi     $r1, [$r0 + 124]
    mfusr   $r1, $IFC_LP
    swi     $r1, [$r0 + 128]
    mfsr    $r1, $PSW
    swi     $r1, [$r0 + 132]
    la      $r0, irq_fp_out
    .irp n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    fsdi    $fd\n, [$r0 + (\n * 8)]
    .endr
    setgie.d
    dsb
    la      $r0, irq_saved_sp
    lwi     $sp, [$r0 + 0]
    lmw.bim $r6, [$sp], $r14, #0xe
    ret

/* overwrite every register a C function is allowed to change; return PSW and $sp through pointers */
    .global irq_clobber
irq_clobber:
    mfsr    $r2, $PSW
    swi     $r2, [$r0 + 0]
    swi     $sp, [$r1 + 0]
    .irp n, 0, 1, 2, 3, 4, 5, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27
    movi    $r\n, -1
    .endr
    mtusr   $r0, $LB
    mtusr   $r0, $LE
    mtusr   $r0, $LC
    mtusr   $r0, $IFC_LP
    la      $r0, irq_fp_junk
    .irp n, 0, 1, 2, 11, 12, 13, 14, 15
    fldi    $fd\n, [$r0 + 0]
    .endr
    ret

    .global irq_resume_addr_marker
irq_resume_addr_marker:
    nop
