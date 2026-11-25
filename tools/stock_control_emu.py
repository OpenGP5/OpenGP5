#!/usr/bin/env python3
"""Run an NDS32 app from reset in the Andes simulator and check startup landmarks."""

import argparse
from hashlib import sha256
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory
import zipfile

import htfw


ANDES = Path(__file__).resolve().parents[1] / "fw-build/andes/v3s/nds32le-elf-mculib-v3s/bin"
SIM = ("target sim --environment operating --memory-region 0x0,0x200000 "
       "--memory-region 0x20000000,0x50000 --memory-region 0x40000000,0x100000")
STOCK = {
    "GP-5": ("a518f44c8f6a899775c1a69636cda7c34c2bbae6afed23e4d6eca88defca61e5",
             0x61d16, 0x62f02, 0x61750),
    "GP-50": ("7ac580be0ea153e15404f1282d6f7480a7781f0e494391daa206b923246575b0",
              0x5dd88, 0x5ef74, 0x5d7c2),
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


def stock(andes, image):
    model, app = htfw.read_section(image, 0x62)
    if model not in STOCK or sha256(app).hexdigest() != STOCK[model][0]:
        raise ValueError(f"unrecognized {model} stock control section")
    _, tlb, efuse, pll = STOCK[model]
    with TemporaryDirectory() as directory:
        stub = Path(directory)
        (stub / "app.bin").write_bytes(app)
        command(andes, "objcopy", "-I", "binary", "-O", "elf32-nds32le", "-B", "nds32",
                "--rename-section", ".data=.text,alloc,load,readonly,code,contents", stub / "app.bin", stub / "app.o")
        command(andes, "ld", "-Ttext", "0x10000", "-e", "0x10000", stub / "app.o", "-o", stub / "app.elf")
        print(model, "stock control app")
        checkpoints(andes, stub / "app.elf", [
            f"tbreak *{tlb:#x}", "run", check("tlb", hex(tlb)), tlb_fixture(tlb),
            f"tbreak *{efuse:#x}", "continue", check("efuse", hex(efuse)),
            "set {unsigned int} 0x40023010 = 2",
            "set {unsigned int} 0x40021078 = 1",
            f"tbreak *{pll:#x}", "continue", check("pll", hex(pll)),
        ], ["tlb", "efuse", "pll"])


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
    args = parser.parse_args()
    try:
        if args.stock:
            stock(args.andes_bin, args.stock)
        else:
            open_app(args.andes_bin, args.open_elf)
    except (OSError, ValueError, subprocess.SubprocessError, zipfile.BadZipFile) as exc:
        detail = getattr(exc, "stdout", "") or ""
        parser.exit(1, f"error: {exc}\n{detail}")


if __name__ == "__main__":
    main()
