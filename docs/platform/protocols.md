# Protocols

This page covers USB commands, update steps, image files and the serial link between chips.
Read [How a firmware works](concepts.md) to see where each part fits.

| Source | Role |
|---|---|
| `tools/gp5_flash.py` | USB message framing |
| `tools/gp5tool.py` | Update sessions |
| `tools/htfw.py` | Image format |
| `platform/nds32/proto.c`, `platform/nds32/dsp_link.c` | Control-processor commands |
| `platform/dsp/responder.c` | DSP commands |

### Notation

Byte sequences use hexadecimal (base 16). For fields longer than one byte, use little-endian order unless noted:
send the least significant byte first. Angle brackets mark fields in a command.
Square brackets mark parts you can leave out. In frame diagrams, brackets group the fields.

| Mark | Meaning |
|---|---|
| `u8`, `u16`, `u32`, `u64` | Unsigned whole numbers of 8, 16, 32 or 64 bits |
| `i16` | Signed 16-bit whole number |
| `f32` | 32-bit float |
| NUL | Zero byte |
| ACK | Reply that accepts a message |
| NAK | Reply that rejects a message or asks for a retry |
| Nibble | Four bits, or half a byte |

Commands marked **1.0** belong to the API that began in platform 0.7.
These docs target platform 1.0.0. See [Versions](api.md#versions).

## 1. USB-MIDI framing

The pedal uses USB-MIDI to send and receive frames. Each frame is one System Exclusive (SysEx) message:

```
F0  <body, every byte sent as two bytes: high nibble, then low nibble>  F7

body = crc8, class, index, length, payload[length]
crc8 = CRC-8, polynomial 0x07, initial value 0, not reflected,
       over the bytes 00, class, index, length, payload
```

Class `0x01` sends a command with index 0. Class `0xFF` sends update data.
For data, the index is an 8-bit packet number that wraps back to 0.
The pedal replies to update frames with `14 08 00`. A non-zero third byte asks for the frame again.

## 2. Commands the open firmware answers (class 0x01)

### Update mode and DSP link

| Request payload | Reply | Action |
|---|---|---|
| `11 6F` | `14 08 00` | restart into the factory loader's USB update mode (nothing is written) |
| `7E 00` | `7E 00 <ok> <version u32>` | the DSP firmware's version (platform DSP: `0x00010000`) |
| `7E 01` | `7E 01 <counters> <last frame>` | DSP link counters: frames, errors, ACKs, NAKs since start-up |
| `7E 03 <off u16> <bytes…>` | `7E 03 <ok>` | stage up to 60 bytes of a DSP link payload at `off` |
| `7E 04 <len u16> <wait>` | `7E 04 <status> <n> <reply…>` | send the staged payload to the DSP and wait up to `wait` × 10 ms; status 1 = reply frame, `0x14` = ACK, `0x13` = NAK, 0 = no answer |

### Flash and stored data

| Request payload | Reply | Action |
|---|---|---|
| `7E 05 <addr u32> <count>` | `7E 05 <ok> <bytes…>` | read up to 64 bytes of the control chip's flash (below 2 MB, memory-mapped; reads only) |
| `7E 06` | `7E 06 1 <JEDEC id u32> <status u32>` | the flash chip's id and status register 1 |
| `7E 09 <addr u32> <count>` | `7E 09 <ok> <bytes…>` | the same as `7E 05`, read with the flash controller's read command (a driver check) |
| `7E 07 <addr u32>` | `7E 07 <ok>` | erase a 4 KB sector of the storage area, 0x13F000–0x189FFF (reject other addresses; [Storage](storage.md)) |
| `7E 08 <addr u32> <bytes…>` | `7E 08 <ok>` | program up to 56 bytes (a multiple of 4) in the storage area |
| `7E 0E <firmware id u32>` | `7E 0E <reloaded>` | the computer rewrote a store record (0.2.2+): the firmware's `fw_store_changed(id)` (1.0: `fw_storage_changed()`; parameter records the platform reloads itself) loads it again (1) or can't (0: restart the pedal) |

### Controls, parameters and presets

| Request payload | Reply | Action |
|---|---|---|
| `7E 10 <id u16> <value u32>` | `7E 10 <taken>` | set a value live (0.5.4). Parameter API: clamp a parameter to its range, send DSP-marked values, and save values with save flags ([Parameters](params.md)), ids past the parameters go to `fw_param_set`; 0 = not taken. Launchpad's Live page |
| `7E 11 <first u16> <count>` | `7E 11 <n> <values × n, 16 bits each>` | read values (0.5.4), at most 40; parameters are signed. `n` stops at the first id the firmware doesn't know |
| `7E 0F` | `7E 0F 1 <knob> <foot> <para>` | the controls (0.5.2+): volume knob 0..255 (filtered, as `gp5_knob()` gives it), foot switch and PARA push pressed now (0/1, not debounced). Launchpad turns its drawn knob with it |
| `7E 12 <id u16>` | `7E 12 1 <count u16> <id u16> <flags> <unit> <min i16> <max i16> <default i16> <name> 0 <group> 0 <choices> 0` | describe parameter `id` (1.0, [Parameters](params.md)); `7E 12 0` past the last one. Choices are `A\|B\|C` |
| `7E 15 <id u16> <value i16>` | `7E 15 1 <text> 0` | the text of a value (1.0, `GP5_U_CUSTOM`: the firmware's `fw_param_text`); `7E 15 0` when it has none |
| `7E 17 0` | `7E 17 1 <count> <current> <dirty> <used u64>` | presets (1.0): how many, the one loaded (`FF` = none), changed since (0/1), a bit per preset that holds a sound |
| `7E 17 1 <n>` / `7E 17 2 <n>` | `7E 17 <done>` | load / save preset `n` |
| `7E 17 3 <n>` | `7E 17 1 <name> 0` | preset `n`'s name |
| `7E 17 4 <n> <name…>` | `7E 17 <done>` | rename preset `n` (printable ASCII, up to 12) |

### Identity, load and log

| Request payload | Reply | Action |
|---|---|---|
| `7E 0D` | `7E 0D 1 <major> <minor> <patch> <name…> 0` | which firmware this is (platform 0.2.2 and later): the platform's version and the firmware's name (1.0: the manifest's `id`; before: `fw_name()`) |
| `7E 16 <offset u16>` | `7E 16 1 <total u16> <up to 48 bytes>` | the firmware's metadata block from `offset` (1.0; `7E 16 0` for a firmware without a manifest). Format: `"GP5META1"`, length u32, the manifest as JSON, NUL, padding to 4, CRC-32 of the JSON (`platform/manifest.py`) |
| `7E 14 [1]` | `7E 14 1 <15 values, u32 each>` | load and memory (1.0). Control processor: cycles per block, audio-link handler last and worst, main loop's longest pass (cycles), RAM used, RAM total. DSP: answered (0/1), cycles per block, last block, worst, late blocks, variables used and total, `GP5_LARGE` used and total. With `1`: clear peak readings |
| `7E 19 [on]` | `7E 19 1 <direct> <playing> <dry u32> <too much u32> <ppm i32> <waiting u16> <rec too much u32> <recording>` | USB audio (1.0): with `on`, 0 leaves the pedal's own sound out of the outputs while the computer plays into the pedal (1 puts it back; [USB audio](api.md#usb-audio)). The reply: the setting, whether the computer is playing now, then counters since start-up: playback ran dry, playback frames dropped for want of room, the resampler's clock correction (ppm), playback frames waiting, recording frames dropped, recording on. `7E 19 0` for a firmware without USB audio |
| `7E 18 <from u32>` | `7E 18 1 <at u32> <end u32> <n> <n bytes>` | the log (1.0, `gp5_log`), from byte `from`. `at` = where the bytes start (the oldest kept, if `from` was overwritten), `end` = bytes written since start-up; ask again from `at + n` |

`tools/gp5_link.py` wraps `7E 00`, `7E 01`, `7E 03` / `7E 04`, `7E 05` (`flash-read`) and the 1.0 commands
(`info`, `params`, `set`, `presets`, `preset`, `log`, `status`). `tools/gp5tool.py info` reads `7E 16` too.

The state query `12 30` gets a reply from Valeton's app or the factory loader:

| State | Reply |
|---|---|
| Valeton app | `11 30 01 00` |
| Update mode | `11 30 01 01` |
| OpenGP5 app | No reply to this query |

`gp5tool.py status` uses those replies and the USB port name (**OpenGP5** from 1.0, **GP-5 Open FW** before, **GP-5** for Valeton's firmware and the loader) to tell the states apart.

**Reply rules from 1.0:** add new fields at the end. Keep old fields in place with the same meaning.
Keep command numbers fixed. A reader must use the fields it knows and skip extra bytes.
For new features, add a command or sub-command, such as `7E 17 5`.
Old tools can keep using the old commands. They need an update to use the new features.

## 3. Firmware update session (factory loader)

Start update mode with `11 6F`, or press and hold the foot switch and PARA while plugging the pedal in.
Release both controls, then confirm update mode with `gp5tool.py status` or Launchpad.
Use the reported state, not the screen alone. One update session writes one or more sections.

> The platform restarts the chip before handing control to the loader. It writes a mark in the top 32 bytes
> of RAM at `0x20047FE0`, outside the app's RAM. On restart, the platform sees the mark before starting the link
> or firmware and enters the loader. This avoids a loader crash seen after direct handover from an active audio link.
> The extra restart takes about one second; the tools wait for it.

Send these frames in order. Wait for the loader to reply with `14 08 00` after each frame:

| Step | Bytes | What happens |
|---|---|---|
| ENTER | `11 60` | the session starts |
| BEGIN | `11 61 <type> 00 00 00 <packet count u32> <length u32>` | the loader erases that section |
| DATA, once per packet | class `0xFF`, index = the packet number; 19 bytes each: `11 <type>`, then the section's data | the data is written |
| END | `11 6E <type> 00` | the section is done; BEGIN … END again for each further section (dsp, app, …) |
| END_IMAGE | `11 6E 6E 00` | the pedal restarts |

`length` includes the two leading bytes `11 <type>`. The loader can write the sections below, but not its own code
at `0x0–0xFFFF`. This preserves entry to update mode. A separate DSP updater fault can still block a flash;
see [J7 recovery](j7-recovery.md).

| Section | Type | Destination | Maximum size |
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

A Valeton image has all five sections. An OpenGP5 build has two: `dsp` and `app`.
Use the order and addresses in the section table above. Run `gp5tool.py check` to check a file without a pedal.

## 5. Link between the processors (UART)

The control chip's UART0 (pins A0 receive / A1 transmit) and the DSP's UART0 (`0x40005000`):
460800 baud, 8N1. Frames in both directions:

```
[crc8][length u16 = whole frame][01][payload]        crc8 as in section 1, over the payload only
payload = [cmd u16][len u16][fields…]
status-only answer: 01 04 00 14 = ACK, 01 04 00 13 = NAK (bad CRC or length)
```

Commands the platform uses:

| Command | Sender | Payload / reply |
|---|---|---|
| `0x6000` | control | version query, `[cmd][8][0 u32]` → `[cmd][12][1 u32][version u32]`. The control side waits for this answer before anything else. |
| `0x1001` | control | output level: `[cmd][12][sub 0 u32][f32 0.0 … 1.0]` → ACK. The DSP is muted until it gets one. |
| `0x4001` | DSP | a 24-byte status frame about every 43 ms (zeros today); not answered |
| `0x7F01` / `0x7F02` | control | DSP flash tick patch: status / apply (see `platform/dsp/tickpatch.h`) |
| `0x7F04` | control | DSP load: `[cmd][8]` or `[cmd][12][1 u32]` (clear peak readings) → `[cmd][24][cycles last block][worst since start-up][late blocks][variables in use, bytes][GP5_LARGE in use, bytes]` (1.0: the last two) |
| `0x7F06` | control | USB audio (1.0): `[cmd][12][off u32][0 u32]` → ACK. 1 = leave the firmware's sound out of the outputs (the computer's playback stays); glides over a block |
| `0x7F10` | control | a parameter (1.0): `[cmd][12][id u32][value u32]` → ACK; the DSP keeps it and calls `fw_param()` |
| `0x4F10` | DSP | a log line (1.0, `gp5_log`): `[cmd][len][text]`, not answered; the control chip adds it to its log as `dsp: …` |
| `0x7F11` … `0x7FFF` | control | the firmware's own commands, handed to `fw_command()` (`platform/dsp/gp5_dsp.h`; from `0x7F10` for a firmware without parameters) |

Any other command is acknowledged.
