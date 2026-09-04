#!/bin/bash
# release.sh — build + dist per OpenHomeNX
# Uso: ./release.sh         -> make release (0.1.24) in dist/
#      ./release.sh serve   -> build + python http server su dist
set -e
cd "$(dirname "$0")"
export DEVKITPRO=/opt/devkitpro
export MAKE=/usr/bin/make
echo "==> make release (APP_VERSION=$(grep -o 'APP_VERSION :=.*' Makefile | cut -d' ' -f3))"
MAKE=/usr/bin/make DEVKITPRO=/opt/devkitpro make release
echo ""
ls -lh dist/OpenHomeNX.nro dist/latest.json
cat dist/latest.json
echo ""
if [ "$1" = "serve" ]; then
  IP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || echo "localhost")
  echo "==> http://$IP:8000  (update.cfg: url=http://$IP:8000 )"
  echo "    POST /upload -> dist/uploads/ (per Send log)"
  python3 tools/serve_upload.py 8000
fi
