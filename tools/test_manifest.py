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
check(m["audio"] == {"sample_rate": 44100, "link": False} and m["control"] == {"mhz": 288} and m["presets"] == 0
      and m["usb_audio"] is False, "defaults (no platform given: USB audio off)")
mu = M.load(with_json(dict(base, usb_audio=True)))
check(M.flags(mu) == ["-DGP5_API1", '-DGP5_FW_ID="my-delay"', "-DGP5_SAMPLE_RATE=44100u", "-DGP5_CTL_MHZ=288u", "-DGP5_USB_AUDIO",
                     "-DGP5_LINK_TOTAL=64u", "-DGP5_USB_PLAY_AT=0u", "-DGP5_USB_REC_AT=0u"], "flags, no link: USB audio's own")
m0 = M.load(with_json(dict(base, usb_audio=False)))
check(M.flags(m0) == ["-DGP5_API1", '-DGP5_FW_ID="my-delay"', "-DGP5_SAMPLE_RATE=44100u", "-DGP5_CTL_MHZ=288u"], "flags, no USB audio")
m2 = M.load(with_json(dict(base, audio={"sample_rate": 48000, "link_words": 264, "link_early": True}, control={"mhz": 320}, presets=8,
                           usb_audio=False)))
check(M.flags(m2) == ["-DGP5_API1", '-DGP5_FW_ID="my-delay"', "-DGP5_SAMPLE_RATE=48000u", "-DGP5_CTL_MHZ=320u", "-DGP5_AUDIO_LINK",
                      "-DGP5_LINK_EARLY", "-DGP5_LINK_WORDS=264u", "-DGP5_PRESETS=8u"], "flags, link + presets")
m3 = M.load(with_json(dict(base, audio={"link_words": 136}, usb_audio=True)))
check(M.flags(m3)[4:] == ["-DGP5_AUDIO_LINK", "-DGP5_LINK_WORDS=136u", "-DGP5_USB_AUDIO", "-DGP5_LINK_TOTAL=200u",
                          "-DGP5_USB_PLAY_AT=136u", "-DGP5_USB_REC_AT=136u"], "flags, link + USB audio from the DSP")
m4 = M.load(with_json(dict(base, audio={"link_words": 248}, usb_audio={"record_from": "control"})))
check(M.flags(m4)[4:] == ["-DGP5_AUDIO_LINK", "-DGP5_LINK_WORDS=248u", "-DGP5_USB_AUDIO", "-DGP5_LINK_TOTAL=264u",
                          "-DGP5_USB_PLAY_AT=200u", "-DGP5_USB_RECORD_CONTROL"], "flags, USB audio recorded on the control processor")
bad(dict(base, audio={"link_words": 208}, usb_audio=True), "USB audio from the DSP with more than 200 words")
# USB audio's default (decided 2026-11-24): on for firmwares written for platform 1.0 or later, off for older ones
m8 = M.load(with_json(dict(base, platform="0.8", audio={"link_words": 264})))
check(m8["usb_audio"] is False, "a 0.8 firmware with 264 link words still builds, without USB audio (NAM Loader 1.3.1)")
# 0.7 and 0.8 introduced the 1.0 API: they build on any 1.x; before 0.7 (another API) and 2.x don't
real = M.platform_version
M.platform_version = lambda: "1.3.0"
try:
    M.load(with_json(dict(base, platform="0.7"))); M.load(with_json(dict(base, platform="0.8")))
    M.load(with_json(dict(base, platform="1.3")))
    check(True, "")
finally:
    M.platform_version = real
M.platform_version = lambda: "1.3.0"
try:
    bad(dict(base, platform="0.6"), "a 0.6 firmware on 1.x")
    bad(dict(base, platform="1.4"), "a newer minor")
    bad(dict(base, platform="2.0"), "another major")
finally:
    M.platform_version = real
real = M.platform_version
M.platform_version = lambda: "1.0.0"
try:
    check(M.load(with_json(dict(base, platform="1.0")))["usb_audio"] == {"record_from": "dsp"}, "a 1.0 firmware: USB audio on")
    check(M.load(with_json(dict(base, platform="1.0", usb_audio=False)))["usb_audio"] is False, "a 1.0 firmware can turn it off")
    bad(dict(base, platform="1.0", audio={"link_words": 264}), "a 1.0 firmware: 264 words don't leave room for USB audio")
finally:
    M.platform_version = real
bad(dict(base, usb_audio={"record_from": "control"}), "record_from control without a link")
bad(dict(base, usb_audio={"record_from": "dac"}), "record_from unknown")
bad(dict(base, usb_audio={"channels": 4}), "usb_audio field unknown")
bad(dict(base, usb_audio="yes"), "usb_audio not a boolean or an object")
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
