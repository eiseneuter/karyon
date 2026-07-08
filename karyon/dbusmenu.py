"""Read and activate an application's tray context menu (com.canonical.dbusmenu),
so the launcher can be a full system-tray replacement without a panel tray.

Menus are read via busctl (like tray.py) so the nested (ia{sv}av) layout comes
back as plain JSON, and clicks are sent with the dbusmenu Event method."""
from __future__ import annotations

import json
import logging
import subprocess
from dataclasses import dataclass, field

from .procenv import child_env

log = logging.getLogger(__name__)


@dataclass
class MenuEntry:
    id: int
    label: str = ""
    separator: bool = False
    enabled: bool = True
    visible: bool = True
    toggle_type: str = ""        # "checkmark" / "radio" / ""
    toggle_state: int = 0        # 1 = checked, 0 = unchecked, -1 = indeterminate
    has_submenu: bool = False
    children: list = field(default_factory=list)


def _val(props: dict, key, default=None):
    v = props.get(key)
    if isinstance(v, dict) and "data" in v:
        return v["data"]
    return default


def _clean_label(label: str) -> str:
    # DBusMenu labels mark the keyboard mnemonic with '_' (Gtk) or '&' (Qt).
    import re
    label = (label or "").replace("&&", "\0")
    label = re.sub(r"[_&](?=\w)", "", label)
    return label.replace("\0", "&").strip()


def _parse(item) -> MenuEntry:
    # item == [id, {props}, [children...]]
    mid = int(item[0])
    props = item[1] or {}
    kids_raw = item[2] or []
    kids = [_parse(c["data"] if isinstance(c, dict) and "data" in c else c)
            for c in kids_raw]
    typ = _val(props, "type", "")
    return MenuEntry(
        id=mid,
        label=_clean_label(_val(props, "label", "")),
        separator=(typ == "separator"),
        enabled=bool(_val(props, "enabled", True)),
        visible=bool(_val(props, "visible", True)),
        toggle_type=_val(props, "toggle-type", "") or "",
        toggle_state=int(_val(props, "toggle-state", 0) or 0),
        has_submenu=(_val(props, "children-display", "") == "submenu" or bool(kids)),
        children=kids,
    )


def read_menu(bus: str, menu_path: str) -> list[MenuEntry]:
    """Top-level entries of the app's context menu (with nested submenus)."""
    if not bus or not menu_path:
        return []
    try:
        # depth: a positive number (NOT -1, which busctl parses as a flag); 8 is
        # plenty for any real tray menu's submenu nesting.
        out = subprocess.run(
            ["busctl", "--user", "--json=short", "call", bus, menu_path,
             "com.canonical.dbusmenu", "GetLayout", "iias", "0", "8", "0"],
            capture_output=True, text=True, timeout=2.0, env=child_env())
        if out.returncode != 0:
            return []
        data = json.loads(out.stdout).get("data")
        # data == [revision, [rootId, {rootProps}, [children...]]]
        root = data[1]
        return [_parse(c["data"] if isinstance(c, dict) and "data" in c else c)
                for c in (root[2] or [])]
    except Exception:  # noqa: BLE001
        log.debug("Failed to read DBusMenu: %s %s", bus, menu_path)
        return []


def about_to_show(bus: str, menu_path: str) -> None:
    """Tell the app we are about to show its menu (lets it refresh dynamic
    entries).  Best-effort; ignored if unsupported."""
    if not bus or not menu_path:
        return
    try:
        subprocess.run(
            ["busctl", "--user", "call", bus, menu_path,
             "com.canonical.dbusmenu", "AboutToShow", "i", "0"],
            capture_output=True, text=True, timeout=1.0, env=child_env())
    except Exception:  # noqa: BLE001
        pass


def send_clicked(bus: str, menu_path: str, item_id: int) -> None:
    """Activate a menu entry (dbusmenu 'clicked' event)."""
    if not bus or not menu_path:
        return
    try:
        subprocess.Popen(
            ["busctl", "--user", "call", bus, menu_path,
             "com.canonical.dbusmenu", "Event", "isvu",
             str(int(item_id)), "clicked", "s", "", "0"],
            env=child_env())
    except Exception:  # noqa: BLE001
        log.exception("DBusMenu click failed: %s %s", menu_path, item_id)
