"""Lightweight performance logging helpers (debug only)."""
from __future__ import annotations

import logging
import time
from contextlib import contextmanager

log = logging.getLogger("karyon.perf")

_start = time.monotonic()


def mark(label: str) -> None:
    log.debug("MARK %8.1fms %s", (time.monotonic() - _start) * 1000.0, label)


@contextmanager
def timed(label: str):
    t0 = time.monotonic()
    try:
        yield
    finally:
        log.debug("TIMED %8.1fms %s", (time.monotonic() - t0) * 1000.0, label)
