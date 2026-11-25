#!/usr/bin/env python3
"""Run an NDS32 app from reset in the Andes simulator and check startup landmarks."""

import argparse
from collections import namedtuple
from hashlib import sha256
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory
import zipfile

import htfw
from gp5_proto import CLASS_CMD, build_sysex


ANDES = Path(__file__).resolve().parents[1] / "fw-build/andes/v3s/nds32le-elf-mculib-v3s/bin"
SIM = ("target sim --environment operating --memory-region 0x0,0x200000 "
       "--memory-region 0x20000000,0x50000 --memory-region 0x40000000,0x100000")
StockBoot = namedtuple("StockBoot", "sha256 tlb efuse pll audio_pll measure calibrated frame iret task adc board gpio_oe")
STOCK = {
    "GP-5": StockBoot("a518f44c8f6a899775c1a69636cda7c34c2bbae6afed23e4d6eca88defca61e5",
                      tlb=0x61d16, efuse=0x62f02, pll=0x61750, audio_pll=0x61846,
                      measure=0x615bc, calibrated=0x615f6, frame=0x5eeb6, iret=0x5eede,
                      task=0x4bc1c, adc=0x604cc, board=0x4bc2c, gpio_oe=0x01418280),
    "GP-50": StockBoot("7ac580be0ea153e15404f1282d6f7480a7781f0e494391daa206b923246575b0",
                       tlb=0x5dd88, efuse=0x5ef74, pll=0x5d7c2, audio_pll=0x5d8b8,
                       measure=0x5d62e, calibrated=0x5d668, frame=0x5b3b6, iret=0x5b3de,
                       task=0x4b5e6, adc=0x5c53e, board=0x4b5fc, gpio_oe=0x11430080),
}


def command(andes, tool, *args):
    result = subprocess.run([andes / f"nds32le-elf-{tool}", *args], check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=10)
    return result.stdout


def checkpoints(andes, elf, commands, expected):
    flags = [arg for line in [SIM, "load", *commands] for arg in ("-ex", line)]
    output = command(andes, "gdb", "-nx", "-batch", elf, *flags)
    actual = re.findall(r"^CHECK ([a-z]+) (\d+) (0x[0-9a-f]+) (0x[0-9a-f]+)$", output, re.M)
    if [label for label, _, _, _ in actual] != expected or any(
        correct != "1" or not 0x20000000 <= int(stack, 16) < 0x20050000
        for _, correct, _, stack in actual
    ):
        raise ValueError(f"startup checkpoint failed\n{output}")
    for label, _, pc, stack in actual:
        print(f"{label} reached at {pc} with SP {stack}")


def check(label, target):
    return f'printf "CHECK {label} %d %#x %#x\\n", $pc == {target}, $pc, $sp'


def tlb_fixture(address):
    # GDB's flat memory map already covers these regions. Replace the unsupported
    # target-write instruction with two 16-bit NOPs only in simulator memory.
    return f"set {{unsigned int}} {address:#x} = 0x00920092"


def gp50_warm_handoff():
    # Enter the stock dispatcher with an already-decoded request. USB emission,
    # the inter-task GPIO update and timer delays are outside this simulation.
    ack = build_sysex(CLASS_CMD, 0, b"\x14\x08\x00")
    expect_ack = " && ".join(
        f"*(unsigned int *)($r0 + {i}) == {int.from_bytes(ack[i:i + 4], 'little'):#x}"
        for i in range(0, len(ack), 4)
    )
    commands = [
        "set {unsigned short} 0x2004ff00 = 0x6f11",
        "set $r6 = 0x2004ff00", "set {unsigned int} ($sp + 4) = 0", "set $pc = 0x42c64",
        "tbreak *0x42b7e", "continue", check("ack", f"0x42b7e && $r1 == {len(ack)} && {expect_ack}"),
        "set $r0 = 1", "set $pc = $lp",
    ]
    for label in ["delayone", "delaytwo"]:
        commands.extend([
            "tbreak *0x4796a", "continue", "set $pc = $lp",
            "tbreak *0x5bf60", "continue", check(label, "0x5bf60 && $r0 == 100"), "set $pc = $lp",
        ])
    commands.extend([
        "tbreak *0x49734", "continue",
        check("handoff", "0x49734 && $r6 == 0 && (*(unsigned int *)0x4000100c & 0x22) == 0x22"),
    ])
    return commands


def stock(andes, image, warm_handoff=False):
    model, app = htfw.read_section(image, 0x62)
    boot = STOCK.get(model)
    if boot is None or sha256(app).hexdigest() != boot.sha256:
        raise ValueError(f"unrecognized {model} stock control section")
    if warm_handoff and model != "GP-50":
        raise ValueError("warm handoff replay requires the GP-50 stock image")
    with TemporaryDirectory() as directory:
        stub = Path(directory)
        (stub / "app.bin").write_bytes(app)
        command(andes, "objcopy", "-I", "binary", "-O", "elf32-nds32le", "-B", "nds32",
                "--rename-section", ".data=.text,alloc,load,readonly,code,contents", stub / "app.bin", stub / "app.o")
        command(andes, "ld", "-Ttext", "0x10000", "-e", "0x10000", stub / "app.o", "-o", stub / "app.elf")
        print(model, "stock control app")
        commands = [
            f"tbreak *{boot.tlb:#x}", "run", check("tlb", hex(boot.tlb)), tlb_fixture(boot.tlb),
            f"tbreak *{boot.efuse:#x}", "continue", check("efuse", hex(boot.efuse)),
            "set {unsigned int} 0x40023010 = 2",
            "set {unsigned int} 0x40021078 = 1",
            f"tbreak *{boot.pll:#x}", "continue", check("pll", hex(boot.pll)),
            "set {unsigned int} 0x40026034 = 1",
            f"tbreak *{boot.audio_pll:#x}", "continue", check("audiopll", hex(boot.audio_pll)),
            "set {unsigned int} 0x40032034 = 1",
            f"tbreak *{boot.measure:#x}", "continue",
            # The 24 MHz reference is divided by 750 for this measurement.
            # Signal completion and supply one count, yielding 32 kHz.
            "set {unsigned int} 0x4002105c = 1",
            "set {unsigned int} 0x40021060 = 1",
            f"tbreak *{boot.calibrated:#x}", "continue",
            check("calibrated", f"{boot.calibrated:#x} && $r0 == 32000"),
            # Capture the first task's IPC/IPSW as the stock scheduler loads
            # them. The simulator lacks IRET and cannot read them over GDB.
            f"tbreak *{boot.frame:#x}", "continue",
            "set $task_pc = $r1", "set $task_psw = $r2",
            f"tbreak *{boot.iret:#x}", "continue", check("scheduler", hex(boot.iret)),
            # Execute the stock mtsr r0,psw once, then resume with the restored
            # R0 and IPC. This models only the initial task dispatch.
            "set $task_r0 = $r0", "set $r0 = $task_psw", "set $pc = 0x11ab2", "si",
            "set $r0 = $task_r0", "set $pc = $task_pc", check("task", hex(boot.task)),
            f"tbreak *{boot.adc:#x}", "continue", check("adc", hex(boot.adc)),
            "set {unsigned int} 0x4002800c = 3",  # ADC calibration complete
            f"tbreak *{boot.board:#x}", "continue",
            check("board", f"{boot.board:#x} && *(unsigned int *)0x40010018 == {boot.gpio_oe:#x}"),
        ]
        expected = ["tlb", "efuse", "pll", "audiopll", "calibrated", "scheduler", "task", "adc", "board"]
        if warm_handoff:
            commands.extend(gp50_warm_handoff())
            expected.extend(["ack", "delayone", "delaytwo", "handoff"])
        checkpoints(andes, stub / "app.elf", commands, expected)


def open_app(andes, elf):
    disassembly = command(andes, "objdump", "-d", elf)
    cache = disassembly.partition("<gp5_cache_on>:\n")[2].partition("\n\n")[0]
    match = re.search(r"^\s*([a-f0-9]+):[^\n]*\btlbop\b[^\n]*targetwrite", cache, re.M)
    if not match or "ex9.it" in disassembly:
        raise ValueError("expected an OpenGP5 app.elf linked without EX9 relaxation")
    tlb = int(match[1], 16)
    print("OpenGP5 control app, emulator build")
    checkpoints(andes, elf, [
        f"tbreak *{tlb:#x}", "run", check("tlb", hex(tlb)), tlb_fixture(tlb),
        "tbreak *main", "continue", check("main", "(unsigned int) &main"),
    ], ["tlb", "main"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--stock", type=Path, help="official Valeton update ZIP or BIN")
    source.add_argument("--open-elf", type=Path, help="OpenGP5 app.elf linked without EX9 relaxation")
    parser.add_argument("--andes-bin", type=Path, default=ANDES)
    parser.add_argument("--gp50-warm-handoff", action="store_true",
                        help="replay the stock GP-50 update-mode request up to the factory-loader jump")
    args = parser.parse_args()
    if args.gp50_warm_handoff and not args.stock:
        parser.error("--gp50-warm-handoff requires --stock")
    try:
        if args.stock:
            stock(args.andes_bin, args.stock, args.gp50_warm_handoff)
        else:
            open_app(args.andes_bin, args.open_elf)
    except (OSError, ValueError, subprocess.SubprocessError, zipfile.BadZipFile) as exc:
        detail = getattr(exc, "stdout", "") or ""
        parser.exit(1, f"error: {exc}\n{detail}")


if __name__ == "__main__":
    main()
