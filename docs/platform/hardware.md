# Hardware notes for firmware authors

These notes come from a GP-5 with main board `GP-5_MB_V01_241115`.
Use `platform/nds32/` for the control chip's register code and `platform/dsp/` for the sound chip's code.
Check your board against this one before using its pin map.

The **DSP** is the digital signal processor for sound. The **control processor** runs the screen, controls,
USB and storage. It can also process sound. RAM holds working data; flash keeps code and saved data when power is off.

## Control processor: MVsilicon BP1048B2 (Andes NDS32 V3, D1088 core)

### Clocks

A phase-locked loop (PLL) multiplies the crystal clock to make the faster clocks used by the chip.

| Clock or setting | Value |
|---|---|
| Crystal | 24 MHz |
| Main PLL and system clock | 288 MHz by default; 320 MHz with `"control": {"mhz": 320}` |
| Audio PLL | 240 MHz; feeds USB through ÷4 and UART0 |
| Core regulator at 320 MHz | 1.25 V, matching Valeton's firmware |
| Regulator setup | `0x40025040` bits 0–5 from efuse trim offset `0x14`, then bit 21 |
| Source | `platform/nds32/hal/clock.c` |

The manifest has supported 320 MHz since platform 0.6.0. The efuse holds the chip's factory trim values.

### Flash and RAM

The board has 2 MB of SPI flash, JEDEC ID `85 60 15`, with no write-protected blocks.
SPI is the serial bus used to reach the flash chip. JEDEC ID identifies that chip.

| Range or address | Contents |
|---|---|
| `0x0–0xFFFF` | Factory loader; USB update tools cannot overwrite it |
| `0x10000` | App start, section `0x62`; accepts up to 1,440 KB |
| `0x13E168` | End of Valeton's V1.0.6 app |
| `0x13F000–0x189FFF` | The installed build's shared storage area |
| `0x160000–0x161FFF` | Settings store, `gp5_store_*`, in two 4 KB sectors |
| `0x170000` | Valeton's 100-byte preset-order table |
| `0x171000–0x189FFF` | Working user-preset store when Valeton's firmware runs |
| `0x190000` | Valeton factory presets, section `0x65` |
| `0x20000000–0x20047FFF` | RAM |
| `0x20003000–0x20004000` | App stack |
| `0x20004000` | Start of app data and bss; see `platform/nds32/start/app.ld` |

An app update erases the range it writes. The flash ranges above were checked with `gp5_link.py flash-read`.
See [Storage](storage.md) for the rules that keep file writes within the shared area.

### App checks at start-up

Before it starts the app, the factory loader checks these fields:

| Field | Expected value |
|---|---|
| First 41 words | Jumps (`0x48……`) into the image: reset, 8 exceptions and 32 interrupt lines |
| Word at `0xC0` | `0xB0BEBDC9` |
| Bytes `0xA4–0x105` | The header layout in `platform/nds32/start/vectors.S` |

`platform/nds32/pack.py` checks the packed image against these rules.

### Interrupts and pins

An interrupt lets the chip handle an event before returning to the main code.

| Line | Source | Task |
|---|---|---|
| 0 | Timer 1 | 1 ms tick |
| 7 | UART0 | DSP link |
| 8 | Timer 2 | LEDs |

UART is the serial link between the two chips. Pin names below match the chip's port names.
A low signal means ground level; a high signal means the pin's logic-high level.

| Pin | Use |
|---|---|
| A0 / A1 | UART0 receive / transmit to the DSP |
| A7 / A9 / A16 | Screen data/command, chip select and reset; SPI master port 0 sends data |
| A10 | Screen backlight, timer 5 pulse-width modulation (PWM) |
| A17 | Foot switch; low when pressed |
| A28 | Volume knob, analog-to-digital converter (ADC) channel 9 |
| A29 / A30 / A31 | Blue / red / green for all three LEDs; high means on |
| B2 / B3 | PARA quadrature encoder; both high at a detent |
| B6 | PARA press; low when pressed |
| A15, A22, A24, B5 | Board outputs; `board_init` in `main.c` drives them as Valeton's app does |

The screen is 160 × 80 colour pixels with an ST7735S-type SPI controller.
USB uses full-speed device mode. The open firmware polls USB and shows the MIDI port and audio device name **OpenGP5** (before 1.0: **GP-5 Open FW**).

## DSP: Arm Cortex-M4F (marked JAT S95WT)

| Resource | Value |
|---|---|
| Clock | About 191.9 MHz |
| Audio | Mono input, stereo output; 64 samples per block |
| Default rate | 44.1 kHz; 1.45 ms and about 278,000 cycles per block |
| Optional rate | 48 kHz; 1.33 ms and about 255,900 cycles per block |
| 48 kHz setting | `"audio": {"sample_rate": 48000}`, supported since 0.5.6 |
| Audio clock register | `0x4000e01c`: `0x2029200f` at 48 kHz, rather than `0x24f3200f` at 44.1 kHz |
| Measured 48 kHz block rate | 749.8 blocks per second |
| Flash `0x0–0x3FFF` | Valeton's resident DSP boot and updater code |
| Flash `0x4000` | Section `0x63`: 1 KB header, then OpenGP5 code |
| Header word `+0x0C` | Vector-table address, `0x4400` |
| RAM | `0x20000000–0x20007FFF` (32 KB) and `0x10000000–0x1001FFFF` (128 KB) |
| Coprocessor `0x28000000` | Programmable float unit; measured at 3 cycles per multiply-add |

The 48 kHz clock value is the second entry in Valeton's DSP clock table.
See `platform/dsp/gp5_dsp.h` for the memory your effect can use and the memory reserved for the platform.
The extra float unit was no faster than the M4 in these tests, so the platform does not use it.

### Debug header J7

J7 provides Serial Wire Debug (SWD). The triangle marks pin 1.

| Pin | Signal |
|---|---|
| 1 | Ground |
| 2 | Reset |
| 3 | SWCLK |
| 4 | SWDIO |
| 5 | SWO |

For the confirmed DSP update fault, follow [J7 recovery](j7-recovery.md).
