#!/usr/bin/env python3
"""Build what stays at the old address, https://opengp5.github.io/OpenGP5/, from the built site.

  python3 site/legacy.py site/public site/legacy

The site lives at https://opengp5.github.io/ now. Released Launchpads and catalog entries still download from the
old address, so the files they use are copied as they are: catalog.json, the web flasher (with its firmware images)
and assets/. Every other page becomes a small page that sends the reader to the same path at the new address.
"""
import html, shutil, sys
from pathlib import Path

NEW = "https://opengp5.github.io/"
KEEP = ["catalog.json", "flasher", "assets"]          # served as they are: apps and catalog entries fetch these

src, dst = Path(sys.argv[1]), Path(sys.argv[2])
shutil.rmtree(dst, ignore_errors=True); dst.mkdir(parents=True)
for k in KEEP:
    s = src / k
    if s.is_dir(): shutil.copytree(s, dst / k)
    elif s.exists(): shutil.copy2(s, dst / k)
    else: sys.exit(f"legacy: {k} is missing from the built site")

n = 0
for page in src.rglob("index.html"):
    rel = page.parent.relative_to(src).as_posix()
    if rel.split("/")[0] in KEEP: continue
    url = NEW + ("" if rel == "." else rel + "/")
    out = dst / page.relative_to(src)
    out.parent.mkdir(parents=True, exist_ok=True)
    u = html.escape(url, quote=True)
    out.write_text(f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<title>OpenGP5 has moved</title>
<link rel="canonical" href="{u}">
<meta name="robots" content="noindex">
<meta http-equiv="refresh" content="0; url={u}">
<script>location.replace({url!r} + location.hash)</script>
</head><body><p>OpenGP5's site has moved to <a href="{u}">{u}</a>.</p></body></html>
""")
    n += 1
(dst / ".nojekyll").touch()
print(f"legacy: {n} pages redirect to {NEW}; kept {', '.join(KEEP)}")
