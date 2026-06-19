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
    name = icon = exec_line = wm_class = ""
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
            key = key.split("[", 1)[0].strip()
            val = val.strip()
            if key == "Name" and not name:
                name = val
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
        scored = []
        for app_id in self._usage:
            if app_id in exclude:
                continue
            app = self.apps.get(app_id) or self._pseudo.get(app_id)
            if app is None or (app.no_display and not app.pseudo):
                continue
            scored.append((self._recency(app_id), app))
        scored.sort(key=lambda t: t[0], reverse=True)
        return [app for _, app in scored[:n]]

    def add_pseudo(self, app_id: str, name: str, exec_line: str, icon: str) -> None:
        self._pseudo[app_id] = App(app_id=app_id, name=name, exec_line=exec_line,
                                   icon=icon, pseudo=True)

    # -- window matching ----------------------------------------------------
    def match_window(self, rc: str, desktop_file: str = "") -> App | None:
        rc_low = (rc or "").lower()
        if desktop_file:
            df = desktop_file.replace(".desktop", "").replace("/", "-")
            app = self._find_app(df)
            if app:
                return app
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
