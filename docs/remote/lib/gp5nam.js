// Copied from work/nam-manager/gp5nam.js by web/remote/sync.mjs: edit the original, then run node web/remote/sync.mjs.
// NAM A2 capture bank of the GP-5 capture player (work/nam-a2split), private for now.
// Part 1, the converter: a .nam file -> the pedal's capture file (.a2r), byte for byte what
//   work/a2-nds32/a2r_export.py model.nam synth out.a2r
// writes (same checks, the same made-up calibration signal, the same rounding).
// Part 2, the pedal: read the bank, write or empty a slot, ask the DSP which capture it holds, over the open
// firmware's USB protocol (platform/nds32/proto.c: 7E 04 / 05 / 07 / 08). No browser APIs in here: the
// MIDI connection comes in as a `link` from web/flasher/webmidi.js (or a test stand-in), so Node runs it too.

import { buildSysex, parseSysex, CLASS_CMD } from "./gp5flash.js";

// ------------------------------------------------------------------ the made-up calibration signal (calib_synth.py)

const SR = 48000;

export function calibSignal() {
  let state = 12345;
  const lcg = () => { state = (Math.imul(1664525, state) + 1013904223) >>> 0; return state / 4294967296.0 - 0.5; };
  const nPl = 6 * SR, nSw = 3 * SR, nNo = 2 * SR;
  const pl = new Float64Array(nPl);
  const pluck = (start, period, length, amp) => {
    const buf = new Float64Array(period);
    for (let i = 0; i < period; i++) buf[i] = lcg();
    let i = 0;
    for (let n = 0; n < length; n++) {
      if (start + n >= pl.length) break;
      const a = buf[i], b = buf[(i + 1) % period];
      pl[start + n] += amp * a;
      buf[i] = 0.4985 * (a + b);
      i = (i + 1) % period;
    }
  };
  const chords = [[218, 163, 109], [146, 98, 73], [436, 327, 245], [97, 65, 49], [290, 218, 184], [122, 91, 61],
                  [183, 137, 92], [82, 61, 41], [326, 244, 163], [109, 82, 55], [245, 184, 123], [73, 55, 41]];
  chords.forEach((ch, k) => ch.forEach((p, j) => pluck(k * (SR / 2) + j * 720, p, SR, 1.0)));
  let peak = 0;
  for (const v of pl) peak = Math.max(peak, Math.abs(v));
  const sig = new Float64Array(nPl + nSw + nNo);
  for (let n = 0; n < nPl; n++) sig[n] = pl[n] / peak * 0.99;
  let x = 1.0, y = 0.0, e = 0.0026;
  const g = 1.0000413358809286;                      // 0x1.0002b58041b9bp+0, as in calib_synth.py
  for (let n = 0; n < nSw; n++) {
    x += e * y; y -= e * x;
    sig[nPl + n] = 0.85 * x;
    e *= g;
  }
  for (let n = 0; n < nNo; n++) {
    const v = lcg() + lcg() + lcg() + lcg();
    sig[nPl + nSw + n] = v * (n < SR ? 0.45 : 0.045);
  }
  return sig;
}

// ------------------------------------------------------------------ the .nam file (tools/a2lite/a2_export.py)

export class NotA2 extends Error {}

function pickWavenet(d, width) {
  if (d.architecture === "SlimmableContainer") {
    const subs = d.config.submodels.map((s) => s.model);
    const m = subs.find((s) => s.config.layers[0].channels === width);
    if (!m) throw new NotA2("This capture has no A2-Lite part (no width-3 submodel). Only NAM A2 captures can be loaded.");
    return m;
  }
  if (d.architecture !== "WaveNet") throw new NotA2(`Not a NAM A2 capture (architecture "${d.architecture}")`);
  return d;
}

function checkA2(m) {
  const cfg = m.config;
  const bad = () => new NotA2("This capture is not an A2-Lite model. Only NAM A2 captures can be loaded.");
  if (cfg.head != null || cfg.layers.length !== 1) throw bad();
  const L = cfg.layers[0], C = L.channels, nl = L.dilations.length;
  const ks = L.kernel_sizes || (Number.isInteger(L.kernel_size) ? Array(nl).fill(L.kernel_size) : L.kernel_size);
  const acts = Array.isArray(L.activation) ? L.activation : Array(nl).fill(L.activation);
  let gm = L.gating_mode ?? Array(nl).fill("none");
  gm = Array.isArray(gm) ? gm : Array(nl).fill(gm);
  const film = Object.entries(L).some(([k, v]) => k.endsWith("_film") && v && typeof v === "object" && v.active);
  const ok = L.input_size === 1 && L.condition_size === 1 && (L.bottleneck ?? C) === C &&
    L.head.out_channels === 1 && (L.head.bias ?? true) && L.layer1x1.active && (L.layer1x1.groups ?? 1) === 1 &&
    !L.head1x1.active && (L.groups_input ?? 1) === 1 && (L.groups_input_mixin ?? 1) === 1 && ks.length === nl &&
    gm.every((g) => g === "none") && acts.every((a) => a.type === "LeakyReLU") && !film;
  if (!ok) throw bad();
  const slopes = new Set(acts.map((a) => a.negative_slope ?? 0.01));
  if (slopes.size !== 1) throw bad();
  const KH = L.head.kernel_size;
  let n = C + C * KH + 2;
  for (const k of ks) n += C * C * k + C + C + C * C + C;
  const w = m.weights.map((v) => { const y = Math.fround(v); if (!Number.isFinite(y)) throw bad(); return y; });
  if (w.length !== n) throw bad();
  if (Math.abs(w[w.length - 1] - (cfg.head_scale ?? w[w.length - 1])) > 1e-5) throw bad();
  if ((m.sample_rate ?? 48000) !== 48000) throw new NotA2("The capture must be made at 48 kHz");
  return { C, NL: nl, K: ks, D: L.dilations, KH, slope: [...slopes][0], w };
}

function unpack(a) {
  const { C, w } = a;
  let p = 0;
  const take = (n) => { const v = w.slice(p, p + n); p += n; return v; };
  const m = { C, rech: take(C), layers: [] };
  a.K.forEach((K, l) => {
    m.layers.push({ K, D: a.D[l], W: take(C * C * K), b: take(C), mix: take(C), l1w: take(C * C), l1b: take(C) });
  });
  m.hw = take(C * a.KH); m.hb = take(1)[0]; m.hs = take(1)[0];
  m.slope = a.slope; m.KH = a.KH;
  return m;
}

// ------------------------------------------------------------------ ranges (a2q_export.py forward_float) + quantize

// The float64 forward over the calibration signal (with NAM's pad_start), only the ranges a2r_export needs.
function ranges(m, u, wantY = false) {
  const C = 3;
  let rf = m.KH;
  for (const L of m.layers) rf += (L.K - 1) * L.D;
  const N = rf - 1 + u.length, off = rf - 1;
  const up = new Float64Array(N); up.set(u, off);
  let x = [0, 1, 2].map((c) => { const r = new Float64Array(N); for (let n = 0; n < N; n++) r[n] = m.rech[c] * up[n]; return r; });
  const head = [0, 1, 2].map(() => new Float64Array(N));
  const R = { x: [], z: [] };
  const maxabs = (arrs) => { let v = 0; for (const a of arrs) for (let n = 0; n < a.length; n++) { const t = Math.abs(a[n]); if (t > v) v = t; } return v; };
  const z = [0, 1, 2].map(() => new Float64Array(N)), a = [0, 1, 2].map(() => new Float64Array(N));
  for (const L of m.layers) {
    R.x.push(maxabs(x));
    for (let o = 0; o < C; o++) { const zo = z[o], b = L.b[o], mx = L.mix[o]; for (let n = 0; n < N; n++) zo[n] = b + mx * up[n]; }
    for (let k = 0; k < L.K; k++) {
      const s = (L.K - 1 - k) * L.D;
      for (let o = 0; o < C; o++) {
        const w0 = L.W[(o * C + 0) * L.K + k], w1 = L.W[(o * C + 1) * L.K + k], w2 = L.W[(o * C + 2) * L.K + k];
        const zo = z[o], x0 = x[0], x1 = x[1], x2 = x[2];
        for (let n = s; n < N; n++) zo[n] += w0 * x0[n - s] + w1 * x1[n - s] + w2 * x2[n - s];
      }
    }
    let zmax = 0;
    for (let o = 0; o < C; o++) {
      const zo = z[o], ao = a[o], ho = head[o];
      for (let n = 0; n < N; n++) {
        const v = zo[n], t = Math.abs(v);
        if (t > zmax) zmax = t;
        const av = v > 0 ? v : m.slope * v;
        ao[n] = av; ho[n] += av;
      }
    }
    R.z.push(zmax);
    const nx = [0, 1, 2].map(() => new Float64Array(N));
    for (let o = 0; o < C; o++) {
      const l0 = L.l1w[o * C], l1 = L.l1w[o * C + 1], l2 = L.l1w[o * C + 2], lb = L.l1b[o];
      const xo = x[o], no = nx[o], a0 = a[0], a1 = a[1], a2 = a[2];
      for (let n = 0; n < N; n++) no[n] = (xo[n] + (l0 * a0[n] + l1 * a1[n] + l2 * a2[n])) + lb;
    }
    x = nx;
  }
  R.x.push(maxabs(x));
  R.head = maxabs(head);
  if (wantY) {                                         // the output, as forward_float: hs * (hb + sum over k of hw[:, k] . head)
    const KH = m.KH, y = new Float64Array(N - off);
    for (let n = off; n < N; n++) {
      let s = 0;
      for (let k = 0; k < KH; k++) {
        const j = n - (KH - 1 - k);
        if (j < 0) continue;
        s += (m.hw[0 * KH + k] * head[0][j] + m.hw[1 * KH + k] * head[1][j]) + m.hw[2 * KH + k] * head[2][j];
      }
      y[n - off] = m.hs * (m.hb + s);
    }
    R.y = y;
  }
  return R;
}

// Output levelling (a2r_export.py output_level, the very same sums): the plucked chords of the calibration signal at
// -15 dBFS peak (hard playing at the pedal's input), K-weighted loudness and peak of the output, the first 50 ms left
// out -> the gain (dB, to 0.01) that puts every capture at OUTPUT_LEVEL with its peak at most OUTPUT_PEAK_MAX.
export const OUTPUT_LEVEL = -20.0, OUTPUT_PEAK_MAX = -3.0;
const PLAY_SCALE = 0.17782794100389229;
const KW = [[1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585],
            [1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621]];
function outputLevel(m, calib) {
  const u = new Float64Array(3 * SR);
  for (let i = 0; i < u.length; i++) u[i] = calib[i] * PLAY_SCALE;
  const y = ranges(m, u, true).y.subarray(2400);
  let kw = y;
  for (const [b0, b1, b2, a1, a2] of KW) {
    const o = new Float64Array(kw.length);
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < kw.length; i++) {
      const x = kw[i], v = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1; x1 = x; y2 = y1; y1 = v; o[i] = v;
    }
    kw = o;
  }
  let ms = 0; for (const v of kw) ms += v * v;
  const loud = 10 * Math.log10(ms / kw.length + 1e-30);
  let pk = 0; for (const v of y) pk = Math.max(pk, Math.abs(v));
  const peak = 20 * Math.log10(pk + 1e-30);
  const g = Math.max(-40, Math.min(40, OUTPUT_LEVEL - loud, OUTPUT_PEAK_MAX - peak));
  return { gainDb: Math.floor(g * 100 + 0.5) / 100, loud, peak };
}

const I16 = 32767, I32 = 2 ** 31 - 1;
function expFor(maxabs, lim, headroom = 1.0, cap = 30) {
  if (maxabs <= 0) return cap;
  return Math.min(cap, Math.floor(Math.log2(lim / (maxabs * headroom))));
}
// Python's round(): halves go to the even neighbour
function rnd(v) {
  const f = Math.floor(v), d = v - f;
  if (d > 0.5) return f + 1;
  if (d < 0.5) return f;
  return f % 2 === 0 ? f : f + 1;
}
const absMax = (arr) => arr.reduce((m, v) => Math.max(m, Math.abs(v)), 0);

function quantize(m, R, hr = 4.0) {
  const X = expFor(Math.max(...R.x), I32, hr);
  const layers = m.layers.map((L, l) => {
    let ez = Math.min(expFor(R.z[l], I32, hr), X - 16 + expFor(absMax(L.W), I16));
    while (absMax(L.mix) * 2 ** (ez + 1) > I32 || absMax(L.b) * 2 ** ez > 2 ** 30) ez -= 1;
    const ew = ez - X + 16;
    let er = Math.min(expFor(2 * Math.max(R.x[l], R.x[l + 1]), I32, hr), ez - 16 + expFor(absMax(L.l1w), I16));
    while (absMax(L.l1b) * 2 ** er > 2 ** 30) er -= 1;
    const e1 = er - ez + 16;
    const d = { K: L.K, D: L.D, ez, er,
      w: L.W.map((v) => rnd(v * 2 ** ew)), b: L.b.map((v) => rnd(v * 2 ** ez)),
      mix: L.mix.map((v) => rnd(v * 2 ** (ez + 1))), l1w: L.l1w.map((v) => rnd(v * 2 ** e1)),
      l1b: L.l1b.map((v) => rnd(v * 2 ** er)) };
    if (d.w.concat(d.l1w).some((v) => Math.abs(v) > I16)) throw new Error(`layer ${l}: weights do not fit 16 bits`);
    return d;
  });
  const eh = Math.min(expFor(R.head, I32, hr), Math.min(...layers.map((d) => d.ez)));
  const s = pairwiseSum(m.hw.map(Math.abs)) + 1e-12;
  const ehw = Math.min(expFor(s * 2 ** 31, 2 ** 62), expFor(absMax(m.hw), I16));
  const q = { nl: layers.length, kh: m.KH, X, eh, ehw, slope: rnd(m.slope * 65536),
    rech: m.rech.map((v) => rnd(v * 2 ** (X + 1))), hb: m.hb, hs: m.hs,
    hw: m.hw.map((v) => rnd(v * 2 ** ehw)), layers };
  if (q.rech.some((v) => Math.abs(v) > I32) || q.hw.some((v) => Math.abs(v) > I16)) throw new Error("head does not fit");
  return q;
}

// numpy's sum of a short float64 array (under 8 values: one by one; up to 128: eight running sums, then added
// pairwise, the rest one by one), so a total on a power-of-two boundary rounds the same way
function pairwiseSum(v) {
  const n = v.length;
  if (n < 8) { let s = -0.0; for (const t of v) s += t; return s; }
  if (n <= 128) {
    const r = v.slice(0, 8);
    let i;
    for (i = 8; i < n - (n % 8); i += 8) for (let j = 0; j < 8; j++) r[j] += v[i + j];
    let s = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
    for (; i < n; i++) s += v[i];
    return s;
  }
  const h = Math.floor(n / 2 / 8) * 8;
  return pairwiseSum(v.slice(0, h)) + pairwiseSum(v.slice(h));
}

// ------------------------------------------------------------------ the capture file (a2r_blob.h)

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
export function crc32(bytes) {
  let c = 0xFFFFFFFF;
  for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

class Writer {
  constructor() { this.a = []; }
  u8(v) { this.a.push(v & 0xFF); }
  i8(v) { this.u8(v); }
  u16(v) { this.u8(v); this.u8(v >> 8); }
  i16(v) { this.u16(v & 0xFFFF); }
  i32(v) { this.u16(v & 0xFFFF); this.u16((v >>> 16) & 0xFFFF); }
  f32(v) { const b = new Uint8Array(new Float32Array([v]).buffer); for (const x of b) this.u8(x); }
  zero(n) { for (let i = 0; i < n; i++) this.u8(0); }
  pad4() { while (this.a.length % 4) this.u8(0); }
}

// name: shown on the pedal's screen, up to 24 bytes of UTF-8 (cut on a byte boundary as the Python does)
export function makeBlob(q, name) {
  const w = new Writer();
  const nm = new TextEncoder().encode(name).slice(0, 24);
  w.u8(q.nl); w.u8(q.kh); w.i8(q.X); w.i8(q.eh); w.i8(q.ehw); w.zero(3);
  w.i32(q.slope); for (const v of q.rech) w.i32(v);
  w.f32(q.hb); w.f32(q.hs);
  for (let i = 0; i < 24; i++) w.u8(i < nm.length ? nm[i] : 0);
  for (const d of q.layers) {
    w.u8(d.K); w.zero(1); w.u16(d.D); w.i8(d.ez); w.i8(d.er); w.zero(2);
    for (const v of d.b) w.i32(v); for (const v of d.mix) w.i32(v); for (const v of d.l1b) w.i32(v);
    for (const v of d.l1w) w.i16(v); w.zero(2);
    for (const v of d.w) w.i16(v); w.pad4();
  }
  for (const v of q.hw) w.i16(v); w.pad4();
  const body = Uint8Array.from(w.a), out = new Uint8Array(12 + body.length), dv = new DataView(out.buffer);
  out.set([0x41, 0x32, 0x52, 0x31]);
  dv.setUint32(4, out.length, true); dv.setUint32(8, crc32(body), true);
  out.set(body, 12);
  return out;
}

// The GP-5's input at full scale, measured 2026-11-17 (Scarlett 18i20 loop-back); a2r_export.py has the same.
export const PEDAL_INPUT_DBU = 7.1;
// The input gain that drives a capture as hard as it was recorded (its metadata.input_level_dbu), 1 without it.
export function calibrationGain(d, pedalDbu = PEDAL_INPUT_DBU) {
  const dbu = d?.metadata?.input_level_dbu;
  return typeof dbu === "number" && Number.isFinite(dbu) ? 10 ** ((pedalDbu - dbu) / 20) : 1;
}

let calibCache = null;
// .nam text -> { blob, name, gainDb, levelDb, calibrated }. Throws NotA2 with a plain-language reason when the capture
// cannot be used. Options (both on unless set false; a2r_export.py --no-calibration / --no-level):
//   calibrate  drive the capture as hard as it was recorded (its metadata.input_level_dbu; nothing without it)
//   level      every capture equally loud (OUTPUT_LEVEL), instead of the level of the gear it was captured from
export function convertNam(text, name, { calibrate = true, level = true } = {}) {
  let d;
  try { d = JSON.parse(text); } catch { throw new NotA2("This file is not a NAM capture (.nam)"); }
  let a;
  try { a = checkA2(pickWavenet(d, 3)); } catch (e) { if (e instanceof NotA2) throw e; throw new NotA2("This capture is not an A2-Lite model. Only NAM A2 captures can be loaded."); }
  const m = unpack(a);
  const g = calibrate ? calibrationGain(d) : 1;       // the guitar enters through rech and each layer's mix only
  m.rech = m.rech.map((v) => v * g);
  for (const L of m.layers) L.mix = L.mix.map((v) => v * g);
  calibCache ??= calibSignal();
  let levelDb = 0;
  if (level) {                                        // every capture equally loud: the head's scale (no range changes)
    levelDb = outputLevel(m, calibCache).gainDb;
    m.hs = m.hs * 10 ** (levelDb / 20);
  }
  const q = quantize(m, ranges(m, calibCache));
  return { blob: makeBlob(q, name), gainDb: 20 * Math.log10(g), levelDb, calibrated: calibrationGain(d) !== 1,
           name: new TextDecoder().decode(new TextEncoder().encode(name).slice(0, 24)).replace(/�$/, "") };
}

export function blobInfo(b) {
  if (b.length < 68 || b[0] !== 0x41 || b[1] !== 0x32 || b[2] !== 0x52 || b[3] !== 0x31) return null;
  const dv = new DataView(b.buffer, b.byteOffset);
  return { length: dv.getUint32(4, true), crc: dv.getUint32(8, true),
           name: new TextDecoder().decode(b.subarray(44, 68)).replace(/\0+$/, "") };
}

// ------------------------------------------------------------------ the pedal (tools/gp5_data.py, a2_load.py)

export const SLOTS = 12, SLOT_BYTES = 4608;          // work/nam-a2split/split.h
const DATA = 0x162000, SECTOR = 0x1000, CHUNK = 56;
const le32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];
const now = () => (typeof performance !== "undefined" ? performance.now() : Date.now());
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
export class PedalError extends Error {}

async function ask(link, payload, timeout = 1000) {
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

export async function flashRead(link, addr, length) {
  const out = new Uint8Array(length);
  for (let o = 0; o < length; o += 64) {
    const n = Math.min(64, length - o);
    const pl = await ask(link, [0x7E, 0x05, ...le32(addr + o), n]);
    if (!pl || pl.length < 3 + n || !pl[2]) throw new PedalError("the pedal did not answer a flash read: is the capture player firmware running?");
    out.set(pl.subarray(3, 3 + n), o);
  }
  return out;
}

async function eraseSector(link, off) {
  const pl = await ask(link, [0x7E, 0x07, ...le32(DATA + off)], 5000);
  if (!pl || pl.length < 3 || !pl[2]) throw new PedalError("the pedal refused to erase its data area: does it run the capture player firmware?");
}

async function program(link, off, bytes) {
  for (let i = 0; i < bytes.length; i += CHUNK) {
    const part = bytes.subarray(i, i + CHUNK);
    const pl = await ask(link, [0x7E, 0x08, ...le32(DATA + off + i), ...part], 2000);
    if (!pl || pl.length < 3 || !pl[2]) throw new PedalError(`the pedal refused a write at 0x${(DATA + off + i).toString(16)}`);
  }
}

const allFF = (b) => b.every((v) => v === 0xFF);

// Write data at data-area offset off, keeping every other byte of the 4 KB sectors it shares (the neighbouring
// slots): read, merge, erase, write back (erased pieces skipped), data's first 16 bytes last, read back.
// progress(fraction) is called along the way.
export async function writeKeep(link, off, data, progress = () => {}) {
  const pad = (4 - (data.length % 4)) % 4, d = new Uint8Array(data.length + pad).fill(0xFF); d.set(data);
  const s0 = Math.floor(off / SECTOR) * SECTOR, s1 = Math.ceil((off + d.length) / SECTOR) * SECTOR;
  const img = await flashRead(link, DATA + s0, s1 - s0);
  progress(0.15);
  img.set(d, off - s0);
  for (let s = s0; s < s1; s += SECTOR) await eraseSector(link, s);
  progress(0.3);
  const h0 = off - s0, h1 = h0 + Math.min(16, d.length);
  const pieces = [];
  for (let i = 0; i < img.length; i += CHUNK) {
    const a = i, b = Math.min(i + CHUNK, img.length);
    for (const [lo, hi] of [[a, Math.min(b, h0)], [Math.max(a, h1), b]])
      if (lo < hi && !allFF(img.subarray(lo, hi))) pieces.push([lo, hi]);
  }
  if (!allFF(img.subarray(h0, h1))) pieces.push([h0, h1]);      // the header last: the firmware sees it whole
  for (let k = 0; k < pieces.length; k++) {
    const [lo, hi] = pieces[k];
    await program(link, s0 + lo, img.subarray(lo, hi));
    progress(0.3 + 0.55 * (k + 1) / pieces.length);
  }
  const back = await flashRead(link, DATA + s0, s1 - s0);
  if (back.some((v, i) => v !== img[i])) throw new PedalError("the data area did not read back correctly: try again");
  progress(0.9);
}

// [{ name, crc } or null] per slot
export async function readBank(link) {
  const bank = [];
  for (let i = 0; i < SLOTS; i++) bank.push(blobInfo(await flashRead(link, DATA + i * SLOT_BYTES, 68)));
  return bank;
}

// The checksum of the capture the DSP holds (its 0x7F30 reply, word 6), or null.
export async function dspCaptureCrc(link) {
  const payload = [0x30, 0x7F, 0x04, 0x00];
  let pl = await ask(link, [0x7E, 0x03, 0, 0, ...payload]);
  if (!pl || pl.length < 3 || pl[2] !== 1) return null;
  pl = await ask(link, [0x7E, 0x04, payload.length, 0, 30], 2000);
  if (!pl || pl.length < 4 || pl[2] !== 1) return null;
  const rep = pl.subarray(4, 4 + pl[3]);
  return rep.length >= 32 ? new DataView(rep.buffer, rep.byteOffset).getUint32(28, true) : null;
}

// Put a capture file into slot i (0-based) and wait until the pedal plays it.
export async function putCapture(link, slot, blob, progress = () => {}) {
  if (blob.length > SLOT_BYTES) throw new PedalError("this capture is too big for a slot (only A2-Lite fits)");
  const info = blobInfo(blob);
  await writeKeep(link, slot * SLOT_BYTES, blob, progress);
  for (let k = 0; k < 20; k++) {
    await sleep(400);
    if ((await dspCaptureCrc(link)) === info.crc) { progress(1); return true; }
  }
  return false;                                        // written, but the pedal has not confirmed it
}

export async function emptySlot(link, slot, progress = () => {}) {
  await writeKeep(link, slot * SLOT_BYTES, new Uint8Array(SLOT_BYTES).fill(0xFF), progress);
  progress(1);
}
