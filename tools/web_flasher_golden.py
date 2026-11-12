#!/usr/bin/env python3
"""Write web/flasher/test/golden.json: what tools/gp5tool.py sends and reports, for the web flasher's tests.

  python3 tools/web_flasher_golden.py [image.bin ...]

- frames for one DSP-sized section (the full update stream gp5_proto.build_update_stream produces)
- gp5tool.inspect()'s report for each image given (release images, Valeton's image if you have it)
"""
import json, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gp5tool
from gp5_proto import build_update_stream

ROOT = Path(__file__).resolve().parents[1]


def main():
    section = bytes((i * 37 + 11) & 0xFF for i in range(1000)) + bytes(range(256)) * 3   # 1768 bytes, 94 packets
    frames = [m.hex() for _, m in build_update_stream(section, 0x63)]
    report = {}
    for p in sys.argv[1:]:
        _, lines = gp5tool.inspect(open(p, "rb").read())
        report[str(Path(p).resolve().relative_to(ROOT))] = [[l.split()[0], int(l.split()[2]), "not stock" not in l] for l in lines]
    out = ROOT / "web/flasher/test/golden.json"
    out.write_text(json.dumps({"type": 0x63, "section_hex": section.hex(), "frames_hex": frames, "inspect": report}))
    print(f"{out}: {len(frames)} frames, {len(report)} image reports")


if __name__ == "__main__":
    main()
