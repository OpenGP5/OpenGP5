/* GP-5 open platform: memory layout of the control processor's app.
 * Flash: the app slot starts at 0x10000 (below it is the factory loader, never written).
 * RAM: 0x20000000..0x20048000; the stack is below 0x20004000, data and zeroed data above it. */
ENTRY(_start)

MEMORY
{
    APP (rx)  : ORIGIN = 0x00010000, LENGTH = 0x168000
    RAM (rwx) : ORIGIN = 0x20004000, LENGTH = 0x44000 - 0x20   /* the top 32 bytes: the hand-over mark (main.c) */
}

SECTIONS
{
    .vector       : { KEEP(*(.vector)) } > APP                 /* 41 jumps, checked by the loader */
    .stub_section : { KEEP(*(.stub_section)) } > APP           /* image header, +0xA4 */
    .text : {
        *(.text.gp5_start)
        *(.startup_section) *(.tcm_section) *(.driver.isr)     /* section names used by vendor objects */
        *(.text .text.* .stub .gnu.linkonce.t.*)
        . = ALIGN(4);
    } > APP
    .ex9.itable : { *(.ex9.itable) } > APP
    .rodata : {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        *(.sdata2 .sdata2.*)
        . = ALIGN(32);
    } > APP

    PROVIDE(_stack = 0x20004000);

    .data : {
        __data_start = .;
        *(.ramfunc)                       /* code that must run from RAM (hal/flash.c) */
        *(.data .data.* .gnu.linkonce.d.*)
        . = ALIGN(8);
        *(.sdata_d .sdata_d.*)
        *(.sdata_w .sdata_w.*)
        *(.sdata_h .sdata_h.*)
        *(.sdata_b .sdata_b.*)
        *(.sdata_f .sdata_f.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > APP
    __data_lmastart = LOADADDR(.data);

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.sbss_f .sbss_f.*) *(.scommon_f .scommon_f.*)
        *(.sbss_b .sbss_b.*) *(.scommon_b .scommon_b.*)
        . = ALIGN(2);
        *(.sbss_h .sbss_h.*) *(.scommon_h .scommon_h.*)
        . = ALIGN(4);
        *(.sbss_w .sbss_w.*) *(.scommon_w .scommon_w.*) *(.scommon)
        . = ALIGN(8);
        *(.sbss_d .sbss_d.*) *(.scommon_d .scommon_d.*)
        *(.bss .bss.* .gnu.linkonce.b.*) *(COMMON)
        . = ALIGN(4);
        _end = .;
    } > RAM

    /DISCARD/ : { *(.eh_frame) *(.eh_frame_hdr) *(.note.nds32) *(.note.nds32.*) *(.comment) }
}
