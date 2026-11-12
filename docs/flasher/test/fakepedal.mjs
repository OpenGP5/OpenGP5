// A simulated GP-5 (ports + factory loader protocol, as observed on the test unit), for tests and for
// trying the page without a pedal (index.html?simulate). Same model as tools/test_gp5tool.py's FakePedal.
import * as F from "../gp5flash.js";

const ACK = [0x14, 0x08, 0x00], NAK = [0x14, 0x08, 0x01];
const check = (cond, msg) => { if (!cond) throw new Error("simulated pedal: " + msg); };

export class FakePedal {
  constructor(mode = F.MODE_STOCK, o = {}) {
    Object.assign(this, { mode, nakEvery: 0, stallBegin: false, dieAt: 0, stayInLoader: false, mute: false, after: null }, o);
    this.present = true; this.written = {}; this.ended = false; this.sentCmds = []; this.sent = []; this.cur = null; this.n = 0;
  }
  async names() {
    if (!this.present) return [];
    return [this.mode === F.MODE_OPEN ? "GP-5 Open FW:GP-5 Open FW MIDI 1 16:0" : "GP-5:GP-5 MIDI 1 16:0"];
  }
  async open(name) {
    check((await this.names()).includes(name), "no such port");
    const p = this, q = [];
    return { async send(raw) { for (const r of p.handle(raw)) q.push(F.buildSysex(F.CLASS_CMD, 0, r)); },
             async recv() { return q.length ? q.shift() : null; }, flush() { q.length = 0; }, async close() {} };
  }
  handle(raw) {
    this.sent.push(Array.from(raw));
    const m = F.parseSysex(raw);
    if (!m.ok || this.mute) return [];
    const pl = Array.from(m.payload), is = (...b) => b.every((v, i) => pl[i] === v) && pl.length === b.length;
    if (m.cls === F.CLASS_CMD) this.sentCmds.push(pl.slice(0, 2).join(","));
    if (this.mode !== F.MODE_LOADER) {
      if (m.cls === F.CLASS_CMD && is(0x12, 0x30)) return [F.MODE_OPEN, F.MODE_SILENT].includes(this.mode) ? [] : [[0x11, 0x30, 1, 0]];
      if (m.cls === F.CLASS_CMD && is(0x11, 0x6F)) { this.mode = F.MODE_LOADER; return [ACK]; }
      return [];
    }
    if (m.cls === F.CLASS_CMD) {
      if (is(0x12, 0x30)) return [[0x11, 0x30, 1, 1]];
      if (is(0x11, 0x60)) return [ACK];
      if (pl[0] === 0x11 && pl[1] === 0x61) {
        if (this.stallBegin) return [];
        const dv = new DataView(Uint8Array.from(pl).buffer);
        this.cur = { type: pl[2], count: dv.getUint32(6, true), length: dv.getUint32(10, true), buf: [], next: 0 };
        return [ACK];
      }
      if (pl[0] === 0x11 && pl[1] === 0x6E && pl[2] === 0x6E) {
        this.ended = true;
        if (!this.stayInLoader) this.mode = this.after || (this.written[0x62] ? F.MODE_OPEN : F.MODE_STOCK);
        return [ACK];
      }
      if (pl[0] === 0x11 && pl[1] === 0x6E) {
        const c = this.cur;
        check(c && pl[2] === c.type && c.next === c.count && c.buf.length === c.length, "section incomplete at END");
        check(c.buf[0] === 0x11 && c.buf[1] === c.type, "section header");
        this.written[c.type] = Uint8Array.from(c.buf.slice(2));
        this.cur = null;
        return [ACK];
      }
      return [];
    }
    if (m.cls === F.CLASS_DATA && this.cur) {
      this.n++;
      if (this.dieAt && this.n >= this.dieAt) return [];
      if (this.nakEvery && this.n % this.nakEvery === 0) return [NAK];
      const c = this.cur;
      if (m.index !== (c.next & 0xFF)) return [NAK];
      c.buf.push(...m.payload); c.next++;
      return [ACK];
    }
    return [];
  }
}

