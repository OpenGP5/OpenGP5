# Protocols

Everything the platform and the flasher speak. Reference implementations: `tools/gp5_flash.py`
(framing), `tools/gp5tool.py` (update session), `tools/htfw.py` (image format),
`platform/nds32/proto.c` + `platform/nds32/dsp_link.c` (control-processor side),
`platform/dsp/responder.c` (DSP side).

## 1. USB-MIDI framing

The pedal is a USB-MIDI device. Every message, in both directions, is one SysEx message:

```
F0  <body, every byte sent as two bytes: high nibble, then low nibble>  F7

body = crc8, class, index, length, payload[length]
crc8 = CRC-8, polynomial 0x07, initial value 0, not reflected,
       over the bytes 00, class, index, length, payload
```

Classes: `0x01` = command (index 0), `0xFF` = update data (index = packet number, 8 bits, wrapping).
The pedal acknowledges update traffic with the payload `14 08 00`; a non-zero third byte asks for the
frame again.

## 2. Commands the open firmware answers (class 0x01)

| request payload | answer | does |
|---|---|---|
| `11 6F` | `14 08 00` | restart into the factory loader's USB update mode (nothing is written) |
| `7E 00` | `7E 00 <ok> <version u32>` | the DSP firmware's version (platform DSP: `0x00010000`) |
| `7E 01` | `7E 01 <counters> <last frame>` | DSP link counters: frames, errors, ACKs, NAKs since start-up |
| `7E 03 <off u16> <bytes…>` | `7E 03 <ok>` | stage up to 60 bytes of a DSP link payload at `off` |
| `7E 04 <len u16> <wait>` | `7E 04 <status> <n> <reply…>` | send the staged payload to the DSP and wait up to `wait` × 10 ms; status 1 = reply frame, `0x14` = ACK, `0x13` = NAK, 0 = no answer |

`tools/gp5_link.py` wraps `7E 00`, `7E 01` and `7E 03` / `7E 04`.

Valeton's own firmware and the factory loader answer the state query `12 30` with `11 30 01 00`
(Valeton's app) or `11 30 01 01` (update mode); the open firmware does not answer it. `gp5tool.py status`
uses this, plus the USB port name ("GP-5 Open FW"), to tell the three apart.

## 3. Firmware update session (factory loader)

Started by `11 6F` from the open firmware (or Valeton's), or by holding the foot switch and the PARA knob
while plugging the pedal in. In update mode the screen stays dark. One session writes one or more
sections; every frame waits for its `14 08 00`:

```
ENTER        11 60
BEGIN        11 61 <type> 00 00 00 <packet count u32> <length u32>      the loader erases the section
DATA × n     class 0xFF, index = packet number: the bytes [11 <type>] + section data, 19 bytes per packet
END          11 6E <type> 00
… BEGIN … END for the next section …
END_IMAGE    11 6E 6E 00                                                the pedal restarts
```

`length` counts the two leading bytes `11 <type>`. The loader can only write the sections below; its own
code (flash `0x0`–`0xFFFF`) is out of reach, which is why recovery with Foot + PARA always works.

| section | type | where it goes | largest accepted |
|---|---|---|---|
| dsp (DSP firmware) | `0x63` | DSP flash `0x4000` | 64 KB |
| dsp-data (Valeton) | `0x67` | DSP flash | 1 MB |
| models (Valeton) | `0x66` | DSP flash | 268 KB |
| app (control processor) | `0x62` | flash `0x10000` | 1,440 KB |
| presets (Valeton) | `0x65` | flash `0x190000` | – |

## 4. Image format (`.bin`)

Valeton's updates and this project's builds use the same container. Little-endian:

```
0x00  'HTFW'                     0x0C  product name, 16 bytes ("GP-5")
0x08  u32 total file size        0x1C  'V' major minor patch (shown by Valeton's updater only)
0x21  u8 compression (0 = none)  0x22  u8 section count       0x24  u32 payload length
0x38  section table, 16 bytes per section:
        u16 CRC-16/MODBUS of the data, stored byte-swapped, u8 0, u8 type,
        u32 flash address, u32 offset of the data after the table, u32 length
      then the section data, in table order
```

A Valeton image has all five sections; a platform build has only `dsp` and `app`. The order and
addresses are fixed (table above, in that order); `gp5tool.py check` validates an image without a pedal.

## 5. Link between the processors (UART)

The control processor's UART0 (pins A0 receive / A1 transmit) and the DSP's UART0 (`0x40005000`):
460800 baud, 8N1. Frames in both directions:

```
[crc8][length u16 = whole frame][01][payload]        crc8 as in section 1, over the payload only
payload = [cmd u16][len u16][fields…]
status-only answer: 01 04 00 14 = ACK, 01 04 00 13 = NAK (bad CRC or length)
```

Commands the platform uses:

| cmd | from | payload / answer |
|---|---|---|
| `0x6000` | control | version query, `[cmd][8][0 u32]` → `[cmd][12][1 u32][version u32]`. The control side waits for this answer before anything else. |
| `0x1001` | control | output level: `[cmd][12][sub 0 u32][f32 0.0 … 1.0]` → ACK. The DSP is muted until it gets one. |
| `0x4001` | DSP | a 24-byte status frame about every 43 ms (zeros today); not answered |
| `0x7F01` / `0x7F02` | control | DSP flash tick patch: status / apply (see `platform/dsp/tickpatch.h`) |
| `0x7F04` | control | DSP load: `[cmd][16][cycles last block][worst since start-up][late blocks]` (`gp5_link.py load`) |
| `0x7F10` … `0x7FFF` | control | the firmware's own commands, handed to `fw_command()` (`platform/dsp/gp5_dsp.h`) |

Any other command is acknowledged.
