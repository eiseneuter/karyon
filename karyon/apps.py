"""Application index, categories (KDE menu), favorites, recents, window matching."""
from __future__ import annotations

import json
import logging
import os
import shlex
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path
from xml.etree import ElementTree as ET

from .config import DATA_DIR
from .procenv import child_env

log = logging.getLogger(__name__)

USAGE_PATH = DATA_DIR / "usage.json"


def _xdg_app_dirs() -> list[Path]:
    dirs = []
    data_home = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    data_dirs = os.environ.get("XDG_DATA_DIRS") or "/usr/local/share:/usr/share"
    for base in [data_home] + data_dirs.split(":"):
        base = base.strip()
        if not base:
            continue
        p = Path(base) / "applications"
        if p.is_dir():
            dirs.append(p)
    return dirs


@dataclass
class App:
    app_id: str
    name: str
    icon: str = ""
    exec_line: str = ""
    terminal: bool = False
    wm_class: str = ""
    path: str = ""
    keywords: list = field(default_factory=list)
    categories: list = field(default_factory=list)
    no_display: bool = False
    pseudo: bool = False


def _parse_desktop(path: Path, app_id: str) -> App | None:
    names: dict[str, str] = {}
    icon = exec_line = wm_class = ""
    terminal = no_display = hidden = False
    keywords: list = []
    categories: list = []
    in_entry = False
    try:
        for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = raw.strip()
            if line.startswith("[") and line.endswith("]"):
                in_entry = line == "[Desktop Entry]"
                continue
            if not in_entry or "=" not in line:
                continue
            key, _, val = line.partition("=")
            key = key.strip()
            val = val.strip()
            
            if key.startswith("Name"):
                if key == "Name":
                    names[""] = val
                elif key.startswith("Name[") and key.endswith("]"):
                    names[key[5:-1]] = val
            elif key == "Icon":
                icon = val
            elif key == "Exec":
                exec_line = val
            elif key == "Terminal":
                terminal = val.lower() in ("true", "1")
            elif key == "NoDisplay":
                no_display = val.lower() in ("true", "1")
            elif key == "Hidden":
                hidden = val.lower() in ("true", "1")
            elif key == "StartupWMClass":
                wm_class = val
            elif key == "Keywords":
                keywords = [k for k in val.split(";") if k]
            elif key == "Categories":
                categories = [c for c in val.split(";") if c]
            elif key == "Type" and val != "Application":
                return None
    except Exception:  # noqa: BLE001
        return None

    # Resolve name by matching locale preference
    name = ""
    loc = (os.environ.get("LC_MESSAGES") or os.environ.get("LANG")
           or os.environ.get("LC_ALL") or "").split(".")[0]
    locale_keys = []
    if loc:
        locale_keys.append(loc)
        if "_" in loc:
            locale_keys.append(loc.split("_")[0])
    for k in locale_keys:
        if k in names:
            name = names[k]
            break
    if not name:
        name = names.get("", "")

    if not name or not exec_line:
        return None
    return App(app_id=app_id, name=name, icon=icon, exec_line=exec_line,
               terminal=terminal, wm_class=wm_class, path=str(path),
               keywords=keywords, categories=categories,
               no_display=no_display or hidden)


class AppIndex:
    def __init__(self) -> None:
        self.apps: dict[str, App] = {}
        self._by_wmclass: dict[str, App] = {}
        self._categories: dict[str, list[App]] = {}
        self._usage: dict = self._load_usage()
        self._pseudo: dict[str, App] = {}

    # -- scanning -----------------------------------------------------------
    def scan(self) -> None:
        self.apps.clear()
        self._by_wmclass.clear()
        for base in _xdg_app_dirs():
            for path in base.rglob("*.desktop"):
                rel = path.relative_to(base)
                app_id = str(rel.with_suffix("")).replace("/", "-")
                app = _parse_desktop(path, app_id)
                if app is None:
                    continue
                # First occurrence wins (XDG precedence by dir order).
                self.apps.setdefault(app_id, app)
        for app in self.apps.values():
            if app.wm_class:
                self._by_wmclass.setdefault(app.wm_class.lower(), app)
        log.info(".desktop-Index: %d Eintraege", len(self.apps))

    def prewarm(self) -> None:
        try:
            self._categories = self._build_categories()
        except Exception:  # noqa: BLE001
            log.exception("Kategorien konnten nicht geparst werden")
            self._categories = {}

    # -- categories (KDE applications.menu) ---------------------------------
    def _menu_files(self) -> list[Path]:
        paths = []
        dirs = os.environ.get("XDG_CONFIG_DIRS") or "/etc/xdg"
        for base in dirs.split(":"):
            base = base.strip()
            if not base:
                continue
            for fn in ("applications.menu", "plasma-applications.menu"):
                p = Path(base) / "menus" / fn
                if p.is_file():
                    paths.append(p)
        for fn in ("applications.menu", "plasma-applications.menu"):
            p = Path("/etc/xdg/menus") / fn
            if p.is_file() and p not in paths:
                paths.append(p)
        return paths

    def _locale_keys(self) -> list[str]:
        loc = (os.environ.get("LC_MESSAGES") or os.environ.get("LANG")
               or os.environ.get("LC_ALL") or "")
        loc = loc.split(".")[0]  # de_DE.UTF-8 -> de_DE
        keys = []
        if loc:
            keys.append(loc)            # de_DE
            if "_" in loc:
                keys.append(loc.split("_")[0])  # de
        return keys

    def _dir_dirs(self) -> list[Path]:
        out = []
        data_home = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
        data_dirs = os.environ.get("XDG_DATA_DIRS") or "/usr/local/share:/usr/share"
        for base in [data_home] + data_dirs.split(":"):
            p = Path(base.strip()) / "desktop-directories"
            if p.is_dir():
                out.append(p)
        return out

    def _localized_dir_name(self, directory: str) -> str:
        """Localized display name from a .directory file (Name[locale])."""
        if not directory:
            return ""
        for base in self._dir_dirs():
            path = base / directory
            if not path.is_file():
                continue
            names: dict[str, str] = {}
            try:
                for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
                    line = raw.strip()
                    if line.startswith("Name[") and "]=" in line:
                        key = line[5:line.index("]")]
                        names[key] = line.split("=", 1)[1]
                    elif line.startswith("Name="):
                        names[""] = line[5:]
            except Exception:  # noqa: BLE001
                continue
            for k in self._locale_keys():
                if k in names:
                    return names[k]
            return names.get("", "")
        return ""

    def _parse_applications_menu(self) -> list[tuple[str, set]]:
        """Ordered (display-name, {freedesktop categories}) from the KDE menu.
        The display name is localized via the menu's <Directory> (.directory)."""
        for path in self._menu_files():
            try:
                root = ET.parse(path).getroot()
            except Exception:  # noqa: BLE001
                continue
            out = []
            for menu in root.findall("Menu"):
                name = menu.findtext("Name")
                if not name:
                    continue
                directory = menu.findtext("Directory") or ""
                label = self._localized_dir_name(directory) or name
                cats = {c.text for c in menu.iter("Category") if c.text}
                out.append((label, cats))
            if out:
                return out
        return []

    _FALLBACK_MENU = [
        ("Development", {"Development"}), ("Education", {"Education"}),
        ("Games", {"Game"}), ("Graphics", {"Graphics"}),
        ("Internet", {"Network"}), ("Multimedia", {"AudioVideo"}),
        ("Office", {"Office"}), ("Science", {"Science"}),
        ("System", {"System", "Settings"}), ("Utilities", {"Utility"}),
    ]

    def _build_categories(self) -> dict[str, list[App]]:
        menus = self._parse_applications_menu() or self._FALLBACK_MENU
        result: dict[str, list[App]] = {}
        for name, cats in menus:
            apps = [a for a in self.apps.values()
                    if not a.no_display and (set(a.categories) & cats)]
            if not apps:
                continue
            apps.sort(key=lambda a: (-self._usage_count(a.app_id), a.name.lower()))
            # de-duplicate while preserving order (an app can match many cats)
            seen, uniq = set(), []
            for a in apps:
                if a.app_id in seen:
                    continue
                seen.add(a.app_id)
                uniq.append(a)
            result[name] = uniq
        return result

    def categories(self) -> dict[str, list[App]]:
        return self._categories

    # -- favorites (kactivitymanagerd) --------------------------------------
    def favorites(self) -> list[App]:
        favs: list[App] = []
        db = Path(os.path.expanduser(
            "~/.local/share/kactivitymanagerd/resources/database"))
        if not db.exists():
            return favs
        try:
            import sqlite3
            con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
            cur = con.execute(
                "SELECT targettedResource FROM ResourceLink "
                "WHERE usedActivity='' OR usedActivity IS NOT NULL")
            seen = set()
            for (res,) in cur.fetchall():
                if not res or not res.startswith("applications:"):
                    continue
                app_id = res.split(":", 1)[1].replace(".desktop", "")
                app_id = app_id.replace("/", "-")
                if app_id in seen:
                    continue
                seen.add(app_id)
                app = self.apps.get(app_id) or self._find_app(app_id)
                if app:
                    favs.append(app)
            con.close()
        except Exception:  # noqa: BLE001
            log.debug("Favoriten konnten nicht gelesen werden", exc_info=True)
        return favs

    def _find_app(self, app_id: str) -> App | None:
        if app_id in self.apps:
            return self.apps[app_id]
        low = app_id.lower()
        for aid, app in self.apps.items():
            if aid.lower() == low:
                return app
        return None

    # -- usage / recents ----------------------------------------------------
    def _load_usage(self) -> dict:
        try:
            return json.loads(USAGE_PATH.read_text(encoding="utf-8"))
        except Exception:  # noqa: BLE001
            return {}

    def _save_usage(self) -> None:
        try:
            DATA_DIR.mkdir(parents=True, exist_ok=True)
            USAGE_PATH.write_text(json.dumps(self._usage), encoding="utf-8")
        except Exception:  # noqa: BLE001
            pass

    def _usage_count(self, app_id: str) -> int:
        return int(self._usage.get(app_id, {}).get("count", 0))

    def _recency(self, app_id: str) -> float:
        rec = self._usage.get(app_id, {})
        return max(float(rec.get("last", 0)), float(rec.get("seen", 0)))

    def note_seen(self, app_id: str) -> None:
        if not app_id:
            return
        rec = self._usage.setdefault(app_id, {})
        rec["seen"] = time.time()
        self._save_usage()

    def record_usage(self, app_id: str) -> None:
        rec = self._usage.setdefault(app_id, {})
        rec["count"] = int(rec.get("count", 0)) + 1
        rec["last"] = time.time()
        self._save_usage()

    def frequent(self, n: int, exclude: set | None = None) -> list[App]:
        exclude = exclude or set()
        resolved_exclude = set(exclude)
        for x in exclude:
            if x.startswith("pseudo:"):
                real = self.match_window(x[7:])
                if real:
                    resolved_exclude.add(real.app_id)
            else:
                resolved_exclude.add(f"pseudo:{x}")

        scored = []
        for app_id in self._usage:
            if app_id in resolved_exclude:
                continue
            app = self.apps.get(app_id) or self._pseudo.get(app_id)
            if app is None or (app.no_display and not app.pseudo):
                continue
            # If this is a pseudo app but we now have a real app matching its name,
            # skip the pseudo app so we don't get duplicates.
            if app_id.startswith("pseudo:"):
                real_app = self.match_window(app_id[7:])
                if real_app and not real_app.pseudo:
                    continue
            scored.append((self._usage_count(app_id), self._recency(app_id), app))
        # Sort by usage count first, then by recency (both descending)
        scored.sort(key=lambda t: (t[0], t[1]), reverse=True)
        return [app for _, _, app in scored[:n]]

    def add_pseudo(self, app_id: str, name: str, exec_line: str, icon: str) -> None:
        self._pseudo[app_id] = App(app_id=app_id, name=name, exec_line=exec_line,
                                   icon=icon, pseudo=True)

    # -- window matching ----------------------------------------------------
    def match_window(self, rc: str, desktop_file: str = "", pid: int = 0) -> App | None:
        rc_low = (rc or "").lower()
        if rc_low in ("soffice", "soffice.bin") and pid:
            try:
                with open(f"/proc/{pid}/cmdline", "rb") as f:
                    cmdline = f.read().decode("utf-8", errors="ignore").lower()
                for key, app_id in (
                    ("writer", "libreoffice-writer"),
                    ("calc", "libreoffice-calc"),
                    ("impress", "libreoffice-impress"),
                    ("draw", "libreoffice-draw"),
                    ("math", "libreoffice-math"),
                    ("global", "libreoffice-writer"),
                    ("web", "libreoffice-writer"),
                ):
                    if key in cmdline:
                        app = self._find_app(app_id)
                        if app:
                            return app
                app = self._find_app("libreoffice-startcenter") or self._find_app("libreoffice")
                if app:
                    return app
            except Exception:
                pass

        if desktop_file:
            df = desktop_file.replace(".desktop", "").replace("/", "-")
            app = self._find_app(df)
            if app:
                return app
        if pid:
            steam_id = ""
            # 1) Try environment variables
            try:
                with open(f"/proc/{pid}/environ", "rb") as f:
                    env_data = f.read()
                for item in env_data.split(b"\x00"):
                    if item.startswith(b"SteamAppId=") or item.startswith(b"STEAM_COMPAT_APP_ID="):
                        steam_id = item.split(b"=", 1)[1].decode("utf-8", errors="ignore")
                        break
            except Exception:
                pass

            # 2) Try executable path lookup in appmanifest .acf files
            if not steam_id:
                try:
                    exe = os.readlink(f"/proc/{pid}/exe")
                    if exe and "steamapps/common/" in exe:
                        parts = exe.split("steamapps/common/", 1)
                        steamapps_dir = os.path.join(parts[0], "steamapps")
                        rest = parts[1].split("/", 1)
                        if rest:
                            installdir = rest[0].lower()
                            if os.path.isdir(steamapps_dir):
                                import re
                                for filename in os.listdir(steamapps_dir):
                                    if filename.startswith("appmanifest_") and filename.endswith(".acf"):
                                        acf_path = os.path.join(steamapps_dir, filename)
                                        try:
                                            with open(acf_path, "r", encoding="utf-8", errors="ignore") as f:
                                                content = f.read()
                                            appid_match = re.search(r'"appid"\s+"([^"]+)"', content)
                                            installdir_match = re.search(r'"installdir"\s+"([^"]+)"', content)
                                            if appid_match and installdir_match:
                                                if installdir_match.group(1).lower() == installdir:
                                                    steam_id = appid_match.group(1)
                                                    break
                                        except Exception:
                                            pass
                except Exception:
                    pass

            if steam_id:
                # 1) Search in scanned apps Exec line first (since Steam menu entries are named '<Name>.desktop' instead of 'steam_app_<appid>.desktop')
                for aid, app in self.apps.items():
                    if f"rungameid/{steam_id}" in app.exec_line or f"applaunch/{steam_id}" in app.exec_line:
                        return app
                
                # 2) Try direct ID lookup
                app = self._find_app(f"steam_app_{steam_id}")
                if app:
                    return app

                # 3) Fallback: Read game name from appmanifest_*.acf and register as a pseudo app
                game_name = ""
                steam_paths = [
                    os.path.expanduser("~/.local/share/Steam/steamapps"),
                    os.path.expanduser("~/.steam/steam/steamapps"),
                ]
                try:
                    exe = os.readlink(f"/proc/{pid}/exe")
                    if exe and "steamapps/common/" in exe:
                        parts = exe.split("steamapps/common/", 1)
                        steam_paths.append(os.path.join(parts[0], "steamapps"))
                except Exception:
                    pass

                for s_path in steam_paths:
                    acf_path = os.path.join(s_path, f"appmanifest_{steam_id}.acf")
                    if os.path.isfile(acf_path):
                        try:
                            with open(acf_path, "r", encoding="utf-8", errors="ignore") as f:
                                content = f.read()
                            import re
                            name_match = re.search(r'"name"\s+"([^"]+)"', content)
                            if name_match:
                                game_name = name_match.group(1)
                                break
                        except Exception:
                            pass

                if not game_name:
                    game_name = rc.capitalize() if rc else f"Steam Game {steam_id}"

                # Register pseudo app so it has a name and icon
                pseudo_id = f"steam_app_{steam_id}"
                self.add_pseudo(pseudo_id, game_name, f"steam steam://rungameid/{steam_id}", f"steam_icon_{steam_id}")
                return self._pseudo[pseudo_id]
        if rc:
            app = self._find_app(rc)
            if app:
                return app
            for aid, app in self.apps.items():
                if aid.lower() == rc_low or app.wm_class.lower() == rc_low:
                    return app
            for aid, app in self.apps.items():
                if aid.lower().endswith("." + rc_low) or aid.lower().endswith("-" + rc_low):
                    return app
            # Substring/prefix match for resource class (e.g. "brave" -> "brave-browser", "antigravity" -> "antigravity-ide")
            for aid, app in self.apps.items():
                aid_low = aid.lower().replace(".desktop", "")
                if len(rc_low) >= 3 and (rc_low in aid_low or aid_low in rc_low):
                    return app
        return None

    # -- launch -------------------------------------------------------------
    def launch(self, app: App) -> None:
        try:
            parts = [p for p in shlex.split(app.exec_line)
                     if not p.startswith("%")]
            if app.terminal:
                parts = ["konsole", "-e"] + parts
            subprocess.Popen(parts, start_new_session=True, env=child_env())
            self.record_usage(app.app_id)
        except Exception:  # noqa: BLE001
            log.exception("App-Start fehlgeschlagen: %s", app.app_id)
