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
    def end_headers(self):
        # Niente cache sul .nro/latest.json: la Switch deve sempre vedere
        # l'ultima build senza dover riavviare il server.
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()
    def copyfile(self, source, outputfile):
        # Chunk da 1MB invece dei 64KB di default: meno syscall/segmenti
        # TCP, meglio su WiFi verso Switch.
        import shutil
        shutil.copyfileobj(source, outputfile, length=1024 * 1024)
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

    def handle_error(self, request, client_address):
        # Client che si disconnette a meta download (Switch): niente traceback.
        import socket
        _, exc, _ = sys.exc_info()
        if isinstance(exc, (ConnectionResetError, BrokenPipeError, socket.timeout)):
            print(f"[{client_address[0]}] disconnesso")
            return
        super().handle_error(request, client_address)

if __name__ == "__main__":
    import sys
    import threading
    import time
    port = int(sys.argv[1]) if len(sys.argv)>1 else 8000
    addr = ("0.0.0.0", port)
    print(f"Serving {DIST} su http://<ip>:{port}/  — POST /upload -> {LOG_DIR}/, /upload-save -> {SAVE_DIR}/")
    print(f"  update.cfg: url=http://<ip>:8000")
    # Watcher dist/: annuncia quando NRO/latest.json cambiano (niente restart,
    # i file sono letti da disco a ogni richiesta).
    def watch():
        seen = {}
        for name in ("OpenHomeNX.nro", "latest.json", "latest-r36s.json"):
            try:
                seen[name] = (DIST / name).stat().st_mtime
            except OSError:
                seen[name] = 0
        while True:
            time.sleep(2)
            for name in ("OpenHomeNX.nro", "latest.json", "latest-r36s.json"):
                try:
                    mt = (DIST / name).stat().st_mtime
                except OSError:
                    mt = 0
                if mt != seen[name]:
                    seen[name] = mt
                    ver, sha = "?", "?"
                    try:
                        import json
                        info = json.loads((DIST / "latest.json").read_text())
                        ver = info.get("version", "?")
                        sha = info.get("sha256", "?")[:12]
                    except OSError:
                        pass
                    print(f"[watch] {name} aggiornato -> v{ver} ({sha}) (gia servito, niente restart)")
    threading.Thread(target=watch, daemon=True).start()
    try:
        ThreadingHTTPServer(addr, Handler).serve_forever()
    except KeyboardInterrupt:
        print("\nserver fermato.")
