#!/usr/bin/env python3
"""platform/manifest.py: good and bad manifests, flags, and the metadata block round trip.  python3 tools/test_manifest.py"""
import json, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "platform"))
import manifest as M

fails = 0
def check(ok, what):
    global fails
    if not ok: fails += 1; print("FAIL", what)

def with_json(obj):
    d = tempfile.mkdtemp(); (Path(d) / "firmware.json").write_text(json.dumps(obj) if not isinstance(obj, str) else obj); return d

def bad(obj, what):
    try: M.load(with_json(obj)); check(False, f"accepted: {what}")
    except M.ManifestError: pass

base = {"id": "my-delay", "name": "My delay", "version": "0.1.0"}
m = M.load(with_json(base))
check(m["audio"] == {"sample_rate": 44100, "link": False} and m["control"] == {"mhz": 288} and m["presets"] == 0, "defaults")
check(M.flags(m) == ["-DGP5_API1", '-DGP5_FW_ID="my-delay"', "-DGP5_SAMPLE_RATE=44100u", "-DGP5_CTL_MHZ=288u"], "flags, no link")
m2 = M.load(with_json(dict(base, audio={"sample_rate": 48000, "link_words": 264, "link_early": True}, control={"mhz": 320}, presets=8)))
check(M.flags(m2) == ["-DGP5_API1", '-DGP5_FW_ID="my-delay"', "-DGP5_SAMPLE_RATE=48000u", "-DGP5_CTL_MHZ=320u", "-DGP5_AUDIO_LINK",
                      "-DGP5_LINK_EARLY", "-DGP5_LINK_WORDS=264u", "-DGP5_PRESETS=8u"], "flags, link + presets")
check(M.load(tempfile.mkdtemp()) is None, "no manifest: None")
bad("{not json", "broken JSON")
bad([1], "not an object")
bad({"name": "x", "version": "1.0.0"}, "no id")
bad(dict(base, id="My Delay"), "id with capitals and a space")
bad(dict(base, id="x" * 32), "id too long")
bad(dict(base, version="1.0"), "version without patch")
bad(dict(base, colour="red"), "unknown field")
bad(dict(base, audio={"sample_rate": 96000}), "sample rate")
bad(dict(base, audio={"link_words": 100}), "link words not a multiple of 8")
bad(dict(base, audio={"link_words": 272}), "link words above the measured limit")
bad(dict(base, audio={"stereo": True}), "unknown audio field")
bad(dict(base, control={"mhz": 400}), "clock")
bad(dict(base, presets=True), "presets as a boolean")
bad(dict(base, presets=99), "too many presets")
bad(dict(base, platform="9.0"), "another platform major")
bad(dict(base, name="a\nb"), "control character in the name")
blob = M.meta_block(m2)
check(M.read(b"\xff" * 100 + blob + b"\x00" * 50)["id"] == "my-delay", "metadata block found in an image")
check(M.read(b"\xff" * 100 + blob[:-1] + b"\x00") is None, "a damaged block is not taken")
check(len(blob) % 4 == 0, "block length a multiple of 4")
print("ALL OK" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
