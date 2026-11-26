// A simulated pedal over Bluetooth, for the tests and for trying the page without one (index.html?simulate):
// a BLE-MIDI characteristic that answers the platform's commands (platform/nds32/main.c, docs/platform/protocols.md)
// for a firmware described like Dreamcatcher (its params.h), or one with platform presets.
import { buildSysex, parseSysex, CLASS_CMD } from "./lib/gp5flash.js";
import { encodeSysex, Decoder, MIDI_SERVICE, MIDI_IO } from "./blemidi.js";

const CRC = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc32 = (b) => { let c = 0xFFFFFFFF; for (const v of b) c = CRC[(c ^ v) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };

/** a firmware's metadata block (platform/manifest.py): "GP5META1", u32 length, JSON, NUL, padding, CRC-32 */
export function metaBlock(meta) {
  const json = new TextEncoder().encode(JSON.stringify(meta)), n = (json.length + 1 + 3) & ~3;
  const out = new Uint8Array(16 + n), dv = new DataView(out.buffer);
  out.set(new TextEncoder().encode("GP5META1")); dv.setUint32(8, n, true); out.set(json, 12); dv.setUint32(12 + n, crc32(json), true);
  return out;
}

// flags (gp5_params.h): DSP 1, PRESET 2, SAVE 4, READONLY 8, HIDDEN 16, CHOICE 0x80
const C = (name, group, choices, def, flags = 0) => ({ name, group, min: 0, max: choices.split("|").length - 1, def, unit: 0, flags: flags | 0x80, choices });
const P = (name, group, min, max, def, flags = 0, unit = 0) => ({ name, group, min, max, def, unit, flags, choices: "" });
export const DREAMCATCHER = {
  meta: { id: "dreamcatcher", name: "Dreamcatcher", version: "1.4.0", author: "OpenGP5" },
  params: [
    C("Effect", "Main", "Off|On", 1, 1), P("Mix", "Main", 0, 255, 128, 1), P("Clock", "Main", 0, 10, 10),
    C("Catch", "Loop", "Listening|Looping|Caught", 0, 8), C("Mode", "Loop", "Tape|Stretch", 0), P("Length", "Loop", 0, 255, 255),
    P("Speed", "Loop", 0, 255, 160), P("Loop level", "Loop", 0, 255, 200), C("Route", "Loop", "In|In+Loop|Loop", 1),
    P("Time", "Wet", 0, 255, 150), P("Smear", "Wet", 0, 255, 150), P("Chance", "Wet", 0, 255, 128),
    P("Rate", "Mod", 0, 255, 70), P("Depth", "Mod", 0, 255, 60), C("Shape", "Mod", "Sine|Square|Ramp|Saw|Rnd sin|Rnd sqr|Env", 0),
    C("Mod type", "Mod", "Tremolo|Vibrato", 0), P("Dissolve", "Color", 0, 255, 128), P("Glitch", "Wet", 0, 255, 85),
    P("Filter", "Color", 0, 255, 110), P("Grain", "Grain", 0, 255, 170), P("Pitch", "Grain", 0, 255, 0),
    C("Scale", "Grain", "Oct up|Oct down|Octaves|5th+oct|Harmony", 3), P("Reverse", "Grain", 0, 255, 0), P("Stutter", "Grain", 0, 255, 0),
    P("Tone", "Wet", 0, 255, 255), P("Fade", "Loop", 0, 255, 230), C("Freeze", "Loop", "Off|On", 0, 8), C("Overdub", "Loop", "Off|On", 0, 8),
    P("Decay", "Reverb", 0, 255, 150, 1), P("Reverb mix", "Reverb", 0, 255, 70, 1), P("Size", "Reverb", 0, 255, 180, 1),
    P("Pre-delay", "Reverb", 0, 255, 20, 1), P("Reverb mod", "Reverb", 0, 255, 80, 1), P("Shimmer", "Reverb", 0, 255, 0, 1),
    C("Sync", "Main", "Free|Sync", 0), P("BPM", "Main", 40, 240, 120),
  ],
  presets: "firmware",                         // its own (DC_P_PRESET 0x101 .. DC_P_SAVE 0x104), none through 7E 17
};
export const TREMOLO = {
  meta: { id: "tremolo", name: "Tremolo", version: "1.0.0", author: "OpenGP5" },
  params: [P("Rate", "Tremolo", 50, 1500, 500, 3, 7), P("Depth", "Tremolo", 0, 100, 50, 3, 1), C("Shape", "Tremolo", "Sine|Triangle|Square", 0, 3),
           P("Level", "Output", -120, 60, 0, 3, 2)],
  presets: "platform", slots: 4,
};

const le16 = (v) => [v & 0xFF, (v >> 8) & 0xFF];
const asciiz = (s) => [...new TextEncoder().encode(s), 0];

export class FakeBlePedal {
  /** o: { latency ms, dropFirst replies, packetGap } */
  constructor(fw = DREAMCATCHER, o = {}) {
    Object.assign(this, { latency: 2, dropFirst: 0 }, o);
    this.fw = fw;
    this.meta = metaBlock(fw.meta);
    this.vals = fw.params.map((p) => p.def);
    this.slots = fw.presets === "firmware" ? 8 : fw.slots || 0;
    this.used = 0b101; this.current = 0; this.dirty = 0;
    this.names = Array.from({ length: this.slots }, (_, i) => (i === 0 ? "Clouds" : i === 2 ? "Shimmer" : ""));
    this.saved = Array.from({ length: this.slots }, () => this.vals.slice());
    this.dec = new Decoder(); this.listeners = new Set(); this.notifying = false; this.connected = true;
    this.writes = []; this.cmds = []; this.saves = [];
  }

  // ---- the characteristic, as Web Bluetooth has it
  get characteristic() {
    const self = this;
    return {
      uuid: MIDI_IO,
      addEventListener(type, fn) { if (type === "characteristicvaluechanged") self.listeners.add(fn); },
      removeEventListener(type, fn) { self.listeners.delete(fn); },
      async startNotifications() { self.notifying = true; return this; },
      async stopNotifications() { self.notifying = false; return this; },
      async writeValueWithoutResponse(v) {
        if (!self.connected) throw new Error("GATT Server is disconnected.");
        const b = new Uint8Array(v.buffer ? v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength) : v);
        if (b.length > 20) throw new Error("longer than the MTU allows");
        self.writes.push(Array.from(b));
        for (const { data } of self.dec.push(b)) if (data[0] === 0xF0) self.command(data);
      },
    };
  }
  /** a navigator.bluetooth stand-in whose picker "finds" this pedal */
  bluetooth() {
    const self = this, dev = new EventTarget();
    dev.name = "OpenGP5"; dev.id = "fake";
    dev.gatt = {
      connected: false,
      async connect() { this.connected = true; self.connected = true; return { device: dev, async getPrimaryService(u) {
        if (u !== MIDI_SERVICE) throw new Error("no such service");
        return { async getCharacteristic(c) { if (c !== MIDI_IO) throw new Error("no such characteristic"); return self.characteristic; } };
      } }; },
      disconnect() { if (!this.connected) return; this.connected = false; self.connected = false; dev.dispatchEvent(new Event("gattserverdisconnected")); },
    };
    this.device = dev;
    return { async requestDevice() { return dev; }, async getAvailability() { return true; } };
  }
  /** the pedal goes away (switched off, out of range) */
  drop() { this.device?.gatt.disconnect(); this.connected = false; }

  notify(payload) {
    if (this.dropFirst > 0) { this.dropFirst--; return; }
    const packets = encodeSysex(buildSysex(CLASS_CMD, 0, payload), Date.now());
    setTimeout(() => {
      if (!this.notifying || !this.connected) return;
      for (const p of packets) {
        const ev = { target: { value: new DataView(p.buffer) } };
        for (const fn of this.listeners) fn(ev);
      }
    }, this.latency);
  }

  // ---- the platform's commands
  command(raw) {
    const m = parseSysex(raw);
    if (!m || !m.ok || m.cls !== CLASS_CMD) return;
    const pl = Array.from(m.payload);
    if (pl[0] !== 0x7E) return;
    this.cmds.push(pl[1]);
    const ps = this.fw.params, reply = (...b) => this.notify([0x7E, pl[1], ...b]);
    const s16 = (lo, hi) => { const v = lo | (hi << 8); return v & 0x8000 ? v - 0x10000 : v; };
    switch (pl[1]) {
      case 0x0D: return reply(1, 1, 0, 0, ...asciiz(this.fw.meta.id));
      case 0x16: {
        const off = pl[2] | (pl[3] << 8);
        return reply(1, ...le16(this.meta.length), ...this.meta.subarray(off, off + 48));
      }
      case 0x12: {
        const id = pl[2] | (pl[3] << 8), d = ps[id];
        if (!d) return reply(0);
        return reply(1, ...le16(ps.length), ...le16(id), d.flags, d.unit, ...le16(d.min), ...le16(d.max), ...le16(d.def),
                     ...asciiz(d.name), ...asciiz(d.group), ...asciiz(d.choices));
      }
      case 0x11: {
        const first = pl[2] | (pl[3] << 8), n = pl[4], out = [];
        const at = (id) => (id < ps.length ? this.vals[id] : id === 0x101 ? this.current : id === 0x102 ? this.used : id === 0x103 ? this.dirty : id === 0x100 ? 255 : 0);
        const known = first >= 0x100 ? Math.max(0, Math.min(n, 0x107 - first)) : Math.max(0, Math.min(n, ps.length - first));
        for (let i = 0; i < known; i++) out.push(...le16(at(first + i)));
        return reply(known, ...out);
      }
      case 0x10: {
        const id = pl[2] | (pl[3] << 8), v = pl[4] | (pl[5] << 8) | (pl[6] << 16) | (pl[7] << 24);
        if (id < ps.length) {
          const d = ps[id], x = s16(v & 0xFF, (v >> 8) & 0xFF);
          if (d.flags & 8) return reply(0);
          this.vals[id] = Math.max(d.min, Math.min(d.max, x)); this.dirty = 1;
          return reply(1);
        }
        if (this.fw.presets === "firmware" && id === 0x101) return reply(this.loadSlot(v));
        if (this.fw.presets === "firmware" && id === 0x104) return reply(this.saveSlot(v));
        return reply(0);
      }
      case 0x15: return reply(0);
      case 0x17: {
        const op = pl[2], n = pl[3];
        if (this.fw.presets !== "platform") return op === 0 ? reply(1, 0, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0) : reply(0);
        if (op === 0) return reply(1, this.slots, this.current, this.dirty, this.used & 0xFF, 0, 0, 0, 0, 0, 0, 0);
        if (op === 1) return reply(this.loadSlot(n));
        if (op === 2) return reply(this.saveSlot(n));
        if (op === 3) return reply(1, ...asciiz(this.names[n] || ""));
        if (op === 4) { this.names[n] = new TextDecoder().decode(Uint8Array.from(pl.slice(4))); return reply(1); }
        return reply(0);
      }
      default: return reply(0);
    }
  }
  loadSlot(n) {
    if (n >= this.slots || !((this.used >> n) & 1)) return 0;
    this.vals = this.saved[n].slice(); this.current = n; this.dirty = 0; return 1;
  }
  saveSlot(n) {
    if (n >= this.slots) return 0;
    this.saved[n] = this.vals.slice(); this.used |= 1 << n; this.current = n; this.dirty = 0; this.saves.push(n); return 1;
  }
}
