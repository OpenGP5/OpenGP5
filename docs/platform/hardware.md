# Hardware notes for firmware authors

Measured on a GP-5 with main board `GP-5_MB_V01_241115`. The platform code is the reference:
`platform/nds32/` (control processor) and `platform/dsp/` (DSP).

## Control processor: MVsilicon BP1048B2 (Andes NDS32 V3, D1088 core)

- **Clocks:** 24 MHz crystal; main PLL 288 MHz = system clock; audio PLL 240 MHz feeds USB (÷4) and
  UART0 (`platform/nds32/hal/clock.c`).
- **Flash:** 2 MB SPI flash (JEDEC id `85 60 15`, no blocks write-protected). `0x0`–`0xFFFF` factory loader
  (USB update mode, Foot + PARA recovery; not writable over USB). `0x10000` the app (section `0x62`, at
  most 1,440 KB; an update erases only the length it writes; Valeton's V1.0.6 app ends at `0x13E168`).
  **`0x160000`–`0x161FFF` the platform's store** (`gp5_store_*`, two 4 KB sectors). `0x170000` a
  100-byte table and `0x171000`–`0x189FFF` Valeton's working preset store (written by Valeton's app);
  `0x190000` Valeton's factory presets (section `0x65`). Measured on the test unit with `gp5_link.py
  flash-read`.
- **RAM:** `0x20000000`–`0x20047FFF`. The app's stack is `0x20003000`–`0x20004000`, its data and bss
  start at `0x20004000` (`platform/nds32/start/app.ld`).
- **What the factory loader checks before it starts the app:** the first 41 words of the image are jumps
  (`0x48……`) into the image (vectors: reset, 8 exceptions, 32 interrupt lines); the word at `0xC0` is
  `0xB0BEBDC9`; the header bytes `0xA4`–`0x105` have the expected layout
  (`platform/nds32/start/vectors.S`, checked by `platform/nds32/pack.py`).
- **Interrupt lines used:** 0 = 1 ms tick (timer 1), 7 = UART0 (the DSP link), 8 = timer 2 (LEDs).
- **Pins:**

| pin | use |
|---|---|
| A0 / A1 | UART0 to the DSP (receive / transmit) |
| A7 / A9 / A16 | screen: data/command, chip select, reset (SPI master port 0 for the data) |
| A10 | screen backlight (timer 5 PWM) |
| A17 | foot switch (low = pressed) |
| A28 | volume knob (ADC channel 9) |
| A29 / A30 / A31 | effect LEDs blue / red / green (shared by all three LEDs, high = on) |
| B2 / B3 | PARA knob turning (quadrature, both high at a detent) |
| B6 | PARA knob push (low = pressed) |
| A15, A22, A24, B5 | board control outputs, driven as Valeton's app does (`board_init` in `main.c`) |

- **Screen:** 160 × 80 colour, ST7735S-type controller over SPI.
- **USB:** full-speed device, polled; the open firmware enumerates as USB-MIDI "GP-5 Open FW".

## DSP: ARM Cortex-M4F (marked JAT S95WT)

- **Clock:** about 191.9 MHz.
- **Audio:** 44.1 kHz, 64-sample blocks (1.45 ms, about 278,000 core cycles); guitar in mono, two
  outputs.
- **Flash:** `0x0`–`0x3FFF` Valeton's DSP resident (boot + its updater; not ours). `0x4000`: section
  `0x63` = a 1 KB header whose word at `+0x0C` is the vector table address (`0x4400`), then our firmware.
- **RAM:** `0x20000000`–`0x20007FFF` (32 KB) and `0x10000000`–`0x1001FFFF` (128 KB). What a firmware
  may use and what the platform keeps: `platform/dsp/gp5_dsp.h`.
- **Coprocessor at `0x28000000`:** a programmable float unit, measured at 3 cycles per multiply-add;
  not faster than the M4 itself, not used by the platform.
- **Debug:** SWD on the 5-pin header J7 (not needed for anything in this repo).
