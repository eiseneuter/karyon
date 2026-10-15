#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Karyon C++ Test Start ==="

# Determine executable to run:
if [ -f "$SCRIPT_DIR/dist/Karyon-x86_64.AppImage" ]; then
    APP_BIN="$SCRIPT_DIR/dist/Karyon-x86_64.AppImage"
elif [ -f "$SCRIPT_DIR/build/bin/karyon" ]; then
    APP_BIN="$SCRIPT_DIR/build/bin/karyon"
elif command -v cmake >/dev/null 2>&1; then
    echo "Kein AppImage gefunden. Versuche lokalen Host-Build..."
    cmake -B build -S .
    cmake --build build -j"$(nproc)"
    APP_BIN="$SCRIPT_DIR/build/bin/karyon"
else
    echo "Kein Binary oder AppImage gefunden. Bitte zuerst './build_appimage_docker.sh' ausführen!"
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
