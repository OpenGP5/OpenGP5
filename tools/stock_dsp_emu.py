#!/usr/bin/env python3
"""Trace GP-5 V1.0.6 and GP-50 V1.0.5 stock DSP startup offline.

Supply an official Valeton update (.bin or .zip). Needs Unicorn 2.1 or later.
By default, stop reset before serial flash transactions and trace the UART and
audio init routines in isolation. With --runtime, boot to the main loop and
send a version query using the stock UART handlers. The runtime fixture stubs
serial flash writes, provides factory section 0x67, and fast-forwards delays.
It does not simulate audio samples or the factory loader.
Each MMIO value in the report is the last write to that address in its phase.
"""

import argparse
from collections import deque
from hashlib import sha256
import json
from pathlib import Path
import struct
import zipfile

from gp5_proto import crc8
import htfw


STOCK = {
    "GP-5": ("f829cefa988aaf0dcc534951f118192d5af9387c331df60f269ef74b982fc67e",
             0x845c, 0x5bfc, 0x57d0),
    "GP-50": ("89ed48f61bb263d614d34d26b4e0295f234f9a15f95b756d4ad9b486fbd29008",
              0x8f76, 0x5f24, 0x5670),
}
RETURN = 0x3000
FACTORY_HASH = {
    "GP-5": "14465725cac7324d3b1b613c5944e8992f6af450f3d1f8ea6c75e171fa9ff7c1",
    "GP-50": "1239fb16a533a3f8b7258e3f6b1bc7f2e244b4a4edd3f5bb377e091d51656e13",
}


def stock_section(path):
    model, section = htfw.read_section(path, 0x63)
    if model not in STOCK or sha256(section).hexdigest() != STOCK[model][0]:
        raise ValueError(f"unrecognized {model} DSP section")
    return model, section


def trace(model, section, factory_data=None):
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
    if factory_data is not None:
        cpu.mem_write(0x11000, b"\xff" * 0x1EF000)
        cpu.mem_write(0x14000, factory_data)
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

    def run(start, end, count=500_000):
        cpu.emu_start(start | 1, end, count=count)
        pc = cpu.reg_read(UC_ARM_REG_PC)
        if pc != end:
            raise ValueError(f"{start:#x} stopped at {pc:#x} before reaching {end:#x}")

    cpu.hook_add(UC_HOOK_MEM_WRITE, write, begin=0x40000000, end=0x50000FFF)
    cpu.reg_write(UC_ARM_REG_SP, stack)
    run(reset, checkpoint)
    boot_writes = dict(writes)

    if model == "GP-50":
        cpu.hook_add(UC_HOOK_CODE, bitbang, begin=0x7968, end=0x7A94)

    if factory_data is not None:
        writes.clear()
        main_loop, reply = version_query(cpu, model, section, run, checkpoint)
        return {"model": model, "reset_checkpoint": hex(checkpoint), "main_loop": hex(main_loop),
                "boot": boot_writes, "runtime": writes, "version_reply": reply.hex(" "),
                "i2c_wire_bytes": [data.hex(" ") for data in i2c]}

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


def version_query(cpu, model, section, run, checkpoint):
    from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
    from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1

    if model == "GP-50":
        uart, main_loop, rx_vector, version = 0x40006000, 0x8F82, 24, 0x01010005
        delay_loops = (0x200002C8, 0x2000035A)
    else:
        uart, main_loop, rx_vector, version = 0x40005000, 0x846A, 22, 0x01010006
        delay_loops = (0x200002A0, 0x20000332)
    sfi = 0x40050000
    rx, tx = deque(), bytearray()
    command = 0
    waits = dict(zip(delay_loops, (UC_ARM_REG_R1, UC_ARM_REG_R0)))

    def delay(mu, pc, _size, _data):
        mu.reg_write(waits[pc], 0)
        mu.reg_write(UC_ARM_REG_PC, (pc + 8) | 1)

    def flash_read(mu, _access, address, _size, _value, _data):
        # Controller ready, chip idle, quad enable set. Erases/programs are stubbed.
        values = {sfi + 0x18: 3, sfi + 0x24: 1, sfi + 0x100: 2 if command == 0x35 else 0}
        if address in values:
            mu.mem_write(address, struct.pack("<I", values[address]))

    def flash_command(_mu, _access, _address, _size, value, _data):
        nonlocal command
        command = value >> 24

    def uart_read(mu, _access, address, _size, _value, _data):
        if address == uart + 0xC:
            mu.mem_write(address, struct.pack("<I", (len(rx) << 4) | 4 if rx else 0))
        elif address == uart and rx:
            mu.mem_write(address, struct.pack("<I", rx.popleft()))

    def uart_write(_mu, _access, _address, _size, value, _data):
        tx.append(value & 0xFF)

    def irq(vector):
        handler = struct.unpack_from("<I", section, 0x400 + 4 * vector)[0]
        before = cpu.context_save()
        cpu.reg_write(UC_ARM_REG_LR, RETURN | 1)
        run(handler, RETURN, 100_000)
        cpu.context_restore(before)

    def frame(payload):
        return struct.pack("<BHB", crc8(payload), len(payload) + 4, 1) + payload

    for pc in waits:
        cpu.hook_add(UC_HOOK_CODE, delay, begin=pc, end=pc)
    cpu.hook_add(UC_HOOK_MEM_READ, flash_read, begin=sfi, end=sfi + 0x100)
    cpu.hook_add(UC_HOOK_MEM_WRITE, flash_command, begin=sfi + 0xC, end=sfi + 0xC)
    cpu.hook_add(UC_HOOK_MEM_READ, uart_read, begin=uart, end=uart + 0xC)
    cpu.hook_add(UC_HOOK_MEM_WRITE, uart_write, begin=uart, end=uart)

    run(checkpoint, main_loop, 5_000_000)
    rx.extend(frame(struct.pack("<HHI", 0x6000, 8, 0)))
    irq(rx_vector)
    cpu.emu_start(main_loop | 1, 0, count=10_000)
    irq(rx_vector - 1)
    if rx or tx != frame(struct.pack("<HHII", 0x6000, 12, 1, version)):
        raise ValueError(f"unexpected {model} UART version reply: {tx.hex(' ')}")
    return main_loop, tx


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="official Valeton firmware archive or image")
    parser.add_argument("--runtime", action="store_true", help="run the stock UART version query")
    args = parser.parse_args()
    try:
        model, section = stock_section(args.image)
        factory_data = None
        if args.runtime:
            _, factory_data = htfw.read_section(args.image, 0x67)
            if sha256(factory_data).hexdigest() != FACTORY_HASH[model]:
                raise ValueError(f"unrecognized {model} factory data")
        report = trace(model, section, factory_data)
    except (OSError, ValueError, ImportError, zipfile.BadZipFile) as exc:
        parser.exit(1, f"error: {exc}\n")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
