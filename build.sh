#!/usr/bin/env bash
set -e

TOPDIR="$(cd "$(dirname "$0")" && pwd)"
RUST_DIR="$TOPDIR/rust"

echo "=== Fase 1/3: Test Rust host ==="
cd "$RUST_DIR"
cargo +nightly test -p openhome_switch --features std --target aarch64-apple-darwin

echo ""
echo "=== Fase 2/3: Build Rust release (aarch64-unknown-none) ==="
cargo +nightly build -p openhome_switch --target aarch64-unknown-none --release

echo ""
echo "=== Fase 3/3: Build OpenHomeNX.nro ==="
cd "$TOPDIR"
DEVKITPRO=/opt/devkitpro MAKE=/usr/bin/make /usr/bin/make clean && DEVKITPRO=/opt/devkitpro MAKE=/usr/bin/make /usr/bin/make -j4

echo ""
echo "=== Build completata ==="
ls -lh "$TOPDIR/OpenHomeNX.nro"
