# Karyon

Radial overlay launcher for KDE Plasma 6 (Wayland **and** X11). Karyon is designed to be your central hub for multitasking, window management, and rapid interactions.

By holding the right mouse button, Karyon opens a sleek, highly responsive radial menu right at your cursor. It provides instant access to open windows, favorite applications, recent files, and essential system functions.

**The Ultimate Minimalist Workspace:**
Karyon is fully capable of entirely replacing your KDE Plasma Panel (Task Manager), System Tray, and Application Launcher. By consolidating these core desktop components into a single, cursor-centered overlay, Karyon empowers you to hide all traditional panels. The result is a radically clean, distraction-free, and highly efficient desktop experience where your tools appear instantly.

## Features & Usage

### 1. General Operation
* **Trigger & Select:** Hold the right mouse button to open the overlay. Hover over the desired item and let the right mouse button go to select it.
* **Cancel:** Left-click anywhere while the overlay is open to cancel and close it without making a selection.
* **Category Switching:** In Pie Mode, hover over the different sections of the central hub to switch categories. In Switch Mode, use your mouse wheel to switch between categories.
* **Volume:** Scroll the mouse wheel up/down (in Pie Mode) anywhere in the overlay (except when hovering over open window segments) to adjust the system volume. In Switch Mode, there is a dedicated Volume Control Area at the bottom of the central hub where you can use the mouse wheel to adjust the volume.
* **Mute Audio:** Middle-click anywhere in the overlay (except on window segments) to toggle system mute.
* **Window Management:** Hover directly over an open window segment and scroll the mouse wheel **Up** to Maximize (or Restore) the window, and **Down** to Minimize it. Middle-click on the segment closes the window.

### 2. Navigation & Window Management
* **Smart Ring Layout:** Karyon arranges your open windows, frequent apps, and recent files in concentric rings around your cursor. Windows and applications are strictly ordered based on usage (Most Recently Used), so your current context is always exactly where you need it.
* **Segment Bars (Cyan):** Hovering over the edges of segments reveals interactive colored bars:
  * **Cyan Bar (Drill-Down):** Appears on segments that contain deeper options or application sub-menus. Hovering over it drills down into the respective app or category.
* **Contextual Badges:**
  * **Window Counter Badge:** A numerical badge on application segments that indicates how many open windows belong to that application. Hovering over this badge transforms it into a close button (X), allowing you to close the entire application group by releasing the right mouse button.
  * **Pin Badge:** Hover over the pin icon and release to permanently pin an application or file to your overlay. Pinned items stay in place even when closed, ensuring your most vital tools are always exactly where you expect them. 
  * **Audio Mute Badge:** If a window is currently playing audio, a speaker badge appears. Hover and release to instantly mute or unmute that specific application's audio stream. (A white line through the symbol indicates a muted stream).

### 3. Gestures (Fast-Action)
* **Mouse Gestures:** Flick your mouse in any of the 8 directions while holding the right mouse button to invoke gestures (like *copy*, *paste*, *maximize*, or custom shortcuts). 
* **Important:** Gestures are designed to be extremely fast. They must be initiated *without* opening the visual overlay. Press the right mouse button and immediately flick your mouse to execute the gesture seamlessly.

### 4. Overlay Modes (Pie & Switch)
Karyon offers two distinct presentation modes for the radial menu:
* **Pie Mode:** All activated categories (Windows, Apps, Recent Files) are displayed simultaneously in a full circle layout, subdivided into equal slices.
* **Switch Mode:** This mode is designed to prevent accidental selections and lets you switch the category via mouse wheel.

### 5. Embedded Utilities & System Hub
* **Central Hub:** The center of the radial menu acts as an orientation guide and information center. It prominently displays an icon indicating the currently active category (Windows, Apps, or Files), along with real-time system information (clock, date, and battery status).
* **System Tray:** Access your system's tray icons directly within Karyon. Hovering over a tray icon and releasing the right mouse button opens its native Plasma flyout window (just like the real system tray), giving you full access to its controls.
* **Notifications:** Completely separate from the tray, Karyon highlights unread notifications from messengers or apps, allowing you to instantly open the respective application and jump straight into the context.
* **Volume Control:** Adjust the system volume instantly using your mouse wheel anywhere inside the overlay (in Pie Mode) or inside the dedicated Volume Control Area at the bottom of the hub (in Switch Mode), or by drilling down into the volume tray icon and hovering over its internal buttons.

### 6. Smart Game Mode & Deep Sleep
Karyon is built to never get in your way when gaming. 
* **Automatic Detection:** Karyon automatically reads the `.desktop` categories of your active windows. If you switch to a game, Karyon instantly enters Game Mode without requiring manual whitelists.
* **Deep Sleep:** While Game Mode is active, Karyon suspends all background polling and forwards your mouse inputs 1:1. This "Deep Sleep" guarantees that 100% of your CPU and RAM are freed up for your game. As soon as you alt-tab out, Karyon instantly wakes up.



## Configuration & Customization
Karyon features a dedicated settings panel accessible via the system tray icon (which can also be reached directly through the overlay itself). Here you can:
* **Customize Hold Time:** Adjust the hold-time to invoke the overlay or firing a gesture.
* **Adjust Aesthetics:** Switch between a sleek Dark Theme (default) and a vibrant Light Theme, and change overlay scale and transparency.
* **Tweak Layout:** Toggle individual segments and rings on or off to keep the overlay minimal or fully loaded.

## Technical Background (How it works)

Karyon uses several advanced techniques to achieve deep system integration and maximum performance on both Wayland and X11:

* **Global Input Interception (evdev/uinput):** To reliably capture the mouse trigger anywhere on the screen (even in Wayland, where global hotkeys are strictly isolated for security), Karyon directly reads from `/dev/input/event*` and uses `uinput` to create a virtual proxy mouse. This allows Karyon to intercept and consume the trigger button before the compositor even sees it.
* **Fullscreen Overlays (Wayland & X11):** While X11 naturally allows drawing over fullscreen apps, Wayland strictly enforces window layers. Karyon bypasses this limitation by dynamically injecting specialized **KWin Window Rules** into KDE Plasma, forcing the compositor to always place the Karyon overlay in the highest possible layer, seamlessly overlapping fullscreen videos or apps.
* **High-Performance Vector Rendering:** Karyon is purely software-rendered via Qt's `QPainter`, but highly optimized. By utilizing simple, clean vector graphics and strictly timed 30 FPS event loops, the overlay draws in just ~1-2 milliseconds per frame. This ensures buttery-smooth animations while keeping CPU usage low.

## Run (from source)
    ./run.sh [--debug]
    # or: python3 -m karyon [--debug]

`run.sh` does not force `QT_QPA_PLATFORM`, so it uses the native Wayland plugin on Wayland and xcb on X11. Needs one-time input access (udev uaccess + uinput) — the app offers to set it up via pkexec on first start, or via the tray menu "Set up input access…".

## Build a self-contained AppImage (X11 + Wayland)
    ./build-appimage-docker.sh        # STANDARD: builds in ubuntu:20.04 (glibc 2.31)

This bundles a standalone Python, PyQt6 + its platform plugins, and (via `ldd`) all required system libraries — including the **xcb** stack for X11 and the **wayland** client libs — so the resulting `dist/Karyon-x86_64.AppImage` runs self-contained on a fresh Plasma 6 system under either session type.

`build-appimage.sh` is the inner build (run directly only when building natively; set `BUNDLE_SYSTEM_LIBS=1` to bundle system libs).

## Permissions
    ./setup-permissions.sh    # udev uaccess for /dev/input/event* + /dev/uinput
    ./reset-permissions.sh    # remove the rule again (re-prompts on next start)

## Things to keep in mind
* **Be patient:** Give your brain and muscle memory some time to adapt. Once you do, Karyon will make your workflow much smoother.
* **Drag & drop:** Dropping files in another window is faster than before. Grab file, call up destination window with Karyon, drop file.
* **Icon fallbacks:** Not all window icons are guaranteed to display; missing ones are replaced by text titles.
* **Startup caution:** do not grab any window or try to manage windows, while Karyon is booting up (KARYON intro). It will result in faulty window focus behavior!
* **System tray:** Completely removing the system tray from your desktop can limit some KDE functions (like the clipboard). If you need Klipper, I recommend keeping the system tray active somewhere on the desktop and hiding all elements. Karyon is generally unable to invoke the actual notification flyout window (only accessable via the true system tray).
