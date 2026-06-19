#!/bin/bash
# Build a self-contained Karyon AppImage around a standalone Python.
#
# Env:
#   BUNDLE_SYSTEM_LIBS=1   bundle all Qt/plugin system libs via ldd (for fresh
#                          systems); default off when building natively.
#
# Result: dist/Karyon-x86_64.AppImage
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/build/AppDir"
DIST="$HERE/dist"
PY_VER="3.12"
PBS_TAG="20240814"
PBS_PY="cpython-3.12.5+${PBS_TAG}-x86_64-unknown-linux-gnu-install_only.tar.gz"
PBS_URL="https://github.com/indygreg/python-build-standalone/releases/download/${PBS_TAG}/${PBS_PY}"
BUNDLE_SYSTEM_LIBS="${BUNDLE_SYSTEM_LIBS:-0}"

rm -rf "$BUILD"
mkdir -p "$BUILD/usr/lib" "$DIST" "$HERE/build/cache"

# --- standalone python -----------------------------------------------------
CACHE="$HERE/build/cache/$PBS_PY"
[[ -f "$CACHE" ]] || curl -L -o "$CACHE" "$PBS_URL"
mkdir -p "$BUILD/opt"
tar -C "$BUILD/opt" -xzf "$CACHE"            # -> $BUILD/opt/python
PYROOT="$BUILD/opt/python"
PYBIN="$PYROOT/bin/python${PY_VER}"

# --- python deps -----------------------------------------------------------
"$PYBIN" -m pip install --no-cache-dir --upgrade pip
# PyQt6 ONLY from a wheel -- never build from source (no Qt/qmake here).  pip
# picks the newest version that actually ships a manylinux wheel, so a brand-new
# release that only has an sdist is skipped automatically.
"$PYBIN" -m pip install --no-cache-dir --only-binary=:all: PyQt6 PyQt6-Qt6 PyQt6-sip
# evdev is a C extension (no wheels) -> compiled here (needs build-essential).
"$PYBIN" -m pip install --no-cache-dir evdev

# --- our package -----------------------------------------------------------
SITE="$("$PYBIN" -c 'import site;print(site.getsitepackages()[0])')"
cp -r "$HERE/karyon" "$SITE/"

# --- bundle system libs (optional) -----------------------------------------
if [[ "$BUNDLE_SYSTEM_LIBS" == "1" ]]; then
    echo "Bundling system libraries (ldd) ..."
    EXCLUDE_RE='ld-linux|libc\.so|libm\.so|libdl\.so|libpthread|librt\.so|libGL|libEGL|libGLX|libgbm|libdrm|libxcb-dri'
    mapfile -t QTLIBS < <(find "$SITE/PyQt6" -name '*.so*')
    for so in "${QTLIBS[@]}"; do
        ldd "$so" 2>/dev/null | awk '/=>/ {print $3}' | grep -E '^/' || true
    done | sort -u | grep -vE "$EXCLUDE_RE" | while read -r lib; do
        [[ -f "$lib" ]] && cp -n "$lib" "$BUILD/usr/lib/" || true
    done
fi

# --- AppRun ----------------------------------------------------------------
cat > "$BUILD/AppRun" <<'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export KARYON_APPDIR="$HERE"
# Preserve the system loader path so child_env() can restore it for foreign apps.
export KARYON_SYS_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$HERE/usr/lib:$HERE/opt/python/lib:${LD_LIBRARY_PATH:-}"
export PYTHONNOUSERSITE=1
exec "$HERE/opt/python/bin/python3.12" -m karyon "$@"
EOF
chmod +x "$BUILD/AppRun"

# --- desktop entry + icon --------------------------------------------------
cat > "$BUILD/karyon.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Karyon
Exec=AppRun
Icon=karyon
Categories=Utility;
Terminal=false
NoDisplay=true
EOF
# Icon: ship the SVG and render a 256x256 PNG from it (AppImage thumbnail).
cp "$HERE/karyon.svg" "$BUILD/karyon.svg"
QT_QPA_PLATFORM=offscreen "$PYBIN" - <<PY
from PyQt6.QtGui import QImage, QPainter, QColor
from PyQt6.QtSvg import QSvgRenderer
img = QImage(256, 256, QImage.Format.Format_ARGB32)
img.fill(QColor(0, 0, 0, 0))
r = QSvgRenderer("$BUILD/karyon.svg")
p = QPainter(img); r.render(p); p.end()
img.save("$BUILD/karyon.png")
PY
cp "$BUILD/karyon.png" "$BUILD/.DirIcon"

# --- appimagetool ----------------------------------------------------------
TOOL="$HERE/build/cache/appimagetool-x86_64.AppImage"
if [[ ! -f "$TOOL" ]]; then
    curl -L -o "$TOOL" \
      "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
    chmod +x "$TOOL"
fi

# --- FUSE-optional runtime -------------------------------------------------
# The classic AppImage runtime requires libfuse2 (libfuse.so.2) on the TARGET
# machine; many systems ship only FUSE3 or no FUSE, so the AppImage fails to
# mount there.  uruntime embeds squashfuse and AUTOMATICALLY extracts + runs
# when FUSE is unavailable, so the resulting AppImage works with or without
# FUSE -- no --appimage-extract-and-run flag needed by the user.  Must match the
# squashfs payload appimagetool produces.
URUNTIME="$HERE/build/cache/uruntime-appimage-squashfs-x86_64"
if [[ ! -f "$URUNTIME" ]]; then
    curl -L -o "$URUNTIME" \
      "https://github.com/VHSgunzo/uruntime/releases/latest/download/uruntime-appimage-squashfs-x86_64"
    chmod +x "$URUNTIME"
fi

# APPIMAGE_EXTRACT_AND_RUN: appimagetool is itself an AppImage; self-extract it
# instead of mounting via FUSE (no FUSE inside the build container).
# --runtime-file embeds the FUSE-optional uruntime into our output AppImage.
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" --no-appstream \
    --runtime-file "$URUNTIME" \
    "$BUILD" "$DIST/Karyon-x86_64.AppImage"
echo "Built: $DIST/Karyon-x86_64.AppImage"
