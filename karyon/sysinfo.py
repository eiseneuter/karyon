"""Battery status + system-load monitor (for the optional hub info card)."""
from __future__ import annotations

import subprocess
import threading
import time
from pathlib import Path


def battery_status() -> dict | None:
    base = Path("/sys/class/power_supply")
    if not base.is_dir():
        return None
    for entry in base.iterdir():
        try:
            if (entry / "type").read_text().strip() != "Battery":
                continue
            capacity = int((entry / "capacity").read_text().strip())
            status = (entry / "status").read_text().strip()
            return {"percent": capacity, "status": status,
                    "charging": status.lower() == "charging"}
        except Exception:  # noqa: BLE001
            continue
    return None


class SystemMonitor:
    """Samples CPU / GPU / RAM utilisation on a background thread.

    CPU comes from /proc/stat deltas, RAM from /proc/meminfo.  GPU is the busiest
    of every AMD card (instant sysfs ``gpu_busy_percent``) and any NVIDIA card,
    and ``gpu_vendor`` is the maker of whichever card is actually carrying that
    load right now (so a hybrid laptop shows "AMD" on the desktop and "NVIDIA"
    when the dGPU is rendering).  NVIDIA is only queried via nvidia-smi when its
    PCI device is NOT runtime suspended, so the discrete GPU is never woken (and
    the battery drained) just to read 0%.
    """

    # Sample fast only while the hub is actually on screen; idle in the
    # background.  nvidia-smi is comparatively very expensive (NVML init per call),
    # so it is throttled hard and only ever run while the hub is being viewed.
    _ACTIVE_INTERVAL = 1.0
    _IDLE_INTERVAL = 5.0
    _ACTIVE_WINDOW = 4.0       # "viewed" if requested within this many seconds
    _NVIDIA_INTERVAL = 3.0     # min seconds between nvidia-smi spawns

    def __init__(self, interval: float = _ACTIVE_INTERVAL) -> None:
        self._interval = interval
        self._cpu = 0.0
        self._gpu = 0.0
        self._ram = 0.0
        self._gpu_vendor = ""
        self._gpu_available = False
        self._prev_cpu: tuple[int, int] | None = None
        self._last_request = 0.0
        self._last_nvidia = 0.0
        self._nvidia_cached: float | None = None
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._amd_paths = sorted(
            str(p) for p in Path("/sys/class/drm").glob("card*/device/gpu_busy_percent"))
        self._nvidia_pci = self._find_nvidia_pci()

    # -- lifecycle ----------------------------------------------------------
    def start(self) -> None:
        if self._thread is not None:
            return
        try:
            self._sample()  # prime (CPU needs two samples for a delta)
        except Exception:  # noqa: BLE001
            pass
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    @property
    def cpu(self) -> float:
        return self._cpu

    @property
    def gpu(self) -> float:
        return self._gpu

    @property
    def ram(self) -> float:
        return self._ram

    @property
    def gpu_vendor(self) -> str:
        return self._gpu_vendor

    @property
    def gpu_available(self) -> bool:
        return self._gpu_available

    # -- sampling -----------------------------------------------------------
    def _run(self) -> None:
        while True:
            active = (time.monotonic() - self._last_request) < self._ACTIVE_WINDOW
            wait = self._ACTIVE_INTERVAL if active else self._IDLE_INTERVAL
            if self._stop.wait(wait):
                return
            try:
                self._sample(active)
            except Exception:  # noqa: BLE001
                pass

    def _sample(self, active: bool = True) -> None:
        self._cpu = self._read_cpu()
        self._ram = self._read_ram()
        self._read_gpu(active)

    def _read_cpu(self) -> float:
        with open("/proc/stat", encoding="ascii") as f:
            parts = f.readline().split()
        vals = [int(x) for x in parts[1:]]
        idle = vals[3] + (vals[4] if len(vals) > 4 else 0)  # idle + iowait
        total = sum(vals)
        prev = self._prev_cpu
        self._prev_cpu = (idle, total)
        if prev is None:
            return self._cpu
        d_total = total - prev[1]
        d_idle = idle - prev[0]
        if d_total <= 0:
            return self._cpu
        return max(0.0, min(100.0, (1.0 - d_idle / d_total) * 100.0))

    @staticmethod
    def _read_ram() -> float:
        total = avail = None
        with open("/proc/meminfo", encoding="ascii") as f:
            for line in f:
                if line.startswith("MemTotal:"):
                    total = float(line.split()[1])
                elif line.startswith("MemAvailable:"):
                    avail = float(line.split()[1])
                if total is not None and avail is not None:
                    break
        if not total:
            return 0.0
        return max(0.0, min(100.0, (1.0 - avail / total) * 100.0))

    def _read_gpu(self, active: bool = True) -> None:
        # Each reading is (vendor, percent); the busiest wins both the value and
        # the displayed vendor label.  nvidia is only polled while the hub is
        # being viewed -- spawning nvidia-smi in the background would burn CPU.
        readings: list[tuple[str, float]] = []
        for path in self._amd_paths:
            try:
                with open(path, encoding="ascii") as f:
                    readings.append(("AMD", float(f.read().strip())))
            except Exception:  # noqa: BLE001
                pass
        nv = self._read_nvidia() if active else None
        if nv is not None:
            readings.append(("NVIDIA", nv))
        if not readings:
            self._gpu_available = False
            self._gpu = 0.0
            self._gpu_vendor = ""
            return
        vendor, value = max(readings, key=lambda r: r[1])
        self._gpu_available = True
        self._gpu = max(0.0, min(100.0, value))
        self._gpu_vendor = vendor

    @staticmethod
    def _find_nvidia_pci() -> list[str]:
        base = Path("/sys/bus/pci/drivers/nvidia")
        if not base.is_dir():
            return []
        return [e.name for e in base.iterdir() if ":" in e.name]

    def _read_nvidia(self) -> float | None:
        if not self._nvidia_pci:
            return None
        # nvidia-smi (NVML init each call) is expensive, so spawn it at most once
        # per _NVIDIA_INTERVAL and reuse the cached value in between.
        now = time.monotonic()
        if now - self._last_nvidia < self._NVIDIA_INTERVAL:
            return self._nvidia_cached
        self._last_nvidia = now
        # Reading runtime_status does NOT wake a suspended device; only query
        # nvidia-smi (which would wake it) when something already powered it on.
        powered = False
        for addr in self._nvidia_pci:
            try:
                st = Path(f"/sys/bus/pci/devices/{addr}/power/runtime_status"
                          ).read_text().strip()
                if st == "active":
                    powered = True
            except Exception:  # noqa: BLE001
                pass
        if not powered:
            self._nvidia_cached = None  # suspended -> idle, don't wake it
            return None
        try:
            r = subprocess.run(
                ["nvidia-smi", "--query-gpu=utilization.gpu",
                 "--format=csv,noheader,nounits"],
                capture_output=True, text=True, timeout=2)
            vals = [float(x) for x in r.stdout.split() if x.strip().rstrip(".").isdigit()]
            self._nvidia_cached = max(vals) if vals else None
        except Exception:  # noqa: BLE001
            self._nvidia_cached = None
        return self._nvidia_cached


_monitor: SystemMonitor | None = None


def _get_monitor() -> SystemMonitor:
    global _monitor
    if _monitor is None:
        _monitor = SystemMonitor()
        _monitor.start()
    return _monitor


def system_load() -> dict:
    """Latest CPU / GPU / RAM utilisation in %.  Starts the sampler on first use.

    Records the access time so the sampler only samples fast (and only spawns
    nvidia-smi) while the hub is actually being viewed; otherwise it idles.
    """
    m = _get_monitor()
    m._last_request = time.monotonic()
    return {"cpu": m.cpu, "gpu": m.gpu, "ram": m.ram,
            "gpu_vendor": m.gpu_vendor, "gpu_available": m.gpu_available}
