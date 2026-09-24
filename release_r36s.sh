#!/bin/bash
# release_r36s.sh — build + dist per OpenHomeNX su R36S (ArkOS).
#
# Gemello di release.sh (Switch): produce lo zip versionato via
# platform/r36s/package_r36s.sh e scrive dist/latest-r36s.json
# { version, zip, sha256 } in un solo passaggio atomico, cosi' versione e
# sha256 non si disallineano mai dal binario vero. Il manifest punta allo
# zip (non al binario sciolto): il client R36S scarica + scompatta.
#
# Uso: ./release_r36s.sh         -> package zip + dist/latest-r36s.json
#      ./release_r36s.sh serve    -> come sopra + python server su dist
# Per far scattare l'update in prova serve version > quella sul device:
# bump APP_VERSION nel Makefile prima (stesso meccanismo Switch).
set -e
cd "$(dirname "$0")"

# Ctrl-C a meta' build: messaggio pulito invece della cascata di errori.
# dist/ resta invariato (zip + json scritti solo a build riuscita).
trap 'echo ""; echo "interrotto: build incompleta, dist/ invariato (rilancia per ricominciare)"; exit 130' INT TERM

VERSION="$(grep -m1 '^APP_VERSION' Makefile | sed -E 's/^APP_VERSION[[:space:]]*:=[[:space:]]*//')"
[ -n "$VERSION" ] || { echo "APP_VERSION non trovata nel Makefile"; exit 1; }

echo "==> package R36S (v$VERSION)"
./platform/r36s/package_r36s.sh

# Chiave "nro" (non "zip"): il fetcher legge version/nro/sha256 come
# latest.json, il valore e' l'URL del payload (qui lo zip). Zero rami in piu'.
ZIP="OpenHomeNX-r36s-v$VERSION.zip"
[ -f "dist/$ZIP" ] || { echo "Zip mancante: dist/$ZIP"; exit 1; }
SHA="$(shasum -a 256 "dist/$ZIP" | cut -d' ' -f1)"
printf '{\n  "version": "%s",\n  "nro": "%s",\n  "sha256": "%s"\n}\n' \
  "$VERSION" "$ZIP" "$SHA" > dist/latest-r36s.json
echo "release_r36s -> dist/ (v$VERSION)"; cat dist/latest-r36s.json
echo ""

if [ "$1" = "serve" ]; then
  OLD=$(lsof -tiTCP:8000 -sTCP:LISTEN 2>/dev/null | head -1)
  if [ -n "$OLD" ]; then
    echo "serve gia attivo (PID $OLD). Killarlo? [y/N]"
    read -r ANS
    if [ "$ANS" = "y" ] || [ "$ANS" = "Y" ]; then
      kill "$OLD" 2>/dev/null
      sleep 1
    else
      echo "ok, tengo quello vecchio (serve gia dist/, vale anche per R36S)."
      exit 0
    fi
  fi
  IP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || echo "localhost")
  echo "==> http://$IP:8000  (update.cfg R36S: url=http://$IP:8000 )"
  echo "    latest-r36s.json + zip serviti da dist/ (stessa istanza dello Switch)"
  python3 tools/serve_upload.py 8000
fi
