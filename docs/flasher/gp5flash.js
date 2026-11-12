// GP-5 flasher core: image checks, USB-MIDI framing and the factory loader's update session.
// A port of tools/gp5tool.py + tools/gp5_proto.py + tools/htfw.py (same frames, waits and retries).
// No browser APIs in here: the MIDI connection comes in as a `ports` object (webmidi.js on a page,
// a simulated pedal in the tests), so this file runs in browsers and in Node alike.

export const SECTIONS = { app: 0x62, dsp: 0x63, "dsp-data": 0x67, models: 0x66, presets: 0x65 };
export const NAMES = Object.fromEntries(Object.entries(SECTIONS).map(([k, v]) => [v, k]));
// Every section the pedal knows, in container order, with its flash address.
export const LAYOUT = [[0x63, 0x200000], [0x67, 0x240000], [0x66, 0x280000], [0x62, 0x000000], [0x65, 0x190000]];
const MAX_LEN = { 0x62: 0x168000, 0x63: 0x10000, 0x67: 0x100000, 0x66: 0x43000 };
// Sections of Valeton's V1.0.6 image: stored CRC and length (to label them).
const STOCK_V106 = { 0x63: [0x3688, 0xC348], 0x67: [0x27A5, 0x55028], 0x66: [0x2010, 0x42FE4], 0x62: [0x985C, null], 0x65: [0xC73C, 0x2D000] };

export const MODE_OPEN = "open firmware", MODE_STOCK = "stock firmware", MODE_LOADER = "update mode";
export const MODE_SILENT = "firmware that does not answer";
const OPEN_PORT = "GP-5 Open FW", ANY_PORT = "GP-5";
export const RECOVERY = "Recovery: unplug the pedal, hold the foot switch and the PARA knob, plug it back in (the screen " +
  "stays dark = update mode), then flash again. The factory loader is never overwritten.";

export const Timing = { reply: 1000, ack: 5000, begin: 30000, beginSettle: 10000, settle: 1500, final: 8000,
                        reenum: 25000, retries: 6, retryDelay: 50, poll: 150 };

export class Problem extends Error {}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const now = () => (typeof performance !== "undefined" ? performance.now() : Date.now());
const hex2 = (v) => v.toString(16).padStart(2, "0");

// ------------------------------------------------------------------ container (htfw.py)

export function crc16modbus(data) {
  let c = 0xFFFF;
  for (const b of data) {
    c ^= b;
    for (let i = 0; i < 8; i++) c = c & 1 ? (c >>> 1) ^ 0xA001 : c >>> 1;
  }
  return c;
}
export const storedCrc = (data) => { const c = crc16modbus(data); return ((c & 0xFF) << 8) | (c >>> 8); };

function u32(b, o) { return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }
function u16(b, o) { return b[o] | (b[o + 1] << 8); }

export function loadSections(blob) {
  if (blob.length < 0x38 || u32(blob, 0) !== 0x57465448) throw new Error("not an HTFW image");
  const product = new TextDecoder().decode(blob.subarray(0x0C, 0x1C)).split("\0")[0];
  const count = blob[0x22];
  if (product !== "GP-5" || blob[0x21] || u32(blob, 0x08) !== blob.length) throw new Error("expected an uncompressed GP-5 HTFW image");
  const base = 0x38 + 16 * count;
  if (blob.length < base) throw new Error("truncated section table");
  const order = LAYOUT.map(([t]) => t);
  const entries = [];
  for (let i = 0; i < count; i++) {
    const o = 0x38 + 16 * i;
    entries.push({ crc: u16(blob, o), type: blob[o + 3], flash: u32(blob, o + 4), doff: u32(blob, o + 8), dlen: u32(blob, o + 12) });
  }
  const idx = entries.map((e) => order.indexOf(e.type));
  if (!entries.length || entries.some((e, i) => idx[i] < 0 || LAYOUT[idx[i]][1] !== e.flash) ||
      idx.some((v, i) => i && v <= idx[i - 1])) throw new Error("unexpected section types, order or addresses");
  const sections = [];
  let cursor = 0;
  for (const e of entries) {
    const data = blob.subarray(base + e.doff, base + e.doff + e.dlen);
    if (e.doff !== cursor || !data.length || data.length !== e.dlen || storedCrc(data) !== e.crc)
      throw new Error(`section 0x${hex2(e.type)}: offset, length or CRC is wrong`);
    sections.push([e.type, data]);
    cursor += data.length;
  }
  if (base + cursor !== blob.length) throw new Error("trailing or missing data");
  return sections;
}

export function checkApp(d) {
  const errs = [];
  if (d.length < 0x108) return ["app section is too short to hold a vector table"];
  for (let o = 0; o < 0xA4; o += 4) if (d[o] !== 0x48) { errs.push(`app vector table: entry at 0x${o.toString(16)} is not a jump`); break; }
  if (u32(d, 0xC0) !== 0xB0BEBDC9) errs.push("app magic at 0xC0 is missing");
  for (let o = 0; o < 0xA4; o += 4) {
    const w = ((d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]) >>> 0;
    const target = o + ((w & 0xFFFFFF) << 1);
    if (w >>> 24 === 0x48 && !(target >= 0x100 && target < d.length)) { errs.push(`app vector at 0x${o.toString(16)} jumps outside the image`); break; }
  }
  return errs;
}

export function checkDsp(d) {
  const errs = [];
  if (d.length < 0x408) return ["DSP section is too short"];
  const vt = u32(d, 0x0C);
  if (!(vt >= 0x4000 && vt < 0x4000 + d.length - 8)) return [`DSP header: vector table address 0x${vt.toString(16)} is outside the section`];
  const sp = u32(d, vt - 0x4000), reset = u32(d, vt - 0x4000 + 4);
  if (!((sp > 0x20000000 && sp <= 0x20008000) || (sp > 0x10000000 && sp <= 0x10020000)))
    errs.push(`DSP initial stack pointer 0x${sp.toString(16)} is not in RAM`);
  if (!(reset & 1) || !((reset & ~1) >= 0x4000 && (reset & ~1) < 0x4000 + d.length))
    errs.push(`DSP reset vector 0x${reset.toString(16)} is not Thumb code inside the section`);
  return errs;
}

// Validate a whole image: { sections: [[type, data]], lines: [description] }. Throws Problem.
export function inspect(blob) {
  let sections;
  try { sections = loadSections(blob); } catch (e) { throw new Problem(`not a valid GP-5 firmware image: ${e.message}`); }
  const lines = [], errs = [];
  for (const [t, d] of sections) {
    const crc = storedCrc(d), stock = STOCK_V106[t];
    const isStock = stock && stock[0] === crc && (stock[1] === null || stock[1] === d.length);
    lines.push({ name: NAMES[t], type: t, bytes: d.length, stock: !!isStock });
    if (MAX_LEN[t] && d.length > MAX_LEN[t]) errs.push(`${NAMES[t]} section is larger than the pedal accepts (${d.length} > ${MAX_LEN[t]})`);
    if (t === 0x62) errs.push(...checkApp(d));
    if (t === 0x63) errs.push(...checkDsp(d));
  }
  if (errs.length) throw new Problem("image would not boot:\n  " + errs.join("\n  "));
  return { sections, lines };
}

// ------------------------------------------------------------------ framing (gp5_proto.py)

export const CLASS_CMD = 0x01, CLASS_DATA = 0xFF, CHUNK = 19;

export function crc8(data) {
  let c = 0;
  for (const b of data) {
    c ^= b;
    for (let i = 0; i < 8; i++) c = c & 0x80 ? ((c << 1) ^ 0x07) & 0xFF : (c << 1) & 0xFF;
  }
  return c;
}

export function buildSysex(cls, index, payload) {
  const pl = Array.from(payload), body = [crc8([0, cls, index, pl.length, ...pl]), cls, index, pl.length, ...pl];
  const out = [0xF0];
  for (const b of body) out.push(b >> 4, b & 0x0F);
  out.push(0xF7);
  return Uint8Array.from(out);
}

export function parseSysex(msg) {
  if (msg[0] !== 0xF0 || msg[msg.length - 1] !== 0xF7 || (msg.length - 2) % 2) return null;
  const dec = [];
  for (let i = 1; i < msg.length - 1; i += 2) dec.push(((msg[i] & 0xF) << 4) | (msg[i + 1] & 0xF));
  if (dec.length < 4) return null;
  const [crc, cls, index, len] = dec, pl = dec.slice(4, 4 + len);
  return { cls, index, len, payload: Uint8Array.from(pl), ok: crc8([0, cls, index, len, ...pl]) === crc };
}

export function buildUpdateStream(section, type) {
  const xfer = new Uint8Array(section.length + 2);
  xfer[0] = 0x11; xfer[1] = type; xfer.set(section, 2);
  const pktcnt = Math.ceil(xfer.length / CHUNK), le32 = (v) => [v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >>> 24) & 0xFF];
  const msgs = [["ENTER", buildSysex(CLASS_CMD, 0, [0x11, 0x60])],
                ["BEGIN_SEC", buildSysex(CLASS_CMD, 0, [0x11, 0x61, type, 0, 0, 0, ...le32(pktcnt), ...le32(xfer.length)])]];
  for (let i = 0; i < pktcnt; i++) msgs.push([`DATA[${i}]`, buildSysex(CLASS_DATA, i & 0xFF, xfer.subarray(i * CHUNK, (i + 1) * CHUNK))]);
  msgs.push(["END_SEC", buildSysex(CLASS_CMD, 0, [0x11, 0x6E, type, 0])]);
  msgs.push(["END_IMAGE", buildSysex(CLASS_CMD, 0, [0x11, 0x6E, 0x6E, 0])]);
  return msgs;
}

const ackStatus = (pl) => (pl && pl.length >= 3 && pl[0] === 0x14 && pl[1] === 0x08 ? pl[2] : null);
const payloadOf = (raw) => { const p = parseSysex(raw); return p && p.ok ? p.payload : null; };

// ------------------------------------------------------------------ talking to the pedal (gp5tool.py)

export function portBase(name) {
  return name.split(":")[0].replace(/\s+\d+$/, "").trim();
}

async function ask(link, payload, want, timeout) {
  link.flush();
  await link.send(buildSysex(CLASS_CMD, 0, payload));
  const end = now() + timeout;
  while (now() < end) {
    const raw = await link.recv(Math.max(0, end - now()));
    if (raw === null) break;
    const pl = payloadOf(raw);
    if (pl && want.every((b, i) => pl[i] === b)) return pl;
  }
  return null;
}

// [name, mode] of the connected pedal, or [null, null].
export async function find(ports) {
  const names = await ports.names();
  let name = names.find((n) => n.includes(OPEN_PORT));
  if (name) return [name, MODE_OPEN];
  name = names.find((n) => n.includes(ANY_PORT));
  if (!name) return [null, null];
  let link;
  try { link = await ports.open(name); } catch { return [null, null]; }   // still restarting: "not there yet"
  let pl;
  try { pl = await ask(link, [0x12, 0x30], [0x11, 0x30], Timing.reply); } catch { return [null, null]; }
  finally { try { await link.close(); } catch { /* gone already */ } }
  if (pl && pl.length >= 4 && pl[3] === 1) return [name, MODE_LOADER];
  if (pl) return [name, MODE_STOCK];
  return [name, MODE_SILENT];
}

async function waitFor(ports, modes, timeout) {
  const end = now() + timeout;
  let last = [null, null];
  while (now() < end) {
    last = await find(ports);
    if (modes.includes(last[1])) return last;
    await sleep(Timing.poll);
  }
  return last;
}

async function toLoader(ports, say) {
  let [name, mode] = await find(ports);
  if (!name) throw new Problem("No GP-5 found on USB. Connect it and switch it on.");
  if (mode === MODE_LOADER) { say("The pedal is already in update mode."); return name; }
  say(`The pedal is running the ${mode}; asking it to restart into update mode.`);
  let ack = null;
  try {
    const link = await ports.open(name);
    try { ack = await ask(link, [0x11, 0x6F], [0x14, 0x08], Timing.reply); } finally { await link.close(); }
  } catch (e) { if (e instanceof Problem) throw e; }      // the port went away: it is already restarting
  if (!ack) say("  (no acknowledgement; waiting for the restart anyway)");
  [name, mode] = await waitFor(ports, [MODE_LOADER], Timing.reenum);
  if (mode !== MODE_LOADER)
    throw new Problem(`The pedal did not go into update mode${mode === MODE_SILENT ? " and does not answer" : ""}.\n${RECOVERY}`);
  return name;
}

async function sendAcked(link, raw, timeout) {
  link.flush();
  await link.send(raw);
  const end = now() + timeout;
  while (now() < end) {
    const got = await link.recv(Math.max(0, end - now()));
    if (got === null) return null;
    const st = ackStatus(payloadOf(got));
    if (st !== null) return st;
  }
  return null;
}

async function sendReliably(link, raw, timeout, what) {
  let st = null;
  for (let attempt = 0; attempt <= Timing.retries; attempt++) {
    if (attempt) await sleep(Timing.retryDelay);
    st = await sendAcked(link, raw, timeout);
    if (st === 0) return attempt;
  }
  throw new Problem(`The pedal stopped acknowledging at ${what} (${st === null ? "no answer" : "error code " + hex2(st)}).`);
}

// Find the pedal (in any mode), returning [name, mode] or [null, null].
export const status = find;

// Write `sections` ([[type, data]]). say(text) logs; progress(sectionName, done, total).
// Returns the mode the pedal is in afterwards (null if it did not re-appear).
export async function flash(ports, sections, say, progress) {
  const name = await toLoader(ports, say);
  const link = await ports.open(name);
  let started = false;
  try {
    if (!(await ask(link, [0x12, 0x30], [0x11, 0x30], Timing.reply))) throw new Problem(`The loader does not answer on ${name}.`);
    await sendReliably(link, buildSysex(CLASS_CMD, 0, [0x11, 0x60]), Timing.ack, "the start of the update");
    await sleep(Timing.settle);
    for (const [t, data] of sections) {
      const body = buildUpdateStream(data, t).slice(1, -1);       // BEGIN, DATA..., END_SEC
      const total = body.length - 2;
      say(`${NAMES[t]} (0x${hex2(t)}): erasing, then ${data.length} bytes in ${total} packets`);
      try {
        await sendReliably(link, body[0][1], Timing.begin, `the erase of the ${NAMES[t]} section`);
      } catch (e) {
        if (!started && e instanceof Problem)
          throw new Problem(e.message + "\nNothing was written. If this happens every time, the audio processor's updater is " +
                            "stalling (a known fault on at least one unit; see 'Known limits' in GETTING-STARTED.md).");
        throw e;
      }
      started = true;
      await sleep(Timing.beginSettle);
      let retried = 0;
      for (let i = 0; i < total; i++) {
        retried += await sendReliably(link, body[1 + i][1], Timing.ack, `${NAMES[t]} packet ${i + 1} of ${total}`);
        if (progress) progress(NAMES[t], i + 1, total);
      }
      await sendReliably(link, body[body.length - 1][1], Timing.ack, `the end of the ${NAMES[t]} section`);
      say(`${NAMES[t]}: written${retried ? ` (${retried} packets repeated)` : ""}`);
      await sleep(Timing.settle);
    }
    const st = await sendAcked(link, buildSysex(CLASS_CMD, 0, [0x11, 0x6E, 0x6E, 0x00]), Timing.final);
    if (st !== 0 && st !== null) throw new Problem(`The pedal refused to finish the update (error code ${hex2(st)}).`);
    say("Update finished" + (st === 0 ? "." : " (no final status; the pedal may already be restarting)."));
  } catch (e) {
    if (started && e instanceof Problem)
      throw new Problem(e.message + "\nThe update is INCOMPLETE: the pedal will stay in update mode until a flash completes.\n" + RECOVERY);
    throw e;
  } finally {
    try { await link.close(); } catch { /* gone */ }
  }
  const [after, mode] = await waitFor(ports, [MODE_OPEN, MODE_STOCK], Timing.reenum);
  return after ? mode : null;
}
