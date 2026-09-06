#!/usr/bin/env python3
"""Serve dist/ e accetta upload: POST /upload -> log, POST /upload-save -> save.
Organizza in dist/uploads/logs/ e dist/uploads/saves/ (i vecchi file restano
in dist/uploads/ dov'erano, nessun move automatico)."""
import os, pathlib, datetime
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

DIST = pathlib.Path(__file__).parent.parent / "dist"
LOG_DIR = DIST / "uploads" / "logs"
SAVE_DIR = DIST / "uploads" / "saves"
LOG_DIR.mkdir(parents=True, exist_ok=True)
SAVE_DIR.mkdir(parents=True, exist_ok=True)

MAX_LOG = 5 * 1024 * 1024
MAX_SAVE = 128 * 1024 * 1024

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=str(DIST), **kw)
    def do_POST(self):
        from urllib.parse import urlsplit, parse_qs
        parts = urlsplit(self.path)
        if parts.path in ("/upload", "/upload.log"):
            return self._store(LOG_DIR, ".log", MAX_LOG, parts)
        if parts.path == "/upload-save":
            return self._store(SAVE_DIR, ".sav", MAX_SAVE, parts)
        self.send_error(404, "usa POST /upload o /upload-save")
    def _store(self, directory, ext, cap, parts):
        from urllib.parse import parse_qs
        # ?f=nome -> distingue più file dello stesso invio (es. libusbhsfs, Red)
        tag = "".join(parse_qs(parts.query).get("f", ["debug"]))[:24] or "debug"
        tag = "".join(c if (c.isalnum() or c in "-_") else "_" for c in tag)
        length = int(self.headers.get("Content-Length", 0) or 0)
        if length == 0 or length > cap:
            self.send_error(400, f"body vuoto o >{cap // (1024 * 1024)}MB")
            return
        data = self.rfile.read(length)
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        # prova a prendere IP client per nome file
        client = self.client_address[0].replace(".", "_")
        out = directory / f"{tag}_{ts}_{client}{ext}"
        try:
            out.write_bytes(data)
        except OSError as e:
            self.send_error(500, f"scrittura fallita: {e}")
            return
        print(f"[upload] {len(data)} byte -> {out}")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"ok")
    def log_message(self, fmt, *args):
        # log anche su stdout con timestamp
        print(f"{self.client_address[0]} - - [{self.log_date_time_string()}] {fmt%args}")

if __name__ == "__main__":
    import sys
    port = int(sys.argv[1]) if len(sys.argv)>1 else 8000
    addr = ("0.0.0.0", port)
    print(f"Serving {DIST} su http://<ip>:{port}/  — POST /upload -> {LOG_DIR}/, /upload-save -> {SAVE_DIR}/")
    print(f"  update.cfg: url=http://<ip>:8000")
    ThreadingHTTPServer(addr, Handler).serve_forever()
