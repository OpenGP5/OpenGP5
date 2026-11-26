// Copied from apps/launchpad/src/lib/gp5store.js by web/remote/sync.mjs: edit the original, then run node web/remote/sync.mjs.
// The OpenGP5 platform store (platform/nds32/gp5_store.c), read and written from the computer, and what the
// firmwares keep in it. The store is two 4 KB flash sectors at 0x160000 holding a log of records:
//   [magic 'GP5S' u32][sequence u32][firmware id u32][length u16][CRC-16 u16][data, padded to 4 bytes with FF]
// The newest valid record per id wins. Writing appends exactly as gp5_store_save does (same sector choice,
// sequence number and padding), so the firmware reads a record written here as one of its own.

export const STORE = 0x160000, STORE_SIZE = 0x2000, SECTOR = 0x1000, MAGIC = 0x53355047, HDR = 16, STORE_MAX = 1024;

export const fourcc = (s) => (s.charCodeAt(0) | (s.charCodeAt(1) << 8) | (s.charCodeAt(2) << 16) | (s.charCodeAt(3) << 24)) >>> 0;
export const idName = (id) => String.fromCharCode(id & 255, (id >>> 8) & 255, (id >>> 16) & 255, id >>> 24);

export function crc16(bytes) {
  let c = 0xFFFF;
  for (const b of bytes) {
    c ^= b << 8;
    for (let i = 0; i < 8; i++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xFFFF : (c << 1) & 0xFFFF;
  }
  return c;
}
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
const recSize = (len) => HDR + ((len + 3) & ~3);
const erased = (b, off, n) => { for (let i = 0; i < n; i++) if (b[off + i] !== 0xFF) return false; return true; };

// Every valid record, and per sector where the free space starts (as scan() in gp5_store.c).
export function parseStore(img) {
  const records = [], end = [0, 0];
  let top = null;
  for (let s = 0; s < 2; s++) {
    let off = 0;
    while (off + HDR <= SECTOR) {
      const a = s * SECTOR + off, m = u32(img, a);
      if (m === 0xFFFFFFFF) break;
      const len = u32(img, a + 12) & 0xFFFF;
      if (m !== MAGIC || len > STORE_MAX || off + recSize(len) > SECTOR) { off = SECTOR; break; }
      const data = img.slice(a + HDR, a + HDR + len);
      if (crc16(data) === u32(img, a + 12) >>> 16) {
        const seq = u32(img, a + 4);
        if (top === null || seq > top) top = seq;
        records.push({ addr: a, seq, id: u32(img, a + 8), len, data });
      }
      off += recSize(len);
    }
    end[s] = off;
  }
  return { records, end, top };
}

// The newest record of each firmware id: { id: data }
export function latest(img) {
  const best = new Map();
  for (const r of parseStore(img).records) if (!best.has(r.id) || r.seq > best.get(r.id).seq) best.set(r.id, r);
  return best;
}

// What gp5_store_save(id, data) would do to this store image: { erase: [sector offsets], at, bytes } (the bytes to
// program at store offset `at`: the new record, or, on a sector switch, the other ids' newest records copied
// across followed by the new record). Byte for byte what platform/nds32/gp5_store.c writes.
export function planSave(img, id, data) {
  if (data.length > STORE_MAX) throw new Error("record too large");
  const { records, end, top } = parseStore(img), size = recSize(data.length);
  let newest = null;
  for (const r of records) if (!newest || r.seq > newest.seq) newest = r;
  let seq = newest ? (top + 1) >>> 0 : 1;
  let s = newest ? (newest.addr >= SECTOR ? 1 : 0) : 0;
  if (newest && end[s] + size <= SECTOR && erased(img, s * SECTOR + end[s], size))
    return { erase: [], at: s * SECTOR + end[s], bytes: record(seq, id, data) };
  // sector switch: the newest record of every other id, oldest first, as many as leave room for the new one
  const keep = [];
  for (const r of records) {                       // (records are in walk order: sector 0, then sector 1)
    if (r.id === id) continue;
    const k = keep.findIndex((x) => x.id === r.id);
    if (k < 0) { if (keep.length < 16) keep.push(r); } else if (r.seq > keep[k].seq) keep[k] = r;
  }
  for (let k = 1; k < keep.length; k++) for (let i = k; i > 0 && keep[i - 1].seq > keep[i].seq; i--) [keep[i - 1], keep[i]] = [keep[i], keep[i - 1]];
  let used = keep.reduce((n, r) => n + recSize(r.len), 0);
  while (keep.length && used + size > SECTOR) used -= recSize(keep.shift().len);
  s = newest ? 1 - s : 0;
  const erase = erased(img, s * SECTOR, SECTOR) ? [] : [s * SECTOR];
  const parts = keep.map((r) => record(seq++, r.id, r.data));
  parts.push(record(seq, id, data));
  const bytes = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let o = 0; for (const p of parts) { bytes.set(p, o); o += p.length; }
  return { erase, at: s * SECTOR, bytes };
}

function record(seq, id, data) {
  const rec = new Uint8Array(recSize(data.length)).fill(0xFF), dv = new DataView(rec.buffer);
  dv.setUint32(0, MAGIC, true); dv.setUint32(4, seq >>> 0, true); dv.setUint32(8, id, true);
  rec.set(data, HDR);
  dv.setUint32(12, (data.length | (crc16(data) << 16)) >>> 0, true);
  return rec;
}

// Apply a plan to an image (to check it, or to simulate)
export function applyPlan(img, plan) {
  const out = img.slice();
  for (const e of plan.erase) out.fill(0xFF, e, e + SECTOR);
  for (let i = 0; i < plan.bytes.length; i++) out[plan.at + i] &= plan.bytes[i];
  return out;
}

// ------------------------------------------------------------------ what the firmwares store

export const IDS = { dreamcatcher: fourcc("DRMC"), nam: fourcc("AMPB"), tuner: fourcc("TUNR") };

// Dreamcatcher (its nds32/controls.c): [version][last][used bits][spare], then 8 slots, each every setting (params.h
// order) and VOLUME; version 4 then 8 names of 12 bytes (printable ASCII, zero-padded). Version 1 (1.0 / 1.1): 19
// settings; version 2 (1.2 before the reverb): 28; version 3: 34; version 4 (1.2 / 1.3): 34 and the names; version 5
// (1.4): 36 (SYNC, BPM) and the names. Launchpad works on 37 values (36 settings + VOLUME); older records are read into
// that (new settings at their defaults, the reverb off so they sound as saved) and written back in the version the
// firmware reads.
export const DC = {
  SLOTS: 8,
  PARAMS: ["on", "mix", "clock", "catch", "loopMode", "length", "motion", "loopLevel", "route", "time", "smear",
           "chance", "rate", "depth", "shape", "modType", "dissolve", "glitch", "filter",
           "grain", "pitch", "scale", "reverse", "stutter", "tone", "fade", "freeze", "overdub",
           "reverbDecay", "reverbMix", "reverbSize", "reverbPredelay", "reverbMod", "reverbShimmer", "sync", "bpm", "volume"],
  MAX: [1, 255, 10, 2, 1, 255, 255, 255, 2, 255, 255, 255, 255, 255, 6, 1, 255, 255, 255,
        255, 255, 4, 255, 255, 255, 255, 1, 1, 255, 255, 255, 255, 255, 255, 1, 240, 255],
  DEFAULTS: [1, 128, 10, 0, 0, 255, 160, 200, 1, 150, 150, 128, 70, 60, 0, 0, 128, 85, 110,
             170, 0, 3, 0, 0, 255, 230, 0, 0, 150, 70, 180, 20, 80, 0, 0, 120, 255],
  SETTINGS: { 1: 19, 2: 28, 3: 34, 4: 34, 5: 36 },   // settings per slot in each version (+ VOLUME)
  NAME_LEN: 12,                            // version 4: a name per slot
  VOL: 36,                                 // VOLUME's index in Launchpad's values
};
DC.ID = Object.fromEntries(DC.PARAMS.map((p, i) => [p, i]));
// Live access (OpenGP5 0.5.4, 7E 10 / 7E 11; Dreamcatcher's params.h DC_P_*): ids 0..33 are the settings, and
DC.P = { VOLUME: 0x100, PRESET: 0x101, USED: 0x102, CHANGED: 0x103, SAVE: 0x104, FORMAT: 0x106 };
// a name as the pedal keeps it: printable ASCII, at most 12 characters, no trailing spaces
export const dcName = (s) => String(s || "").replace(/[^\x20-\x7e]/g, "").slice(0, DC.NAME_LEN).replace(/\s+$/, "");

// One preset's values from any version's layout (20, 29, 35 or 37 values: settings then VOLUME) as Launchpad's 37
// (backups and files made by older Launchpads hold 20). null if it isn't one of them.
export function dcValues(v) {
  if (!Array.isArray(v)) return null;
  const n = v.length - 1;
  if (![19, 28, 34, 36].includes(n) || !v.every((x) => Number.isInteger(x) && x >= 0 && x <= 255)) return null;
  const out = DC.DEFAULTS.slice();
  for (let k = 0; k < n; k++) out[k] = v[k];
  if (n < DC.ID.reverbMix) out[DC.ID.reverbMix] = 0;
  out[DC.VOL] = v[n];
  return out;
}

export function decodeDreamcatcher(data) {
  if (!data || data.length < 4) return null;
  const version = data[0], n = DC.SETTINGS[version], names = version >= 4 ? DC.SLOTS * DC.NAME_LEN : 0;
  if (!n || data.length !== 4 + DC.SLOTS * (n + 1) + names) return null;
  const slots = [];
  for (let i = 0; i < DC.SLOTS; i++) {
    if (!((data[2] >> i) & 1)) { slots.push(null); continue; }
    const o = data.subarray(4 + (n + 1) * i, 4 + (n + 1) * (i + 1)), v = DC.DEFAULTS.slice();
    for (let k = 0; k < n; k++) v[k] = o[k];
    if (n < DC.ID.reverbMix) v[DC.ID.reverbMix] = 0;          // older presets: no reverb, as saved
    v[DC.VOL] = o[n];
    slots.push(v);
  }
  const at = 4 + DC.SLOTS * (n + 1);
  const nameOf = (i) => {
    if (!names) return "";
    const b = data.subarray(at + i * DC.NAME_LEN, at + (i + 1) * DC.NAME_LEN), z = b.indexOf(0);
    return dcName(new TextDecoder("latin1").decode(z < 0 ? b : b.subarray(0, z)));
  };
  return { last: data[1] < DC.SLOTS ? data[1] : 0, slots, names: slots.map((_, i) => (slots[i] ? nameOf(i) : "")), version };
}

export function encodeDreamcatcher({ last, slots, names = [] }, version = 5) {
  const n = DC.SETTINGS[version];
  if (!n) throw new Error(`Unknown Dreamcatcher preset version ${version}`);
  const d = new Uint8Array(4 + DC.SLOTS * (n + 1) + (version >= 4 ? DC.SLOTS * DC.NAME_LEN : 0));
  d[0] = version; d[1] = Math.min(Math.max(last | 0, 0), DC.SLOTS - 1);
  slots.forEach((v, i) => {
    if (!v) return;
    d[2] |= 1 << i;
    const at = 4 + (n + 1) * i, clamp = (k) => Math.min(Math.max(v[k] | 0, 0), DC.MAX[k]);
    for (let k = 0; k < n; k++) d[at + k] = clamp(k);
    d[at + n] = clamp(DC.VOL);
    if (version >= 4) {
      const nm = dcName(names[i]), off = 4 + DC.SLOTS * (n + 1) + i * DC.NAME_LEN;
      for (let k = 0; k < nm.length; k++) d[off + k] = nm.charCodeAt(k);
    }
  });
  return d;
}
