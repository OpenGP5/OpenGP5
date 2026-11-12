// Tests for gp5flash.js against a simulated pedal (the same model as tools/test_gp5tool.py's FakePedal).
//   node --test web/flasher/test/
// Golden check: the frames for a given image must equal what tools/gp5tool.py sends (golden.json, written
// by tools/web_flasher_golden.py).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync, existsSync } from "node:fs";
import * as F from "../gp5flash.js";
import { FakePedal } from "./fakepedal.mjs";

Object.assign(F.Timing, { reply: 10, ack: 10, begin: 10, final: 10, beginSettle: 0, settle: 0, retryDelay: 0, reenum: 60, poll: 1 });

// smallest sections the checks accept (as tools/test_gp5tool.py's tiny_app / tiny_dsp)
function tinyApp(n = 0x200) {
  const a = new Uint8Array(n).fill(0xFF);
  for (let o = 0; o < 0xA4; o += 4) { const w = ((0x48 << 24) | ((0x108 - o) >> 1)) >>> 0; a.set([w >>> 24, (w >> 16) & 255, (w >> 8) & 255, w & 255], o); }
  a.set([0xC9, 0xBD, 0xBE, 0xB0], 0xC0);
  return a;
}
function tinyDsp(len = 0xC348) {
  const d = new Uint8Array(len).fill(0xFF);
  d.set(new TextEncoder().encode("V106"), 8); d.set([0x00, 0x44, 0, 0], 12); d.fill(0, 16, 0x400);
  d.set([0x00, 0x80, 0x00, 0x20, 0x09, 0x44, 0, 0], 0x400); d.fill(0, 0x408, 0x508);
  return d;
}
function makeImage(sections) {          // as gp5tool.make_image
  const parts = F.LAYOUT.filter(([t]) => sections[t]).map(([t, flash]) => [t, flash, sections[t]]);
  const total = 0x38 + 16 * parts.length + parts.reduce((s, p) => s + p[2].length, 0);
  const img = new Uint8Array(total), dv = new DataView(img.buffer);
  dv.setUint32(0, 0x57465448, true); dv.setUint16(6, 1, true); dv.setUint32(8, total, true);
  img.set(new TextEncoder().encode("GP-5"), 0x0C); img.set([0x56, 0, 0, 0], 0x1C); img[0x22] = parts.length;
  let off = 0, pos = 0x38 + 16 * parts.length;
  parts.forEach(([t, flash, d], i) => {
    const o = 0x38 + 16 * i; dv.setUint16(o, F.storedCrc(d), true); img[o + 3] = t;
    dv.setUint32(o + 4, flash, true); dv.setUint32(o + 8, off, true); dv.setUint32(o + 12, d.length, true);
    img.set(d, pos); pos += d.length; off += d.length;
  });
  dv.setUint32(0x24, off, true);
  return img;
}
const SMALL = [[0x63, tinyDsp()], [0x65, new Uint8Array(5000)]];
const say = () => {};

test("images: a partial image is accepted, in the pedal's order", () => {
  const { sections, lines } = F.inspect(makeImage({ 0x62: tinyApp(), 0x63: tinyDsp() }));
  assert.deepEqual(sections.map(([t]) => t), [0x63, 0x62]);
  assert.deepEqual(lines.map((l) => l.name), ["dsp", "app"]);
});

test("images: corrupt, truncated, foreign and unbootable images are refused", () => {
  const good = makeImage({ 0x62: tinyApp(), 0x63: tinyDsp() });
  const flip = good.slice(); flip[good.length - 10] ^= 1;
  for (const bad of [flip, good.slice(0, -1), new TextEncoder().encode("hello".repeat(20)), new Uint8Array(0)])
    assert.throws(() => F.inspect(bad), F.Problem);
  assert.throws(() => F.inspect(makeImage({ 0x62: new Uint8Array(4096) })), /would not boot/);
});

test("images: the release images and Valeton's image (when present) check out like in gp5tool", () => {
  const g = new URL("./golden.json", import.meta.url);
  if (!existsSync(g)) return;
  for (const [rel, want] of Object.entries(JSON.parse(readFileSync(g)).inspect)) {
    const path = new URL("../../../" + rel, import.meta.url);           // paths are relative to the repo root
    if (!existsSync(path)) continue;
    const got = F.inspect(new Uint8Array(readFileSync(path))).lines.map((l) => [l.name, l.bytes, l.stock]);
    assert.deepEqual(got, want, path);
  }
});

test("wire: every frame equals gp5tool's for the same section", () => {
  const g = new URL("./golden.json", import.meta.url);
  assert.ok(existsSync(g), "run tools/web_flasher_golden.py first");
  const golden = JSON.parse(readFileSync(g));
  const data = Uint8Array.from(Buffer.from(golden.section_hex, "hex"));
  const got = F.buildUpdateStream(data, golden.type).map(([, m]) => Buffer.from(m).toString("hex"));
  assert.deepEqual(got, golden.frames_hex);
});

test("flash: from the stock firmware; data arrives exactly, image ended, restart command sent", async () => {
  const p = new FakePedal(F.MODE_STOCK);
  assert.equal(await F.flash(p, SMALL, say), F.MODE_STOCK);
  for (const [t, d] of SMALL) assert.deepEqual(p.written[t], d);
  assert.ok(p.ended); assert.ok(p.sentCmds.includes("17,111"));          // 11 6F
});

test("flash: from the open firmware, app section, comes back as open firmware", async () => {
  const p = new FakePedal(F.MODE_OPEN);
  assert.equal(await F.flash(p, [[0x62, tinyApp(40000)]], say), F.MODE_OPEN);
});

test("flash: already in update mode, no restart command", async () => {
  const p = new FakePedal(F.MODE_LOADER);
  await F.flash(p, SMALL, say);
  assert.ok(!p.sentCmds.includes("17,111"));
});

test("flash: refused packets are repeated and the data is still exact", async () => {
  const p = new FakePedal(F.MODE_STOCK, { nakEvery: 97 });
  const log = [];
  await F.flash(p, SMALL, (s) => log.push(s));
  for (const [t, d] of SMALL) assert.deepEqual(p.written[t], d);
  assert.ok(log.some((l) => l.includes("repeated")));
});

test("flash: a stall at the erase says nothing was written", async () => {
  const p = new FakePedal(F.MODE_LOADER, { stallBegin: true });
  await assert.rejects(F.flash(p, SMALL, say), (e) => e instanceof F.Problem && /Nothing was written/.test(e.message));
  assert.deepEqual(p.written, {});
});

test("flash: dying mid-section reports INCOMPLETE and never ends the image", async () => {
  const p = new FakePedal(F.MODE_LOADER, { dieAt: 500 });
  await assert.rejects(F.flash(p, SMALL, say), (e) => /INCOMPLETE/.test(e.message) && /Recovery/.test(e.message));
  assert.ok(!p.ended);
});

test("flash: no pedal, and a pedal that does not answer", async () => {
  const p = new FakePedal(); p.present = false;
  await assert.rejects(F.flash(p, SMALL, say), /No GP-5 found/);
  const m = new FakePedal(F.MODE_STOCK, { mute: true });
  await assert.rejects(F.flash(m, SMALL, say), /did not go into update mode/);
});

test("flash: staying in update mode afterwards, and a silent app afterwards, are reported as such", async () => {
  assert.equal(await F.flash(new FakePedal(F.MODE_STOCK, { stayInLoader: true }), SMALL, say), F.MODE_LOADER);
  assert.equal(await F.flash(new FakePedal(F.MODE_STOCK, { after: F.MODE_SILENT }), SMALL, say), F.MODE_SILENT);
});

test("ports: input and output names pair up across systems", () => {
  assert.equal(F.portBase("GP-5 Open FW:GP-5 Open FW MIDI 1 16:0"), "GP-5 Open FW");
  assert.equal(F.portBase("GP-5 Open FW 2"), "GP-5 Open FW");
  assert.equal(F.portBase("GP-5"), "GP-5");
});
