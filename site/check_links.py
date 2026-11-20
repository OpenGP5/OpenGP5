#!/usr/bin/env python3
"""Check every link and image on the built site: the page it points to exists, and so does the #anchor.

  python3 site/check_links.py site/public [/OpenGP5/]

Links to other sites aren't fetched. The web flasher (flasher/) is its own app and is skipped.
"""
import os, sys
from html.parser import HTMLParser
from pathlib import Path

root = Path(sys.argv[1]); base = sys.argv[2] if len(sys.argv) > 2 else "/OpenGP5/"
pages, links = {}, []


class Page(HTMLParser):
    def __init__(self, f):
        super().__init__(); self.f = f; self.ids = set()

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if "id" in a: self.ids.add(a["id"])
        ref = a.get("href") if tag == "a" else a.get("src") if tag == "img" else None
        if ref: links.append((self.f, ref))


for f in root.rglob("*.html"):
    if f.relative_to(root).parts[0] == "flasher": continue
    p = Page(f); p.feed(f.read_text(errors="ignore")); pages[f] = p.ids

bad = []
for f, ref in links:
    if ref.startswith(("http://", "https://", "mailto:", "javascript:")) or ref == "#": continue
    path, _, frag = ref.partition("#")
    if not path: target = f
    else:
        if path.startswith(base): path = path[len(base):]
        elif path.startswith("/"): bad.append((f, ref, "outside the site")); continue
        else: path = os.path.normpath(os.path.join(str(f.parent.relative_to(root)), path))
        target = root / path
        if target.is_dir(): target = target / "index.html"
        if not target.exists(): bad.append((f, ref, "no such page")); continue
    if frag and target.suffix == ".html" and target in pages and frag not in pages[target]:
        bad.append((f, ref, "no such anchor"))

for f, ref, why in bad: print(f"{f.relative_to(root)}: {ref} ({why})")
print(f"{len(links)} links on {len(pages)} pages, {len(bad)} broken")
sys.exit(1 if bad else 0)
