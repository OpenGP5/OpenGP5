#!/usr/bin/env python3
"""Checks of tools/catalog.py's rules with listings that should pass and ones that shouldn't. python3 tools/test_catalog.py"""
import json, pathlib, sys, tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import catalog                                              # noqa: E402

RES = {"tremolo": "an example", "my-effect": "the template's"}
GOOD = {"id": "my-delay", "name": "My Delay", "author": "Someone", "repo": "https://github.com/someone/my-delay",
        "description": "A delay.", "licence": "MIT",
        "releases": [{"version": "1.0.0", "platform": "0.8", "url": "https://github.com/someone/my-delay/releases/download/v1.0.0/GP-5_my-delay.bin",
                      "sha256": "a" * 64, "size": 90000}]}
fails = 0


def case(what, entry, want_ok, name=None, needle=None):
    global fails
    with tempfile.TemporaryDirectory() as d:
        p = pathlib.Path(d) / f"{name or entry.get('id', 'x')}.json"
        p.write_text(json.dumps(entry))
        errs, _ = catalog.check_one(p, RES)
    ok = (not errs) == want_ok and (needle is None or any(needle in e for e in errs))
    print(("ok   " if ok else "FAIL ") + what + ("" if ok else f": {errs}"))
    fails += not ok


def edit(**kw):
    e = json.loads(json.dumps(GOOD)); r = kw.pop("rel", None)
    e.update(kw)
    if r: e["releases"][0].update(r)
    return e


case("a good listing", GOOD, True)
case("a listing with no releases yet (claims the id)", edit(releases=[]), True)
case("the file must be named after the id", GOOD, False, name="other", needle="named after")
case("a reserved id", edit(id="my-effect", repo="https://github.com/someone/my-effect", rel={"url": "https://github.com/someone/my-effect/releases/download/v1/x.bin"}), False, needle="reserved")
case("an official listing may use a reserved id", edit(id="tremolo", repo="https://github.com/OpenGP5/OpenGP5", rel={"url": "https://opengp5.github.io/OpenGP5/flasher/firmware/GP-5_tremolo.bin"}), True)
case("a bad id", edit(id="My_Delay"), False, name="My_Delay", needle="lowercase")
case("a release from another repository", edit(rel={"url": "https://github.com/else/x/releases/download/v1/x.bin"}), False, needle="listing's repository")
case("the official host only for official listings", edit(rel={"url": "https://opengp5.github.io/OpenGP5/x.bin"}), False, needle="listing's repository")
case("a bad checksum", edit(rel={"sha256": "xyz"}), False, needle="sha256")
case("a bad platform", edit(rel={"platform": "1"}), False, needle="platform")
case("an unknown field", edit(extra=1), False, needle="unknown field")
case("a missing field", {k: v for k, v in GOOD.items() if k != "licence"}, False, needle="missing")
case("the same version twice", edit(releases=GOOD["releases"] * 2), False, needle="twice")
case("a description too long", edit(description="x" * 201), False, needle="description")
case("a repo that isn't GitHub", edit(repo="https://example.com/x"), False, needle="repo")
print("ALL OK" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
