// The browser's Web MIDI API as the `ports` object gp5flash.js expects: names(), open(name) -> link with
// send(bytes), recv(timeoutMs) -> message or null, flush(), close(). Needs SysEx permission.
import { portBase } from "./gp5flash.js";

export async function webMidiPorts() {
  if (!navigator.requestMIDIAccess) throw new Error("This browser has no Web MIDI. Use Chrome, Edge or Opera on a computer.");
  const access = await navigator.requestMIDIAccess({ sysex: true });
  const live = (map) => [...map.values()].filter((p) => p.state === "connected");

  return {
    async names() {
      const bases = new Set(live(access.inputs).map((p) => portBase(p.name)));
      return live(access.outputs).map((p) => p.name).filter((n) => bases.has(portBase(n)));
    },
    async open(name) {
      const out = live(access.outputs).find((p) => p.name === name);
      const inp = live(access.inputs).find((p) => p.name === name) ||
                  live(access.inputs).find((p) => portBase(p.name) === portBase(name));
      if (!out || !inp) throw new Error(`port ${name} is gone`);
      const queue = [];
      let waiter = null;
      const onMessage = (ev) => {
        if (ev.data[0] !== 0xF0) return;                       // notes and controllers from the pedal: not ours
        if (waiter) { const w = waiter; waiter = null; w(ev.data); } else queue.push(ev.data);
      };
      inp.addEventListener("midimessage", onMessage);
      await Promise.all([out.open(), inp.open()]);
      return {
        async send(bytes) { out.send(Array.from(bytes)); },
        recv(timeout) {
          if (queue.length) return Promise.resolve(queue.shift());
          return new Promise((resolve) => {
            const t = setTimeout(() => { waiter = null; resolve(null); }, timeout);
            waiter = (msg) => { clearTimeout(t); resolve(msg); };
          });
        },
        flush() { queue.length = 0; },
        async close() { inp.removeEventListener("midimessage", onMessage); },
      };
    },
  };
}
