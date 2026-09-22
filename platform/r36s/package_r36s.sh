#!/bin/bash
# package_r36s.sh — genera lo zip di distribuzione per R36S/ArkOS.
#
# A differenza di deploy_r36s.sh (che aggiorna il TUO dispositivo via SSH),
# questo script produce l'archivio da distribuire agli utenti finali: uno
# zip che rispecchia la root della SD card, pronto per essere estratto li'
# sopra (via lettore SD/PC o rete) — niente terminale, niente SSH richiesti
# lato utente. Contiene SEMPRE romfs/ aggiornato (niente staleness per chi
# scarica una release nuova, a differenza dei deploy incrementali via SSH).
#
# Uso: ./package_r36s.sh
# Output: dist/OpenHomeNX-r36s-vX.Y.Z.zip (nella root del repo)
set -e
cd "$(dirname "$0")"

TOPDIR="$(pwd)"
PROJ="$(cd ../.. && pwd)"
PKG="$TOPDIR/build/pkg"

echo "==> clean build R36S"
rm -rf "$TOPDIR/build-r36s" "$TOPDIR/OpenHomeNX"
make r36s

BIN="$TOPDIR/OpenHomeNX"
[ -f "$BIN" ] || { echo "Build fallita: $BIN non trovato"; exit 1; }

VERSION="$(grep -m1 '^APP_VERSION' "$PROJ/Makefile" | sed -E 's/^APP_VERSION[[:space:]]*:=[[:space:]]*//')"
[ -n "$VERSION" ] || { echo "APP_VERSION non trovata in $PROJ/Makefile"; exit 1; }

echo "==> stage layout SD (v$VERSION)"
rm -rf "$PKG"
mkdir -p "$PKG/home/ark/OpenHomeNX" "$PKG/roms/tools" "$PKG/roms/ports/OpenHomeNX"

cp "$BIN" "$PKG/home/ark/OpenHomeNX/OpenHomeNX"
cp -R "$PROJ/romfs" "$PKG/home/ark/OpenHomeNX/romfs"
cp "$PROJ/tools/OpenHomeNX.sh" "$PKG/roms/tools/OpenHomeNX.sh"
cp "$PROJ/tools/OpenHomeNX.sh" "$PKG/roms/ports/OpenHomeNX/OpenHomeNX.sh"
chmod +x "$PKG/home/ark/OpenHomeNX/OpenHomeNX" "$PKG/roms/tools/OpenHomeNX.sh" "$PKG/roms/ports/OpenHomeNX/OpenHomeNX.sh"

OUT_DIR="$PROJ/dist"
OUT_ZIP="$OUT_DIR/OpenHomeNX-r36s-v$VERSION.zip"
mkdir -p "$OUT_DIR"
rm -f "$OUT_ZIP"

echo "==> zip -> $OUT_ZIP"
(cd "$PKG" && zip -rq "$OUT_ZIP" home roms)

rm -rf "$PKG"
echo "==> fatto: $OUT_ZIP ($(du -h "$OUT_ZIP" | cut -f1))"
echo "Estrarre sulla root della SD card ArkOS (unisce le cartelle esistenti)."
