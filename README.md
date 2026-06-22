# Karyon

Radial overlay launcher for KDE Plasma 6 (Wayland **and** X11) meant to replace system task bar, -tray and -start menu. Hold the mouse trigger button to open a radial menu (Windows / Apps / Files + Tray, Session, Favorites, Gestures); hold and flick for a mouse gesture. Created with Claude Opus.

Things to keep in mind:
- not all window icons are showing
- grabbing or resizing  a window while Karyon is starting up will result in faulty behavior
- If you choose to get rid of your system tray in a task bar, some KDE tray-functions might be limited (like showing the clipboard with a hotkey)
- To  access configuration of Karyon, while the program is closed, you need to open it via your system tray
- if checked in the settings: you can increase and decrease the system volume fast in the overlay by using the scrollwheel, middle mouse click to mute

## Run (from source)
    ./run.sh [--debug]
    # or: python3 -m karyon [--debug]

`run.sh` does not force `QT_QPA_PLATFORM`, so it uses the native Wayland plugin
on Wayland and xcb on X11. Needs one-time input access (udev uaccess + uinput) —
the app offers to set it up via pkexec on first start, or via the tray menu
"Set up input access…".

## Build a self-contained AppImage (X11 + Wayland)
    ./build-appimage-docker.sh        # STANDARD: builds in ubuntu:20.04 (glibc 2.31)

This bundles a standalone Python, PyQt6 + its platform plugins, and (via `ldd`)
all required system libraries — including the **xcb** stack for X11 and the
**wayland** client libs — so the resulting `dist/Karyon-x86_64.AppImage`
runs self-contained on a fresh Plasma 6 system under either session type.

`build-appimage.sh` is the inner build (run directly only when building natively;
set `BUNDLE_SYSTEM_LIBS=1` to bundle system libs).

## Icon
`karyon.svg` is the program icon, bundled in the package and used for the
window / taskbar / tray and rendered into the AppImage thumbnail at build time.

## Permissions
    ./setup-permissions.sh    # udev uaccess for /dev/input/event* + /dev/uinput
    ./reset-permissions.sh    # remove the rule again (re-prompts on next start)
