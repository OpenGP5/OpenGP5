// OpenGP5 Remote: the page. Connects over Web Bluetooth (webble.js), then shows what the firmware describes
// (remote.js): its name, its settings by group, its presets. index.html?simulate runs against a simulated pedal.
import { connectPedal, bluetoothSupport } from "./webble.js";
import { Remote, FLAG } from "./remote.js";
import { icon } from "./lib/icons.js";

const POLL_MS = 1500;
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const params = new URLSearchParams(location.search);
const S = { conn: null, remote: null, timer: null, dragging: -1, busy: false, polling: false, fake: null, picking: false };

// ------------------------------------------------------------------ start

$("start-icon").innerHTML = icon("bluetooth", 40);
$("disconnect").innerHTML = icon("bluetooth-off", 22);
let bluetooth = navigator.bluetooth;
if (params.has("simulate")) {
  const { FakeBlePedal, DREAMCATCHER, TREMOLO } = await import("./fakeble.js");
  S.fake = new FakeBlePedal(params.get("simulate") === "tremolo" ? TREMOLO : DREAMCATCHER, { latency: 40 });
  bluetooth = S.fake.bluetooth();
  window.fakePedal = S.fake;                                        // for poking at it from the console
  $("connect").insertAdjacentHTML("beforebegin", `<p class="note info">${icon("info-circle", 18)} A simulated pedal: nothing is sent over Bluetooth.</p>`);
} else {
  const why = bluetoothSupport();
  if (why) {
    const n = $("unsupported");
    n.innerHTML = `${icon("alert-triangle", 20)}<div><p><strong>${why.platform === "ios" ? "Safari can't connect to the pedal." : "No Bluetooth in this browser."}</strong></p><p>${esc(why.text)}</p>${
      why.platform === "ios" ? `<p class="small-text" style="margin-top:6px">Copy this page's address, open it in Bluefy or BLE Link, and tap Connect there.</p>` : ""}</div>`;
    n.hidden = false;
    $("connect").disabled = true;
  }
}

$("connect").onclick = connect;
$("disconnect").onclick = async () => { await stop(); setState("Not connected"); };

function setState(text, kind = "") {
  $("state").textContent = text;
  $("dot").className = "dot " + kind;
}
function say(text) { $("live").textContent = ""; setTimeout(() => { $("live").textContent = text; }, 30); }
let toastTimer = null;
function toast(text) {
  const t = $("toast");
  t.textContent = text; t.hidden = false; say(text);
  clearTimeout(toastTimer); toastTimer = setTimeout(() => { t.hidden = true; }, 2600);
}
function showError(text) {
  const n = $("error");
  if (!text) { n.hidden = true; return; }
  n.innerHTML = `${icon("alert-triangle", 20)}<p>${esc(text)}</p>`;
  n.hidden = false;
}

async function connect() {
  showError(null);
  $("connect").disabled = true;
  try {
    setState("Pick the pedal…", "busy");
    S.conn = await connectPedal({ bluetooth, onGone: lost });
    setState("Connected, reading…", "busy");
    S.remote = new Remote(S.conn.link);
    await S.remote.load((t) => setState(t, "busy"));
    setState(S.remote.fw.title || S.conn.device.name || "Connected", "on");
    $("start").hidden = true; $("disconnect").hidden = false;
    render();
    say(`Connected. ${S.remote.fw.title}${S.remote.fw.version ? " " + S.remote.fw.version : ""}.`);
    S.timer = setInterval(poll, POLL_MS);
  } catch (e) {
    await stop();
    setState("Not connected");
    if (e?.name === "NotFoundError" && /cancel/i.test(e.message)) return;          // the picker was closed
    showError(explain(e));
  } finally { $("connect").disabled = !!bluetoothSupport() && !S.fake; }
}

function explain(e) {
  const m = String(e?.message || e);
  if (e?.name === "NotFoundError") return "No pedal found. Is it switched on, with an OpenGP5 firmware that has Bluetooth? If a computer or another phone is connected to it over Bluetooth, disconnect it there first.";
  if (e?.name === "NetworkError" || /GATT|connect/i.test(m)) return "Couldn't connect to the pedal. Move closer, make sure nothing else is connected to it, and try again.";
  if (e?.name === "SecurityError") return "The browser didn't allow Bluetooth here. Open the page over https://.";
  return m;
}

function lost() {
  setWorld(null);
  clearInterval(S.timer); S.timer = null;
  S.conn = null; S.remote = null;
  $("pedal").hidden = true; $("pedal").innerHTML = "";
  $("start").hidden = false; $("disconnect").hidden = true;
  setState("Disconnected");
  showError("The pedal disconnected (switched off, out of range, or another device took it). Tap Connect to try again.");
}

async function stop() {
  setWorld(null);
  clearInterval(S.timer); S.timer = null;
  const c = S.conn;
  S.conn = null; S.remote = null;
  $("pedal").hidden = true; $("pedal").innerHTML = "";
  $("start").hidden = false; $("disconnect").hidden = true;
  if (c) await c.disconnect().catch(() => {});
}

// ------------------------------------------------------------------ the pedal's page

// the firmware's colour (index.html: :root[data-fw]): Dreamcatcher violet, NAM Loader gold, OpenGP5 teal for the rest
const WORLD = { dreamcatcher: "dreamcatcher", nam: "nam" };
function setWorld(fw) {
  const w = fw && (WORLD[fw.name] || WORLD[fw.kind]);
  if (w) document.documentElement.dataset.fw = w; else delete document.documentElement.dataset.fw;
}

function render() {
  const r = S.remote, fw = r.fw, root = $("pedal");
  setWorld(fw);
  S.picking = false;
  let html = `<section class="now" id="now" aria-label="Now playing"></section>
    <section id="presets" aria-label="Presets" ${r.presets ? "" : "hidden"}></section>`;
  const groups = r.groups();
  if (!groups.length) html += `<section class="panel"><p class="empty-note">This firmware describes no settings, so there is nothing to turn from here.</p></section>`;
  groups.forEach((g, gi) => {
    html += `<details class="panel" open><summary><h2 id="g${gi}">${esc(g.name)}</h2>${icon("chevron-right", 20).replace('class="icon"', 'class="icon chev"')}</summary>
      <div class="ctrls" role="group" aria-labelledby="g${gi}">${g.items.map(control).join("")}</div></details>`;
  });
  root.innerHTML = html;
  root.hidden = false;
  renderPresets(true);
  wire(root);
  refresh();
}

// A knob: a 270-degree arc from 7 to 5 o'clock, the value's part in the firmware's colour, and a pointer
const KR = 26, KC = 2 * Math.PI * KR;
const frac = (d, v) => (d.max > d.min ? (v - d.min) / (d.max - d.min) : 0);
const arc = (f) => `${(KC * 0.75 * f).toFixed(2)} ${KC.toFixed(2)}`;
const angle = (f) => (-135 + 270 * f).toFixed(1);

function control(d) {
  const r = S.remote, v = r.vals?.[d.id] ?? d.def, id = `p${d.id}`;
  if (d.flags & FLAG.READONLY)
    return `<div class="ctl ro" data-p="${d.id}"><span class="v" data-v id="${id}-v"></span><span class="nm" id="${id}-l">${esc(d.name)}</span></div>`;
  if (d.choices && d.choices.length <= 4)
    return `<div class="ctl wide" data-p="${d.id}"><span class="nm" id="${id}-l">${esc(d.name)}</span>
      <div class="seg" role="radiogroup" aria-labelledby="${id}-l">${d.choices.map((c, i) =>
        `<button type="button" role="radio" data-val="${i}" aria-checked="${i === v}" tabindex="${i === v ? 0 : -1}">${esc(c)}</button>`).join("")}</div></div>`;
  if (d.choices)
    return `<div class="ctl wide" data-p="${d.id}"><label class="nm" for="${id}">${esc(d.name)}</label>
      <select id="${id}">${d.choices.map((c, i) => `<option value="${i}" ${i === v ? "selected" : ""}>${esc(c)}</option>`).join("")}</select></div>`;
  const f = frac(d, v);
  return `<div class="ctl" data-p="${d.id}">
    <div class="knob" role="slider" tabindex="0" aria-label="${esc(d.name)}" aria-valuemin="${d.min}" aria-valuemax="${d.max}" aria-valuenow="${v}">
      <svg viewBox="0 0 64 64" aria-hidden="true">
        <circle class="trk" cx="32" cy="32" r="${KR}" stroke-dasharray="${arc(1)}" transform="rotate(135 32 32)"/>
        <circle class="val" cx="32" cy="32" r="${KR}" stroke-dasharray="${arc(f)}" transform="rotate(135 32 32)"/>
        <circle class="cap" cx="32" cy="32" r="17"/>
        <line class="ptr" x1="32" y1="26" x2="32" y2="18" transform="rotate(${angle(f)} 32 32)"/>
      </svg></div>
    <span class="v" data-v></span><span class="nm">${esc(d.name)}</span></div>`;
}

function change(d, x) {
  const r = S.remote;
  x = Math.max(d.min, Math.min(d.max, Math.round(x)));
  if (!r || x === (r.vals?.[d.id] ?? d.def) || !r.set(d.id, x)) return;
  refresh(d.id);
  const sent = r.sending;
  sent?.then((ok) => { if (ok === false) showError("The pedal didn't take a change."); else showError(null); refresh(); })
      .catch((e) => showError(`Couldn't reach the pedal: ${e.message}`));
}

// A knob's input: drag up / down (160 px = the whole range), arrow keys, Page Up / Down, Home / End; double-click: default
function wireKnob(k, d) {
  const r = S.remote, span = d.max - d.min, now = () => r.vals?.[d.id] ?? d.def;
  let y0 = 0, x0 = 0, pid = null;
  k.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    e.preventDefault(); k.focus({ preventScroll: true });
    try { k.setPointerCapture(e.pointerId); } catch { /* keep turning without capture */ }
    pid = e.pointerId; S.dragging = d.id; y0 = e.clientY; x0 = now(); k.classList.add("turning");
  });
  k.addEventListener("pointermove", (e) => {
    if (e.pointerId !== pid) return;
    change(d, x0 + ((y0 - e.clientY) / 160) * Math.max(span, 8));   // few-step knobs still need some travel per step
  });
  const up = (e) => {
    if (e.pointerId !== pid) return;
    try { k.releasePointerCapture(e.pointerId); } catch { /* not captured */ }
    pid = null; if (S.dragging === d.id) S.dragging = -1; k.classList.remove("turning");
  };
  k.addEventListener("pointerup", up); k.addEventListener("pointercancel", up);
  k.addEventListener("keydown", (e) => {
    const x = now(), big = span > 32 ? Math.round(span / 10) : 1;
    const to = { ArrowUp: x + 1, ArrowRight: x + 1, ArrowDown: x - 1, ArrowLeft: x - 1, PageUp: x + big, PageDown: x - big, Home: d.min, End: d.max }[e.key];
    if (to === undefined) return;
    e.preventDefault(); change(d, to);
  });
  k.addEventListener("dblclick", () => change(d, d.def));
}

function wire(root) {
  const r = S.remote;
  root.querySelectorAll("[data-p]").forEach((el) => {
    const d = r.desc[+el.dataset.p];
    el.querySelectorAll("[role=radio]").forEach((b) => {
      b.onclick = () => change(d, +b.dataset.val);
      b.onkeydown = (e) => {
        const n = d.choices.length, i = +b.dataset.val;
        const to = { ArrowRight: i + 1, ArrowDown: i + 1, ArrowLeft: i - 1, ArrowUp: i - 1, Home: 0, End: n - 1 }[e.key];
        if (to === undefined) return;
        e.preventDefault();
        const j = (to + n) % n; change(d, j); el.querySelector(`[data-val="${j}"]`)?.focus();
      };
    });
    const sel = el.querySelector("select");
    if (sel) sel.onchange = () => change(d, +sel.value);
    const k = el.querySelector(".knob");
    if (k) wireKnob(k, d);
  });
}

// show the values (all, or one setting) without rebuilding: a knob being turned keeps its place
function refresh(only = -1) {
  const r = S.remote;
  if (!r?.vals) return;
  for (const d of r.desc) {
    if (only >= 0 && d.id !== only) continue;
    const el = document.querySelector(`[data-p="${d.id}"]`);
    if (!el) continue;
    const v = r.vals[d.id], t = r.text(d, v);
    const out = el.querySelector("[data-v]"); if (out && out.textContent !== t) out.textContent = t;
    const k = el.querySelector(".knob");
    if (k) {
      const f = frac(d, v);
      k.querySelector(".val").setAttribute("stroke-dasharray", arc(f));
      k.querySelector(".ptr").setAttribute("transform", `rotate(${angle(f)} 32 32)`);
      k.setAttribute("aria-valuenow", v); k.setAttribute("aria-valuetext", t);
    }
    el.querySelectorAll("[role=radio]").forEach((b) => { const on = +b.dataset.val === v; b.setAttribute("aria-checked", on); b.tabIndex = on ? 0 : -1; });
    const sel = el.querySelector("select"); if (sel && +sel.value !== v && document.activeElement !== sel) sel.value = v;
  }
}

// ------------------------------------------------------------------ presets: the display, the pads, Save

let presetsKey = "";
function renderPresets(force = false) {
  const r = S.remote, box = $("presets"), now = $("now");
  if (!box || !now) return;
  const p = r.presets, fw = r.fw;
  const key = JSON.stringify([p, S.picking]);
  if (!force && key === presetsKey) return;
  presetsKey = key;
  const fwLine = `${esc(fw.title)}${fw.version ? ` ${esc(fw.version)}` : ""}`;
  if (!p) {                                   // a firmware without presets: the display shows the firmware
    now.innerHTML = `<div class="fw">${fwLine}</div><span></span><div class="mid"><div class="name">${esc(fw.title)}</div>
      <div class="sub">${fw.meta?.author ? `by ${esc(fw.meta.author)}` : ""}</div></div><span></span>`;
    box.hidden = true; return;
  }
  box.hidden = false;
  const cur = p.current, has = cur >= 0, used = p.used;
  const nameOf = (i) => (used[i] ? p.names[i] || "Saved sound" : "Empty");
  const usedIdx = used.map((u, i) => (u ? i : -1)).filter((i) => i >= 0);
  const prevI = has ? [...usedIdx].reverse().find((i) => i < cur) : undefined, nextI = has ? usedIdx.find((i) => i > cur) : usedIdx[0];
  now.innerHTML = `<div class="fw">${fwLine}</div>
    <button type="button" class="step" id="prev" aria-label="Previous preset" ${prevI === undefined ? "disabled" : ""}>${icon("chevron-left", 28)}</button>
    <div class="mid" aria-live="polite">
      <div class="num">${has ? `P${cur + 1}` : "–"}</div>
      <div class="name">${has ? esc(nameOf(cur)) : "No preset loaded"}${has && p.rename && used[cur]
        ? `<button type="button" class="rename" id="rename" aria-label="Rename P${cur + 1}">${icon("pencil", 18)}</button>` : ""}</div>
      <div class="sub ${p.dirty ? "changed" : ""}">${p.dirty ? "Changed since saved" : has ? "As saved" : "Tap a pad to load one"}</div>
    </div>
    <button type="button" class="step" id="next" aria-label="Next preset" ${nextI === undefined ? "disabled" : ""}>${icon("chevron-right", 28)}</button>`;
  box.className = S.picking ? "picking" : "";
  box.innerHTML = `<div class="pads" role="group" aria-label="Presets">${used.map((u, i) => {
      const on = i === cur, nm = nameOf(i);
      const label = S.picking ? `Save into P${i + 1}${u ? `, replacing ${nm}` : ""}` : `P${i + 1}, ${nm}${on ? ", playing" : ""}`;
      return `<button type="button" class="pad ${u ? "" : "empty"} ${on && !S.picking ? "on" : ""}" data-slot="${i}" ${!u && !S.picking ? "disabled" : ""}
        aria-label="${esc(label)}" ${on ? 'aria-current="true"' : ""}><span class="pn">${i + 1}</span><span class="pl">${esc(u ? (p.names[i] || "") : "empty")}</span></button>`;
    }).join("")}</div>
    ${S.picking ? `<p class="pickhint" role="status">Tap a pad to save the current sound into it.</p>
      <div class="acts"><button type="button" class="btn ghost" id="pick-cancel">Cancel</button></div>`
    : `<div class="acts"><button type="button" class="btn" id="save" ${has ? "" : "hidden"}>${icon("device-floppy", 20)} Save to P${cur + 1}</button>
      <button type="button" class="btn ghost" id="saveto">${has ? "Save to…" : `${icon("device-floppy", 20)} Save to a pad…`}</button></div>`}`;

  const load = (n) => presetDo(`Loaded P${n + 1}`, "Loading didn't work on the pedal.", () => r.loadPreset(n), true);
  $("prev").onclick = () => prevI !== undefined && load(prevI);
  $("next").onclick = () => nextI !== undefined && load(nextI);
  const rn = $("rename");
  if (rn) rn.onclick = async () => {
    const name = await ask(`Rename P${cur + 1}`, "Up to 12 letters, digits and spaces.", "Rename", p.names[cur] || "");
    if (name === null || name === false) return;
    presetDo("Renamed", "Renaming didn't work on the pedal.", () => r.renamePreset(cur, name.trim()));
  };
  const saveInto = async (n) => {
    const holds = used[n] && n !== cur;
    if (!(await ask(`Save into P${n + 1}?`, holds ? `P${n + 1} holds ${p.names[n] ? `"${p.names[n]}"` : "a sound"}; the current sound replaces it. This writes to the pedal's memory.`
                                                   : "This writes the current sound into the pedal's memory.", "Save"))) return;
    S.picking = false;
    presetDo(`Saved into P${n + 1}`, "Saving didn't work on the pedal.", () => r.savePreset(n));
  };
  box.querySelectorAll("[data-slot]").forEach((b) => {
    const n = +b.dataset.slot;
    b.onclick = () => (S.picking ? saveInto(n) : load(n));
  });
  const sv = $("save"); if (sv) sv.onclick = () => saveInto(cur);
  const st = $("saveto"); if (st) st.onclick = () => { S.picking = true; renderPresets(true); box.querySelector(".pad")?.focus(); };
  const pc = $("pick-cancel"); if (pc) pc.onclick = () => { S.picking = false; renderPresets(true); $("saveto")?.focus(); };
}

async function presetDo(done, failed, fn, values = false) {
  if (S.busy) return;
  S.busy = true;
  try {
    const ok = await fn();
    if (ok) { toast(done); showError(null); } else showError(failed);
    if (values) refresh();
    renderPresets(true);
  } catch (e) { showError(`Couldn't reach the pedal: ${e.message}`); }
  finally { S.busy = false; }
}

// a small dialog: true / false, or the text typed (null when cancelled) when `value` is given
function ask(title, text, ok, value) {
  const dlg = $("dlg"), inp = $("dlg-in");
  $("dlg-h").textContent = title; $("dlg-p").textContent = text; $("dlg-ok").textContent = ok;
  inp.hidden = value === undefined; inp.value = value ?? "";
  return new Promise((resolve) => {
    // answered from the buttons themselves (a dialog's close event waits for the page to be drawn)
    const done = (ok) => {
      form.onsubmit = null; dlg.oncancel = null; $("dlg-cancel").onclick = null;
      if (dlg.open) dlg.close();
      resolve(ok ? (value === undefined ? true : inp.value) : value === undefined ? false : null);
    };
    const form = dlg.querySelector("form");
    form.onsubmit = (e) => { e.preventDefault(); done(true); };        // OK, or Enter in the name
    $("dlg-cancel").onclick = () => done(false);
    dlg.oncancel = (e) => { e.preventDefault(); done(false); };
    dlg.showModal();
    if (value !== undefined) { inp.focus(); inp.select(); }
  });
}

// ------------------------------------------------------------------ keeping up with the pedal's own controls

async function poll() {
  const r = S.remote;
  if (!r || S.polling || S.busy || S.dragging >= 0 || document.hidden) return;
  S.polling = true;
  try {
    if (await r.poll()) { refresh(); renderPresets(); }
  } catch { /* a missed poll: the next one */ }
  finally { S.polling = false; }
}
