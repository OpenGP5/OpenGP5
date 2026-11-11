#!/usr/bin/env python3
"""
htfw.py - unpack / repack Valeton (HotOne) HTFW firmware images.

Format (reverse-engineered from Valeton Suite's 5868USB.dylib, deviceStartUpdate):

  0x00  Header, fixed 0x38 (56) bytes:
          0x00  u32   magic 'HTFW' (0x57465448 little-endian)
          0x04  u16   unknown
          0x06  u16   unknown
          0x08  u32   total file size (bytes)
          0x0C  16    product name, NUL-padded (e.g. "GP-5")
          0x1C  4     version: 'V'(0x56) + major + minor + patch bytes
                       (e.g. 56 01 00 06 = "V1.0.6"). THIS is the version the
                       Valeton Suite displays for the file — not the ASCII
                       "V106"/"V1.0.6" strings inside the sections. Header is
                       not CRC-covered, so patch it freely.
          0x20  u8    unknown
          0x21  u8    compression flag (1 => payload LZO-compressed)
          0x22  u8    section count
          0x23  u8    unknown
          0x24  u32   total payload length (file size - 0x38 - table)
          0x28  16    unknown / reserved
  0x38  Section table: `count` entries of 16 bytes, with payload immediately after the last entry. Each entry:
          +0x00 u16   CRC-16/MODBUS of the section data, stored BYTE-SWAPPED
          +0x02 u8    pad/unknown (0 in observed images)
          +0x03 u8    section type tag
          +0x04 u32   flash/load address
          +0x08 u32   data offset (relative to payload base)
          +0x0C u32   data length
  payload_base = 0x38 + count*16
        Section payloads, concatenated in table order.

Integrity: each section's u16 is crc16_modbus(data) with its two bytes swapped.
No cryptographic signature is present. This tool recomputes CRCs and the size
fields on pack, so edited section data produces a valid image the desktop
updater will accept. (On-device bootloader acceptance is a separate question.)

Compression: images with the compression flag set wrap the whole payload in a
single LZO blob; this tool handles uncompressed images (the common, editable
case) and refuses to repack compressed ones rather than guess.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

MAGIC = 0x57465448  # 'HTFW'
HEADER_LEN = 0x38
ENTRY_LEN = 0x10


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for x in data:
        crc ^= x
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def stored_crc(data: bytes) -> int:
    """CRC-16/MODBUS stored byte-swapped, as the firmware stores it."""
    c = crc16_modbus(data)
    return ((c & 0xFF) << 8) | (c >> 8)


def parse(blob: bytes):
    if len(blob) < HEADER_LEN:
        raise ValueError("file smaller than header")
    magic = struct.unpack_from("<I", blob, 0)[0]
    if magic != MAGIC:
        raise ValueError(f"bad magic 0x{magic:08x} (expected HTFW)")
    size_field = struct.unpack_from("<I", blob, 0x08)[0]
    product = blob[0x0C:0x1C].split(b"\x00")[0].decode("ascii", "replace")
    comp_flag = blob[0x21]
    count = blob[0x22]

    if len(blob) < HEADER_LEN + count * ENTRY_LEN:
        raise ValueError("truncated section table")
    entries = []
    o = HEADER_LEN
    for _ in range(count):
        crc, pad, typ = struct.unpack_from("<HBB", blob, o)
        flash, doff, dlen = struct.unpack_from("<III", blob, o + 4)
        entries.append(dict(crc=crc, pad=pad, type=typ, flash=flash, doff=doff, dlen=dlen))
        o += ENTRY_LEN

    # No sentinel word: the count byte @0x22 defines the table; payload follows
    # immediately at payload_base. (Section 0's data may itself begin with 0xFF.)
    payload_base = HEADER_LEN + count * ENTRY_LEN
    return dict(
        size_field=size_field, product=product, comp_flag=comp_flag, count=count,
        entries=entries, payload_base=payload_base,
        header=blob[:HEADER_LEN],
    )


def cmd_info(args):
    blob = Path(args.image).read_bytes()
    h = parse(blob)
    print(f"product        : {h['product']}")
    print(f"size field     : {h['size_field']} (0x{h['size_field']:x})  file: {len(blob)} (0x{len(blob):x})"
          + ("  OK" if h['size_field'] == len(blob) else "  MISMATCH"))
    print(f"compression    : {h['comp_flag']}" + ("  (LZO payload)" if h['comp_flag'] else "  (none)"))
    print(f"sections       : {h['count']}   payload base: 0x{h['payload_base']:x}")
    print("idx type   flashAddr   dataOff    dataLen    crc(stored) crc(calc)  ok")
    cursor = 0
    for i, e in enumerate(h['entries']):
        s = h['payload_base'] + e['doff']
        data = blob[s:s + e['dlen']]
        calc = stored_crc(data)
        chain = "" if e['doff'] == cursor else f"  <-OFFSET GAP (expected 0x{cursor:x})"
        print(f"{i:3d} 0x{e['type']:02x}  0x{e['flash']:08x} 0x{e['doff']:08x} 0x{e['dlen']:08x} "
              f"0x{e['crc']:04x}      0x{calc:04x}     {'OK' if calc == e['crc'] else 'BAD'}{chain}")
        cursor = e['doff'] + e['dlen']
    end = h['payload_base'] + cursor
    print(f"payload end    : 0x{end:x}" + ("  == EOF OK" if end == len(blob) else f"  != EOF 0x{len(blob):x}"))


def cmd_unpack(args):
    blob = Path(args.image).read_bytes()
    h = parse(blob)
    if h['comp_flag']:
        sys.exit("error: compressed image (LZO) - unpack of compressed payloads is not supported yet")
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    manifest = dict(
        product=h['product'],
        header_hex=h['header'].hex(),
        comp_flag=h['comp_flag'],
        sections=[],
    )
    cursor = 0
    for i, e in enumerate(h['entries']):
        if e['doff'] != cursor:
            sys.exit(f"error: section {i} offset 0x{e['doff']:x} not contiguous (expected 0x{cursor:x})")
        s = h['payload_base'] + e['doff']
        data = blob[s:s + e['dlen']]
        if stored_crc(data) != e['crc']:
            sys.exit(f"error: section {i} CRC mismatch in source image")
        name = f"section_{i:02d}_type_0x{e['type']:02x}.bin"
        (outdir / name).write_bytes(data)
        manifest['sections'].append(dict(
            file=name, type=e['type'], flash=e['flash'], pad=e['pad']))
        cursor = e['doff'] + e['dlen']
    (outdir / "manifest.json").write_text(json.dumps(manifest, indent=2))
    print(f"unpacked {len(h['entries'])} sections to {outdir}/ (+ manifest.json)")


def cmd_pack(args):
    srcdir = Path(args.indir)
    manifest = json.loads((srcdir / "manifest.json").read_text())
    if manifest.get("comp_flag"):
        sys.exit("error: manifest marks a compressed image - repack of compressed payloads is not supported yet")
    header = bytearray(bytes.fromhex(manifest['header_hex']))
    if len(header) != HEADER_LEN:
        sys.exit("error: header in manifest is not 0x38 bytes")
    secs = manifest['sections']
    header[0x22] = len(secs)  # section count

    # Build payloads and table
    payload = bytearray()
    entries = []
    doff = 0
    for s in secs:
        data = (srcdir / s['file']).read_bytes()
        entries.append(dict(crc=stored_crc(data), pad=s.get('pad', 0),
                            type=s['type'], flash=s['flash'], doff=doff, dlen=len(data)))
        payload += data
        doff += len(data)

    table = bytearray()
    for e in entries:
        table += struct.pack("<HBBIII", e['crc'], e['pad'], e['type'], e['flash'], e['doff'], e['dlen'])

    payload_len = len(payload)
    total = HEADER_LEN + len(table) + payload_len
    struct.pack_into("<I", header, 0x08, total)        # size field
    struct.pack_into("<I", header, 0x24, payload_len)  # payload length

    out = bytes(header) + bytes(table) + bytes(payload)
    Path(args.out).write_bytes(out)
    print(f"packed {len(secs)} sections -> {args.out} ({total} bytes, 0x{total:x})")


def cmd_replace_section(args):
    """Validate source, replace one payload, rebuild table and lengths."""
    blob = Path(args.image).read_bytes()
    h = parse(blob)
    if h['comp_flag']:
        raise ValueError('compressed images are unsupported')
    if h['size_field'] != len(blob):
        raise ValueError('source size field mismatch')
    if not 0 <= args.index < h['count']:
        raise ValueError('section index out of range')
    target = h['entries'][args.index]
    if target['type'] != 0x62 or target['flash'] != 0:
        raise ValueError('replacement target must be app type 0x62 at flash address 0')
    replacement = Path(args.app).read_bytes()
    if not replacement:
        raise ValueError('replacement app is empty')
    payload, table = bytearray(), bytearray()
    cursor = 0
    for i, e in enumerate(h['entries']):
        start = h['payload_base'] + e['doff']
        data = blob[start:start + e['dlen']]
        if e['doff'] != cursor or len(data) != e['dlen'] or stored_crc(data) != e['crc']:
            raise ValueError(f'invalid source section {i}: offset, length or CRC')
        cursor += e['dlen']
        if i == args.index:
            data = replacement
        table += struct.pack('<HBBIII', stored_crc(data), e['pad'], e['type'],
                             e['flash'], len(payload), len(data))
        payload += data
    if h['payload_base'] + cursor != len(blob):
        raise ValueError('source has trailing or missing payload bytes')
    header = bytearray(h['header'])
    header[0x22] = h['count']
    struct.pack_into('<I', header, 8, HEADER_LEN + len(table) + len(payload))
    struct.pack_into('<I', header, 0x24, len(payload))
    Path(args.out).write_bytes(header + table + payload)
    print(f'replaced section {args.index} -> {args.out} ({len(replacement)} app bytes)')


def main():
    ap = argparse.ArgumentParser(description="Unpack/repack Valeton HTFW firmware images")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info", help="print header + section table with CRC check")
    p.add_argument("image"); p.set_defaults(func=cmd_info)
    p = sub.add_parser("unpack", help="split image into sections + manifest.json")
    p.add_argument("image"); p.add_argument("outdir"); p.set_defaults(func=cmd_unpack)
    p = sub.add_parser("pack", help="rebuild an image from a section dir + manifest.json")
    p.add_argument("indir"); p.add_argument("out"); p.set_defaults(func=cmd_pack)
    p = sub.add_parser("replace-section", help="replace app section (default index 3), preserving other payloads")
    p.add_argument("image"); p.add_argument("app"); p.add_argument("out")
    p.add_argument("--index", type=int, default=3)
    p.set_defaults(func=cmd_replace_section)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
