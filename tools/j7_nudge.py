#!/usr/bin/env python3
"""Get a GP-5 whose DSP updater stalls through one update, over the DSP's debug header J7 (docs/platform/j7-recovery.md).

Some GP-5s stop every firmware update at its start, Valeton's own included: the DSP's updater waits for a timer
that never moves (a flash read fault in its first moments: its timer interrupt counts in the wrong place). This
tool fixes that for one update session, in the DSP's RAM only (nothing is written to flash, a power cycle undoes it):

  1. checks that the DSP is stalled exactly there (its registers, the waiting code, the vector table's fingerprint);
  2. copies the DSP's own vector table (read from the pedal) to spare RAM with the timer entry pointed at the
     updater's own counting routine, already in RAM, and switches the DSP to that table;
  3. puts back the one word the misdirected timer had been counting in, and lets the DSP run on.

Then flash an OpenGP5 firmware in the same session (gp5tool.py flash ... --sections all), start the pedal again and
run `gp5tool.py tickfix --apply`: the 4-byte fix in the DSP's updater, after which updates never stall again and J7 is
no longer needed.

  python3 tools/j7_nudge.py check      connect over J7 and say what the DSP is doing (reads only)
  python3 tools/j7_nudge.py nudge      the fix above (the pedal in update mode, stalled)

Needs OpenOCD (0.11 or newer) and an FT232H board wired to J7: AD0 -> pin 3 (SWCLK), AD1 and AD2 -> pin 4 (SWDIO),
GND -> pin 1. Option --openocd PATH if `openocd` isn't on the PATH.

Nothing from Valeton's firmware is in this file: the table is read from the pedal and compared with a SHA-256.
"""
import argparse, hashlib, os, re, shutil, socket, struct, subprocess, sys, tempfile, time

OPENOCD_CFG = """
gdb_port disabled
telnet_port disabled
tcl_port {port}
adapter driver ftdi
ftdi vid_pid 0x0403 0x6014
ftdi channel 0
ftdi layout_init 0x0008 0x0003
ftdi layout_signal SWD_EN -data 0x0000
ftdi layout_signal SWDIO_OE -data 0x0000
transport select swd
adapter speed 100
swd newdap gp5 cpu -expected-id 0x00000000
dap create gp5.dap -chain-position gp5.cpu
target create gp5.cpu cortex_m -dap gp5.dap
init
"""

# What the stalled updater looks like (measured on a stalling pedal, 2026-11-06; addresses, no code)
STALL_PC = (0x20000878, 0x20000880)       # the 12-tick wait, in RAM
STALL_MSP, STALL_LR, STALL_R0 = 0x20003214, 0x2000034B, 0x20000DC0
TICK = 0x20000DC0                         # the counter the wait reads
WRONG = 0x20000D40                        # where the faulty read sends the timer interrupt (a word that holds 2)
TICK_PTR = 0x2000088C                     # the wait's pointer to the counter (in RAM)
HELPER = 0x20000862                       # the updater's own 'count one tick' routine in RAM (Thumb entry +1)
TABLE_COPY = 0x20007000                   # spare RAM for the vector table (512 bytes)
VTOR, DHCSR = 0xE000ED08, 0xE000EDF0
TABLE_SHA = "dd88664ad865c2ac171a4ac26bfc939427a0abae1252e21554e9d4244cf02a4f"   # words 0..63 of the table
HELPER_SHA = "00f14affeefab4bd711ef83de8b2ab030ab97e98f7d8b1c80b68bf6a4e264265"  # the routine's 10 bytes
SYSTICK = 15                              # the timer's entry in the table


class Problem(Exception):
    pass


class OpenOCD:
    """OpenOCD's Tcl port: one command, one reply (each ends with 0x1a)."""

    def __init__(self, exe, port=6666):
        self.port = port
        self.cfg = tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False)
        self.cfg.write(OPENOCD_CFG.format(port=port)); self.cfg.close()
        self.proc = subprocess.Popen([exe, "-f", self.cfg.name], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     text=True)
        self.sock = None
        for _ in range(50):
            if self.proc.poll() is not None:
                out = self.proc.stdout.read()
                raise Problem("OpenOCD stopped:\n" + out.strip() +
                              "\n\nCheck the wiring (pins 3 and 4), that the FT232H is plugged in, and (Linux) the "
                              "USB permissions; on Windows the FT232H needs the WinUSB driver (Zadig).")
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=1); break
            except OSError:
                time.sleep(0.2)
        if not self.sock:
            self.close(); raise Problem("OpenOCD didn't open its port")
        self.sock.settimeout(10)

    def cmd(self, text):
        self.sock.sendall(text.encode() + b"\x1a")
        buf = b""
        while not buf.endswith(b"\x1a"):
            part = self.sock.recv(4096)
            if not part: raise Problem("OpenOCD closed the connection")
            buf += part
        return buf[:-1].decode(errors="replace")

    def read32(self, addr, n=1):
        r = self.cmd(f"read_memory {addr:#x} 32 {n}").split()
        if len(r) != n: raise Problem(f"read at {addr:#010x} failed: {' '.join(r)[:200]}")
        return [int(x, 0) for x in r]

    def read16(self, addr, n=1):
        r = self.cmd(f"read_memory {addr:#x} 16 {n}").split()
        if len(r) != n: raise Problem(f"read at {addr:#010x} failed: {' '.join(r)[:200]}")
        return [int(x, 0) for x in r]

    def write32(self, addr, words):
        self.cmd(f"write_memory {addr:#x} 32 {{{' '.join(hex(w) for w in words)}}}")

    def reg(self, name):
        r = self.cmd(f"reg {name}")
        m = re.search(r"0x[0-9a-fA-F]+", r)
        if not m: raise Problem(f"can't read register {name}: {r[:200]}")
        return int(m.group(0), 16)

    def close(self):
        try:
            if self.sock: self.cmd("shutdown")
        except Exception:
            pass
        try:
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()
        try: os.unlink(self.cfg.name)
        except OSError: pass


def stalled_state(ocd):
    """Halt and read what decides it. Returns (regs dict, list of problems). Leaves the DSP halted."""
    ocd.cmd("halt 1000")
    regs = {r: ocd.reg(r) for r in ("pc", "msp", "lr", "r0", "r1")}
    bad = []
    if not STALL_PC[0] <= regs["pc"] <= STALL_PC[1]: bad.append(f"pc {regs['pc']:#010x} isn't in the updater's wait")
    if regs["msp"] != STALL_MSP: bad.append(f"stack {regs['msp']:#010x}")
    if regs["lr"] != STALL_LR: bad.append(f"return address {regs['lr']:#010x}")
    if regs["r0"] != STALL_R0: bad.append(f"r0 {regs['r0']:#010x} (the counter it waits on)")
    if regs["r1"] != 0: bad.append(f"r1 {regs['r1']:#x} (ticks so far: not 0, so the timer does count)")
    if bad: return regs, bad
    if ocd.read32(STALL_MSP)[0] != 12: bad.append("the wait isn't the 12-tick one")
    if ocd.read32(VTOR)[0] != 0: bad.append("the vector table was already moved (nudged before?)")
    if ocd.read32(TICK_PTR)[0] != TICK: bad.append("the wait's counter pointer is different")
    helper = struct.pack("<5H", *ocd.read16(HELPER, 5))
    if hashlib.sha256(helper).hexdigest() != HELPER_SHA: bad.append("the counting routine in RAM is different")
    table = struct.pack("<64I", *ocd.read32(0, 64))
    if hashlib.sha256(table).hexdigest() != TABLE_SHA:
        bad.append("the DSP's vector table isn't the one this was made for (a different updater)")
    return regs, bad


def cmd_check(ocd):
    info = ocd.cmd("dap info").strip().splitlines()
    print("connected over J7: " + (info[0] if info else "(no DAP info)"))
    regs, bad = stalled_state(ocd)
    print("registers: " + ", ".join(f"{k} {v:#010x}" for k, v in regs.items()))
    ocd.cmd("resume")
    if bad:
        print("Not the updater stall this fixes:\n  - " + "\n  - ".join(bad))
        print("(Normal when the pedal is running normally, or after `gp5tool.py tickfix --apply`.)")
        return 1
    print("The DSP's updater is stalled the known way: `j7_nudge.py nudge` gets it through this session.")
    return 0


def cmd_nudge(ocd):
    if ocd.read32(DHCSR)[0] & 0x20000:
        raise Problem("the DSP was already halted (another debugger?): stop it and try again")
    regs, bad = stalled_state(ocd)
    if bad:
        ocd.cmd("resume")
        raise Problem("refusing, nothing changed: this isn't the known stall:\n  - " + "\n  - ".join(bad) +
                      "\nIs the pedal in update mode (hold the foot switch and PARA while plugging it in) and has "
                      "nothing been nudged since it started?")
    saved_copy = ocd.read32(TABLE_COPY, 128)
    saved_wrong = ocd.read32(WRONG)[0]
    table = ocd.read32(0, 128)
    table[SYSTICK] = HELPER | 1
    try:
        ocd.write32(TABLE_COPY, table)
        if ocd.read32(TABLE_COPY, 128) != table: raise Problem("the table copy didn't read back")
        ocd.write32(VTOR, [TABLE_COPY])
        if ocd.read32(VTOR)[0] != TABLE_COPY: raise Problem("the DSP didn't take the new table")
        ocd.write32(WRONG, [2])
    except Exception:
        ocd.write32(VTOR, [0]); ocd.write32(WRONG, [saved_wrong]); ocd.write32(TABLE_COPY, saved_copy)
        ocd.cmd("resume")
        raise
    ocd.cmd("resume")
    time.sleep(0.3)
    t = ocd.read32(TICK)[0]
    if t == 0: raise Problem("the timer still doesn't count after the change: power-cycle the pedal and try again")
    print(f"Done: the updater's timer counts again ({t} ticks so far). Now, without unplugging the pedal:")
    print("  python3 tools/gp5tool.py flash <an OpenGP5 firmware .bin> --sections all")
    print("then unplug it, plug it in, and: python3 tools/gp5tool.py tickfix --apply")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("what", choices=["check", "nudge"])
    ap.add_argument("--openocd", default=shutil.which("openocd") or "openocd")
    ap.add_argument("--port", type=int, default=6666)
    a = ap.parse_args(argv)
    ocd = None
    try:
        ocd = OpenOCD(a.openocd, a.port)
        return cmd_check(ocd) if a.what == "check" else cmd_nudge(ocd)
    except FileNotFoundError:
        print("OpenOCD isn't installed (or give its path with --openocd): see docs/platform/j7-recovery.md"); return 2
    except Problem as e:
        print(f"ERROR: {e}"); return 1
    finally:
        if ocd: ocd.close()


if __name__ == "__main__":
    sys.exit(main())
