"""Per-app audio state via PipeWire/PulseAudio (pactl): which apps currently play
sound, and muting them.  Polled only while the overlay is (recently) shown."""
from __future__ import annotations

import logging
import os
import subprocess
import threading
import time

from .procenv import child_env

log = logging.getLogger(__name__)


class AudioMonitor:
    _ACTIVE_WINDOW = 4.0     # poll fast for this long after a request
    _ACTIVE_INTERVAL = 0.4   # fallback re-poll while active (events drive the rest)
    _IDLE_INTERVAL = 6.0

    def __init__(self) -> None:
        self._streams: list[dict] = []   # {index, pid, binary, name, muted, corked}
        self._last_request = 0.0
        self._overlay_active = False
        self._on_change = None
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._thread: threading.Thread | None = None
        self._sub_thread: threading.Thread | None = None
        self._sub_proc = None

    def start(self) -> None:
        if self._thread is not None:
            return
        try:
            self._poll()
        except Exception:  # noqa: BLE001
            pass
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        # Event-driven updates: pactl subscribe wakes us the instant a stream
        # appears / disappears / (un)mutes, so the speaker badge is near real-time
        # while the overlay is open (no waiting for the next poll tick).
        self._sub_thread = threading.Thread(target=self._run_subscribe, daemon=True)
        self._sub_thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._wake.set()
        proc = self._sub_proc
        if proc is not None:
            try:
                proc.terminate()
            except Exception:  # noqa: BLE001
                pass

    def request(self) -> None:
        """Mark the data as wanted now (overlay open) -> poll fast + honour events."""
        self._last_request = time.monotonic()
        self._wake.set()

    def set_overlay_active(self, active: bool) -> None:
        self._overlay_active = bool(active)
        if active:
            self.request()

    def set_on_change(self, callback) -> None:
        self._on_change = callback

    def _active(self) -> bool:
        return (self._overlay_active
                or (time.monotonic() - self._last_request) < self._ACTIVE_WINDOW)

    # -- polling ------------------------------------------------------------
    def _run(self) -> None:
        while True:
            active = self._active()
            wait = self._ACTIVE_INTERVAL if active else self._IDLE_INTERVAL
            
            # Wait for either the timeout or an explicit wake/stop signal
            self._wake.wait(wait)
            self._wake.clear()
            
            if self._stop.is_set():
                return
                
            if self._active():
                try:
                    self._poll()
                except Exception:  # noqa: BLE001
                    pass

    def _run_subscribe(self) -> None:
        # Block on `pactl subscribe` and re-poll on each sink-input event, but
        # only while active (overlay open) so a closed overlay costs nothing.
        while not self._stop.is_set():
            try:
                self._sub_proc = subprocess.Popen(
                    ["pactl", "subscribe"], stdout=subprocess.PIPE, text=True, bufsize=1,
                    env=child_env())
                for line in self._sub_proc.stdout:
                    if self._stop.is_set():
                        break
                    if "sink-input" not in line:
                        continue
                    if self._active():
                        # Signal the main polling loop to fetch the new state immediately,
                        # naturally debouncing rapid bursts of subscribe events.
                        self._wake.set()
            except Exception:  # noqa: BLE001
                pass
            if self._stop.wait(2.0):
                return

    def _poll(self) -> None:
        out = subprocess.run(["pactl", "list", "sink-inputs"],
                             capture_output=True, text=True, timeout=3,
                             env=child_env()).stdout
        streams = []
        cur: dict | None = None
        for line in out.splitlines():
            s = line.strip()
            if s.startswith("Sink Input #"):
                if cur is not None:
                    streams.append(cur)
                cur = {"index": s.split("#", 1)[1].strip(), "pid": 0,
                       "binary": "", "name": "", "muted": False, "corked": False}
            elif cur is None:
                continue
            elif s.startswith("Mute:"):
                cur["muted"] = s.split(":", 1)[1].strip().lower() == "yes"
            elif s.startswith("Corked:"):
                cur["corked"] = s.split(":", 1)[1].strip().lower() == "yes"
            elif s.startswith("application.process.id"):
                cur["pid"] = _int(_qval(s))
            elif s.startswith("application.process.binary"):
                cur["binary"] = _qval(s).lower()
            elif s.startswith("application.name"):
                cur["name"] = _qval(s).lower()
        if cur is not None:
            streams.append(cur)
        if streams != self._streams:
            self._streams = streams
            cb = self._on_change
            if cb is not None:
                try:
                    cb()
                except Exception:  # noqa: BLE001
                    pass

    # -- queries ------------------------------------------------------------
    def _matches(self, st: dict, pid: int, rc: str) -> bool:
        if pid and st["pid"] == pid:
            return True
        rc = (rc or "").lower()
        if not rc:
            return False
        b, n = st["binary"], st["name"]
        if b and (rc in b or b in rc):
            return True
        if n and (rc in n or n in rc):
            return True
        return False

    def _live(self, pid: int, rc: str) -> list:
        # Active (non-corked) streams of the app -- muted or not.
        return [st for st in self._streams
                if not st["corked"] and self._matches(st, pid, rc)]

    def has_stream(self, pid: int, rc: str) -> bool:
        """The app has a live audio stream (so the speaker badge is shown)."""
        return bool(self._live(pid, rc))

    def playing(self, pid: int, rc: str) -> bool:
        """A live, un-muted stream (actually outputting sound)."""
        return any(not st["muted"] for st in self._live(pid, rc))

    def is_muted(self, pid: int, rc: str) -> bool:
        """Has a live stream, all of them muted."""
        live = self._live(pid, rc)
        return bool(live) and all(st["muted"] for st in live)

    def toggle_mute(self, pid: int, rc: str) -> None:
        live = self._live(pid, rc)
        if not live:
            return
        mute = 1 if any(not st["muted"] for st in live) else 0
        for st in live:
            try:
                subprocess.Popen(
                    ["pactl", "set-sink-input-mute", st["index"], str(mute)],
                    env=child_env())
            except Exception:  # noqa: BLE001
                log.exception("Mute-Toggle fehlgeschlagen")
            st["muted"] = bool(mute)


def _qval(line: str) -> str:
    v = line.split("=", 1)[1].strip() if "=" in line else ""
    return v.strip().strip('"')


def _int(s: str) -> int:
    try:
        return int(s)
    except Exception:  # noqa: BLE001
        return 0
