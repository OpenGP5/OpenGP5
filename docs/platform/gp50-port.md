# GP-50 stock comparison

These findings compare the official GP-5 V1.0.6 and GP-50 V1.0.5 updates. The Andes
instruction simulator ran the stock LCD, PWM and control input routines. Unicorn
ran the stock DSP startup up to serial flash access and replayed the UART and audio
initializers. The findings describe the firmware's register behavior. Board traces,
analog behavior, complete stock boot and recovery still need verification.

## Update images

All five sections have the same type, container address and ordering. Each section
has different contents between the two images. The DSP vector table points to
`0x4400` in both. The control app runs at `0x10000` and its header at
`+0xa4..+0x105` matches.

| Section | HTFW address | GP-5 bytes | GP-50 bytes |
|---|---:|---:|---:|
| DSP program, `0x63` | `0x200000` | `0xc348` | `0xb874` |
| DSP data, `0x67` | `0x240000` | `0x55028` | `0x573e8` |
| Models, `0x66` | `0x280000` | `0x42fe4` | `0x43004` |
| Control app, `0x62` | `0x000000` | `0x12e168` | `0x155338` |
| Factory presets, `0x65` | `0x190000` | `0x2d000` | `0x2d000` |

GP-50's app ends at `0x165338` and overlaps the GP-5 settings and file allocation.
The two resident bootloaders are outside the stock update sections.

## Control processor

Both control apps use the NDS32 instruction set, and their DSP link uses control
UART0 on A0/A1 at 460800 baud. GP-50 uses control UART1 on A9/A10 at 31250 baud for
its external MIDI jacks.

| Function | GP-5 | GP-50 |
|---|---|---|
| Display | 160 × 80 | 160 × 128 |
| Display SPI clock and data | A5, A6 | A5, A6 |
| Display data/command, CS, reset | A7, A9, A16 | A7, A16, B6 |
| Display backlight | A10, Timer 5 PWM | A24, Timer 5 PWM |
| PARA turn and press | B2/B3, B6 | B2/B3, A15 |
| Footswitches | A17 | Control shift input bits 14 and 15 |
| Ring LEDs | A29/A30/A31 | Control shift output bits 9/8/10 and 12/11/13 |
| Analog inputs | A28, ADC selector 9 | A29/A30/A31 enabled, ADC selectors 12/12/11/10 |

GP-50 stock `0x4851e` shifts two bytes using A17 to load, A28 for serial out,
B4 for serial in and B5 for clock. Stock `0x14dac` reads the left, middle and
right knobs from shift input pairs 4/5, 0/1 and 2/3. Buttons are active low.
DRUM, EXIT, GLOBAL and SAVE use bits 6, 7, 12 and 13. Output bit 6
switches the expression ADC between its first two readings.

GP-50 initializes the panel at `0x47e48` and draws windows at `0x47d74`.
It uses MADCTL `0x68`, COLMOD `0x55` and a zero drawing offset. GP-5's
normal stock drawing offset is +1, +26. The simulator ran the LCD GPIO
and PWM setup and captured the panel commands with SPI sends and delays
intercepted.

GP-50 control outputs 0–7 also participate in its Off/Charge/Run state machine,
including analog bypass and power. Those outputs need a board-specific startup
sequence. The stock callbacks and saved power state are referenced from
`0x4aaae` and `0x4000100c`. The attached rails and switches still need
electrical verification.

## DSP

Both execute Cortex-M4F code. The stock routines at GP-5 `0x7660` and GP-50
`0x785c` program the audio clock in the same way, including the two values
for 44.1 and 48 kHz. The DSP core clock of the GP-50 needs measurement.

| Function | GP-5 | GP-50 |
|---|---|---|
| DSP-side UART | UART0 at `0x40005000` | UART1 at `0x40006000` |
| Audio DMA base | `0x40030000` | `0x40030000` |
| Audio DMA input FIFO addresses | `0x50000034`, `0x50000038` | `0x5000000c`, `0x50000010` |
| Audio DMA output FIFO addresses | `0x5000003c`, `0x50000040` | `0x50000014`, `0x50000018` |
| Stock audio interface enable write | `0x8070741b` | `0x0000741f` |
| Stock codec initialization | GP-5 audio init path | Bit-banged I²C on DSP GPIO bits 9/8 |

The GP-50 mux setup at `0x594c` changes different fields from the GP-5.
It also clears memory at `0x29000000..0x290d757f` during startup. GP-50
audio init at `0x5670` writes I²C wire addresses `0x26` and `0x10` before
configuring DMA. The DSP trace captures those transfers with modeled ACKs.
Codec identity, physical memory geometry and the DSP resident updater still
need validation on the board.

## Run the checks

Use the two official stock update ZIPs or their BIN files, the Andes tools
installed by [toolchain setup](setup.md), and Unicorn 2.1 or newer.

```sh
python3 tools/stock_control_emu.py --stock path/to/gp5.zip
python3 tools/stock_control_emu.py --stock path/to/gp50.zip
python3 tools/stock_dsp_emu.py path/to/gp5.zip > gp5-dsp.json
python3 tools/stock_dsp_emu.py path/to/gp50.zip > gp50-dsp.json
diff -u gp5-dsp.json gp50-dsp.json
```

Both stock control apps reach the TLB setup instruction. The simulator uses
a flat memory map, so the harness replaces that unsupported instruction in
simulated memory, then returns ready values for eFuse and the oscillator.
Both apps reach the main PLL lock wait. This checkpoint precedes display,
audio and USB startup. The DSP harness replays selected init routines after
the reset checkpoint and reports the last write to each register per phase.
It ends before any stock flash transaction.

For an OpenGP5 GP-5 baseline, build passthrough and relink the control ELF
without EX9 relaxation for the Andes simulator. Run from the repository root.

```sh
platform/build_firmware.sh passthrough
(
  cd platform/nds32/out
  ../../../fw-build/andes/v3s/nds32le-elf-mculib-v3s/bin/nds32le-elf-gcc \
    -Os -nostartfiles -static -mcmodel=medium -mext-dsp -mvh -T ../start/app.ld \
    -Wl,--gc-sections,--mno-ex9 -o app-emu.elf ./*.o -ldsp
)
python3 tools/stock_control_emu.py --open-elf platform/nds32/out/app-emu.elf
python3 tools/dsp_emu_check.py build/GP-5_passthrough.bin
```

The relinked OpenGP5 control app reaches `main`. The existing DSP check
verifies the UART reply and processed audio blocks. A full board emulator
still needs the flash, interrupt timing, panel, control shift I/O and audio
peripherals, plus the link between the two CPUs.
