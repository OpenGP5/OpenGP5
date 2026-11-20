#!/usr/bin/env python3
"""The firmware catalog (catalog/README.md): listings that Launchpad and the web flasher offer, and the registry of ids.

  catalog.py check                         every listing: fields, ids free and not reserved, URLs on the listing's
                                           own repository (offline)
  catalog.py verify [ids...] [--local DIR] [--wait S]
                                           download each release (or read DIR/<file name>) and check its size,
                                           checksum and the image's metadata (id, version, platform); --wait: a
                                           download that doesn't match yet is tried again every 30 s for up to S
                                           seconds (a release on GitHub Pages is served a minute or two after the push)
  catalog.py index [--check]               write docs/catalog.json, the list Launchpad and the web flasher read
                                           (--check: fail if it isn't up to date)
  catalog.py release <file.bin> <url>      the release entry for a built image, read from the image itself
"""
import hashlib, json, pathlib, re, sys, time, urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
CAT = ROOT / "catalog"
INDEX = ROOT / "docs" / "catalog.json"
sys.path.insert(0, str(ROOT / "platform"))
import manifest                                             # noqa: E402

ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,30}$")
VER_RE = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$")
REPO_RE = re.compile(r"^https://github\.com/([A-Za-z0-9-]+)/([A-Za-z0-9._-]+)$")
FIELDS = {"id": str, "name": str, "author": str, "repo": str, "description": str, "licence": str, "releases": list}
REL_FIELDS = {"version": str, "platform": str, "url": str, "sha256": str, "size": int}
OFFICIAL_ACCOUNT = "OpenGP5"
OFFICIAL_HOST = "https://opengp5.github.io/OpenGP5/"


def listings():
    return sorted(p for p in CAT.glob("*.json") if p.name != "reserved.json")


def official(e):
    m = REPO_RE.match(e.get("repo", ""))
    return bool(m) and m.group(1) == OFFICIAL_ACCOUNT


def check_one(p, reserved):
    errs = []
    def bad(s): errs.append(f"{p.name}: {s}")
    try: e = json.loads(p.read_text())
    except json.JSONDecodeError as x: return [f"{p.name}: not valid JSON ({x})"], None
    if not isinstance(e, dict): return [f"{p.name}: must be one JSON object"], None
    for k in e:
        if k not in FIELDS: bad(f"unknown field '{k}' (known: {', '.join(FIELDS)})")
    for k, t in FIELDS.items():
        if k not in e: bad(f"'{k}' is missing")
        elif not isinstance(e[k], t): bad(f"'{k}' must be a {t.__name__}")
    if errs: return errs, None
    if not ID_RE.match(e["id"]): bad(f"id '{e['id']}': lowercase letters, digits and '-', at most 31 characters")
    if p.stem != e["id"]: bad(f"the file must be named after the id: {e['id']}.json")
    if e["id"] in reserved and not official(e): bad(f"id '{e['id']}' is reserved ({reserved[e['id']]})")
    for k in ("name", "author", "licence"):
        if not e[k].strip() or len(e[k]) > 60: bad(f"'{k}': 1..60 characters")
    if not e["description"].strip() or len(e["description"]) > 200: bad("'description': 1..200 characters")
    if any(ord(c) < 32 for k in ("name", "author", "description", "licence") for c in e[k]): bad("control characters")
    m = REPO_RE.match(e["repo"])
    if not m: bad("'repo': https://github.com/<account>/<repository>")
    seen = set()
    for i, r in enumerate(e["releases"]):
        w = f"releases[{i}]"
        if not isinstance(r, dict): bad(f"{w}: an object"); continue
        for k in r:
            if k not in REL_FIELDS: bad(f"{w}: unknown field '{k}' (known: {', '.join(REL_FIELDS)})")
        for k, t in REL_FIELDS.items():
            if k not in r or not isinstance(r[k], t) or isinstance(r[k], bool): bad(f"{w}: '{k}' ({t.__name__}) is missing or wrong")
        if any(k not in r or not isinstance(r[k], t) for k, t in REL_FIELDS.items()): continue
        if not VER_RE.match(r["version"]): bad(f"{w}: version like 1.2.0")
        if r["version"] in seen: bad(f"{w}: version {r['version']} listed twice")
        seen.add(r["version"])
        if not re.match(r"^\d+\.\d+$", r["platform"]): bad(f"{w}: platform like 0.8 (major.minor)")
        if not re.match(r"^[0-9a-f]{64}$", r["sha256"]): bad(f"{w}: sha256: 64 lowercase hex digits")
        if not 1024 <= r["size"] <= 2 * 1024 * 1024: bad(f"{w}: size in bytes (an image is 1 KB .. 2 MB)")
        if m:
            own = f"{e['repo']}/releases/download/"
            if not (r["url"].startswith(own) or (official(e) and r["url"].startswith(OFFICIAL_HOST))):
                bad(f"{w}: url must be a release file of the listing's repository ({own}<tag>/<file>.bin)")
            if not r["url"].endswith(".bin"): bad(f"{w}: url: the .bin image")
    return errs, e


def load_all():
    reserved = json.loads((CAT / "reserved.json").read_text())["ids"]
    errs, entries = [], []
    for p in listings():
        er, e = check_one(p, reserved)
        errs += er
        if e and not er: entries.append(e)
    return errs, entries


def index_text(entries):
    out = {"about": "Firmwares built with OpenGP5 that anyone can install (catalog/README.md). Not reviewed by OpenGP5, "
                    "except where official is true. Generated by tools/catalog.py index: do not edit.",
           "version": 1,
           "firmwares": [dict(e, official=official(e)) for e in sorted(entries, key=lambda e: e["name"].lower())]}
    return json.dumps(out, indent=1, ensure_ascii=False) + "\n"


def verify(entries, ids, local, wait=0):
    errs = []
    for e in entries:
        if ids and e["id"] not in ids: continue
        for r in e["releases"]:
            w = f"{e['id']} {r['version']}"
            try:
                if local: data = (pathlib.Path(local) / r["url"].rsplit("/", 1)[1]).read_bytes()
                else:
                    until = time.time() + wait
                    while True:
                        req = urllib.request.Request(r["url"], headers={"User-Agent": "opengp5-catalog-check", "Cache-Control": "no-cache"})
                        with urllib.request.urlopen(req, timeout=60) as f: data = f.read(3 * 1024 * 1024)
                        if hashlib.sha256(data).hexdigest() == r["sha256"] or time.time() >= until: break
                        print(f"wait {w}: {r['url']} doesn't match yet (still deploying?), again in 30 s", flush=True)
                        time.sleep(30)
            except Exception as x:
                errs.append(f"{w}: can't get {r['url']}: {x}"); continue
            if len(data) != r["size"]: errs.append(f"{w}: {len(data)} bytes, listed {r['size']}")
            if hashlib.sha256(data).hexdigest() != r["sha256"]: errs.append(f"{w}: the checksum doesn't match")
            meta = manifest.read(data)
            if not meta: errs.append(f"{w}: the image has no metadata (built without firmware.json?)"); continue
            if meta.get("id") != e["id"]: errs.append(f"{w}: the image says id '{meta.get('id')}'")
            if meta.get("version") != r["version"]: errs.append(f"{w}: the image says version {meta.get('version')}")
            want = meta.get("platform") or ".".join(str(meta.get("built_on", "")).split(".")[:2])
            if want != r["platform"]: errs.append(f"{w}: the image is for platform {want}, listed {r['platform']}")
            print(f"ok   {w}" if not any(x.startswith(w) for x in errs) else f"FAIL {w}")
    return errs


def main():
    a = sys.argv[1:]
    if not a or a[0] not in ("check", "verify", "index", "release"): sys.exit(__doc__)
    if a[0] == "release":
        if len(a) != 3: sys.exit(__doc__)
        data = pathlib.Path(a[1]).read_bytes()
        meta = manifest.read(data)
        if not meta: sys.exit(f"{a[1]}: no metadata in it (built without a firmware.json?)")
        plat = meta.get("platform") or ".".join(str(meta.get("built_on", "0.0")).split(".")[:2])
        print(json.dumps({"version": meta["version"], "platform": plat, "url": a[2],
                          "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}, indent=2))
        return
    errs, entries = load_all()
    if a[0] == "check":
        ids = [e["id"] for e in entries]
        print(f"{len(entries)} listings, {sum(len(e['releases']) for e in entries)} releases")
    elif a[0] == "verify":
        local = a[a.index("--local") + 1] if "--local" in a else None
        wait = int(a[a.index("--wait") + 1]) if "--wait" in a else 0
        ids = [x for x in a[1:] if not x.startswith("--") and x not in (local, str(wait))]
        errs += verify(entries, ids, local, wait)
    elif a[0] == "index":
        text = index_text(entries)
        if "--check" in a:
            if not INDEX.exists() or INDEX.read_text() != text:
                errs.append("docs/catalog.json is not up to date: run  python3 tools/catalog.py index  and commit it")
        elif not errs:
            INDEX.parent.mkdir(parents=True, exist_ok=True); INDEX.write_text(text); print(f"wrote {INDEX}")
    for x in errs: print("FAIL " + x)
    sys.exit(1 if errs else 0)


if __name__ == "__main__":
    main()
