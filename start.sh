#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Karyon C++ Test Start ==="

# Build and run locally if cmake is available on host
if command -v cmake >/dev/null 2>&1; then
    cmake -B build -S .
    cmake --build build -j4
    APP_BIN="$SCRIPT_DIR/build/bin/karyon"
elif [ -f "$SCRIPT_DIR/build/bin/karyon" ]; then
    APP_BIN="$SCRIPT_DIR/build/bin/karyon"
elif [ -f "$SCRIPT_DIR/dist/Karyon-x86_64.AppImage" ]; then
    APP_BIN="$SCRIPT_DIR/dist/Karyon-x86_64.AppImage"
else
    echo "Fehler: 'cmake' ist auf deinem System nicht installiert."
    echo "Für schnelles lokales Kompilieren & Testen (ohne AppImage) bitte ausführen:"
    echo "  sudo pacman -S cmake"
    exit 1
fi

# Terminate existing Karyon instances for exclusive mouse access
echo "Stopping previous Karyon instances..."
pkill -9 -f 'karyon' 2>/dev/null || true
sleep 0.3

# QT_QPA_PLATFORM=xcb for KWin/XWayland (synchronous cursor positioning & override-redirect without animations)
export QT_QPA_PLATFORM=xcb

# English locale enforcement
export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8
export LANGUAGE=en_US:en

echo "Starting Karyon C++ ($APP_BIN)..."
echo "Hold right mouse button for 120 ms."
echo "Press Ctrl+C to stop."

exec "$APP_BIN" "$@"
