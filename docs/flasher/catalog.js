// The firmware catalog (OpenGP5's catalog/README.md): the listings Launchpad and the web flasher offer, read from
// catalog.json on OpenGP5's site. Shared by both (Launchpad keeps a synced copy in src/lib). No network here: the
// caller fetches; this decides what to show and checks what was downloaded.
export const INDEX_URL = "https://opengp5.github.io/OpenGP5/catalog.json";
export const SITE = "https://opengp5.github.io/OpenGP5/";
const URL_OK = /^(https:\/\/|http:\/\/(localhost|127\.0\.0\.1)(:\d+)?\/)/;   // (a local address: only for trying things out)

/** the firmwares worth showing: listings with a release, newest release first, OpenGP5's own first, then by name */
export function entries(index) {
  const list = (index && Array.isArray(index.firmwares) ? index.firmwares : []).filter((e) =>
    e && typeof e.id === "string" && typeof e.name === "string" && Array.isArray(e.releases) && e.releases.length &&
    e.releases.every((r) => r && typeof r.url === "string" && URL_OK.test(r.url) && /^[0-9a-f]{64}$/.test(r.sha256) && Number.isInteger(r.size)));
  return list.map((e) => ({ ...e, latest: e.releases[0], official: e.official === true }))
    .sort((a, b) => (b.official - a.official) || a.name.localeCompare(b.name));
}

/** a release that the page can download itself (OpenGP5's site, or the page's own origin); others (GitHub release files) can't be fetched from
 * a web page: the person downloads them and chooses the file */
export const fetchable = (release, origin = "") => release.url.startsWith(SITE) || (!!origin && release.url.startsWith(origin + "/"));

/** the listing and release a file belongs to, by its checksum, or null */
export function findBySha(index, sha) {
  for (const e of entries(index)) for (const r of e.releases) if (r.sha256 === sha) return { entry: e, release: r };
  return null;
}

export async function sha256(bytes) {
  const d = await globalThis.crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

/** what's wrong with downloaded bytes for this release ([] = all is well): size, checksum, the image's own id and version */
export async function check(bytes, entry, release) {
  const errs = [];
  if (bytes.length !== release.size) errs.push(`it is ${bytes.length} bytes, the catalog says ${release.size}`);
  if ((await sha256(bytes)) !== release.sha256) errs.push("its checksum doesn't match the catalog");
  const m = parseMeta(bytes);
  if (!m) errs.push("it doesn't say what it is (no metadata)");
  else {
    if (m.id !== entry.id) errs.push(`it says it is '${m.id}', not '${entry.id}'`);
    if (m.version !== release.version) errs.push(`it says version ${m.version}, not ${release.version}`);
  }
  return errs;
}

/** what to tell someone before they install a listing */
export const caution = (entry) => entry.official
  ? "From OpenGP5's own repositories."
  : `Not reviewed by OpenGP5: made by ${entry.author}. The catalog checks the file is the one listed, not what it does. ` +
    "Install firmware you trust, and know how to recover the pedal (unplug, hold the foot switch and PARA, plug in, flash again).";

// ---- a firmware's metadata block (platform/manifest.py): "GP5META1", u32 length, the manifest as JSON, a NUL,
// padding to 4 bytes, the CRC-32 of the JSON
const MAGIC = [0x47, 0x50, 0x35, 0x4D, 0x45, 0x54, 0x41, 0x31];
const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(b) { let c = 0xFFFFFFFF; for (const v of b) c = CRC[(c ^ v) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }

/** the metadata in bytes (an image, an app section or the block itself), or null */
export function parseMeta(bytes) {
  outer: for (let i = 0; i + 16 <= bytes.length; i++) {
    for (let k = 0; k < 8; k++) if (bytes[i + k] !== MAGIC[k]) continue outer;
    const dv = new DataView(bytes.buffer, bytes.byteOffset + i);
    const n = dv.getUint32(8, true);
    if (n > 65536 || i + 16 + n > bytes.length) continue;
    const body = bytes.subarray(i + 12, i + 12 + n);
    let end = body.length; while (end > 0 && body[end - 1] === 0) end--;
    if (crc32(body.subarray(0, end)) !== dv.getUint32(12 + n, true)) continue;
    try { const m = JSON.parse(new TextDecoder().decode(body.subarray(0, end))); if (m && typeof m.id === "string") return m; } catch { /* not ours */ }
  }
  return null;
}
