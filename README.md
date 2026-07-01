# Karyon

Radial overlay launcher for KDE Plasma 6 (Wayland **and** X11). Hold the mouse trigger button to open a radial menu (Windows / Apps / Files + Tray, Session, Favorites, Gestures); hold and flick for a mouse gesture.

## Features

### Navigation & Control
* **Customizable Triggers:** Bind separate mouse buttons for triggering and cancelling the radial overlay, and adjust the trigger hold duration to your preference.
* **Smart Window Management:** Switch between windows with minimal movement, show-desktop, close individual windows or application groups directly, mute individual audio streams, and toggle focus for window-switcher.
* **Mouse Gestures:** Flick in any of the 8 directions while holding the trigger button to invoke gestures (like *copy*, *paste*, *maximize*, or custom keys).

### UI Customization & Aesthetics
* **Visual Styling:** Personalize the overlay's appearance with custom accent colors, scale, and transparency.
* **Modular Layout Elements:** Toggle individual segments to keep the overlay minimal or fully loaded.

### Embedded Utilities
* **System Tray & Notifications:** Access your system's tray operations and receive dedicated notifications.
* **System Hub:** Real-time clock, date, battery status, and CPU/GPU/RAM system monitor can be viewed inside the central hub.
* **Volume & Task Progress:** Adjust system volume instantly with the mouse wheel inside the overlay, and view active file transfers or download operations via a visual progress ring.

### Performance & Integration
* **Game Mode:** Automatically suspends Karyon's input capture when a running game is detected, forwarding mouse inputs 1:1.
* **Performance Mode:** Minimizes rendering overhead (e.g. disabling antialiasing and halve fps) for lower-end hardware.


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

## Things to keep in mind
* **Be patient:** Give your brain and muscle memory some time to adapt. Once you do, Karyon will make your workflow much smoother.
* **Under development:** Karyon is still in active development — unexpected or unintended behavior might occur.
* **Drag & drop:** Dropping files in an other window is faster than before. Grab file, call up destination window with Karyon, drop file.
* **Icon fallbacks:** Not all window icons are guaranteed to display; missing ones are replaced by text titles.
* **Window interactions:** Grabbing or resizing a window while Karyon is starting up can result in faulty behavior.
* **System tray:** Completely removing the system tray from your desktop can limit some KDE functions (like the clipboard). If you need Klipper, I recommend keeping the system tray active somewhere on the desktop and hide all elements.
* **Accessing settings:** To configure Karyon while the overlay is closed, open the settings via the system tray.
