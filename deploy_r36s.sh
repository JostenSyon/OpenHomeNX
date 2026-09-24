#!/bin/bash
# deploy_r36s.sh — build pulita + deploy su R36S (ArkOS) in un colpo.
#
# SEMPRE build pulita (rm -rf build-r36s prima di make). Era stata resa
# incrementale di default in 74eb33c per velocita', ma il build-r36s viene
# compilato dentro un container Docker su Linux VM (Docker Desktop Mac):
# se l'orologio della VM va fuori sincrono con l'host (capita dopo sleep/
# wake del Mac, problema noto di Docker Desktop) il tracking incrementale
# di make puo' vedere i sorgenti modificati come "piu' vecchi" degli
# oggetti gia' compilati e saltare silenziosamente la ricompilazione --
# esattamente il sintomo "deploy fatto ma non ha la roba aggiornata"
# visto dal vivo in sessione. In un flusso di iterazione attiva come
# questo la correttezza vale piu' della velocita': sempre pulita.
#
# Uso: ./deploy_r36s.sh 192.168.4.40
#      ./deploy_r36s.sh 192.168.4.40 ark ark   # user/pass espliciti
set -e
if [ "$1" = "-clean" ] || [ "$1" = "--clean" ]; then shift; fi # retrocompat, ora e' sempre pulita
IP="${1:-192.168.4.40}"
USER="${2:-ark}"
PASS="${3:-ark}"
if [ -z "$IP" ]; then echo "Uso: $0 <IP_R36S> [user] [pass]"; exit 1; fi

PROJ="$(cd "$(dirname "$0")" && pwd)"
# Build infra versionata in platform/r36s (fallback: vecchia dir fuori repo).
R36SDIR="$PROJ/platform/r36s"
if [ ! -f "$R36SDIR/Makefile" ]; then R36SDIR="$PROJ/../r36s/OpenHomeNX"; fi
if [ ! -f "$R36SDIR/Makefile" ]; then R36SDIR="$PROJ/r36s/OpenHomeNX"; fi
if [ ! -f "$R36SDIR/Makefile" ]; then echo "R36S dir non trovata: $R36SDIR"; exit 1; fi

echo "==> clean build R36S"
rm -rf "$R36SDIR/build-r36s"
make -C "$R36SDIR" r36s

BIN="$R36SDIR/OpenHomeNX"
if [ ! -f "$BIN" ]; then echo "Build fallita: $BIN non trovato"; exit 1; fi
echo "==> binario: $(ls -lh "$BIN" | awk '{print $9, $5}') $(md5 -q "$BIN" 2>/dev/null | cut -c1-8)"

SSH="ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10"
SCP="scp -o StrictHostKeyChecking=no"
if command -v sshpass >/dev/null 2>&1 && [ -n "$PASS" ]; then
  export SSHPASS="$PASS"
  SSH="sshpass -e $SSH"
  SCP="sshpass -e $SCP"
fi

echo "==> deploy su $USER@$IP"
# kill vecchia istanza (non fatale)
$SSH "$USER@$IP" 'pkill -9 -x OpenHomeNX 2>/dev/null; echo ok' 2>&1 | tail -1
# binario in entrambe le posizioni (Tools usa /home/ark, Ports usa /roms/ports)
$SCP "$BIN" "$USER@$IP:/home/ark/OpenHomeNX/OpenHomeNX" 2>&1 | tail -1
$SCP "$BIN" "$USER@$IP:/roms/ports/OpenHomeNX/OpenHomeNX" 2>&1 | tail -1
# romfs: e' una cartella REALE sul device (romfs: e' solo un symlink che ci
# punta, vedi Makefile), mai sincronizzata da scp del solo binario -> senza
# questo passo stringhe/i18n/sprite restano vecchie anche a binario aggiornato
# (bug reale visto dal vivo: romfs fermo a giorni prima nonostante piu' deploy).
echo "==> sync romfs (rsync)"
rsync -az --delete -e "$SSH" "$PROJ/romfs/" "$USER@$IP:/home/ark/OpenHomeNX/romfs/"
# script launcher
if [ -f "$PROJ/tools/OpenHomeNX.sh" ]; then
  $SCP "$PROJ/tools/OpenHomeNX.sh" "$USER@$IP:/roms/tools/OpenHomeNX.sh" 2>&1 | tail -1
  $SCP "$PROJ/tools/OpenHomeNX.sh" "$USER@$IP:/roms/ports/OpenHomeNX/OpenHomeNX.sh" 2>&1 | tail -1
fi
$SSH "$USER@$IP" 'chmod +x /home/ark/OpenHomeNX/OpenHomeNX /roms/tools/OpenHomeNX.sh /roms/ports/OpenHomeNX/OpenHomeNX.sh 2>/dev/null; md5sum /home/ark/OpenHomeNX/OpenHomeNX | cut -d" " -f1 | head -1; echo "deploy ok"' 2>&1 | tail -5
echo "==> fatto. Lancia da ES Tools -> OpenHomeNX"