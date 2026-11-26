// Copied from apps/launchpad/src/lib/params.js by web/remote/sync.mjs: edit the original, then run node web/remote/sync.mjs.
// A firmware's parameters and presets (OpenGP5 1.0: platform/dsp/gp5_params.h, the USB commands in
// docs/platform/protocols.md). Any firmware that declares them gets Launchpad's Live page from this alone.
//   7E 12 <id>        describe one: flags, unit, min, max, default, name, group, choices
//   7E 11 / 7E 10     read / set values (16-bit, signed for parameters)
//   7E 15 <id> <v>    the text of a value (GP5_U_CUSTOM: the firmware writes it)
//   7E 17 ...         presets: info, load, save, name, rename
import { cmd, paramSet } from "./device.js";

export const FLAG = { DSP: 1, PRESET: 2, SAVE: 4, READONLY: 8, HIDDEN: 16, CHOICE: 0x80 };
export const UNIT = { NONE: 0, PERCENT: 1, DB10: 2, MS: 3, HZ: 4, SEMITONES: 5, CENTS10: 6, CUSTOM: 7 };
const s16 = (lo, hi) => { const v = lo | (hi << 8); return v & 0x8000 ? v - 0x10000 : v; };

/** parse one 7E 12 reply into a description, or null past the last */
export function parseDescription(r) {
  if (!r || r.length < 15 || r[2] !== 1) return null;
  const strs = [];
  let at = 15;
  for (let k = 0; k < 3; k++) { let e = at; while (e < r.length && r[e] !== 0) e++; strs.push(new TextDecoder().decode(r.subarray(at, e))); at = e + 1; }
  const flags = r[7];
  return { count: r[3] | (r[4] << 8), id: r[5] | (r[6] << 8), flags, unit: r[8], min: s16(r[9], r[10]), max: s16(r[11], r[12]),
           def: s16(r[13], r[14]), name: strs[0], group: strs[1], choices: flags & FLAG.CHOICE ? strs[2].split("|") : null };
}

/** every parameter's description, [] for a firmware without them (or a platform before 1.0) */
export async function readDescriptions(link) {
  const out = [];
  for (let i = 0; i < 256; i++) {
    const d = parseDescription(await cmd(link, [0x7E, 0x12, i & 0xFF, i >> 8], 400));
    if (!d) break;
    out.push(d);
    if (out.length >= d.count) break;
  }
  return out;
}

/** current values of ids 0..n-1 (signed) */
export async function readValues(link, n) {
  const out = [];
  for (let at = 0; at < n; at += 40) {
    const k = Math.min(40, n - at), r = await cmd(link, [0x7E, 0x11, at & 0xFF, at >> 8, k], 400);
    if (!r || r.length < 3) return null;
    for (let i = 0; i < r[2]; i++) out.push(s16(r[3 + 2 * i], r[4 + 2 * i]));
    if (r[2] < k) break;
  }
  return out;
}

export const setValue = (link, id, v) => paramSet(link, id, v >>> 0);

/** the pedal's text for a value (GP5_U_CUSTOM), or null */
export async function valueText(link, id, v) {
  const r = await cmd(link, [0x7E, 0x15, id & 0xFF, id >> 8, v & 0xFF, (v >> 8) & 0xFF], 400);
  if (!r || r.length < 4 || r[2] !== 1) return null;
  let e = 3; while (e < r.length && r[e] !== 0) e++;
  return new TextDecoder().decode(r.subarray(3, e));
}

/** how a value reads, from its unit (choices by name); GP5_U_CUSTOM needs the pedal's text (valueText) */
export function formatValue(d, v) {
  if (d.choices) return d.choices[v] ?? String(v);
  const sign = (x) => (x > 0 ? "+" : x < 0 ? "−" : "") + Math.abs(x);
  switch (d.unit) {
    case UNIT.PERCENT: return `${v} %`;
    case UNIT.DB10: return `${sign(v / 10)} dB`;
    case UNIT.MS: return v >= 1000 ? `${(v / 1000).toFixed(v % 1000 ? 2 : 0)} s` : `${v} ms`;
    case UNIT.HZ: return `${v} Hz`;
    case UNIT.SEMITONES: return `${sign(v)} st`;
    case UNIT.CENTS10: return `${sign(v / 10)} cents`;
    default: return String(v);
  }
}

/** presets: { count, current (-1 none), dirty, used: [bool], names: [string] }, or null when the firmware keeps none */
export async function readPresets(link) {
  const r = await cmd(link, [0x7E, 0x17, 0], 400);
  if (!r || r.length < 14 || r[2] !== 1 || !r[3]) return null;
  const count = r[3], used = [], names = [];
  let bits = 0n;
  for (let i = 0; i < 8; i++) bits |= BigInt(r[6 + i]) << BigInt(8 * i);
  for (let n = 0; n < count; n++) {
    used.push(((bits >> BigInt(n)) & 1n) === 1n);
    const m = used[n] ? await cmd(link, [0x7E, 0x17, 3, n], 400) : null;
    let e = 3; if (m) while (e < m.length && m[e] !== 0) e++;
    names.push(m ? new TextDecoder().decode(m.subarray(3, e)) : "");
  }
  return { count, current: r[4] === 0xFF ? -1 : r[4], dirty: !!r[5], used, names };
}
const presetOp = async (link, op, n, extra = []) => { const r = await cmd(link, [0x7E, 0x17, op, n, ...extra], 1500); return !!(r && r.length >= 3 && r[2] === 1); };
export const loadPreset = (link, n) => presetOp(link, 1, n);
export const savePreset = (link, n) => presetOp(link, 2, n);
export const renamePreset = (link, n, name) => presetOp(link, 4, n, [...new TextEncoder().encode(name.replace(/[^\x20-\x7E]/g, "?").slice(0, 12))]);
