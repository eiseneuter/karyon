#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="/tmp/karyon-build"
APPDIR="/tmp/karyon-build/AppDir"
DIST_DIR="${DIST_DIR:-$HERE/dist}"

echo "=== [1/6] Building Karyon C++ (Release) ==="
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR" "$APPDIR" "$DIST_DIR"

cd "$BUILD_DIR"
cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    "$HERE"
ninja

echo "=== [2/6] Setting up AppDir directory tree ==="
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib"
ln -sf lib "$APPDIR/usr/lib64"
mkdir -p "$APPDIR/usr/plugins"
mkdir -p "$APPDIR/usr/qml"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

# Copy binary
cp "$BUILD_DIR/bin/karyon" "$APPDIR/usr/bin/karyon"

# Desktop Entry & Icons
cat > "$APPDIR/karyon.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Karyon
GenericName=Radial Launcher
Comment=Radial launcher and window manager for KDE Plasma 6
Exec=karyon
Icon=karyon
Categories=Utility;KDE;Qt;
Terminal=false
StartupNotify=false
EOF
cp "$APPDIR/karyon.desktop" "$APPDIR/usr/share/applications/karyon.desktop"

# Icons
cp "$HERE/resources/karyon.svg" "$APPDIR/karyon.svg"
cp "$HERE/resources/karyon.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/karyon.svg"
rsvg-convert -w 256 -h 256 "$HERE/resources/karyon.svg" -o "$APPDIR/karyon.png"
cp "$APPDIR/karyon.png" "$APPDIR/.DirIcon"
cp "$APPDIR/karyon.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/karyon.png"

# AppStream Metainfo
if [ -f "$HERE/resources/io.github.eiseneuter.karyon.metainfo.xml" ]; then
    mkdir -p "$APPDIR/usr/share/metainfo"
    cp "$HERE/resources/io.github.eiseneuter.karyon.metainfo.xml" "$APPDIR/usr/share/metainfo/"
    mkdir -p "$APPDIR/usr/share/appdata"
    cp "$HERE/resources/io.github.eiseneuter.karyon.metainfo.xml" "$APPDIR/usr/share/appdata/"
    if [ -f "$HERE/resources/screenshot.png" ]; then
        cp "$HERE/resources/screenshot.png" "$APPDIR/usr/share/metainfo/"
    fi
fi

echo "=== [3/6] Bundling Qt6 Plugins & QML Modules ==="
QT6_PLUGINS=""
for d in /usr/lib/x86_64-linux-gnu/qt6/plugins /usr/lib64/qt6/plugins /usr/lib/qt6/plugins; do
    if [ -d "$d" ]; then
        QT6_PLUGINS="$d"
        break
    fi
done

for pdir in platforms xcbglintegrations egldeviceintegrations wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client imageformats iconengines platforminputcontexts tls kf6; do
    if [ -n "$QT6_PLUGINS" ] && [ -d "$QT6_PLUGINS/$pdir" ]; then
        mkdir -p "$APPDIR/usr/plugins/$pdir"
        cp -aL "$QT6_PLUGINS/$pdir"/* "$APPDIR/usr/plugins/$pdir/" 2>/dev/null || true
    fi
done

QT6_QML=""
for d in /usr/lib/x86_64-linux-gnu/qt6/qml /usr/lib64/qt6/qml /usr/lib/qt6/qml; do
    if [ -d "$d" ]; then
        QT6_QML="$d"
        break
    fi
done

for qmod in QtCore QtQml QtQuick; do
    if [ -n "$QT6_QML" ] && [ -d "$QT6_QML/$qmod" ]; then
        mkdir -p "$APPDIR/usr/qml/$qmod"
        cp -aL "$QT6_QML/$qmod"/* "$APPDIR/usr/qml/$qmod/" 2>/dev/null || true
    fi
done

echo "=== [4/6] Resolving and Bundling Shared Libraries ==="
# Exclude low-level glibc & GPU driver libraries that must come from the host
EXCLUDE_RE='^(libc\.so|libm\.so|libpthread\.so|libdl\.so|librt\.so|libresolv\.so|libutil\.so|ld-linux|libGL\.so|libGLX\.so|libOpenGL\.so|libEGL\.so|libGLESv2\.so|libglapi\.so|libgbm\.so|libdrm\.so|libasound\.so|libGLdispatch\.so|libvulkan\.so)'

copy_lib() {
    local lib="$1"
    [ -e "$lib" ] || return 0
    local base
    base="$(basename "$lib")"
    if echo "$base" | grep -qE "$EXCLUDE_RE"; then
        return 0
    fi
    if [ ! -e "$APPDIR/usr/lib/$base" ]; then
        cp -aL "$lib" "$APPDIR/usr/lib/$base" 2>/dev/null || true
        local real
        real="$(readlink -f "$lib" 2>/dev/null || true)"
        if [ -n "$real" ] && [ -f "$real" ]; then
            local rbase
            rbase="$(basename "$real")"
            [ ! -e "$APPDIR/usr/lib/$rbase" ] && cp -aL "$real" "$APPDIR/usr/lib/$rbase" 2>/dev/null || true
        fi
    fi
}

inspect_and_copy() {
    local elf="$1"
    [ -f "$elf" ] || return 0
    ldd "$elf" 2>/dev/null | awk '/=> \/[^ ]+/ {print $3} /^\/[^ ]+/ {print $1}' | while read -r dep; do
        copy_lib "$dep"
    done
}

echo "Collecting binary and plugin dependencies..."
inspect_and_copy "$APPDIR/usr/bin/karyon"

find "$APPDIR/usr/plugins" "$APPDIR/usr/qml" -type f -name "*.so*" | while read -r f; do
    inspect_and_copy "$f"
done

# Transitive resolution
for pass in 1 2 3 4; do
    count_before=$(find "$APPDIR/usr/lib" -type f | wc -l)
    find "$APPDIR/usr/lib" -type f -name "*.so*" | while read -r lib; do
        inspect_and_copy "$lib"
    done
    count_after=$(find "$APPDIR/usr/lib" -type f | wc -l)
    if [ "$count_before" -eq "$count_after" ]; then
        break
    fi
done

echo "Setting RPATH and stripping binaries..."
patchelf --set-rpath '$ORIGIN/../lib:$ORIGIN/../lib64' "$APPDIR/usr/bin/karyon" || true
find "$APPDIR/usr/lib" "$APPDIR/usr/plugins" "$APPDIR/usr/qml" -type f -name "*.so*" | while read -r f; do
    patchelf --set-rpath '$ORIGIN:$ORIGIN/../lib:$ORIGIN/../../lib:$ORIGIN/../../../lib' "$f" 2>/dev/null || true
    strip --strip-unneeded "$f" 2>/dev/null || true
done
strip --strip-unneeded "$APPDIR/usr/bin/karyon" 2>/dev/null || true

echo "=== [5/6] Writing AppRun Script ==="
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export APPDIR="$HERE"

# Backup host environment for child processes launched by Karyon
export APPIMAGE_ORIGINAL_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export APPIMAGE_ORIGINAL_QT_PLUGIN_PATH="${QT_PLUGIN_PATH:-}"
export APPIMAGE_ORIGINAL_QML_IMPORT_PATH="${QML_IMPORT_PATH:-}"
export APPIMAGE_ORIGINAL_QML2_IMPORT_PATH="${QML2_IMPORT_PATH:-}"
export APPIMAGE_ORIGINAL_XDG_DATA_DIRS="${XDG_DATA_DIRS:-}"
export APPIMAGE_ORIGINAL_QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-}"
export APPIMAGE_ORIGINAL_PATH="${PATH:-}"

export LD_LIBRARY_PATH="$HERE/usr/lib:$HERE/usr/lib64:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$HERE/usr/plugins:${QT_PLUGIN_PATH:-}"
export QML_IMPORT_PATH="$HERE/usr/qml:${QML_IMPORT_PATH:-}"
export QML2_IMPORT_PATH="$QML_IMPORT_PATH"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"

exec "$HERE/usr/bin/karyon" "$@"
EOF
chmod +x "$APPDIR/AppRun"

echo "=== [6/6] Packaging AppImage ==="
URUNTIME="/opt/uruntime/uruntime-appimage-squashfs-x86_64"
OUTPUT_APPIMAGE="$DIST_DIR/Karyon-x86_64.AppImage"
rm -f "$OUTPUT_APPIMAGE"

export ARCH=x86_64
export APPIMAGE_EXTRACT_AND_RUN=1

if [ -f "$URUNTIME" ]; then
    echo "Embedding FUSE-optional uruntime..."
    appimagetool --no-appstream --runtime-file "$URUNTIME" "$APPDIR" "$OUTPUT_APPIMAGE"
else
    echo "Packaging with standard runtime..."
    appimagetool --no-appstream "$APPDIR" "$OUTPUT_APPIMAGE"
fi

chmod +x "$OUTPUT_APPIMAGE"
echo "=== Success! AppImage created at: $OUTPUT_APPIMAGE ==="
