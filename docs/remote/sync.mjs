// Copy the shared pedal code into lib/ (the page is static: it carries its own copies, as Launchpad does):
//   web/flasher/gp5flash.js, catalog.js; work/nam-manager/gp5nam.js; Launchpad's device.js, params.js, fwmeta.js,
//   gp5store.js, dcformat.js, icons.js. Edit the originals, then run `node web/remote/sync.mjs`.
// `node sync.mjs --check` fails when a copy is out of date (run by the tests).
import { readFileSync, writeFileSync, existsSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
const root = new URL("../../", import.meta.url), lib = new URL("./lib/", import.meta.url);
const same = (s) => s;
const files = [
  ["web/flasher/gp5flash.js", "gp5flash.js", same],
  ["web/flasher/catalog.js", "catalog.js", same],
  ["work/nam-manager/gp5nam.js", "gp5nam.js", (s) => s.replace('"../../web/flasher/gp5flash.js"', '"./gp5flash.js"')],
  ["apps/launchpad/src/lib/device.js", "device.js", same],
  ["apps/launchpad/src/lib/params.js", "params.js", same],
  ["apps/launchpad/src/lib/fwmeta.js", "fwmeta.js", same],
  ["apps/launchpad/src/lib/gp5store.js", "gp5store.js", same],
  ["apps/launchpad/src/lib/dcformat.js", "dcformat.js", same],
  ["apps/launchpad/src/lib/icons.js", "icons.js", same],
];
const check = process.argv.includes("--check");
let stale = 0;
if (!check) mkdirSync(lib, { recursive: true });
for (const [src, dst, fix] of files) {
  const want = `// Copied from ${src} by web/remote/sync.mjs: edit the original, then run node web/remote/sync.mjs.\n` + fix(readFileSync(new URL(src, root), "utf8"));
  const out = new URL(dst, lib);
  if (check) { if (!existsSync(out) || readFileSync(out, "utf8") !== want) { console.error(`out of date: web/remote/lib/${dst}`); stale++; } }
  else writeFileSync(out, want);
}
if (check && stale) process.exit(1);
if (!check) console.log(`synced ${files.length} files into ${fileURLToPath(lib)}`);
