#!/bin/bash
# release_all.sh — build Switch + R36S + dist completa + serve unico.
#
# release.sh serve e release_r36s.sh serve fanno la stessa cosa sulla stessa
# porta (8000): il secondo killa il primo e viceversa. Questo script li
# sostituisce quando servono entrambe le versioni: builda tutte e due,
# rigenera entrambi i manifest e alza UN solo server che serve NRO,
# latest.json, zip e latest-r36s.json insieme.
#
# Uso: ./release_all.sh        -> build Switch + package R36S + manifest
#      ./release_all.sh serve   -> come sopra + python server su dist
set -e
cd "$(dirname "$0")"

# Ctrl-C a meta' build: messaggio pulito invece della cascata di errori.
trap 'echo ""; echo "interrotto: build incompleta, dist/ invariato (rilancia per ricominciare)"; exit 130' INT TERM

# Le due build sono indipendenti (dir build, target Rust e file dist
# diversi; l'unico file condiviso e' include/app_version.h, scritto in modo
# idempotente con contenuto identico): girano in parallelo, log separati.
echo "===== Switch + R36S in parallelo ====="
./release.sh > /tmp/release_sw.log 2>&1 & P_SW=$!
./release_r36s.sh > /tmp/release_r36s.log 2>&1 & P_R36S=$!
FAIL=0
wait $P_SW || { FAIL=1; echo "--- Switch FALLITA (coda log):"; tail -20 /tmp/release_sw.log; }
wait $P_R36S || { FAIL=1; echo "--- R36S FALLITA (coda log):"; tail -20 /tmp/release_r36s.log; }
[ $FAIL -eq 0 ] || { echo "build fallita, vedi /tmp/release_sw.log e /tmp/release_r36s.log"; exit 1; }
echo "===== entrambe ok ====="
grep -h "release.*-> dist/" /tmp/release_sw.log /tmp/release_r36s.log

if [ "$1" = "serve" ]; then
  echo ""
  OLD=$(lsof -tiTCP:8000 -sTCP:LISTEN 2>/dev/null | head -1)
  if [ -n "$OLD" ]; then
    echo "serve gia attivo (PID $OLD). Killarlo? [y/N]"
    read -r ANS
    if [ "$ANS" = "y" ] || [ "$ANS" = "Y" ]; then
      kill "$OLD" 2>/dev/null
      sleep 1
    else
      echo "ok, tengo quello vecchio (serve gia dist/ per entrambe)."
      exit 0
    fi
  fi
  IP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null || echo "localhost")
  echo "==> http://$IP:8000  (Switch: url=http://$IP:8000 — R36S: stesso url)"
  echo "    NRO + latest.json + zip + latest-r36s.json da dist/ in un colpo solo"
  python3 tools/serve_upload.py 8000
fi
