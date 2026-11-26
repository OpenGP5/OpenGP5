// Copied from apps/launchpad/src/lib/device.js by web/remote/sync.mjs: edit the original, then run node web/remote/sync.mjs.
// The connected pedal: what it is running, and the reads and writes Launchpad does over the OpenGP5 platform's
// USB commands (platform/nds32/main.c: 7E 05 read flash, 7E 07 erase / 7E 08 program the store and data areas).
import { readPedalMeta, kindOf } from "./fwmeta.js";
import { find, buildSysex, parseSysex, CLASS_CMD, MODE_OPEN, MODE_STOCK, MODE_LOADER, MODE_SILENT } from "./gp5flash.js";
import { flashRead } from "./gp5nam.js";
import { STORE, STORE_SIZE, SECTOR, planSave, latest } from "./gp5store.js";

export { MODE_OPEN, MODE_STOCK, MODE_LOADER, MODE_SILENT };
export class DeviceError extends Error {}

const now = () => (typeof performance !== "undefined" ? performance.now() : Date.now());
const le32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];

export async function cmd(link, payload, timeout = 1000) {
  link.flush();
  await link.send(buildSysex(CLASS_CMD, 0, payload));
  const end = now() + timeout;
  while (now() < end) {
    const raw = await link.recv(Math.max(0, end - now()));
    if (raw === null) break;
    const p = parseSysex(raw);
    if (p && p.ok && p.payload[0] === payload[0] && p.payload[1] === payload[1]) return p.payload;
  }
  return null;
}

// ------------------------------------------------------------------ which firmware

// Firmwares built with OpenGP5, recognised by text in their code (works with every release so far). A
// platform that answers the identity command 7E 0D says it directly (platform/nds32/main.c).
export const FIRMWARES = {
  nam: { title: "NAM Loader", marks: ["push to load"] },
  dreamcatcher: { title: "Dreamcatcher", marks: ["DREAMCATCHER"] },
  tuner: { title: "Strobe tuner", marks: ["play a string"] },
  opengp5: { title: "OpenGP5 firmware", marks: [] },
};

const APP = 0x10000, APP_MAX = 0x168000, SCAN = 0x2000;

function findText(bytes, text) {
  const t = new TextEncoder().encode(text);
  outer: for (let i = 0; i + t.length <= bytes.length; i++) {
    for (let k = 0; k < t.length; k++) if (bytes[i + k] !== t[k]) continue outer;
    return i;
  }
  return -1;
}

// { kind, title, platform } of the open firmware on the pedal. progress(fraction) while it reads.
export async function identify(link, progress = () => {}) {
  let id = await cmd(link, [0x7E, 0x0D], 300);
  // Over Bluetooth the pedal runs OpenGP5 1.0, which always answers this; right after connecting its first replies
  // can be lost (the computer hasn't switched notifications on yet): ask again rather than scan its code (slow there)
  for (let k = 0; !id && link.bluetooth && k < 6; k++) id = await cmd(link, [0x7E, 0x0D], 1000);
  if (id && id.length >= 7 && id[2] === 1) {           // 7E 0D 1 <platform major minor patch> <name, zero-terminated>
    const name = new TextDecoder().decode(id.subarray(6)).split("\0")[0];
    let meta = await readPedalMeta(link).catch(() => null);         // 1.0: the firmware says what it is
    // a platform with the 1.0 API (0.7 and later) always has it: a missed answer (a busy moment) is asked again,
    // or Launchpad would take it for an older firmware (NAM Loader 1.3 showed its older Tones page)
    const api10 = id[3] > 0 || id[4] >= 7;
    for (let k = 0; !meta && api10 && k < 3; k++) meta = await readPedalMeta(link, 800).catch(() => null);
    if (meta) {
      const kind = kindOf(meta.id);
      return { kind, title: meta.name, name: meta.id, version: meta.version, meta, platform: `${id[3]}.${id[4]}.${id[5]}` };
    }
    const kind = Object.keys(FIRMWARES).find((k) => k === name) || "opengp5";
    // "opengp5" is the platform's default: a firmware that doesn't name itself (e.g. Dreamcatcher 1.1.0 built on a
    // newer platform). Recognise it from its code, as for platforms without the identity command.
    if (kind !== "opengp5" || name !== "opengp5")
      return { kind, title: FIRMWARES[kind].title, name, platform: `${id[3]}.${id[4]}.${id[5]}` };
    const byCode = await identifyByCode(link, progress);
    return { ...byCode, name, platform: `${id[3]}.${id[4]}.${id[5]}` };
  }
  if (link.bluetooth) throw new DeviceError("The pedal didn't answer over Bluetooth. Try again in a moment.");
  return identifyByCode(link, progress);
}

async function identifyByCode(link, progress) {
  // No identity command (every release up to platform 0.2.1): read the firmware's code until a firmware's
  // marks turn up, or 24 KB past the platform's version text, or blank flash. (The code does not say how long it
  // is, and older, longer firmwares leave their bytes behind it.)
  const code = new Uint8Array(APP_MAX);
  let have = 0, version = -1;
  const match = () => Object.entries(FIRMWARES).find(([, f]) => f.marks.length && f.marks.every((m) => findText(code.subarray(0, have), m) >= 0));
  while (have < APP_MAX) {
    const c = await flashRead(link, APP + have, SCAN);
    code.set(c, have); have += SCAN;
    progress(Math.min(0.95, have / 0x10000));
    if (version < 0) version = findText(code.subarray(0, have), "OPENGP5 ");
    const m = match();
    if (m) {                                           // the platform version is a little further on, usually
      for (let more = 0; version < 0 && more < 0x8000 && have < APP_MAX; more += SCAN) {
        const d = await flashRead(link, APP + have, SCAN);
        code.set(d, have); have += SCAN;
        version = findText(code.subarray(0, have), "OPENGP5 ");
        if (d.every((v) => v === 0xFF)) break;
      }
      progress(1);
      return { kind: m[0], title: m[1].title, platform: platformAt(code, version) };
    }
    if (c.every((v) => v === 0xFF) || (version >= 0 && have > version + 0x6000)) break;
  }
  progress(1);
  return { kind: "opengp5", title: FIRMWARES.opengp5.title, platform: platformAt(code, version) };
}

function platformAt(code, v) {
  return v >= 0 ? new TextDecoder().decode(code.subarray(v + 8, v + 20)).split("\0")[0] : null;
}

// Find the pedal: { name, mode } (mode: MODE_OPEN / MODE_STOCK / MODE_LOADER / MODE_SILENT), or null.
export async function locate(ports) {
  const [name, mode] = await find(ports);
  return name ? { name, mode } : null;
}

// ------------------------------------------------------------------ the store (open firmware)

export async function readStore(link) { return flashRead(link, STORE, STORE_SIZE); }

async function program(link, addr, bytes) {
  for (let i = 0; i < bytes.length; i += 56) {
    const part = bytes.subarray(i, i + 56);
    const r = await cmd(link, [0x7E, 0x08, ...le32(addr + i), ...part], 2000);
    if (!r || r.length < 3 || !r[2]) throw new DeviceError("The pedal refused a write to its settings store.");
  }
}

// Save a record the way the firmware would (gp5_store_save), then read it back. The firmware keeps its own
// copy in memory: it sees the new record after a restart.
export async function storeSave(link, id, data) {
  const img = await readStore(link);
  const plan = planSave(img, id, data);
  for (const e of plan.erase) {
    const r = await cmd(link, [0x7E, 0x07, ...le32(STORE + e)], 5000);
    if (!r || r.length < 3 || !r[2]) throw new DeviceError("The pedal refused to erase its settings store.");
  }
  // header last: a record is only valid once its first words are in
  await program(link, STORE + plan.at + 16, plan.bytes.subarray(16));
  await program(link, STORE + plan.at, plan.bytes.subarray(0, 16));
  const back = latest(await readStore(link)).get(id);
  if (!back || back.data.length !== data.length || back.data.some((v, i) => v !== data[i]))
    throw new DeviceError("The settings did not read back correctly. Nothing else was changed; try again.");
  return { reloaded: await storeChanged(link, id) };
}

// Tell the firmware a record changed (platform 0.2.2+, 7E 0E): true if it loaded it again, false if the pedal
// must be restarted for it (older platforms don't answer).
export async function storeChanged(link, id) {
  const r = await cmd(link, [0x7E, 0x0E, ...le32(id)], 500);
  return !!(r && r.length >= 3 && r[2] === 1);
}

// The controls (platform 0.5.2+, 7E 0F): { knob 0..1, foot, para } or null when the platform doesn't answer.
// A firmware's own settings, live (platform 0.5.4: fw_param_set / fw_param_get). set -> true if taken;
// read -> the values from `first` on (fewer when the firmware knows fewer), or null without the command.
export async function paramSet(link, id, value, timeout = 400) {
  const r = await cmd(link, [0x7E, 0x10, id & 0xFF, id >> 8, value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF, value >>> 24], timeout);
  return !!(r && r.length >= 3 && r[2] === 1);
}
export async function paramsRead(link, first, count, timeout = 400) {
  const out = [];
  for (let at = 0; at < count; at += 40) {
    const n = Math.min(40, count - at);
    const r = await cmd(link, [0x7E, 0x11, (first + at) & 0xFF, (first + at) >> 8, n], timeout);
    if (!r || r.length < 3 || r.length < 3 + 2 * r[2]) return out.length ? out : null;
    for (let i = 0; i < r[2]; i++) out.push(r[3 + 2 * i] | (r[4 + 2 * i] << 8));
    if (r[2] < n) break;
  }
  return out;
}

export async function readControls(link, timeout = 300) {
  const r = await cmd(link, [0x7E, 0x0F], timeout);
  return r && r.length >= 6 && r[2] === 1 ? { knob: r[3] / 255, foot: !!r[4], para: !!r[5] } : null;
}

// USB audio (OpenGP5 1.0, 7E 19): whether the pedal's own sound reaches its outputs while the computer plays into the
// pedal (off: a DAW monitoring the pedal isn't heard twice), and whether the computer is playing. null: no USB audio.
export async function readDirect(link, timeout = 400) {
  const r = await cmd(link, [0x7E, 0x19], timeout);
  return r && r.length >= 5 && r[2] === 1 ? { direct: !!r[3], playing: !!r[4] } : null;
}
export async function setDirect(link, on, timeout = 400) {
  const r = await cmd(link, [0x7E, 0x19, on ? 1 : 0], timeout);
  return r && r.length >= 5 && r[2] === 1 ? { direct: !!r[3], playing: !!r[4] } : null;
}

export { SECTOR };
