#!/usr/bin/env python3
"""Trace isolated GP-5 V1.0.6 and GP-50 V1.0.5 stock DSP init routines offline.

Supply an official Valeton update (.bin or .zip). Needs Unicorn 2.1 or later.
The trace stops reset before serial flash transactions. Memory-mapped devices are
stubbed. GP-50 codec setup sees ACKs and skips its bit-bang delays.
Each MMIO value in the report is the last write to that address in its phase.
"""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import struct
import zipfile

import htfw


STOCK = {
    "GP-5": ("f829cefa988aaf0dcc534951f118192d5af9387c331df60f269ef74b982fc67e",
             0x845c, 0x5bfc, 0x57d0),
    "GP-50": ("89ed48f61bb263d614d34d26b4e0295f234f9a15f95b756d4ad9b486fbd29008",
              0x8f76, 0x5f24, 0x5670),
}
RETURN = 0x3000


def stock_section(path):
    model, section = htfw.read_section(path, 0x63)
    if model not in STOCK or sha256(section).hexdigest() != STOCK[model][0]:
        raise ValueError(f"unrecognized {model} DSP section")
    return model, section


def trace(model, section):
    from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_MODE_MCLASS, UC_MODE_THUMB
    from unicorn.arm_const import (
        UC_ARM_REG_CONTROL, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R1, UC_ARM_REG_SP,
        UC_CPU_ARM_CORTEX_M4,
    )

    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
    for base, size in ((0, 0x300000), (0x10000000, 0xA0000), (0x20000000, 0x100000),
                       (0x28000000, 0x100000), (0x29000000, 0x100000),
                       (0x40000000, 0x400000), (0x50000000, 0x1000), (0xE000E000, 0x1000)):
        cpu.mem_map(base, size)
    cpu.mem_write(0x4000, section)
    cpu.mem_write(0xE000ED88, struct.pack("<I", 0xF00000))  # resident enables the FPU
    cpu.reg_write(UC_ARM_REG_CONTROL, 0)
    stack, reset = struct.unpack_from("<2I", section, 0x400)
    _, checkpoint, uart, audio = STOCK[model]
    writes, i2c = {}, []

    def write(_cpu, _access, address, _size, value, _data):
        writes[f"{address:#010x}"] = f"{value:#010x}"

    def bitbang(mu, pc, _size, _data):
        if pc == 0x7968:
            mu.reg_write(UC_ARM_REG_PC, mu.reg_read(UC_ARM_REG_LR))
        elif pc == 0x7A12:
            i2c.append(bytearray())
        elif pc == 0x7A94:
            i2c[-1].append(mu.reg_read(UC_ARM_REG_R1) & 0xFF)

    def run(start, end):
        cpu.emu_start(start | 1, end, count=500_000)
        pc = cpu.reg_read(UC_ARM_REG_PC)
        if pc != end:
            raise ValueError(f"{start:#x} stopped at {pc:#x} before reaching {end:#x}")

    cpu.hook_add(UC_HOOK_MEM_WRITE, write, begin=0x40000000, end=0x50000FFF)
    cpu.reg_write(UC_ARM_REG_SP, stack)
    run(reset, checkpoint)
    boot_writes = dict(writes)

    if model == "GP-50":
        cpu.hook_add(UC_HOOK_CODE, bitbang, begin=0x7968, end=0x7A94)

    def call(address):
        writes.clear()
        cpu.reg_write(UC_ARM_REG_SP, stack)
        cpu.reg_write(UC_ARM_REG_LR, RETURN | 1)
        run(address, RETURN)
        return dict(writes)

    uart_writes = call(uart)
    audio_writes = call(audio)
    return {"model": model, "reset_checkpoint": hex(checkpoint), "boot": boot_writes,
            "uart": uart_writes, "audio": audio_writes, "i2c_wire_bytes": [data.hex(" ") for data in i2c]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="official Valeton firmware archive or image")
    args = parser.parse_args()
    try:
        report = trace(*stock_section(args.image))
    except (OSError, ValueError, ImportError, zipfile.BadZipFile) as exc:
        parser.exit(1, f"error: {exc}\n")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
