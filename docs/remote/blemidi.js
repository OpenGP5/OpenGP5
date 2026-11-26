// BLE-MIDI framing (the MIDI over Bluetooth Low Energy spec, as the pedal speaks it: platform/nds32/bt/ble.c).
// A packet (one characteristic value, at most 20 bytes with the pedal's MTU of 23) is
//   [header][timestamp][MIDI message]([timestamp][MIDI message])...
// header = 1 0 t12..t7, timestamp = 1 t6..t0 (milliseconds, 13 bits). A byte with bit 7 set is a timestamp unless it
// directly follows one: then it is a status byte. After a timestamp a data byte means running status. SysEx runs over
// several packets: the ones after the first carry only the header and data, and F7 gets a timestamp before it.

export const MIDI_SERVICE = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
export const MIDI_IO = "7772e5db-3868-4112-a1a9-f2669d106bf3";
export const PACKET = 20;                        // MTU 23 minus the 3 bytes of the ATT write / notification

const hdr = (ms) => 0x80 | ((ms >> 7) & 0x3F);
const ts = (ms) => 0x80 | (ms & 0x7F);

/** one SysEx message (F0 ... F7) -> the packets that carry it */
export function encodeSysex(msg, ms = 0, max = PACKET) {
  const m = Array.from(msg);
  if (m[0] !== 0xF0 || m[m.length - 1] !== 0xF7) throw new Error("not a SysEx message");
  ms &= 0x1FFF;
  const body = m.slice(1, -1), out = [];
  let p = [hdr(ms), ts(ms), 0xF0];
  for (const b of body) {
    if (p.length >= max) { out.push(p); p = [hdr(ms)]; }
    p.push(b);
  }
  if (p.length + 2 > max) { out.push(p); p = [hdr(ms)]; }
  p.push(ts(ms), 0xF7);
  out.push(p);
  return out.map((x) => Uint8Array.from(x));
}

/** channel / system messages (each [status, data...]) packed into as few packets as fit, each with its timestamp */
export function encodeMessages(msgs, ms = 0, max = PACKET) {
  ms &= 0x1FFF;
  const out = [];
  let p = [hdr(ms)];
  for (const m of msgs) {
    if (m[0] === 0xF0) { if (p.length > 1) out.push(p); out.push(...encodeSysex(m, ms, max).map((x) => Array.from(x))); p = [hdr(ms)]; continue; }
    if (p.length + 1 + m.length > max) { out.push(p); p = [hdr(ms)]; }
    p.push(ts(ms), ...m);
  }
  if (p.length > 1) out.push(p);
  return out.map((x) => Uint8Array.from(x));
}

const DATA_LEN = (s) => (s < 0xF0 ? ((s & 0xF0) === 0xC0 || (s & 0xF0) === 0xD0 ? 1 : 2) : s === 0xF1 || s === 0xF3 ? 1 : s === 0xF2 ? 2 : 0);

/** Reassembles packets into MIDI messages: push(packet) -> [{ data: Uint8Array, time }], time in ms (13 bits) */
export class Decoder {
  constructor() { this.sysex = null; this.running = 0; this.msg = null; this.time = 0; }
  reset() { this.sysex = null; this.running = 0; this.msg = null; }
  push(packet) {
    const p = packet instanceof Uint8Array ? packet : Uint8Array.from(packet), out = [];
    if (p.length < 2 || !(p[0] & 0x80)) return out;           // not a BLE-MIDI packet
    let high = p[0] & 0x3F, low = -1, afterTs = false;
    const emit = (data) => out.push({ data: Uint8Array.from(data), time: this.time });
    for (let i = 1; i < p.length; i++) {
      const b = p[i];
      if (b & 0x80 && !afterTs) {                              // a timestamp (its low 7 bits wrap: the high part counts on)
        const t = b & 0x7F;
        if (low >= 0 && t < low) high = (high + 1) & 0x3F;
        low = t; this.time = (high << 7) | t; afterTs = true;
        continue;
      }
      afterTs = false;
      if (b & 0x80) {                                          // a status byte
        if (b >= 0xF8) { emit([b]); continue; }                // real time: may sit inside SysEx
        if (b === 0xF0) { this.sysex = [0xF0]; this.msg = null; continue; }
        if (b === 0xF7) { if (this.sysex) { this.sysex.push(0xF7); emit(this.sysex); this.sysex = null; } continue; }
        this.sysex = null;                                     // anything else ends an unfinished SysEx (dropped)
        this.running = b < 0xF0 ? b : 0;                       // system common clears running status
        if (DATA_LEN(b) === 0) { emit([b]); this.msg = null; } else this.msg = [b];
        continue;
      }
      if (this.sysex) { this.sysex.push(b); continue; }        // SysEx data (continuation packets: no timestamps)
      if (!this.msg) {                                         // running status: a data byte after a timestamp, or
        if (!this.running) continue;                           // more data after a full message
        this.msg = [this.running];
      }
      this.msg.push(b);
      if (this.msg.length - 1 === DATA_LEN(this.msg[0])) { emit(this.msg); this.msg = null; }
    }
    return out;
  }
}
