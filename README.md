# Karyon 3.0

Radial overlay launcher and multitasking hub for **KDE Plasma 6** (**Wayland** and **X11**). Re-engineered from the ground up in modern **C++20** and **Qt Quick / QML**.

![Karyon 3.0](resources/screenshot.png)

By holding the right mouse button, Karyon opens a sleek, GPU-accelerated radial menu right at your cursor. It provides instant access to open windows, favorite applications, recent files, system tray applets, and 8-directional mouse flick gestures.

**The Ultimate Minimalist Workspace:**  
Karyon is fully capable of entirely replacing your KDE Plasma Panel (Task Manager), System Tray, and Application Launcher. By consolidating these core desktop components into a single, cursor-centered overlay, Karyon empowers you to hide all traditional panels. The result is a radically clean, distraction-free, and highly efficient desktop experience where your tools appear instantly at your fingertips.

---

## What's New in Karyon 3.0

* **Native C++20 Core:** Completely rewritten from the previous Python version for ultra-low latency, instant startup, and near-zero idle resource consumption.
* **Qt Quick / QML Architecture:** Hardware-accelerated graphics (OpenGL / Vulkan) replace software rendering, guaranteeing fluid animations and crisp vector typography.
* **Seamless Wayland & X11 Integration:** Direct communication with KWin via native D-Bus bridges and KWin scripting, eliminating window activation delays.
* **Refined Drag & Drop Workflow:** Seamlessly drag files into background windows by combining Left Mouse Button drag with Right Mouse Button overlay aiming.
* **Visual Gesture Trails:** Dynamic 8-directional mouse flick gestures now feature responsive visual trails and custom key combination emulation.
* **Integrated Permission Setup:** Clean, automated Polkit (`pkexec`) permission handling for `/dev/uinput` and `/dev/input/event*`.

---

## Features & Usage

### 1. General Operation
* **Trigger & Select:** Hold the **Right Mouse Button (RMB)** to open the overlay. Hover over the desired segment and release RMB to select or launch it.
* **Cancel:** Left-click anywhere while the overlay is open, or flick away to close without making a selection.
* **Category Switching:** Scroll the mouse wheel inside the **Central Hub** to instantly switch between categories (**Windows**, **Apps**, and **Files**).
* **Volume Control:** Scroll the mouse wheel in the dedicated **Volume Control Area** at the bottom of the central hub, or drag the volume slider ring.
* **Smart Media Controls:** When media playback is active (MPRIS), dedicated controls (*Previous*, *Play/Pause*, *Next*) appear dynamically with live progress tracking.
* **Mute Audio:** Middle-click anywhere in the central hub to toggle system mute.
* **Window Management:**
  * Hover over an open window segment and scroll **Wheel Up** to **Maximize / Restore**.
  * Scroll **Wheel Down** to **Minimize**.
  * **Middle-Click** directly on a segment to **Close** the window.

### 2. Seamless Drag & Drop
Moving files between obscured windows is faster than ever:
1. Click and hold the **Left Mouse Button (LMB)** on any file (e.g. in Dolphin).
2. While still holding LMB, press and hold the **Right Mouse Button (RMB)** to summon Karyon.
3. Aim at the destination window segment (tracked smoothly via direct hardware input, bypassing compositor drag locks).
4. Release RMB to bring the destination window to the foreground, then release LMB to drop your file.

### 3. Navigation & Contextual Badges
Karyon arranges open windows, frequent apps, and recent files in concentric rings around your cursor. Windows and applications are strictly ordered based on **Most Recently Used (MRU)** order:
* **Window Counter Badge:** Shows the number of open windows belonging to an application. Hovering over the badge reveals an **(X)** button to close the entire application group at once.
* **Pin Badge:** Hover over the pin icon and release to permanently pin applications or documents to your ring. Pinned items stay in place across sessions.
* **Audio Mute Badge:** If an application is actively playing audio, a speaker badge appears. Hover and release to mute or unmute that specific audio stream.
* **Job Progress Indicators:** When file transfers or downloads are running via KDE Frameworks (KIO / KJob), segments pulse or display active progress rings.

### 4. 8-Directional Flick Gestures
* **Instant Actions:** Flick your mouse in any of the 8 cardinal/diagonal directions while pressing RMB to trigger lightning-fast actions (e.g., *Copy*, *Paste*, *Maximize*, *Overview*, *Virtual Desktop Switch*).
* **Gesture Trail:** High-performance particle and path trails visualize your motion in real time.
* **Custom Key Combinations:** Emulate arbitrary keyboard shortcuts via the built-in virtual keyboard proxy.
* **Speed:** Gestures are designed to be instantaneous — press RMB and immediately flick without waiting for the radial overlay to show.

### 5. Central Hub & Embedded Utilities
* **System Tray:** Access native system tray icons (StatusNotifierItem) directly within Karyon. Clicking a tray item opens its native Plasma flyout.
* **System Status:** Glance at the clock, calendar date, and battery level in the central hub.
* **Session Manager:** Access a dedicated session menu to *Lock*, *Suspend*, *Hibernate*, *Reboot*, or *Shut Down* cleanly.

### 6. Smart Game Mode
Karyon is built to never interfere with gaming:
* **Automatic Detection:** Karyon automatically identifies active games via window classes, `.desktop` categories, Steam, Lutris, and Heroic.
* **Deep Sleep Passthrough:** While a game is active, Karyon switches the input monitor to direct 1:1 hardware passthrough. 100% of your CPU and RAM are freed up for your game. As soon as you switch out, Karyon wakes up instantly.

---

## Technical Architecture

Karyon achieves deep desktop integration and maximum responsiveness on Linux:

* **Hardware Input Interception (`evdev` & `uinput`):**  
  To reliably capture mouse gestures and triggers globally under Wayland (where global input hooks are strictly isolated for security), Karyon directly listens to `/dev/input/event*` devices and communicates via `/dev/uinput`. Mouse triggers are intercepted and consumed before the compositor or client windows receive them.
* **KWin Scripting & D-Bus Bridge:**  
  Karyon runs a specialized KWin integration script that queries window states, thumbnails, and workspaces asynchronously. Focus stealing prevention rules ensure that window switches happen with zero perceptible delay.
* **Single-Instance Enforcement (`QLockFile`):**  
  A robust system lock ensures that only one Karyon instance can access the hardware input subsystem at any time.

---

## Running from Source

Karyon 3.0 provides a smart test and run script that automatically compiles and runs the application locally:

```bash
./start.sh
```

* Automatically detects CMake and GCC/Clang on your host.
* Performs fast incremental builds (`-j 4`) so desktop responsiveness is preserved.
* Automatically launches the native binary (`build/bin/karyon`).

---

## Building the Portable AppImage

To build a standalone, self-contained AppImage that runs on any modern Linux distribution:

```bash
./build_appimage_docker.sh
```

* **Target Base:** Ubuntu 22.04 LTS (Jammy) with KDE Neon runtime repositories (**glibc 2.35**).
* **Fully Bundled:** Includes Qt 6.7, KDE Frameworks 6, Wayland/XCB platforms, SVG renderers, and required shared libraries.
* **Delta Updates:** Automatically embeds `.zsync` update metadata for AppImageUpdate and Gear Lever.
* **Output:** Generates `dist/Karyon-x86_64.AppImage`.

---

## Permissions & Polkit Setup

Karyon requires read access to `/dev/input/event*` and write access to `/dev/uinput`:

1. **Automatic Setup:** On first launch, if permissions are missing, Karyon automatically opens a setup dialog. Clicking **Grant** prompts for your administrator password via Polkit (`pkexec`) and configures `/etc/udev/rules.d/70-karyon.rules`.
2. **Instant Reload:** Permissions take effect immediately without requiring a system reboot or logout.

---

## License

This project is licensed under the **GNU General Public License v3.0 or later** (GPL-3.0-or-later).  
See the [LICENSE](LICENSE) file for details.
