# Karyon

Radial overlay launcher for KDE Plasma 6 (Wayland **and** X11). Karyon is designed to be your central hub for multitasking, window management, and rapid interactions.

![Karyon](resources/screenshot.png)

By holding the right mouse button, Karyon opens a sleek, highly responsive radial menu right at your cursor. It provides instant access to open windows, favorite applications, recent files, and essential system functions.

**The Ultimate Minimalist Workspace:**  
Karyon is fully capable of entirely replacing your KDE Plasma Panel (Task Manager), System Tray, and Application Launcher. By consolidating these core desktop components into a single, cursor-centered overlay, Karyon empowers you to hide all traditional panels. The result is a radically clean, distraction-free, and highly efficient desktop experience where your tools appear instantly.

---

## Features & Usage

### 1. General Operation
* **Trigger & Select:** Hold the right mouse button to open the overlay. Hover over the desired item and let the right mouse button go to select it.
* **Cancel:** Left-click anywhere while the overlay is open to cancel and close it without making a selection.
* **Category Switching:** Use your mouse wheel inside the central hub to switch between categories (**Windows**, **Apps**, and **Files**).
* **Volume Control:** Use your mouse wheel to adjust the system volume in the dedicated Volume Control Area at the bottom of the central hub.
* **Media Controls:** When media is actively playing or paused, smart media controls (Previous, Play/Pause, Next) appear dynamically. Activate them (hover and release) to control playback.
* **Mute Audio:** Middle-click anywhere in the central hub to toggle system mute.
* **Window Management:** Hover directly over an open window segment and scroll the mouse wheel **Up** to Maximize (or Restore) the window, and **Down** to Minimize (or Restore) it. Middle-click directly on the segment closes the window.

### 2. Navigation & Window Management
* **Smart Ring Layout:** Karyon arranges your open windows, frequent apps, and recent files in concentric rings around your cursor. Windows and applications are strictly ordered based on usage (Most Recently Used), so your current context is always exactly where you need it.
* **Interactive Segment Bars (Drill-Down):** Hovering over the outer edge of segments reveals interactive bars:
  * **Drill-Down Bar:** Appears on segments that contain deeper options or application sub-menus. Hovering over it drills down into the respective app or category.
* **Contextual Badges:**
  * **Window Counter Badge:** A numerical badge on application segments indicating how many open windows belong to that application. Hovering over this badge transforms it into a close button (X), allowing you to close the entire application group by releasing the right mouse button.
  * **Pin Badge:** Hover over the pin icon and release to permanently pin an application or file to your overlay. Pinned items stay in place even when closed, ensuring your most vital tools are always exactly where you expect them.
  * **Job Progress Indicators:** When file operations or downloads are running via KDE Frameworks (KIO), segments pulse or display active progress rings.

### 3. Gestures (Fast-Action)
* **Mouse Gestures:** Flick your mouse in any of the 8 directions while holding the right mouse button to invoke gestures (like *copy*, *paste*, *maximize*, *virtual desktop switch*, or custom shortcuts). Actions trigger immediately upon flicking.
* **Important:** Gestures are designed to be extremely fast. They must be initiated *without* opening the visual overlay. Press the right mouse button and immediately flick your mouse to execute the gesture seamlessly.

### 4. Embedded Utilities & System Hub
* **Central Hub:** The center of the radial menu acts as an orientation guide and information center. It prominently displays an icon indicating the currently active category (Windows, Apps, or Files), along with real-time system information (clock and date).
* **System Tray:** Access your system's tray icons directly within Karyon. Hovering over a tray icon and releasing the right mouse button opens its native Plasma flyout window (just like the real system tray), giving you full access to its controls.
* **Session Controls:** Access quick power options to lock the screen, switch user, log out, sleep, restart, or shut down cleanly.
* **Volume Control:** Adjust the system volume instantly using your mouse wheel inside the dedicated Volume Control Area at the bottom of the hub.

### 5. Game Mode & Deep Sleep
Karyon is built to never get in your way when gaming.
* **Automatic Detection:** Karyon automatically detects active games (via `.desktop` categories, Steam, Lutris, and Heroic). When a game is focused, Karyon enters Game Mode without requiring manual configuration.
* **Deep Sleep:** While Game Mode is active, Karyon suspends all background polling and forwards your mouse inputs 1:1 with zero overhead. As soon as you switch back to desktop applications, Karyon wakes up instantly.

---

## Configuration & Customization
Karyon features a dedicated settings panel accessible via the system tray icon (which can also be reached directly through the overlay itself). Here you can:
* **Customize Hold Time:** Adjust the hold-time to invoke the overlay or firing a gesture.
* **Adjust Aesthetics:** Change overlay scale and transparency.
* **Tweak Layout:** Toggle individual segments, rings, or utilities on or off to keep the overlay minimal or fully loaded.

---

## Technical Background (How it works)
Karyon uses several advanced techniques to achieve deep system integration and maximum performance on both Wayland and X11:
* **Global Input Interception (evdev/uinput):** To reliably capture the mouse trigger anywhere on the screen (even in Wayland, where global hotkeys are strictly isolated for security), Karyon directly reads from `/dev/input/event*` and uses `uinput` to create a virtual proxy mouse and keyboard. This allows Karyon to intercept and consume the trigger button before the compositor even sees it.
* **Fullscreen Overlays (Wayland & X11):** Karyon renders as a frameless, override-redirect top-level window (`Qt::BypassWindowManagerHint`), ensuring the overlay draws seamlessly on top of all windows, including fullscreen applications.
* **Hardware-Accelerated Qt Quick / QML:** Karyon utilizes Qt Quick's GPU-accelerated scene graph (OpenGL / Vulkan) for vector rendering and animations, drawing with sub-millisecond frame latency while keeping CPU usage minimal.

---

## Things to keep in mind
* **Be patient:** Give your brain and muscle memory some time to adapt. Once you do, Karyon will make your workflow much smoother.
* **Drag & drop:** Dropping files in another window is faster than before. Grab file with left mouse button, call up destination window with Karyon (hold right mouse button), aim and release right button to raise the window, drop file.
* **Icon fallbacks:** Not all window icons are guaranteed to display; missing ones are replaced by text titles.
* **System tray:** Completely removing the system tray from your desktop can limit some KDE functions (like the clipboard). If you need Klipper, I recommend keeping the system tray active somewhere on the desktop and hiding all elements. Karyon is generally unable to invoke the actual notification flyout window (only accessible via the true system tray).

---

## Run (from source)
```bash
./start.sh
```
Karyon automatically builds incrementally and runs natively against your host's Qt 6 and KDE Frameworks 6 libraries.

---

## Build a self-contained AppImage (X11 + Wayland)
```bash
./build_appimage_docker.sh
```
This builds a standalone, self-contained AppImage on Ubuntu 22.04 LTS (glibc 2.35) bundling Qt 6, KDE Frameworks 6, and all runtime dependencies. The resulting file is created in `dist/Karyon-x86_64.AppImage`.

---

## Permissions
Karyon needs one-time input access (`/dev/input/event*` and `/dev/uinput`). The application automatically displays a permission setup dialog on first launch to configure the required udev rule via Polkit (`pkexec`).

---

## License
This project is licensed under the **GNU General Public License v3.0 or later** (GPL-3.0-or-later).  
See the [LICENSE](LICENSE) file for details.
