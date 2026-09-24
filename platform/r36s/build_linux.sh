#!/bin/bash
# build_linux.sh — build OpenHomeNX per R36S (ArkOS) via Docker.
#
# Compila il binario aarch64-linux-gnu sia del crate Rust che del C++,
# usando il container docker su Mac arm64 (velocita' nativa, niente qemu).
# L'output e' OpenHomeNX nella directory corrente (../r36s/OpenHomeNX/).
#
# Uso: ./build_linux.sh            -> build INCREMENTALE (solo file toccati;
#                                     se invariato finisce subito)
#      ./build_linux.sh image      -> ricostruisce solo l'immagine toolchain
#      ./build_linux.sh clean      -> rimuove gli artefatti build locali
#      ./build_linux.sh -clean     -> come clean + full rebuild locale
#      ./build_linux.sh 192.168.4.40 [user] [pass] -> build incrementale + deploy su R36S
#      ./build_linux.sh -clean 192.168.4.40 [user] [pass] -> full rebuild + deploy
set -e
cd "$(dirname "$0")"

IMG="ohnx-r36s"

if [ "$1" = "image" ]; then
  echo "==> build immagine toolchain ($IMG)"
  docker build -f Dockerfile.r36s -t "$IMG" .
  exit 0
fi

if [ "$1" = "clean" ]; then
  echo "==> clean artefatti locali"
  rm -rf build build-r36s OpenHomeNX rust-target
  exit 0
fi

# -clean: full rebuild (locale o + deploy se segue un IP)
if [ "$1" = "-clean" ] || [ "$1" = "--clean" ]; then
  shift
  echo "==> clean artefatti locali"
  rm -rf build build-r36s OpenHomeNX rust-target
  if [ -z "$1" ]; then
    set -- # nessun IP: solo rebuild locale sotto
  fi
fi

# IP come primo argomento -> dopo la build lancia il deploy
DEPLOY_IP=""
DEPLOY_ARGS=()
case "$1" in
  ""|--*|-*) ;;
  *) DEPLOY_IP="$1"; DEPLOY_ARGS=("$@"); ;;
esac

# Con IP: il deploy ricompila da solo, inutile buildare due volte
if [ -n "$DEPLOY_IP" ]; then
  DEPLOY_SH="$(cd "$(dirname "$0")/../.." && pwd)/deploy_r36s.sh"
  if [ ! -f "$DEPLOY_SH" ]; then
    echo "deploy_r36s.sh non trovato, compilo solo locale."
  else
    exec "$DEPLOY_SH" "${DEPLOY_ARGS[@]}"
  fi
fi

echo "==> verifica Docker..."
if ! docker info > /dev/null 2>&1; then
  echo "Docker non e' in esecuzione. Avvia Docker Desktop e riprova."
  exit 1
fi

echo "==> build immagine toolchain (se manca)..."
docker build -f Dockerfile.r36s -t "$IMG" .

echo "==> make r36s (Rust + C++ per aarch64-linux-gnu)"
make r36s

echo ""
echo "==> BINARIO: $(pwd)/OpenHomeNX"
file OpenHomeNX

echo ""
echo "Per copiarlo sulla R36S:"
echo "  scp OpenHomeNX ark@<IP>:/roms/ports/OpenHomeNX/OpenHomeNX"
echo "e i dati (romfs) vanno accanto al binario con un symlink 'romfs:' oppure"
echo "estratto nella cartella. Vedi lo script /roms/ports/OpenHomeNX/OpenHomeNX.sh."