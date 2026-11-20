#!/usr/bin/env python3
"""A firmware's manifest (firmware.json) -> what the build needs (OpenGP5 1.0; platform/README.md).

  manifest.py <firmware folder> --check            check it; print what it says
  manifest.py <firmware folder> --flags            the -D flags both processors' builds get (one line)
  manifest.py <firmware folder> --meta out.c       the metadata block, as C, for the control processor's app
  manifest.py --read image.bin                     the metadata block of a built image (or app section), as JSON

The metadata block is "GP5META1", a u32 length, the manifest as compact JSON (with the platform version that built
it), a NUL, padded to 4 bytes, then the CRC-32 of the JSON. Launchpad, the web flasher and gp5tool find it by its
magic, so a .bin says what it is before it is flashed.
"""
import json, re, struct, sys, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MAGIC = b"GP5META1"
ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,30}$")
VER_RE = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$")
FIELDS = {"id": str, "name": str, "version": str, "author": str, "url": str, "licence": str, "description": str,
          "platform": str, "audio": dict, "control": dict, "presets": int, "legacy_tag": str}
REQUIRED = ("id", "name", "version")
AUDIO = {"sample_rate": (44100, 48000), "link_words": None, "link_early": (False, True), "link": (False, True)}
CONTROL = {"mhz": (288, 320)}
LINK_WORDS_MAX = 264                                     # the most measured to run reliably (48 kHz, 2026-11-16)


class ManifestError(Exception):
    pass


def platform_version():
    t = (ROOT / "nds32" / "gp5_firmware.h").read_text()
    return re.search(r'#define GP5_VERSION_TEXT "([^"]+)"', t).group(1)


def load(folder):
    """firmware.json of `folder`, checked, with defaults filled in. None when the folder has none (pre-1.0 firmware)."""
    p = Path(folder) / "firmware.json"
    if not p.exists():
        return None
    try:
        m = json.loads(p.read_text())
    except json.JSONDecodeError as e:
        raise ManifestError(f"{p}: not valid JSON ({e})")
    if not isinstance(m, dict):
        raise ManifestError(f"{p}: must be one JSON object")
    bad = [k for k in m if k not in FIELDS]
    if bad:
        raise ManifestError(f"{p}: unknown field(s) {', '.join(bad)} (known: {', '.join(FIELDS)})")
    for k, t in FIELDS.items():
        if k in m and not isinstance(m[k], t) or (t is int and isinstance(m.get(k), bool)):
            raise ManifestError(f"{p}: '{k}' must be a {t.__name__}")
    for k in REQUIRED:
        if not m.get(k):
            raise ManifestError(f"{p}: '{k}' is required")
    if not ID_RE.match(m["id"]):
        raise ManifestError(f"{p}: id '{m['id']}': lowercase letters, digits and '-', at most 31 characters")
    if not VER_RE.match(m["version"]):
        raise ManifestError(f"{p}: version '{m['version']}': like 1.2.0 (or 1.2.0-beta.1)")
    for k in ("name", "author", "description", "url", "licence"):
        if k in m and (len(m[k]) > 200 or any(ord(c) < 32 for c in m[k])):
            raise ManifestError(f"{p}: '{k}' is too long or has control characters")
    a = m.setdefault("audio", {})
    for k, v in a.items():
        if k not in AUDIO:
            raise ManifestError(f"{p}: audio.{k} is not known (known: {', '.join(AUDIO)})")
        if AUDIO[k] and v not in AUDIO[k]:
            raise ManifestError(f"{p}: audio.{k} = {v!r}: one of {AUDIO[k]}")
    a.setdefault("sample_rate", 44100)
    if "link_words" in a:
        w = a["link_words"]
        if not isinstance(w, int) or isinstance(w, bool) or w < 8 or w > LINK_WORDS_MAX or w % 8:
            raise ManifestError(f"{p}: audio.link_words = {w!r}: a multiple of 8, 8..{LINK_WORDS_MAX} (more words don't fit "
                                "in a block reliably: measured at 48 kHz, 296 dropped out now and then)")
        a.setdefault("link", True)
    a.setdefault("link", bool(a.get("link_early")))
    c = m.setdefault("control", {})
    for k, v in c.items():
        if k not in CONTROL:
            raise ManifestError(f"{p}: control.{k} is not known (known: {', '.join(CONTROL)})")
        if v not in CONTROL[k]:
            raise ManifestError(f"{p}: control.{k} = {v!r}: one of {CONTROL[k]}")
    c.setdefault("mhz", 288)
    if not 0 <= m.setdefault("presets", 0) <= 64:
        raise ManifestError(f"{p}: presets: 0..64")
    want = m.get("platform")
    if want:
        have = platform_version().split(".")[:2]
        if not re.match(r"^\d+\.\d+$", want):
            raise ManifestError(f"{p}: platform '{want}': the API version it was written for, like 1.0")
        if want.split(".")[0] != have[0] or int(want.split(".")[1]) > int(have[1]):
            raise ManifestError(f"{p}: written for platform {want}; this is {'.'.join(have)}")
    return m


def flags(m, folder=None):
    """-D flags (the same macros firmwares used to #define themselves)"""
    a = m["audio"]
    f = ["-DGP5_API1", f'-DGP5_FW_ID="{m["id"]}"', f'-DGP5_SAMPLE_RATE={a["sample_rate"]}u', f'-DGP5_CTL_MHZ={m["control"]["mhz"]}u']
    if a["link"]:
        f.append("-DGP5_AUDIO_LINK")
        if a.get("link_early"):
            f.append("-DGP5_LINK_EARLY")
        f.append(f'-DGP5_LINK_WORDS={a.get("link_words", 128)}u')
    if m["presets"]:
        f.append(f'-DGP5_PRESETS={m["presets"]}u')
    if folder is not None and (Path(folder) / "params.h").exists():
        f.append("-DGP5_HAS_PARAMS")                  # the firmware declares parameters (gp5_params.h)
    return f


def meta_block(m):
    j = dict(m); j["built_on"] = platform_version()
    body = json.dumps(j, separators=(",", ":"), ensure_ascii=True).encode() + b"\0"
    body += b"\0" * (-len(body) % 4)
    return MAGIC + struct.pack("<I", len(body)) + body + struct.pack("<I", zlib.crc32(body.rstrip(b"\0")) & 0xFFFFFFFF)


def meta_c(m):
    b = meta_block(m)
    rows = ",\n".join("    " + ", ".join(f"0x{x:02x}" for x in b[i:i + 16]) for i in range(0, len(b), 16))
    return ("/* generated by platform/manifest.py from firmware.json: the firmware's metadata block (do not edit) */\n"
            "#include <stdint.h>\n"
            "__attribute__((used, aligned(4))) const uint8_t gp5_meta[%d] = {\n%s\n};\n" % (len(b), rows))


def read(data):
    """the metadata block in `data` (an image or an app section): dict, or None"""
    i = data.find(MAGIC)
    while i >= 0:
        n = struct.unpack_from("<I", data, i + 8)[0]
        body = data[i + 12:i + 12 + n]
        if len(body) == n and n < 65536:
            j = body.rstrip(b"\0")
            crc = struct.unpack_from("<I", data, i + 12 + n)[0] if i + 16 + n <= len(data) else None
            if crc == zlib.crc32(j) & 0xFFFFFFFF:
                return json.loads(j)
        i = data.find(MAGIC, i + 1)
    return None


def main(argv):
    if len(argv) == 2 and argv[0] == "--read":
        m = read(Path(argv[1]).read_bytes())
        print(json.dumps(m, indent=2) if m else "no metadata block (built before OpenGP5 1.0, or not an OpenGP5 image)")
        return 0 if m else 1
    if len(argv) < 2:
        print(__doc__); return 2
    try:
        m = load(argv[0])
    except ManifestError as e:
        print(f"firmware.json: {e}", file=sys.stderr); return 1
    if m is None:
        if argv[1] == "--flags":
            return 0                                  # pre-1.0 firmware: the builds read its #defines as before
        print(f"no firmware.json in {argv[0]}", file=sys.stderr); return 1
    if argv[1] == "--check":
        a = m["audio"]
        print(f'{m["name"]} {m["version"]} (id {m["id"]}): {a["sample_rate"]} Hz, control processor {m["control"]["mhz"]} MHz, '
              f'{"audio link " + str(a.get("link_words", 128)) + " words" if a["link"] else "no audio link"}'
              f'{", " + str(m["presets"]) + " presets" if m["presets"] else ""}')
    elif argv[1] == "--flags":
        print(" ".join(flags(m, argv[0])))
    elif argv[1] == "--meta" and len(argv) == 3:
        Path(argv[2]).write_text(meta_c(m))
    else:
        print(__doc__); return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
