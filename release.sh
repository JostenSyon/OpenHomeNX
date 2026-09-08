#!/bin/bash
# release.sh — build + dist per OpenHomeNX
#
# UNICO modo per produrre un .nro destinato a un test HW o a dist/: legge
# APP_VERSION dal Makefile, builda, copia in dist/OpenHomeNX.nro e rigenera
# dist/latest.json {version, nro, sha256} in un solo passaggio atomico. Mai
# `make release` a mano o una cp manuale in dist/ — versione e sha256
# finiscono per disallinearsi dal binario vero.
#
# Uso: ./release.sh         -> build + dist/ (versione da Makefile)
#      ./release.sh serve   -> come sopra + python server su dist (update.cfg)
#      VAR=1 ./release.sh   -> override make (es. USB_LIB_DEBUG=1 per una
#                              build diagnostica che linka libusbhsfsd.a)
set -e
cd "$(dirname "$0")"
export DEVKITPRO=/opt/devkitpro
export MAKE=/usr/bin/make
echo "==> make release (APP_VERSION=$(grep '^APP_VERSION' Makefile | awk '{print $3}'))"
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
