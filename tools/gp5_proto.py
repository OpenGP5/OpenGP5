#!/usr/bin/env python3
"""GP-5 USB-MIDI framing and the factory loader's update stream (docs/platform/protocols.md).
Pure functions: no MIDI, no device I/O. Used by gp5tool.py."""
import struct

# ---------- pure transport (no device I/O) ----------
def crc8(data):           # poly 0x07, init 0 (FLAC/SMBUS table)
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
    return c

def nibble_encode(raw):
    out = bytearray()
    for b in raw:
        out += bytes((b >> 4, b & 0x0F))
    return bytes(out)

def nibble_decode(body):
    if len(body) % 2:
        raise ValueError("odd nibble count")
    return bytes(((body[i] & 0xF) << 4) | (body[i+1] & 0xF) for i in range(0, len(body), 2))

def build_sysex(cls, index, payload):
    """Return full F0..F7 SysEx bytes for one framed message."""
    payload = bytes(payload)
    ln = len(payload)
    body = bytes([crc8(bytes([0, cls, index, ln]) + payload), cls, index, ln]) + payload
    return bytes([0xF0]) + nibble_encode(body) + bytes([0xF7])

def parse_sysex(msg):
    """Inverse of build_sysex. Returns (cls, index, len, payload, crc_ok)."""
    assert msg[0] == 0xF0 and msg[-1] == 0xF7
    dec = nibble_decode(msg[1:-1])
    cls, index, ln = dec[1], dec[2], dec[3]
    pl = dec[4:4+ln]
    ok = crc8(bytes([0, cls, index, ln]) + pl) == dec[0]
    return cls, index, ln, pl, ok

# ---------- update-stream builder (compared with installed Suite native encoder) ----------
CLASS_CMD, CLASS_DATA = 0x01, 0xFF
# This encoder always nibble-encodes the envelope and payload. Suite selects
# 19-byte chunks in that mode; 42 is for its *unencoded* transport mode.
CHUNK = 19

def build_update_stream(section_bytes, sec_type):
    """Build the full list of SysEx messages to write one section (type sec_type).
    Mirrors deviceStartUpdate/timerCallback: enter, begin, data chunks, end, end-image.
    Returns list[bytes]. PURE — sends nothing."""
    msgs = []
    # transfer buffer = [0x11, type] + section payload   (per deviceStartUpdate)
    xfer = bytes([0x11, sec_type]) + section_bytes
    pktcnt = (len(xfer) + CHUNK - 1) // CHUNK
    msgs.append(("ENTER",       build_sysex(CLASS_CMD, 0, bytes([0x11, 0x60]))))
    begin = bytes([0x11, 0x61, sec_type, 0x00, 0x00, 0x00]) + \
            struct.pack("<I", pktcnt) + struct.pack("<I", len(xfer))
    msgs.append(("BEGIN_SEC",   build_sysex(CLASS_CMD, 0, begin)))
    for i in range(pktcnt):
        chunk = xfer[i*CHUNK:(i+1)*CHUNK]
        # Suite stores the running index with STRB before nibble encoding it.
        # The decoded index is eight bits; only the encoded MIDI bytes are 7-bit.
        msgs.append((f"DATA[{i}]", build_sysex(CLASS_DATA, i & 0xFF, chunk)))
    msgs.append(("END_SEC",     build_sysex(CLASS_CMD, 0, bytes([0x11, 0x6e, sec_type, 0x00]))))
    msgs.append(("END_IMAGE",   build_sysex(CLASS_CMD, 0, bytes([0x11, 0x6e, 0x6e, 0x00]))))
    return msgs


def update_ack_status(payload):
    """Return Suite's update status byte, or None for unrelated messages."""
    if len(payload) >= 3 and payload[:2] == b"\x14\x08":
        return payload[2]
    return None
