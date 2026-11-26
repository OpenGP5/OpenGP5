// Copied from apps/launchpad/src/lib/fwmeta.js by web/remote/sync.mjs: edit the original, then run node web/remote/sync.mjs.
// A firmware's metadata block (OpenGP5 1.0, platform/manifest.py): "GP5META1", u32 length, the manifest as JSON,
// a NUL, padding to 4 bytes, the CRC-32 of the JSON. Read from an image before flashing, or from the pedal (7E 16).
// Its id decides who owns the pedal's storage; Launchpad's own pages know three ids, any other firmware is
// "fw:<id>" with the name, version and author it gives itself.
import { cmd } from "./device.js";

export { parseMeta } from "./catalog.js";              // (the reader is shared with the web flasher)
import { parseMeta } from "./catalog.js";
export const KNOWN = { dreamcatcher: "dreamcatcher", nam: "nam", tuner: "tuner" };

/** the kind Launchpad's pages use for a firmware id */
export const kindOf = (id) => KNOWN[id] || `fw:${id}`;

/** the running firmware's metadata over USB (7E 16, platform 1.0), or null for an older platform / firmware */
export async function readPedalMeta(link, timeout = 300) {
  const first = await cmd(link, [0x7E, 0x16, 0, 0], timeout);
  if (!first || first.length < 5 || first[2] !== 1) return null;
  const total = first[3] | (first[4] << 8), out = new Uint8Array(total);
  out.set(first.subarray(5, 5 + Math.min(48, total)), 0);
  for (let off = 48; off < total; off += 48) {
    const r = await cmd(link, [0x7E, 0x16, off & 0xFF, off >> 8], timeout);
    if (!r || r.length < 5 || r[2] !== 1) return null;
    out.set(r.subarray(5, 5 + Math.min(48, total - off)), off);
  }
  return parseMeta(out);
}
