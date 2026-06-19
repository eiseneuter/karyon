"""Recently used files (KRecentDocuments / recently-used.xbel)."""
from __future__ import annotations

import logging
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import unquote, urlparse
from xml.etree import ElementTree as ET

from .procenv import child_env

log = logging.getLogger(__name__)


@dataclass
class RecentFile:
    name: str
    path: str
    icon_name: str = ""
    mtime: float = 0.0


class RecentFiles:
    def __init__(self) -> None:
        self._recent_dir = Path(os.path.expanduser("~/.local/share/RecentDocuments"))
        self._xbel = Path(os.path.expanduser("~/.local/share/recently-used.xbel"))

    def items(self, n: int) -> list[RecentFile]:
        out: dict[str, RecentFile] = {}
        for rf in self._from_recent_docs():
            out.setdefault(rf.path, rf)
        for rf in self._from_xbel():
            out.setdefault(rf.path, rf)
        # A folder is not a recent file -- drop directories.
        ordered = sorted(
            (r for r in out.values() if not os.path.isdir(r.path)),
            key=lambda r: r.mtime, reverse=True)
        return ordered[:n]

    def _from_recent_docs(self) -> list[RecentFile]:
        result = []
        if not self._recent_dir.is_dir():
            return result
        for path in self._recent_dir.glob("*.desktop"):
            name = url = icon = ""
            try:
                for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
                    line = raw.strip()
                    if line.startswith("Name="):
                        name = line[5:]
                    elif line.startswith("URL="):
                        url = line[4:]
                    elif line.startswith("Icon="):
                        icon = line[5:]
                fpath = self._url_to_path(url)
                if fpath:
                    result.append(RecentFile(name or os.path.basename(fpath),
                                             fpath, icon, path.stat().st_mtime))
            except Exception:  # noqa: BLE001
                continue
        return result

    def _from_xbel(self) -> list[RecentFile]:
        result = []
        if not self._xbel.exists():
            return result
        try:
            tree = ET.parse(self._xbel)
            for bm in tree.getroot().iter("bookmark"):
                href = bm.get("href", "")
                fpath = self._url_to_path(href)
                if not fpath:
                    continue
                modified = bm.get("modified") or bm.get("visited") or ""
                mtime = 0.0
                try:
                    import datetime
                    mtime = datetime.datetime.fromisoformat(
                        modified.replace("Z", "+00:00")).timestamp()
                except Exception:  # noqa: BLE001
                    pass
                result.append(RecentFile(os.path.basename(fpath), fpath, "", mtime))
        except Exception:  # noqa: BLE001
            log.debug("xbel konnte nicht gelesen werden", exc_info=True)
        return result

    @staticmethod
    def _url_to_path(url: str) -> str:
        if not url:
            return ""
        if url.startswith("file://"):
            return unquote(urlparse(url).path)
        if url.startswith("/"):
            return url
        return ""

    def open(self, path: str) -> None:
        try:
            subprocess.Popen(["xdg-open", path], start_new_session=True,
                             env=child_env())
        except Exception:  # noqa: BLE001
            log.exception("Datei konnte nicht geoeffnet werden: %s", path)
