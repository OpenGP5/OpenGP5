// The shared catalog module (catalog.js): which listings show, which can be downloaded by a page, matching a file by
// its checksum, and the checks on a download (size, checksum, the image's own id and version).
//   node --test test/catalog.test.mjs
import { test } from "node:test";
import assert from "node:assert/strict";
import * as C from "../catalog.js";

// a tiny "image": a metadata block as platform/manifest.py writes it, inside other bytes
function image(meta) {
  const json = new TextEncoder().encode(JSON.stringify(meta));
  const body = new Uint8Array(Math.ceil((json.length + 1) / 4) * 4); body.set(json);
  const crcTable = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  let c = 0xFFFFFFFF; for (const v of json) c = crcTable[(c ^ v) & 0xFF] ^ (c >>> 8); c = (c ^ 0xFFFFFFFF) >>> 0;
  const out = new Uint8Array(64 + 12 + body.length + 4 + 64), dv = new DataView(out.buffer);
  out.set([0x47, 0x50, 0x35, 0x4D, 0x45, 0x54, 0x41, 0x31], 64); dv.setUint32(72, body.length, true);
  out.set(body, 76); dv.setUint32(76 + body.length, c, true);
  return out;
}
const bytes = image({ id: "my-delay", version: "1.0.0", name: "My Delay" });
const sha = await C.sha256(bytes);
const rel = (o = {}) => ({ version: "1.0.0", platform: "0.8", url: "https://github.com/a/b/releases/download/v1/x.bin", sha256: sha, size: bytes.length, ...o });
const idx = { version: 1, firmwares: [
  { id: "my-delay", name: "My Delay", author: "a", repo: "https://github.com/a/b", description: "d", releases: [rel()] },
  { id: "claimed", name: "Claimed", author: "b", repo: "https://github.com/b/c", description: "no release yet", releases: [] },
  { id: "zed", name: "Zed", author: "OpenGP5", repo: "https://github.com/OpenGP5/Zed", description: "d", official: true,
    releases: [rel({ url: C.SITE + "flasher/firmware/z.bin", sha256: "0".repeat(64) })] },
  { id: "bad", name: "Bad", author: "c", repo: "https://github.com/c/d", description: "d", releases: [rel({ url: "ftp://x/y.bin" })] },
] };

test("listings with a release show, OpenGP5's own first; bad URLs and empty listings don't", () => {
  assert.deepEqual(C.entries(idx).map((e) => e.id), ["zed", "my-delay"]);
  assert.equal(C.entries(idx)[0].official, true);
  assert.equal(C.entries(null).length, 0);
});
test("pages can download OpenGP5's own files and their own site's, not GitHub release files", () => {
  const [zed, mine] = C.entries(idx);
  assert.equal(C.fetchable(zed.latest), true);
  assert.equal(C.fetchable(mine.latest), false);
  assert.equal(C.fetchable({ url: "http://localhost:8767/x.bin" }, "http://localhost:8767"), true);
});
test("a file is found by its checksum", () => {
  assert.equal(C.findBySha(idx, sha).entry.id, "my-delay");
  assert.equal(C.findBySha(idx, "1".repeat(64)), null);
});
test("a download that matches passes", async () => {
  const e = C.entries(idx)[1];
  assert.deepEqual(await C.check(bytes, e, e.latest), []);
});
test("a download that doesn't match is refused, with the reasons", async () => {
  const e = C.entries(idx)[1];
  const other = image({ id: "my-effect", version: "0.1.0" });
  const errs = await C.check(other, e, e.latest);
  assert.ok(errs.some((x) => x.includes("checksum")));
  assert.ok(errs.some((x) => x.includes("'my-effect'")));
  assert.ok(errs.some((x) => x.includes("version 0.1.0")));
  assert.ok((await C.check(new Uint8Array(bytes.length), e, e.latest)).some((x) => x.includes("no metadata")));
});
test("the caution says who made it unless it's OpenGP5's", () => {
  const [zed, mine] = C.entries(idx);
  assert.match(C.caution(mine), /Not reviewed by OpenGP5: made by a\./);
  assert.match(C.caution(zed), /OpenGP5's own/);
});
