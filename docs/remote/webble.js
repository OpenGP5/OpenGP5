// The pedal over Web Bluetooth, as the `link` object gp5flash.js and Launchpad's device.js / params.js expect:
// send(bytes), recv(timeoutMs) -> SysEx message or null, flush(), close(), bluetooth: true.
// The pedal (OpenGP5 1.0 with "bluetooth": true) is a BLE-MIDI peripheral named "GP-5": one connection, MTU 23,
// a few packets per connection event. Its command protocol runs as SysEx over that (platform/nds32/proto.c).
import { MIDI_SERVICE, MIDI_IO, PACKET, encodeSysex, encodeMessages, Decoder } from "./blemidi.js";

export { MIDI_SERVICE, MIDI_IO };
export const EXTRA_MS = 800;                     // replies take a few connection events: added to every recv wait

const clock = () => Math.floor(typeof performance !== "undefined" ? performance.now() : Date.now());

/** Why this browser can't do it, or null when it can: { platform: "ios" | "android" | "other", text } */
export function bluetoothSupport(nav = globalThis.navigator, secure = globalThis.isSecureContext) {
  if (nav?.bluetooth?.requestDevice) return secure === false ? { platform: "other", text: "Bluetooth needs a secure page: open it over https:// (or http://localhost)." } : null;
  const ua = nav?.userAgent || "", ios = /iPhone|iPad|iPod/.test(ua) || (/Macintosh/.test(ua) && (nav?.maxTouchPoints || 0) > 1);
  if (ios) return { platform: "ios", text: "Safari can't talk to Bluetooth devices. Open this page in a free Web Bluetooth browser from the App Store, such as Bluefy or BLE Link." };
  if (/Android/.test(ua)) return { platform: "android", text: "This browser has no Web Bluetooth. Open the page in Chrome." };
  return { platform: "other", text: "This browser has no Web Bluetooth. Use Chrome or Edge (on a computer or Android), or Bluefy on an iPhone." };
}

/**
 * A link over a BLE-MIDI characteristic (a BluetoothRemoteGATTCharacteristic, or anything with the same shape:
 * writeValueWithoutResponse, startNotifications, stopNotifications, add/removeEventListener("characteristicvaluechanged")).
 * onMidi(data) gets the pedal's other MIDI (notes, controllers, program changes).
 */
export async function bleLink(chr, { extra = EXTRA_MS, onMidi = null, now = clock } = {}) {
  const dec = new Decoder(), queue = [];
  let waiter = null, closed = false, chain = Promise.resolve();
  const onValue = (ev) => {
    const dv = ev.target.value;
    for (const { data } of dec.push(new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength))) {
      if (data[0] !== 0xF0) { if (onMidi) onMidi(data); continue; }
      if (waiter) { const w = waiter; waiter = null; w(data); } else queue.push(data);
    }
  };
  chr.addEventListener("characteristicvaluechanged", onValue);
  await chr.startNotifications();
  const write = (packets) => {
    // one write at a time: the browser refuses a GATT operation while another is under way
    const run = chain.then(async () => { for (const p of packets) await chr.writeValueWithoutResponse(p); });
    chain = run.catch(() => {});
    return run;
  };
  return {
    bluetooth: true,
    async send(bytes) {
      if (closed) throw new Error("The pedal is disconnected.");
      await write(encodeSysex(bytes, now(), PACKET));
    },
    async sendMidi(msgs) { if (!closed) await write(encodeMessages(msgs, now(), PACKET)); },
    recv(timeout) {
      if (queue.length) return Promise.resolve(queue.shift());
      if (closed) return Promise.resolve(null);
      return new Promise((resolve) => {
        const t = setTimeout(() => { waiter = null; resolve(null); }, timeout + extra);
        waiter = (msg) => { clearTimeout(t); resolve(msg); };
      });
    },
    flush() { queue.length = 0; dec.reset(); },
    async close() {
      if (closed) return;
      closed = true;
      chr.removeEventListener("characteristicvaluechanged", onValue);
      if (waiter) { const w = waiter; waiter = null; w(null); }
      try { await chr.stopNotifications(); } catch { /* already gone */ }
    },
  };
}

/**
 * Ask the browser for the pedal (its device picker: needs a click), connect, and return
 * { device, link, disconnect() }. onGone() is called when the connection drops.
 */
export async function connectPedal({ bluetooth = globalThis.navigator?.bluetooth, onGone = () => {}, onMidi = null } = {}) {
  const device = await bluetooth.requestDevice({
    filters: [{ services: [MIDI_SERVICE] }, { name: "OpenGP5" }, { name: "GP-5" }],   /* "GP-5" before OpenGP5 1.0 */
    optionalServices: [MIDI_SERVICE],
  });
  const gone = () => onGone(device);
  device.addEventListener("gattserverdisconnected", gone);
  try {
    const server = await device.gatt.connect();
    const svc = await server.getPrimaryService(MIDI_SERVICE);
    const chr = await svc.getCharacteristic(MIDI_IO);
    const link = await bleLink(chr, { onMidi });
    return {
      device, link,
      async disconnect() {
        device.removeEventListener("gattserverdisconnected", gone);
        await link.close();
        if (device.gatt.connected) device.gatt.disconnect();
      },
    };
  } catch (e) {
    device.removeEventListener("gattserverdisconnected", gone);
    if (device.gatt?.connected) device.gatt.disconnect();
    throw e;
  }
}
