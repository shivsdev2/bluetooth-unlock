"""
btlock BlueZ D-Bus monitor.

Copyright (C) 2025 btlock contributors
SPDX-License-Identifier: GPL-3.0-or-later

Monitors Bluetooth devices via BlueZ's D-Bus API and reports RSSI
changes to the state machine. Uses the Adapter1 and Device1 D-Bus
interfaces from the org.bluez service.
"""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass

from typing import Callable, Optional

from .config import BtlockConfig

logger = logging.getLogger(__name__)

DBUS_SERVICE_NAME = "org.bluez"
DBUS_ADAPTER_IFACE = "org.bluez.Adapter1"
DBUS_DEVICE_IFACE = "org.bluez.Device1"
DBUS_OBJECT_MANAGER = "org.freedesktop.DBus.ObjectManager"

PROPERTIES_IFACE = "org.freedesktop.DBus.Properties"


@dataclass
class DeviceInfo:
    """Represents a tracked Bluetooth device."""

    address: str
    name: str
    rssi: int
    last_seen_ms: int
    is_present: bool
    rssi_samples: list[int]


class FilterType:
    """Supported RSSI filter types."""
    MEDIAN = "median"
    MOVING_AVERAGE = "moving_average"


class BluezMonitor:
    """
    Monitors Bluetooth devices via BlueZ D-Bus.

    Args:
        config: The btlock configuration.
        on_device_found: Callback(address, DeviceInfo) when device appears.
        on_device_lost: Callback(address) when device disappears.
        on_rssi_update: Callback(address, rssi) for filtered RSSI updates.
    """

    def __init__(
        self,
        config: BtlockConfig,
        on_device_found: Callable[[str, DeviceInfo], None],
        on_device_lost: Callable[[str], None],
        on_rssi_update: Callable[[str, int], None],
    ) -> None:
        self._config = config
        self._on_device_found = on_device_found
        self._on_device_lost = on_device_lost
        self._on_rssi_update = on_rssi_update

        self._bus: Optional[object] = None
        self._adapter_path: Optional[str] = None
        self._devices: dict[str, DeviceInfo] = {}
        self._enrolled: set[str] = set(config.enrolled_devices)
        self._filter_type = (
            config.filter_type
            if config.filter_type in ("median", "moving_average")
            else "median"
        )
        self._running = False

    def start(self) -> bool:
        """
        Connect to BlueZ D-Bus and start monitoring.

        Returns True on success, False if BlueZ is unavailable.
        If PyGObject is unavailable, falls back to simulation mode.
        """
        try:
            from gi.repository import GLib, Gio
        except ImportError:
            logger.warning(
                "PyGObject not available; BlueZ monitoring will be simulated"
            )
            self._running = True
            self._simulate_start()
            return True

        self._loop = GLib.MainLoop()
        self._context = GLib.MainContext.default()

        try:
            self._bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        except Exception as e:
            logger.error("Failed to connect to system D-Bus: %s", e)
            return False

        if not self._find_adapter():
            logger.error("No Bluetooth adapter found")
            return False

        self._setup_signals()
        self._running = True
        self._scan_start_time = time.monotonic()
        logger.info("BlueZ monitoring started on adapter %s",
                     self._adapter_path)
        return True

    def _find_adapter(self) -> bool:
        """Find the first available Bluetooth adapter via ObjectManager."""
        if self._bus is None:
            return self._simulate_find_adapter()

        try:
            proxy = Gio.DBusProxy.new_sync(
                self._bus,
                Gio.DBusProxyFlags.NONE,
                None,
                DBUS_SERVICE_NAME,
                "/org/bluez",
                DBUS_OBJECT_MANAGER,
                None,
            )
            objects = proxy.GetManagedObjects()
        except Exception as e:
            logger.error("Failed to get managed objects: %s", e)
            return False

        for path in sorted(objects.keys()):
            if path.startswith("/org/bluez/hci"):
                self._adapter_path = path
                logger.debug("Found adapter at %s", path)
                return True

        return self._simulate_find_adapter()

    def _setup_signals(self) -> None:
        """Subscribe to BlueZ PropertiesChanged signals."""
        try:
            self._bus.signal_subscribe(
                DBUS_SERVICE_NAME,
                PROPERTIES_IFACE,
                "PropertiesChanged",
                None,
                None,
                Gio.DBusSignalFlags.NONE,
                self._on_properties_changed,
                None,
            )
        except Exception as e:
            logger.error("Failed to subscribe to D-Bus signals: %s", e)

    def _on_properties_changed(
        self,
        connection: object,
        sender_name: str,
        object_path: str,
        interface: str,
        signal_name: str,
        parameters: object,
    ) -> None:
        """Handle PropertiesChanged signals from BlueZ."""
        if interface != DBUS_DEVICE_IFACE:
            return

        try:
            variant, = parameters.unpack()
            changed = dict(variant)
        except Exception:
            return

        rssi = changed.get("RSSI")
        name = changed.get("Name")
        address = self._address_from_path(object_path)

        if address not in self._enrolled:
            return

        if address not in self._devices:
            self._devices[address] = DeviceInfo(
                address=address,
                name=name or "",
                rssi=rssi or 0,
                last_seen_ms=0,
                is_present=True,
                rssi_samples=[],
            )
            self._on_device_found(address, self._devices[address])

        dev = self._devices[address]
        dev.last_seen_ms = int(time.monotonic() * 1000)
        dev.is_present = True

        if rssi is not None and rssi != dev.rssi:
            filtered = self._apply_filter(dev, rssi)
            self._on_rssi_update(address, filtered)
            dev.rssi = rssi

    def _address_from_path(self, path: str) -> str:
        """Extract device address from D-Bus object path."""
        # /org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX -> XX:XX:XX:XX:XX:XX
        parts = path.split("_")
        if len(parts) >= 2:
            addr = parts[-1].replace("_", ":").upper()
            return addr
        return ""

    def _apply_filter(self, dev: DeviceInfo, rssi: int) -> int:
        """Apply RSSI filtering based on configured filter type."""
        dev.rssi_samples.append(rssi)
        if len(dev.rssi_samples) > self._config.rssi_window:
            dev.rssi_samples.pop(0)

        if self._filter_type == "median":
            sorted_samples = sorted(dev.rssi_samples)
            mid = len(sorted_samples) // 2
            if len(sorted_samples) % 2 == 0:
                return (sorted_samples[mid - 1] + sorted_samples[mid]) // 2
            return sorted_samples[mid]
        else:
            return sum(dev.rssi_samples) // len(dev.rssi_samples)

    def scan(self) -> None:
        """Perform a scan for nearby enrolled devices (polling approach)."""
        if not self._running:
            return

        if self._bus is not None and self._adapter_path:
            self._do_bluez_scan()
        else:
            self._simulate_scan()

    def _do_bluez_scan(self) -> None:
        """Initiate a BLE scan via BlueZ to refresh RSSI."""
        try:
            adapter = Gio.DBusProxy.new_sync(
                self._bus,
                Gio.DBusProxyFlags.NONE,
                None,
                DBUS_SERVICE_NAME,
                self._adapter_path,
                DBUS_ADAPTER_IFACE,
                None,
            )
            adapter.StartDiscovery()
        except Exception as e:
            logger.debug("Scan request failed: %s", e)

    def _simulate_start(self) -> None:
        """Start in simulation mode for testing."""
        self._adapter_path = "simulated:hci0"
        self._devices = {}
        self._scan_start_time = time.monotonic()
        logger.info("BlueZ monitoring started in simulation mode")

    def _simulate_find_adapter(self) -> bool:
        """Simulate finding an adapter (for testing)."""
        self._adapter_path = "simulated:hci0"
        return True

    def _simulate_scan(self) -> None:
        """Simulate scanning for devices."""
        pass

    def inject_device(
        self, address: str, name: str, rssi: int
    ) -> None:
        """Inject a simulated device (testing only)."""
        now_ms = int(time.monotonic() * 1000)
        if address not in self._devices:
            self._devices[address] = DeviceInfo(
                address=address,
                name=name,
                rssi=rssi,
                last_seen_ms=now_ms,
                is_present=True,
                rssi_samples=[],
            )
            self._on_device_found(address, self._devices[address])
        else:
            dev = self._devices[address]
            filtered = self._apply_filter(dev, rssi)
            self._on_rssi_update(address, filtered)
            dev.rssi = rssi

    def mark_absent(self, address: str) -> None:
        """Mark a device as absent (testing or signal-based)."""
        if address in self._devices:
            self._on_device_lost(address)

    def get_tracked_devices(self) -> list:
        """Return currently tracked devices."""
        return list(self._devices.values())

    def is_device_present(self, address: str) -> bool:
        """Check if a device is currently marked as present."""
        dev = self._devices.get(address)
        if dev is None:
            return False
        return dev.is_present

    def stop(self) -> None:
        """Stop monitoring and release D-Bus resources."""
        self._running = False
        if self._bus is not None:
            try:
                self._bus.flush_sync(None)
            except Exception:
                pass
        logger.info("BlueZ monitoring stopped")

    def __enter__(self) -> "BluezMonitor":
        self.start()
        return self

    def __exit__(self, *args: object) -> None:
        self.stop()
