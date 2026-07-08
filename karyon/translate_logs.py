import os
import re

replacements = {
    # input_proxy.py
    r"python-evdev not available - Input Proxy disabled": "python-evdev not available - Input Proxy disabled",
    r"Virtual device for %s failed": "Virtual device for %s failed",
    r"Maus erfasst: %s \(%s\)": "Mouse captured: %s (%s)",
    r"Touchpad erfasst: %s \(Trigger\+Navigation\)": "Touchpad captured: %s (Trigger+Navigation)",
    r"Touchpad full mode failed: %s": "Touchpad full mode failed: %s",
    r"Touchpad erfasst: %s \(Navigation\)": "Touchpad captured: %s (Navigation)",
    r"Mouse removed: %s": "Mouse removed: %s",
    r"Keyboard injection device not available": "Keyboard injection device not available",
    r"send_keys failed": "send_keys failed",
    
    # main.py
    r"Failed to write KWin rule": "Failed to write KWin rule",
    r"Overlay-acceptfocus=false gesetzt \(Fensterwechsel\)\.": "Overlay acceptfocus=false set (Window Switch).",
    r"Failed to set acceptfocus rule": "Failed to set acceptfocus rule",
    r"Focus-Stealing-Prevention auf 0 gesetzt \(Fensterwechsel\)\.": "Focus Stealing Prevention set to 0 (Window Switch).",
    r"Failed to set FSP level": "Failed to set FSP level",
    r"Failed to change volume via mouse wheel": "Failed to change volume via mouse wheel",
    r"Failed to change mute via mouse trigger": "Failed to change mute via mouse trigger",
    r"Eingabe-Zugriff vorhanden: %s \(pkexec: %s\)": "Input access available: %s (pkexec: %s)",
    r"Karyon ready - hold %s trigger for %d ms\.": "Karyon ready - hold %s trigger for %d ms.",
    r"Game Mode \(rc=%s\): %s": "Game Mode (rc=%s): %s",
    
    # kwin.py
    r"Failed to register DBus Result Service": "Failed to register DBus Result Service",
    r"Failed to register DBus Result Object": "Failed to register DBus Result Object",
    r"Fullscreen event callback failed": "Fullscreen event callback failed",
    r"Snapshot callback failed": "Snapshot callback failed",
    r"Failed to write KWin script": "Failed to write KWin script",
    r"loadScript no response: %s": "loadScript no response: %s",
    r"loadScript returned invalid id: %r": "loadScript returned invalid id: %r",
    r"Failed to write KWin Daemon script": "Failed to write KWin Daemon script",
    r"loadScript for daemon no response: %s": "loadScript for daemon no response: %s",
    r"loadScript for daemon returned invalid id: %r": "loadScript for daemon returned invalid id: %r",
    
    # tray.py
    r"Failed to read appletsrc": "Failed to read appletsrc",
    r"Failed to register StatusNotifierHost": "Failed to register StatusNotifierHost",
    r"activate_sni: invalid service/path %r": "activate_sni: invalid service/path %r",
    r"run_builtin failed: %s": "run_builtin failed: %s",
    
    # apps.py
    r"\.desktop index: %d entries": ".desktop index: %d entries",
    r"Failed to parse categories": "Failed to parse categories",
    r"Failed to read favorites": "Failed to read favorites",
    r"App launch failed: %s": "App launch failed: %s",
    
    # media.py
    r"Failed to retrieve media status": "Failed to retrieve media status",
    r"Fehler bei Media Action \{cmd\}": "Media action failed {cmd}",
    
    # dbusmenu.py
    r"Failed to read DBusMenu: %s %s": "Failed to read DBusMenu: %s %s",
    r"DBusMenu click failed: %s %s": "DBusMenu click failed: %s %s",
    
    # permissions.py
    r"Failed to set up permissions": "Failed to set up permissions",
    
    # overlay.py
    r"UNPINNED FILE: %s": "UNPINNED FILE: %s",
    r"PINNED FILE: %s": "PINNED FILE: %s",
    r"UNPINNED APP: %s": "UNPINNED APP: %s",
    r"PINNED APP: %s": "PINNED APP: %s",
    r"ACTIVATE window %s": "ACTIVATE window %s",
    r"Tray activation: bringing window %s to foreground": "Tray activation: bringing window %s to foreground",
    r"Tray activation: calling activate_sni for %s": "Tray activation: calling activate_sni for %s",
    
    # pins.py
    r"Failed to load pins from %s": "Failed to load pins from %s",
    r"Failed to save pins to %s": "Failed to save pins to %s",
    
    # procenv.py
    r"QProcess\.startDetached failed for: %s": "QProcess.startDetached failed for: %s",
    r"run_detached failed: %s": "run_detached failed: %s",
    
    # config.py
    r"Failed to read configuration: %s": "Failed to read configuration: %s",
    r"Failed to save configuration: %s": "Failed to save configuration: %s",
    
    # session.py
    r"Session DBus failed: %s": "Session DBus failed: %s",
    
    # progress.py
    r"LauncherEntry-Signal nicht verbunden \(kein Fortschritt\)": "LauncherEntry signal not connected (no progress)",
}

for root, _, files in os.walk("/home/eisen/Data/Programme/PythonProjects/Dumb Launcher 2/karyon/karyon"):
    for file in files:
        if file.endswith(".py"):
            filepath = os.path.join(root, file)
            with open(filepath, "r", encoding="utf-8") as f:
                content = f.read()
                
            original_content = content
            for old, new in replacements.items():
                content = re.sub(old, new, content)
                
            if content != original_content:
                with open(filepath, "w", encoding="utf-8") as f:
                    f.write(content)
                print(f"Updated {file}")
