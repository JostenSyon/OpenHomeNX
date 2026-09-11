#!/bin/bash
# release.sh — build + dist per OpenHomeNX
#
# UNICO modo per produrre un .nro destinato a un test HW o a dist/: legge
# APP_VERSION dal Makefile, builda, copia in dist/OpenHomeNX.nro e rigenera
# dist/latest.json {version, nro, sha256} in un solo passaggio atomico. Mai
# `make release` a mano o una cp manuale in dist/ — versione e sha256
# finiscono per disallinearsi dal binario vero.
#
# Uso: ./release.sh         -> build DEBUG + dist/ (versione da Makefile)
#      ./release.sh release  -> build RELEASE (senza logger) + dist/
#      ./release.sh serve    -> come sopra + python server su dist (update.cfg)
#      VAR=1 ./release.sh   -> override make (es. USB_LIB_DEBUG=1 per una
#                              build diagnostica che linka libusbhsfsd.a)
set -e
cd "$(dirname "$0")"
export DEVKITPRO=/opt/devkitpro
export MAKE=/usr/bin/make
VARIANT="debug"
EXTRA=""
# Il cambio variante richiede clean (le flags non invalidano gli .o da sole).
want_clean=""
if [ "$1" = "release" ]; then
  VARIANT="release"
  EXTRA="DEBUG_LOG=0"
  shift
fi
# Stato ignoto (file mancante) = pulisci: mai linkare .o di variante incerta.
if [ ! -f .build-variant ] || [ "$(cat .build-variant)" != "$VARIANT" ]; then
  want_clean="1"
fi
if [ -n "$want_clean" ]; then
  echo "==> cambio variante ($VARIANT): make clean"
  MAKE=/usr/bin/make DEVKITPRO=/opt/devkitpro make clean > /dev/null
fi
echo "$VARIANT" > .build-variant
echo "==> make release [$VARIANT] (APP_VERSION=$(grep '^APP_VERSION' Makefile | awk '{print $3}'))"
MAKE=/usr/bin/make DEVKITPRO=/opt/devkitpro make release $EXTRA
echo "$VARIANT" > dist/variant.txt
echo ""
ls -lh dist/OpenHomeNX.nro dist/latest.json
cat dist/latest.json
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
      echo "ok, tengo quello vecchio."
      exit 0
    fi
  fi
  IP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || echo "localhost")
  echo "==> http://$IP:8000  (update.cfg: url=http://$IP:8000 )"
  echo "    POST /upload -> dist/uploads/ (per Send log)"
  python3 tools/serve_upload.py 8000
fi
