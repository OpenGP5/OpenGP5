# Valeton's DSP firmware: link commands

What Valeton's DSP firmware (V1.0.6, the `dsp` section) understands on the link from the control processor. It was
worked out from both firmwares and checked on a pedal. It is here for the
[DSP updater fault](dsp-updater-fault.md) work: on a pedal whose updates stall, Valeton's DSP firmware stays installed,
and an OpenGP5 app can talk to it.

Addresses are DSP addresses in Valeton's V1.0.6 (its `dsp` section is loaded at DSP flash `0x4000`) or, marked
"control", addresses in Valeton's control-processor app (runtime address = file offset + `0x10000`). Other versions
differ. Everything here comes from reading the code and from tests on one pedal; treat it as notes, not a spec.

## Sending a command from a computer

The link itself (UART, 460800 baud, framing, CRC) is in [Protocols, section 5](protocols.md#5-link-between-the-processors-uart).
OpenGP5's control firmware passes any payload through to the DSP and returns the answer: `7E 03` stages the
payload, `7E 04` sends it ([Protocols](protocols.md)). From Python:

```python
import gp5_link as gl                    # tools/gp5_link.py
link = gl.open_link()
print(gl.raw(link, bytes.fromhex("00600800 00000000")))   # 0x6000: version query
link.close()
```

`raw()` returns `(status, reply)`: status 1 with the reply payload, `0x14` for ACK, `0x13` for NAK, or `None`.

**Not tested yet:** today's OpenGP5 platform running with Valeton's DSP firmware. An early control firmware of this
project did exactly this (November 2026) and drove Valeton's DSP to full sound with the commands below. The
platform's start-up only needs the version answer, which Valeton's DSP gives.

## Payloads and dispatch

Payload = `[cmd u16][len u16 = payload length][fields]`, little-endian. The command's high nibble is the group, the
low 12 bits the index. Valeton's dispatcher (`0x5C68`, jump table at `0x5CD2`) has one handler table per group:

| group | table | handlers |
|---|---|---|
| 0 | `0x8DD0` | 7 |
| 1 | `0x8E04` | 2 |
| 2 | `0x8E0C` | 4 |
| 3 | `0x8E34` | 1 (empty) |
| 4 | `0x8E38` | 3 |
| 5 | `0x8E44` | 3 |
| 6 | `0x8E5C` | 5 |
| 7 | `0x8E1C` | 6 |

Answers: a status-only frame `01 04 00 14` (ACK) for "set" commands, a data frame for queries, nothing for a few.
A bad CRC or length gets `01 04 00 13` (NAK).

## Commands

| cmd | payload after `[cmd][len]` | DSP handler | control sender | notes |
|---|---|---|---|---|
| `0x0000` | whole preset, len `0x170` (below) | `0x5314` | `0x465F0` | load, state = 3 |
| `0x0001` | `u8 module, u8 param, u16 –, u32 model index, f32 value` (len `0x10`) | `0x5346` | `0x46B94` | one parameter; ignored if module ≥ 10 |
| `0x0002` | `u32 which, u32 value` (len `0xC`): 0 = preset volume, 1 = BPM | `0x53A0` | `0x46678`, `0x466A8` | |
| `0x0003` | `u8 order[10]` (len `0x10`) | `0x53CC` | `0x466D8` | only a permutation of 0..9 is applied |
| `0x0004` | `u8 module, u8 on` (len 8) | `0x537E` | `0x46AFC` | module on/off |
| `0x0005` | `u8 module, u8 flag, u16 model, 8 × f32` (len `0x28`) | `0x5330` | `0x46B3C` | module model |
| `0x0006` | `u32 arg` | `0x52F6` | `0x46AC4` | |
| `0x1000` | global settings, len `0x10` (below) | `0x5084` | `0x4658C` | |
| `0x1001` | `u32 sub, 4-byte value` (len `0xC`) | `0x50C8` | `0x468EC` | one field of `0x1000`; sub 0 = **output level** (f32) |
| `0x2001` | **not decoded**: cabinet / user IR slot management | `0x50F4` | `0x16678` | upload path, see the fault page |
| `0x2002` | `u32 id` → 76-byte module info | `0x51D0` | `0x163DC` | cabinet list, ids 0–39 |
| `0x2003` | **not decoded**: cabinet / user IR slot management | `0x519C` | `0x165F4` | upload path |
| `0x4000` | `u8, u8, u16` | `0x56B0` | `0x46C08` | looks like the tuner (reference pitch) |
| `0x4001` | DSP → control, 20-byte report about every 43 ms | `0x5720` (on request) | | |
| `0x4002` | `u16 –, u16 value` | `0x56EC` | `0x46C60` | |
| `0x5000` | `u16 which (0–2), u16 value` | `0x5022` | `0x4680C` … | |
| `0x5001` | `u8, u8, u8` | `0x500E` | `0x46844` | |
| `0x5002` | `u32 0` → data | `0x4FFA` | `0x46770` | **crashes the DSP unless `0x5000`/`0x5001` came first** |
| `0x6000` | `u32 0` → 12-byte version | `0x5652` | `0x46714` | |
| `0x6001` | `u32 flag` | `0x5604` | `0x46740` | |
| `0x6002` | `u32 0, u32 0` → two floats | `0x567C` | `0x46884` | |
| `0x6003` | `u32 state`: 0 = output held, 3 = run | `0x5630` | `0x46A34` | |
| `0x6004` | `u32 flag` | `0x55D8` | `0x468BC` | |
| `0x7001` | **not decoded**: SnapTone (Tone Catch) slot management | `0x55A8` | `0x16B30` | upload path |
| `0x7002` | `u32 id` → 76-byte module info | `0x551E` | `0x16864` | SnapTone list, ids 0–79 |
| `0x7003` | **not decoded**: SnapTone slot management | `0x54EC` | `0x16AAC` | upload path |
| `0x7004` | | empty | | the control side has a handler for a `0x7004` frame (`0x42FDC`) |
| `0x7005` | `u32` → 16-byte echo, then ACK | `0x54AE` | `0x16890` | |

"state" and "action" are two bytes of the DSP's control block at `0x20002098 + 0x2E0` / `+0x2E1`.

Module info (76 bytes): `[u16 id][u16][u32][16-byte name][16-byte name][8 × f32 parameters]`.

### `0x0000` whole preset (`0x170` bytes)

| offset | field |
|---|---|
| `+0x04` | f32 preset volume |
| `+0x08` | u16 BPM |
| `+0x0A` | u16 module enable mask (bit n = module n) |
| `+0x0C` | u8 order[10] |
| `+0x18` | u8 model index[10]: position in that module's list |
| `+0x24` | u8 flag[10]: byte 9 of the model's catalog record |
| `+0x30` | 10 × 8 × f32 parameters |

The ten modules: 0 noise gate, 1 boost/comp, 2 drive, 3 amp, 4 cabinet, 5 EQ, 6 modulation, 7 delay, 8 reverb,
9 SnapTone. **A wrong flag crashes the DSP** when it applies the preset (it calls a missing callback).

### `0x1000` global settings (`0x10` bytes)

`+0x04` f32 (sub 4), `+0x08` f32 output level (sub 0), `+0x0C` s8 (sub 1), `+0x0D` u8 (sub 2), `+0x0E` u8 (sub 3),
`+0x0F` s8 (sub 5). Only the output level's meaning is known.

## Start-up and sound

Valeton's control app, after the version query, lists the modules (`0x2002` for ids 0–39, `0x7002` for 0–79,
then `0x7005`) and sends: `0x1000`, `0x1001` sub 0 = 0 (mute), `0x6003` 0, `0x0000`, `0x6003` 0. The DSP is
silent until `0x6003` 3 (run) and `0x1001` sub 0 = 1.0 (output level) follow.

## When the DSP crashes

A crash (wrong preset flag, `0x5002` sent first) stops all answers; the pedal goes silent. Unplug the pedal and plug
it in again: the DSP starts Valeton's firmware afresh. Nothing in flash changes, as long as the commands sent
didn't write it.

## Open

- Groups 2 and 7: the slot-management commands (`0x2001`, `0x2003`, `0x7001`, `0x7003`). Where do their writes go?
- The meaning of "state" / "action" in the DSP's main loop; groups 4 and 5 beyond the notes above.
