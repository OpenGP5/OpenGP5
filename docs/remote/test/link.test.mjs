// The Web Bluetooth link against a fake BLE-MIDI characteristic: the shape Launchpad's code expects, and the whole
// page logic (identify, descriptions, live changes, presets) through it, as with the pedal.
import { test } from "node:test";
import assert from "node:assert/strict";
import { bleLink, connectPedal, bluetoothSupport } from "../webble.js";
import { FakeBlePedal, DREAMCATCHER, TREMOLO } from "../fakeble.js";
import { Remote } from "../remote.js";
import { cmd, identify } from "../lib/device.js";
import { readDescriptions } from "../lib/params.js";
import { buildSysex, CLASS_CMD } from "../lib/gp5flash.js";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

const quick = { extra: 50 };

test("a link has the shape gp5flash / device.js use, and a command goes out as BLE-MIDI packets of 20 bytes at most", async () => {
  const fake = new FakeBlePedal(), link = await bleLink(fake.characteristic, quick);
  assert.equal(link.bluetooth, true);
  for (const k of ["send", "recv", "flush", "close"]) assert.equal(typeof link[k], "function");
  assert.ok(fake.notifying, "notifications switched on");
  const r = await cmd(link, [0x7E, 0x0D], 300);
  assert.deepEqual(Array.from(r.subarray(0, 3)), [0x7E, 0x0D, 1]);
  assert.ok(fake.writes.length >= 1 && fake.writes.every((w) => w.length <= 20 && w[0] & 0x80));
  await link.close();
  assert.equal(fake.notifying, false);
  assert.equal(await link.recv(10), null, "closed: nothing more");
});

test("recv waits its timeout plus the Bluetooth extra; flush drops what's waiting", async () => {
  const fake = new FakeBlePedal(DREAMCATCHER, { latency: 60 }), link = await bleLink(fake.characteristic, { extra: 100 });
  await link.send(buildSysex(CLASS_CMD, 0, [0x7E, 0x0D]));
  assert.ok(await link.recv(0), "a reply 60 ms later still arrives inside the extra wait");
  await link.send(buildSysex(CLASS_CMD, 0, [0x7E, 0x0D]));
  await new Promise((r) => setTimeout(r, 90));
  link.flush();
  const t0 = Date.now();
  assert.equal(await link.recv(0), null);
  assert.ok(Date.now() - t0 >= 90);
});

test("writes are one at a time (the browser refuses overlapping GATT operations)", async () => {
  const fake = new FakeBlePedal(), chr = fake.characteristic;
  let busy = false, overlaps = 0;
  const orig = chr.writeValueWithoutResponse;
  chr.writeValueWithoutResponse = async (v) => { if (busy) overlaps++; busy = true; await new Promise((r) => setTimeout(r, 2)); busy = false; return orig.call(chr, v); };
  const link = await bleLink(chr, quick);
  const long = buildSysex(CLASS_CMD, 0, [0x7E, 0x17, 4, 1, ...new TextEncoder().encode("twelve chars")]);
  await Promise.all([link.send(long), link.send(long), link.send(long)]);
  assert.equal(overlaps, 0);
  assert.equal(fake.cmds.filter((c) => c === 0x17).length, 3, "all three arrived whole");
});

test("notes and controllers from the pedal go to onMidi, not to the command replies", async () => {
  const fake = new FakeBlePedal(), seen = [];
  const link = await bleLink(fake.characteristic, { ...quick, onMidi: (d) => seen.push(Array.from(d)) });
  for (const fn of fake.listeners) fn({ target: { value: new DataView(Uint8Array.from([0x80, 0x80, 0xC0, 3, 0x80, 0xB0, 20, 64]).buffer) } });
  assert.deepEqual(seen, [[0xC0, 3], [0xB0, 20, 64]]);
  assert.equal(await link.recv(0), null);
});

test("identify over Bluetooth asks again when the first answers are lost", async () => {
  const fake = new FakeBlePedal(DREAMCATCHER, { dropFirst: 2 }), link = await bleLink(fake.characteristic, quick);
  const fw = await identify(link);
  assert.equal(fw.title, "Dreamcatcher"); assert.equal(fw.version, "1.4.0"); assert.equal(fw.name, "dreamcatcher");
});

test("Dreamcatcher: descriptions by group, live changes, its own presets (load, save)", async () => {
  const fake = new FakeBlePedal(DREAMCATCHER), link = await bleLink(fake.characteristic, quick);
  const r = await new Remote(link).load();
  assert.equal(r.fw.title, "Dreamcatcher");
  assert.equal(r.desc.length, 36);
  assert.deepEqual(r.groups().map((g) => g.name), ["Main", "Loop", "Wet", "Mod", "Color", "Grain", "Reverb"]);
  assert.equal(r.vals[1], 128);
  assert.equal(r.text(r.desc[0]), "On");
  assert.match(r.text(r.desc[1]), /50/, "Mix in Dreamcatcher's own words");
  // live: the last of quick changes wins, read-only ones are refused here
  r.set(1, 10); r.set(1, 20); r.set(1, 300);
  assert.equal(r.vals[1], 255, "clamped");
  assert.equal(r.set(3, 1), false, "Catch is the foot's");
  await r.sending;
  assert.equal(fake.vals[1], 255);
  // presets through DC_P_PRESET / DC_P_SAVE
  assert.equal(r.presetKind, "firmware");
  assert.deepEqual(r.presets.used, [true, false, true, false, false, false, false, false]);
  assert.equal(r.presets.current, 0);
  fake.saved[2][1] = 77;
  assert.ok(await r.loadPreset(2));
  assert.equal(r.vals[1], 77); assert.equal(r.presets.current, 2); assert.equal(r.presets.dirty, false);
  r.set(1, 99); await r.sending;
  await r.poll();
  assert.equal(r.presets.dirty, true);
  assert.ok(await r.savePreset(5));
  assert.deepEqual(fake.saves, [5]); assert.equal(fake.saved[5][1], 99);
  assert.equal(r.presets.used[5], true); assert.equal(r.presets.current, 5);
  assert.equal(await r.renamePreset(5, "x"), false, "no names through the protocol");
});

test("a firmware with platform presets: names, load, save, rename; custom-unit texts asked of the pedal", async () => {
  const fake = new FakeBlePedal(TREMOLO), link = await bleLink(fake.characteristic, quick);
  const r = await new Remote(link).load();
  assert.equal(r.presetKind, "platform");
  assert.equal(r.presets.count, 4); assert.deepEqual(r.presets.names.slice(0, 3), ["Clouds", "", "Shimmer"]);
  assert.equal(r.text(r.desc[1]), "50 %");
  assert.equal(r.text(r.desc[3]), "+0 dB".replace("+", ""), "dB10 from its unit");
  assert.equal(r.text(r.desc[0]), "500", "custom unit: the pedal had no text, the number");
  assert.ok(await r.renamePreset(2, "Wobble"));
  assert.equal(r.presets.names[2], "Wobble");
  r.set(1, 80); await r.sending;
  assert.ok(await r.savePreset(1)); assert.equal(fake.saved[1][1], 80);
});

test("a pedal that changes on its own (knob, footswitch) shows up at the next poll, but not over a value just set", async () => {
  let t = 0;
  const fake = new FakeBlePedal(DREAMCATCHER), link = await bleLink(fake.characteristic, quick);
  const r = await new Remote(link, { now: () => t }).load();
  r.set(9, 10); await r.sending;
  fake.vals[9] = 200; fake.vals[12] = 33;
  assert.ok(await r.poll());
  assert.equal(r.vals[12], 33); assert.equal(r.vals[9], 10, "held");
  t += 2000; await r.poll();
  assert.equal(r.vals[9], 200);
});

test("connectPedal: the picker, the MIDI service and characteristic, and the drop is reported", async () => {
  const fake = new FakeBlePedal();
  let gone = 0;
  const c = await connectPedal({ bluetooth: fake.bluetooth(), onGone: () => gone++ });
  assert.equal(c.device.name, "OpenGP5");
  const descs = await readDescriptions(c.link);
  assert.equal(descs.length, 36);
  fake.drop();
  assert.equal(gone, 1);
  const again = new FakeBlePedal(), c2 = await connectPedal({ bluetooth: again.bluetooth(), onGone: () => gone++ });
  await c2.disconnect();
  assert.equal(gone, 1, "our own disconnect isn't a drop");
});

test("no Web Bluetooth: iPhone is sent to Bluefy / BLE Link, Android to Chrome", () => {
  const ios = bluetoothSupport({ userAgent: "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) Safari" });
  assert.equal(ios.platform, "ios"); assert.match(ios.text, /Bluefy/); assert.match(ios.text, /BLE Link/); assert.match(ios.text, /Safari/);
  const ipad = bluetoothSupport({ userAgent: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Safari", maxTouchPoints: 5 });
  assert.equal(ipad.platform, "ios");
  assert.equal(bluetoothSupport({ userAgent: "Mozilla/5.0 (Linux; Android 14) Firefox" }).platform, "android");
  assert.equal(bluetoothSupport({ userAgent: "Chrome", bluetooth: { requestDevice() {} } }, true), null);
  assert.match(bluetoothSupport({ userAgent: "Chrome", bluetooth: { requestDevice() {} } }, false).text, /https/);
});

const full = (() => { try { readFileSync(new URL("../../../apps/launchpad/src/lib/device.js", import.meta.url)); return true; } catch { return false; } })();
test("the shared code in lib/ is up to date with its originals", { skip: !full && "the originals aren't in this copy" }, async () => {
  const { execFileSync } = await import("node:child_process");
  execFileSync(process.execPath, [fileURLToPath(new URL("../sync.mjs", import.meta.url)), "--check"]);
  assert.ok(readFileSync(new URL("../lib/icons.js", import.meta.url), "utf8").includes('"bluetooth"'));
});
