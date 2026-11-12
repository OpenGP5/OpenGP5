#!/usr/bin/env python3
"""Drive the web flasher page in headless Chromium against a real pedal (hardware test of web/flasher).

  web_flasher_drive.py <page url> status
  web_flasher_drive.py <page url> flash <firmware title> --i-mean-it

Starts Chromium headless with remote debugging, grants the page MIDI + SysEx permission (the prompt a user
clicks), then clicks through the page like a user and prints what the page shows. Standard library only
(minimal WebSocket client for the DevTools protocol).
"""
import base64, json, os, socket, struct, subprocess, sys, tempfile, time, urllib.parse, urllib.request

PORT = 9333


class WS:
    def __init__(self, url):
        u = urllib.parse.urlparse(url)
        self.s = socket.create_connection((u.hostname, u.port))
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f"GET {u.path} HTTP/1.1\r\nHost: {u.hostname}:{u.port}\r\nUpgrade: websocket\r\n"
                        f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        buf = b""
        while b"\r\n\r\n" not in buf: buf += self.s.recv(4096)
        assert b" 101 " in buf.split(b"\r\n")[0], buf[:100]
        self.rest, self.n = buf.split(b"\r\n\r\n", 1)[1], 0

    def send(self, obj):
        data = json.dumps(obj).encode()
        mask = os.urandom(4)
        head = bytes([0x81]) + (bytes([0x80 | len(data)]) if len(data) < 126 else bytes([0x80 | 126]) + struct.pack(">H", len(data))
                                if len(data) < 65536 else bytes([0x80 | 127]) + struct.pack(">Q", len(data)))
        self.s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _read(self, n):
        while len(self.rest) < n: self.rest += self.s.recv(65536)
        out, self.rest = self.rest[:n], self.rest[n:]
        return out

    def recv(self):
        b0, b1 = self._read(2)
        ln = b1 & 0x7F
        if ln == 126: ln = struct.unpack(">H", self._read(2))[0]
        elif ln == 127: ln = struct.unpack(">Q", self._read(8))[0]
        return json.loads(self._read(ln))

    def call(self, method, **params):
        self.n += 1
        self.send({"id": self.n, "method": method, "params": params})
        while True:
            m = self.recv()
            if m.get("id") == self.n:
                if "error" in m: raise RuntimeError(m["error"])
                return m.get("result", {})


def js(page, expr):
    r = page.call("Runtime.evaluate", expression=expr, awaitPromise=True, returnByValue=True)
    if "exceptionDetails" in r: raise RuntimeError(r["exceptionDetails"])
    return r["result"].get("value")


def main():
    url, what = sys.argv[1], sys.argv[2]
    if what == "flash" and "--i-mean-it" not in sys.argv: sys.exit("flash needs --i-mean-it")
    prof = tempfile.mkdtemp()
    chrome = subprocess.Popen(["chromium", "--headless=new", f"--remote-debugging-port={PORT}", f"--user-data-dir={prof}",
                               "--no-first-run", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try: info = json.load(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json/version")); break
            except OSError: time.sleep(0.1)
        browser = WS(info["webSocketDebuggerUrl"])
        origin = "{0.scheme}://{0.netloc}".format(urllib.parse.urlparse(url))
        browser.call("Browser.grantPermissions", origin=origin, permissions=["midi", "midiSysex"])
        tgt = json.load(urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{PORT}/json/new?{url}", method="PUT")))
        page = WS(tgt["webSocketDebuggerUrl"])
        time.sleep(1.5)
        js(page, 'document.getElementById("connect").click()')
        for _ in range(60):
            time.sleep(0.25)
            st = js(page, 'document.getElementById("status").innerText')
            if not st.startswith("Looking") and not st.startswith("Plug"): break
        print("status:", st)
        if what == "flash":
            title = sys.argv[3]
            js(page, f'''[...document.querySelectorAll(".choice")].find(c => c.innerText.includes({json.dumps(title)})).querySelector("input").click()''')
            time.sleep(2)
            print("image:", js(page, 'document.getElementById("imageinfo").hidden ? document.getElementById("imageerror").innerText : document.getElementById("sections").innerText'))
            js(page, 'document.getElementById("agree").click()')
            assert js(page, '!document.getElementById("flash").disabled'), "flash button stays disabled"
            js(page, 'document.getElementById("flash").click()')
            t0, last = time.time(), ""
            while time.time() - t0 < 300:
                time.sleep(1)
                lab = js(page, 'document.getElementById("progresslabel").innerText')
                if lab != last and not lab.startswith("Writing") or lab.endswith("0 %") and lab != last: print("  ", lab); last = lab
                if not js(page, 'document.getElementById("result").hidden'): break
            print("log:\n" + js(page, 'document.getElementById("log").innerText'))
            print("result:", js(page, 'document.getElementById("result").innerText'))
            time.sleep(3)
            print("status after:", js(page, 'document.getElementById("status").innerText'))
    finally:
        chrome.terminate()


if __name__ == "__main__":
    main()
