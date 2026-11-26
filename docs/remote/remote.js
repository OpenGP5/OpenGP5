// What the remote page does with a connected pedal, without the screen: who it is, its settings (from the
// firmware's own descriptions, as Launchpad's Live page), live changes, presets. Every pedal command goes through
// one queue: the protocol is one question, one answer.
import { identify } from "./lib/device.js";
import { paramSet, paramsRead } from "./lib/device.js";
import { FLAG, UNIT, readDescriptions, readValues, setValue, valueText, formatValue, readPresets, loadPreset, savePreset, renamePreset } from "./lib/params.js";
import { DC } from "./lib/gp5store.js";
import { valueText as dcText } from "./lib/dcformat.js";

export { FLAG, UNIT };
export const HOLD_MS = 1500;                     // a value just changed here isn't overwritten by a poll for this long

// Firmwares that keep their own presets (not the platform's 7E 17): how to reach them through 7E 10 / 7E 11
const OWN_PRESETS = {
  dreamcatcher: { slots: DC.SLOTS, current: DC.P.PRESET, used: DC.P.USED, changed: DC.P.CHANGED, save: DC.P.SAVE },
};

export class Remote {
  constructor(link, { now = () => Date.now() } = {}) {
    this.link = link; this.now = now;
    this.fw = null; this.desc = []; this.vals = null; this.presets = null; this.presetKind = null;
    this.texts = new Map(); this.local = new Map(); this.pending = new Map(); this.sending = null;
    this.chain = Promise.resolve();
  }

  /** run fn(link) when the pedal is free */
  use(fn) {
    const run = this.chain.then(() => fn(this.link));
    this.chain = run.catch(() => {});
    return run;
  }

  /** identify the firmware and read what it describes. progress(text) */
  async load(progress = () => {}) {
    progress("Asking the pedal what it runs…");
    this.fw = await this.use((l) => identify(l));
    progress(`Reading ${this.fw.title}'s settings…`);
    this.desc = await this.use((l) => readDescriptions(l));
    if (this.desc.length) {
      progress("Reading the values…");
      this.vals = await this.use((l) => readValues(l, this.desc.length));
    }
    progress("Reading the presets…");
    await this.readPresets();
    await this.fetchTexts();
    return this;
  }

  get dreamcatcher() { return this.fw?.name === "dreamcatcher" || this.fw?.kind === "dreamcatcher"; }

  /** settings shown, grouped in the firmware's order: [{ name, items: [description] }] */
  groups() {
    const out = [];
    for (const d of this.desc) {
      if (d.flags & FLAG.HIDDEN) continue;
      let g = out.find((x) => x.name === d.group);
      if (!g) out.push(g = { name: d.group || "Settings", items: [] });
      g.items.push(d);
    }
    return out;
  }

  /** how a value reads */
  text(d, v = this.vals?.[d.id]) {
    if (v === undefined || v === null) return "…";
    if (d.choices) return formatValue(d, v);
    if (this.dreamcatcher && this.vals && DC.PARAMS[d.id]) {
      const t = dcText(DC.PARAMS[d.id], v, this.vals);
      if (t && t !== "?") return t.replace(/([A-Z]+)/g, (w) => (w.length > 2 && !/^(BPM|LP|HP|FB|RND|REV|FWD|LOFI)$/.test(w) ? w[0] + w.slice(1).toLowerCase() : w))
        .replace(/(\d)MS\b/, "$1 ms").replace(/(\d)S\b/, "$1 s").replace(/(\d)HZ\b/, "$1 Hz").replace(/(\d)KHZ\b/, "$1 kHz").replace(/(\d)K\b/, "$1 kHz");
    }
    if (d.unit === UNIT.CUSTOM) return this.texts.get(`${d.id}:${v}`) ?? String(v);
    return formatValue(d, v);
  }

  async fetchTexts() {
    if (!this.vals) return false;
    const want = this.desc.filter((d) => d.unit === UNIT.CUSTOM && !d.choices && !(d.flags & FLAG.HIDDEN) && !this.texts.has(`${d.id}:${this.vals[d.id]}`));
    if (!want.length) return false;
    await this.use(async (l) => { for (const d of want) { const v = this.vals[d.id]; this.texts.set(`${d.id}:${v}`, (await valueText(l, d.id, v)) ?? String(v)); } });
    if (this.texts.size > 2000) this.texts.clear();
    return true;
  }

  // ---------------------------------------------------------------- values

  /** change a setting: shown at once, sent as soon as the pedal is free (the latest value wins) */
  set(id, x) {
    const d = this.desc[id];
    if (!d || d.flags & FLAG.READONLY || !this.vals) return false;
    x = Math.max(d.min, Math.min(d.max, Math.round(x)));
    if (x === this.vals[id]) return false;
    this.vals[id] = x; this.local.set(id, this.now());
    this.pending.set(id, x);
    if (!this.sending) this.sending = this.flush();
    return true;
  }

  /** send what's waiting; resolves to false if the pedal refused a change */
  async flush() {
    let ok = true;
    try {
      while (this.pending.size) {
        const [id, v] = this.pending.entries().next().value;
        this.pending.delete(id);
        if (!(await this.use((l) => setValue(l, id, v)))) ok = false;
      }
      await this.fetchTexts();
    } finally { this.sending = null; }
    return ok;
  }

  /** read values and presets again (the pedal's own knob and footswitch change them). true if anything changed */
  async poll() {
    if (!this.desc.length && !this.presetKind) return false;
    if (this.sending) await this.sending;
    const before = JSON.stringify([this.vals, this.presets]);
    if (this.desc.length) {
      const got = await this.use((l) => readValues(l, this.desc.length));
      if (got) {
        const t = this.now();
        if (!this.vals) this.vals = got.slice();
        else got.forEach((v, i) => { const at = this.local.get(i); if (!this.pending.has(i) && (at === undefined || t - at >= HOLD_MS)) this.vals[i] = v; });
      }
    }
    await this.readPresets();
    await this.fetchTexts();
    return before !== JSON.stringify([this.vals, this.presets]);
  }

  // ---------------------------------------------------------------- presets

  /** { count, current, dirty, used: [bool], names: [string], rename: bool } or null */
  async readPresets() {
    if (this.presetKind !== "firmware") {
      const p = await this.use((l) => readPresets(l));
      if (p) { this.presetKind = "platform"; this.presets = { ...p, rename: true }; return this.presets; }
    }
    const own = OWN_PRESETS[this.fw?.name] || OWN_PRESETS[this.fw?.kind];
    if (own) {
      const r = await this.use((l) => paramsRead(l, own.current, 3));
      if (r && r.length >= 3) {
        const [current, used, changed] = r, names = this.presets?.names || Array(own.slots).fill("");
        this.presetKind = "firmware";
        this.presets = { count: own.slots, current: (used >> current) & 1 ? current : -1, dirty: !!changed,
                         used: Array.from({ length: own.slots }, (_, i) => !!((used >> i) & 1)), names, rename: false };
        return this.presets;
      }
    }
    this.presetKind = null; this.presets = null;
    return null;
  }

  async loadPreset(n) {
    if (this.sending) await this.sending;
    const own = this.ownPresets();
    const ok = await this.use((l) => (own ? paramSet(l, own.current, n, 1500) : loadPreset(l, n)));
    this.local.clear();
    if (ok && this.desc.length) this.vals = (await this.use((l) => readValues(l, this.desc.length))) || this.vals;
    await this.readPresets(); await this.fetchTexts();
    return ok;
  }

  /** save the current sound into slot n (writes the pedal's storage) */
  async savePreset(n) {
    if (this.sending) await this.sending;
    const own = this.ownPresets();
    const ok = await this.use((l) => (own ? paramSet(l, own.save, n, 3000) : savePreset(l, n)));
    await this.readPresets();
    return ok;
  }

  async renamePreset(n, name) {
    if (this.presetKind !== "platform") return false;
    const ok = await this.use((l) => renamePreset(l, n, name));
    await this.readPresets();
    return ok;
  }

  ownPresets() { return this.presetKind === "firmware" ? OWN_PRESETS[this.fw?.name] || OWN_PRESETS[this.fw?.kind] : null; }
}
