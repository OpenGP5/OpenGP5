#!/usr/bin/env python3
"""Export the saved presets and settings of a stock Valeton GP-50 over USB MIDI."""

import argparse
import hashlib
import json
import struct
import time
from pathlib import Path

from gp5_proto import CLASS_CMD, CHUNK, build_sysex, crc8, parse_sysex
from gp5tool import MidiPorts, Problem, ask, port_base


def read(link, selector, length, args=b""):
    # Stock replies use the frame count as the class and number frames from zero.
    request = bytes((0x12, selector))
    count = (length + CHUNK - 1) // CHUNK
    for delay in (0.12, 0.45, 0.45):
        time.sleep(delay)
        link.flush()
        link.send(build_sysex(CLASS_CMD, 0, request + args))
        reply = bytearray()
        deadline = time.monotonic() + 2
        for index in range(count):
            raw = link.recv(max(0, deadline - time.monotonic()))
            if raw is None:
                break
            try:
                frames, sequence, size, data, crc_ok = parse_sysex(raw)
            except (ValueError, IndexError, AssertionError):
                break
            if not crc_ok or frames != count or sequence != index or size != min(CHUNK, length - len(reply)):
                break
            reply.extend(data)
        if len(reply) == length and reply.startswith(request):
            return bytes(reply[2:])
    raise Problem(f"incomplete GP-50 read for selector 0x{selector:02x} ({args.hex()})")


def decode_name(raw):
    return raw.split(b"\x00", 1)[0].decode("latin1").strip()


def asset_slots(data, count):
    return [
        {"slot": index + 1, "name": decode_name(data[count + 16 * index:count + 16 * (index + 1)]),
         "factory_or_empty": bool(data[index])}
        for index in range(count)
    ]


def snapshot(link):
    # The GP-50 export request takes a zero-based slot and does not select it.
    def export(slot):
        return read(link, 0x4F, 533, struct.pack("<I", slot))

    current = read(link, 0x43, 4)
    modified = read(link, 0x45, 3)
    names = read(link, 0x40, 2002)
    settings = read(link, 0x10, 138)
    irs = read(link, 0x20, 342)
    snaptones = read(link, 0x24, 1362)
    presets = []
    for slot in range(100):
        body = export(slot)
        list_id, name = struct.unpack_from("<I16s", names, 20 * slot)
        if not body.startswith(b"\xff" * 4) or decode_name(body[4:20]) != decode_name(name):
            raise Problem(f"export does not match the name table for preset {slot + 1}")
        presets.append((list_id, body))
        if (slot + 1) % 20 == 0:
            print(f"Read {slot + 1}/100 presets", flush=True)
    for slot, (_, body) in enumerate(presets):
        if export(slot) != body:
            raise Problem(f"preset {slot + 1} changed between reads")
    if read(link, 0x40, 2002) != names:
        raise Problem("the preset list changed between reads")
    return settings, {
        "model": "GP-50", "original_active_slot": int.from_bytes(current, "little") + 1,
        "original_active_modified": bool(modified[0]), "user_ir_slots": asset_slots(irs, 20),
        "snaptone_slots": asset_slots(snaptones, 80),
    }, presets


def save(out, settings, metadata, presets):
    out.mkdir(mode=0o700, parents=True)
    directory = out / "Presets"
    directory.mkdir()
    entries = []
    for slot, (list_id, body) in enumerate(presets, start=1):
        name = decode_name(body[4:20])
        filename = "".join(c if c.isalnum() or c in " -_" else "_" for c in name)
        filename = f"{slot:03}-{filename or 'Unnamed'}.prst"
        prst = b"GP-50" + bytes(13) + b"\x01\x00" + bytes((crc8(body),)) + body
        (directory / filename).write_bytes(prst)
        entries.append({"slot": slot, "list_id": list_id, "name": name, "file": filename,
                        "sha256": hashlib.sha256(prst).hexdigest()})
    (out / "globals.bin").write_bytes(b"\x12\x10" + settings)
    (out / "Backup.json").write_text(json.dumps({**metadata, "presets": entries}, indent=2) + "\n",
                                     encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="new directory for the backup")
    out = parser.parse_args(argv).directory
    try:
        if out.exists():
            raise Problem(f"{out} already exists")
        ports = MidiPorts()
        matches = [name for name in ports.names() if port_base(name) in ("GP-50", "GP-50 MIDI")]
        if len(matches) != 1:
            raise Problem("connect exactly one GP-50 in stock firmware mode")
        link = ports.open(matches[0])
        try:
            if ask(link, [0x12, 0x30], [0x11, 0x30], 1) != b"\x11\x30\x01\x00":
                raise Problem("the GP-50 did not report stock firmware")
            data = snapshot(link)
        finally:
            link.close()
        save(out, *data)
    except (OSError, ValueError, Problem) as exc:
        parser.exit(1, f"error: {exc}\n")
    print(f"100 presets verified and saved to {out}")


if __name__ == "__main__":
    raise SystemExit(main())
