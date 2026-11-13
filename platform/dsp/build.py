#!/usr/bin/env python3
"""
Build the from-scratch DSP "hello" firmware into an HTFW image.

  build.py <out_htfw> [--src hello.c|sniff.c]                  an image holding only section 0x63
  build.py <base_htfw> <out_htfw> [--src ...]                    base image with section 0x63 replaced

Section 0x63 (DSP region c = DSP flash 0x4000..0x10348) is:
  [0x400-byte header: ffffffff ffffffff "V106" 0x4400 + zeros; the DSP resident reads word 3 = where the
   vector table is]  [our image linked at 0x4400: vector table + code]  [0xFF padding to 0xc348 bytes]
The length is the stock section's (0xc348). No stock image is needed: the header is written here.
"""
import re, subprocess, sys, tempfile, struct
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import gp5tool

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
HTFW = ROOT / "tools" / "htfw.py"
SEC = "section_00_type_0x63.bin"
SEC_LEN = 0xC348
HDR_LEN = 0x400
VTOR = 0x4400
CFLAGS = ["-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16", "-O2", "-ffreestanding",
          "-fno-math-errno", "-nostdlib", "-nostartfiles", "-ffunction-sections", "-fdata-sections",
          "-Wall", "-Wextra", "-Wdouble-promotion"]
PINNED = ROOT / "fw-build" / "arm" / "bin"          # release/setup-toolchain.sh; else arm-none-eabi-* from PATH
def tool(name): return str(PINNED / name) if (PINNED / name).exists() else name
LIBS = ["-lgcc"]                         # the compiler's own helpers (64-bit division, double maths); no C library


def main():
    import argparse
    ap = argparse.ArgumentParser(); ap.add_argument("stock", metavar="[BASE] OUT"); ap.add_argument("out", nargs="?")
    ap.add_argument("--src", default="hello.c", help="firmware source in this directory")
    ap.add_argument("-D", dest="defines", action="append", default=[], help="extra -D for the firmware")
    ap.add_argument("--firmware", help="name under firmwares/ or a firmware folder: builds its dsp/firmware.h in (needs responder.c "
                                       "with -D AUDIO_PASSTHROUGH -D AUDIO_LEVEL)")
    a = ap.parse_args()
    base, out = (a.stock, a.out) if a.out else (None, a.stock)
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        elf = d / "hello.elf"
        fw = []
        if a.firmware:
            inc = Path(a.firmware).resolve() / "dsp"
            if not (inc / "firmware.h").is_file(): inc = ROOT / "firmwares" / a.firmware / "dsp"
            assert (inc / "firmware.h").is_file(), f"no {inc}/firmware.h"
            fw = [f"-I{inc}", f"-I{HERE}", "-DGP5_FIRMWARE"]
            if re.search(r"^\s*#\s*define\s+GP5_AUDIO_LINK\b", (inc / "firmware.h").read_text(), re.M):
                fw.append("-DAUDIO_SPI1")                   # the firmware opts in to the audio link
        srcs = [str(HERE / a.src)] + ([str(HERE / "gp5_math.c")] if a.src == "responder.c" else [])
        subprocess.run([tool("arm-none-eabi-gcc"), *CFLAGS, f"-I{HERE}", *fw, *[f"-D{x}" for x in a.defines],
                        f"-Wl,-T,{HERE/'link.ld'}", "-Wl,--gc-sections", *srcs, *LIBS, "-o", str(elf)], check=True)
        binf = d / "hello.bin"
        subprocess.run([tool("arm-none-eabi-objcopy"), "-O", "binary", "--only-section=.text", "--only-section=.data",
                        str(elf), str(binf)], check=True)
        used = {}
        for l in subprocess.check_output([tool("arm-none-eabi-objdump"), "-h", str(elf)]).decode().splitlines():
            f = l.split()
            if len(f) >= 3 and f[1] in (".data", ".bss", ".large"): used[f[1]] = int(f[2], 16)
        print(f"DSP RAM: variables {used.get('.data', 0) + used.get('.bss', 0)} of 19968 bytes, "
              f"GP5_LARGE {used.get('.large', 0)} of 124928 bytes")
        img = binf.read_bytes()
        sp, reset = struct.unpack_from("<2I", img, 0)
        assert 0x20000000 < sp <= 0x20008000 and reset & 1 and VTOR < reset < VTOR + len(img), (hex(sp), hex(reset))

        header = b"\xff" * 8 + b"V106" + struct.pack("<I", VTOR) + bytes(HDR_LEN - 16)
        assert HDR_LEN + len(img) <= SEC_LEN
        new = header + img + b"\xff" * (SEC_LEN - HDR_LEN - len(img))
        if base:
            blob = gp5tool.combine(open(base, "rb").read(), {0x63: new})
        else:
            blob = gp5tool.make_image({0x63: new})
        open(out, "wb").write(blob)
        print(f"DSP {a.src}: {len(img)} bytes @0x{VTOR:x}, reset=0x{reset:x}, section 0x63 len 0x{len(new):x} -> {out}")


if __name__ == "__main__":
    main()
