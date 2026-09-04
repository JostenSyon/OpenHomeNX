#!/usr/bin/env python3
"""Serve dist/ e accetta POST /upload per i log. Salva in dist/uploads/"""
import os, pathlib, datetime
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

DIST = pathlib.Path(__file__).parent.parent / "dist"
UPLOAD_DIR = DIST / "uploads"
UPLOAD_DIR.mkdir(exist_ok=True)

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=str(DIST), **kw)
    def do_POST(self):
        if self.path.split("?")[0] not in ("/upload", "/upload.log"):
            self.send_error(404, "usa POST /upload")
            return
        length = int(self.headers.get("Content-Length", 0) or 0)
        if length == 0 or length > 5*1024*1024:
            self.send_error(400, "body vuoto o >5MB")
            return
        data = self.rfile.read(length)
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        # prova a prendere IP client per nome file
        client = self.client_address[0].replace(".", "_")
        out = UPLOAD_DIR / f"debug_{ts}_{client}.log"
        out.write_bytes(data)
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
    print(f"Serving {DIST} su http://<ip>:{port}/  — POST /upload -> {UPLOAD_DIR}/")
    print(f"  update.cfg: url=http://<ip>:{port}")
    ThreadingHTTPServer(addr, Handler).serve_forever()
