// BLE-MIDI framing: SysEx split over 20-byte packets and joined again, timestamps, running status, real time.
import { test } from "node:test";
import assert from "node:assert/strict";
import { encodeSysex, encodeMessages, Decoder, PACKET } from "../blemidi.js";
import { buildSysex, CLASS_CMD } from "../lib/gp5flash.js";

const sx = (n) => Uint8Array.from([0xF0, ...Array.from({ length: n }, (_, i) => i & 0x7F), 0xF7]);
const join = (packets) => { const d = new Decoder(); return packets.flatMap((p) => d.push(p)); };

test("a short SysEx fits one packet: header, timestamp, F0 ... timestamp, F7", () => {
  const p = encodeSysex([0xF0, 1, 2, 3, 0xF7], 0x1234 & 0x1FFF);
  assert.equal(p.length, 1);
  const ms = 0x1234 & 0x1FFF;
  assert.deepEqual(Array.from(p[0]), [0x80 | (ms >> 7), 0x80 | (ms & 0x7F), 0xF0, 1, 2, 3, 0x80 | (ms & 0x7F), 0xF7]);
});

test("every length splits into packets of at most 20 bytes and joins back the same", () => {
  for (let n = 0; n < 120; n++) {
    const m = sx(n), packets = encodeSysex(m, 777);
    assert.ok(packets.every((p) => p.length <= PACKET && p.length >= 2), `length ${n}`);
    assert.ok(packets.every((p) => p[0] & 0x80), "each starts with a header");
    assert.ok(packets.slice(1).every((p) => !(p[1] & 0x80) || p[1] === 0x80 + (777 & 0x7F)), "continuations: header then data");
    const last = packets[packets.length - 1];
    assert.equal(last[last.length - 1], 0xF7); assert.ok(last[last.length - 2] & 0x80, "F7 has a timestamp before it");
    const got = join(packets);
    assert.equal(got.length, 1, `length ${n}`);
    assert.deepEqual(Array.from(got[0].data), Array.from(m));
  }
});

test("a protocol command (parameter set: 26 bytes) is two packets", () => {
  const m = buildSysex(CLASS_CMD, 0, [0x7E, 0x10, 5, 0, 200, 0, 0, 0]);
  assert.equal(m.length, 26);
  const p = encodeSysex(m, 0);
  assert.deepEqual(p.map((x) => x.length), [20, 10]);
  assert.deepEqual(Array.from(join(p)[0].data), Array.from(m));
});

test("timestamps: 13 bits from header and timestamp byte; the low part wrapping inside a packet counts on", () => {
  const d = new Decoder();
  const [a] = d.push([0x80 | 5, 0x80 | 100, 0x90, 60, 100]);
  assert.equal(a.time, (5 << 7) | 100); assert.deepEqual(Array.from(a.data), [0x90, 60, 100]);
  const [b, c] = d.push([0x80 | 5, 0x80 | 120, 0xB0, 7, 1, 0x80 | 3, 0xB0, 7, 2]);
  assert.equal(b.time, (5 << 7) | 120); assert.equal(c.time, (6 << 7) | 3);
  assert.equal(encodeSysex([0xF0, 0xF7], 0x2000 + 3)[0][1], 0x80 | 3, "times wrap at 13 bits");
});

test("running status: data after a timestamp, and more data after a full message, reuse the last status", () => {
  const d = new Decoder();
  const got = d.push([0x80, 0x81, 0x90, 60, 100, 62, 101, 0x82, 64, 102, 0x83, 0xC0, 5, 6]).map((m) => Array.from(m.data));
  assert.deepEqual(got, [[0x90, 60, 100], [0x90, 62, 101], [0x90, 64, 102], [0xC0, 5], [0xC0, 6]]);
  assert.deepEqual(d.push([0x80, 0x81, 1, 2]).map((m) => Array.from(m.data)), [[0xC0, 1], [0xC0, 2]], "kept across packets");
});

test("real-time bytes inside a SysEx come out on their own; the SysEx stays whole", () => {
  const d = new Decoder();
  const a = d.push([0x80, 0x80, 0xF0, 1, 2, 0x81, 0xF8, 3]);
  assert.deepEqual(a.map((m) => Array.from(m.data)), [[0xF8]]);
  const b = d.push([0x80, 4, 5, 0x82, 0xF7]);
  assert.deepEqual(b.map((m) => Array.from(m.data)), [[0xF0, 1, 2, 3, 4, 5, 0xF7]]);
});

test("a new status ends an unfinished SysEx (dropped); not-BLE-MIDI packets are ignored", () => {
  const d = new Decoder();
  assert.deepEqual(d.push([0x80, 0x80, 0xF0, 1, 2]), []);
  assert.deepEqual(d.push([0x80, 0x80, 0xB0, 1, 2]).map((m) => Array.from(m.data)), [[0xB0, 1, 2]]);
  assert.deepEqual(d.push([0x01, 0x80, 0x90, 1, 2]), []);
  assert.deepEqual(d.push([0x80]), []);
});

test("channel messages pack into packets with a timestamp each; SysEx among them keeps its own packets", () => {
  const msgs = [[0xC0, 3], [0xB0, 20, 64], [0xF0, 1, 2, 0xF7], ...Array.from({ length: 8 }, (_, i) => [0xB0, i, i])];
  const p = encodeMessages(msgs, 50);
  assert.ok(p.every((x) => x.length <= PACKET));
  assert.deepEqual(join(p).map((m) => Array.from(m.data)), msgs);
});

test("the pedal's parser (ble.c midi_in) gets the same bytes: timestamps out, everything else in order", () => {
  // ble.c: a byte with bit 7 set is a timestamp unless it follows one
  const pedal = (packets) => packets.flatMap((p) => { const out = []; let after = false;
    for (let i = 1; i < p.length; i++) { const b = p[i]; if (b & 0x80 && !after) { after = true; continue; } after = false; out.push(b); } return out; });
  const m = buildSysex(CLASS_CMD, 0, [0x7E, 0x17, 4, 1, ...new TextEncoder().encode("Long name 12")]);
  assert.deepEqual(pedal(encodeSysex(m, 1000)), Array.from(m));
});
