/* DSP section 0x63: XIP flash, vector table at 0x4400 (the 1 KB header occupies 0x4000..0x43FF).
 *
 * RAM use (see gp5_dsp.h):
 *   0x20000000..0x20000DFF  kept free: the tick patcher copies the DSP resident's RAM image here
 *   0x20000E00..0x20005BFF  VARS: initialised and zeroed variables (.data, .bss), 19.5 KB
 *   0x20005C00..0x20006BFF  stack, 4 KB (grows down from 0x20006C00)
 *   0x20006C00..0x20007FFF  platform: receive buffer, patcher context, frame log, status block
 *   0x10000000..0x1001E7FF  LARGE: buffers tagged GP5_LARGE (.large), zeroed at start-up, 122 KB
 *   0x1001E800..0x1001FFFF  platform: audio DMA buffers */
ENTRY(reset_handler)
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x4400,     LENGTH = 0x10348 - 0x4400
  VARS  (rwx) : ORIGIN = 0x20000E00, LENGTH = 0x20005C00 - 0x20000E00
  LARGE (rw)  : ORIGIN = 0x10000000, LENGTH = 0x1001E800 - 0x10000000
}
SECTIONS
{
  .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > VARS AT > FLASH
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > VARS
  .large (NOLOAD) : { __large_start = .; *(.large*) . = ALIGN(4); __large_end = .; } > LARGE
  /DISCARD/ : { *(.ARM.exidx*) *(.comment) *(.ARM.attributes) }
}
