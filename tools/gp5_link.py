#!/usr/bin/env python3
"""Talk to the DSP through the open firmware's link bridge, over USB-MIDI (the pedal must run the open firmware).

  gp5_link.py version              the DSP firmware's version (the platform's DSP answers 0x00010000 = 1.0)
  gp5_link.py stats                link counters: frames, errors, ACKs / NAKs since start-up
  gp5_link.py load                 DSP load: cycles of the last audio block, the worst since start-up, late
                                   blocks (278,000 cycles per 64-sample block is 100 %)
  gp5_link.py raw <hex payload>    send one DSP link payload [cmd u16][len u16][fields], print the reply
  gp5_link.py flash-read <addr> <length> [out.bin]
                                   read the control processor's flash (below 2 MB; reading only) and say
                                   whether it is erased (all FF); with out.bin, save the bytes

Commands of the control processor's USB protocol (platform/nds32/proto.c), SysEx class 1:
  7E 00                      DSP version            -> 7E 00 <ok> <version u32 LE>
  7E 01                      link counters
  7E 03 <off u16> <bytes>    stage up to 48 bytes of a payload
  7E 04 <len u16> <wait/10ms> send the staged payload to the DSP -> 7E 04 <status> <n> <reply>
  7E 05 <addr u32> <count>   read up to 64 bytes of flash -> 7E 05 <ok> <bytes>
A firmware's own DSP commands start at 0x7F10 (platform/dsp/gp5_dsp.h); for example the granular delay's
`gp5_link.py raw 117f0400` reads its settings. Nothing here writes the pedal's flash.
Needs `pip install mido python-rtmidi`.
"""
import struct, sys
import gp5tool
from gp5tool import MidiPorts, ask as _ask

STATUS = {0: "no answer", 1: "reply", 0x14: "ACK", 0x13: "NAK"}


def hx(b): return " ".join("%02x" % x for x in b)


def ask(link, payload, timeout=1.0):
    return _ask(link, bytes(payload), bytes(payload[:2]), timeout)


def open_link():
    ports = MidiPorts()
    names = [n for n in ports.names() if gp5tool.OPEN_PORT in n]
    if not names: sys.exit("no GP-5 running the open firmware found on USB")
    return ports.open(names[0])


def stats(link):
    pl = ask(link, [0x7E, 0x01])
    if pl is None: print("stats: no reply"); return
    fr, un, ucmd, err, bad, lost, acks, naks, off, nbytes = struct.unpack_from("<HHHBBBBBBH", pl, 2)
    print("frames %d  unsolicited %d (last cmd 0x%04x)  uart-errors %d  bad-frames %d  ring-lost %d  "
          "acks %d  naks %d  irq-off %d  bytes(lo16) %d" % (fr, un, ucmd, err, bad, lost, acks, naks, off, nbytes))
    if len(pl) > 16: print("last unsolicited payload:", hx(pl[16:]))


def raw(link, payload, wait_ms=300):
    """Stage `payload` (7E 03, 48 bytes a piece), then send it (7E 04). Returns (status, reply) or None."""
    for off in range(0, len(payload), 48):
        r = ask(link, [0x7E, 0x03, off & 0xFF, off >> 8] + list(payload[off:off + 48]))
        if r is None or len(r) < 3 or r[2] != 1: return None
    r = ask(link, [0x7E, 0x04, len(payload) & 0xFF, len(payload) >> 8, (wait_ms + 9) // 10], timeout=2.0)
    if r is None or len(r) < 4: return None
    return r[2], r[4:4 + r[3]]


def show_raw(link, payload):
    r = raw(link, payload)
    cmd = payload[0] | payload[1] << 8
    if r is None: print("cmd %04x (%d bytes): no reply from the firmware" % (cmd, len(payload))); return False
    st, rep = r
    print("cmd %04x (%d bytes): %s%s" % (cmd, len(payload), STATUS.get(st, hex(st)), "  " + hx(rep) if rep else ""))
    return st in (1, 0x14)


def flash_read(link, addr, length):
    out = bytearray()
    while len(out) < length:
        n = min(64, length - len(out))
        a = addr + len(out)
        pl = ask(link, [0x7E, 0x05, a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF, a >> 24, n])
        if not pl or len(pl) < 3 or not pl[2]:
            sys.exit("no flash-read reply at 0x%x (needs an OpenGP5 platform with 7E 05)" % a)
        out += pl[3:3 + n]
    return bytes(out)


def main():
    a = sys.argv[1:] or ["stats"]
    if a[0] not in ("version", "stats", "raw", "load", "flash-read"): sys.exit(__doc__)
    link = open_link()
    if a[0] == "version":
        pl = ask(link, [0x7E, 0x00])
        print("no reply" if pl is None else "DSP version 0x%08x" % struct.unpack_from("<I", pl, 3)[0]
              if len(pl) >= 7 and pl[2] else "DSP did not answer")
    elif a[0] == "stats":
        stats(link)
    elif a[0] == "load":
        r = raw(link, bytes.fromhex("047f0400"))
        if not r or r[0] != 1 or len(r[1]) < 16: print("no load report (needs platform DSP 1.0 or later)")
        else:
            last, worst, late = struct.unpack_from("<3I", r[1], 4)
            print("DSP load: last block %d cycles (%.1f %%), worst %d (%.1f %%), late blocks %d"
                  % (last, 100 * last / 278000, worst, 100 * worst / 278000, late))
    elif a[0] == "flash-read":
        if len(a) < 3: sys.exit(__doc__)
        addr, length = int(a[1], 0), int(a[2], 0)
        data = flash_read(link, addr, length)
        used = [i for i in range(0, len(data), 4096) if any(b != 0xFF for b in data[i:i + 4096])]
        print("0x%06x..0x%06x: %s" % (addr, addr + length - 1, "erased (all FF)" if not used else
              "%d of %d 4 KB sectors hold data, first at 0x%06x" % (len(used), (length + 4095) // 4096, addr + used[0])))
        if len(a) > 3: open(a[3], "wb").write(data); print("saved", a[3])
    else:
        show_raw(link, bytes.fromhex("".join(a[1:])))
    link.close()


if __name__ == "__main__":
    main()
