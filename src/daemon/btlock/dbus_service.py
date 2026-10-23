"""
btlock D-Bus service module.

Copyright (C) 2025 btlock contributors
SPDX-License-Identifier: GPL-3.0-or-later

Exposes the btlock daemon on the system D-Bus so that clients
(GUI, CLI, other services) can query status, lock manually, and
subscribe to device proximity events.

Service name: org.btlock.Service
Object path:  /org/btlock
Interface:    org.btlock.Service

Methods:
- Lock()           - Lock the screen manually
- Unlock()         - Unlock the screen (requires auth)
- GetStatus()      - Return current status as a dict
- GetDevices()     - Return list of tracked devices
- SetEnrolled()    - Update enrolled devices list (auth required)
- ReloadConfig()   - Reload configuration

Signals:
- DeviceFound(address, name, rssi)
- DeviceLost(address)
- LockStateChanged(bool locked)
- StatusChanged(string status)
"""

from __future__ import annotations

import logging

from typing import Any, Callable, Optional

from .config import BtlockConfig
from .desktop_adapter import DesktopAdapter
from .bluez_monitor import BluezMonitor, DeviceInfo

logger = logging.getLogger(__name__)

DBUS_SERVICE_NAME = "org.btlock.Service"
DBUS_OBJECT_PATH = "/org/btlock"
DBUS_INTERFACE = "org.btlock.Service"

GLIB_NODE_PATH = "/org/btlock"


class BtlockDBusService:
    """
    Registers btlock on the system D-Bus and handles method calls.
    """

    def __init__(
        self,
        config: BtlockConfig,
        monitor: BluezMonitor,
        desktop: DesktopAdapter,
        on_lock: Callable[[], bool],
        on_unlock: Callable[[], bool],
    ) -> None:
        self._config = config
        self._monitor = monitor
        self._desktop = desktop
        self._on_lock = on_lock
        self._on_unlock = on_unlock
        self._connection: Optional[object] = None
        self._registration_id: Optional[int] = None

    def register(self) -> bool:
        """Register the service on the system D-Bus."""
        try:
            from gi.repository import GLib, Gio
        except ImportError:
            logger.warning("PyGObject not available; D-Bus service disabled")
            return False

        try:
            self._connection = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        except Exception as e:
            logger.error("Failed to connect to system bus: %s", e)
            return False

        try:
            result = self._connection.call_sync(
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus",
                "NameHasOwner",
                GLib.Variant("(s)", (DBUS_SERVICE_NAME,)),
                None,
                Gio.DBusCallFlags.NONE,
                -1,
                None,
            )
            has_owner, = result.unpack()
            if has_owner:
                logger.warning("btlock service already running")
                return False
        except Exception as e:
            logger.error("Name check failed: %s", e)
            return False

        try:
            self._connection.call_sync(
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus",
                "RequestName",
                GLib.Variant("(su)", (DBUS_SERVICE_NAME, 4)),
                None,
                Gio.DBusCallFlags.NONE,
                -1,
                None,
            )
        except Exception as e:
            logger.error("Failed to register name: %s", e)
            return False

        self._register_object()
        logger.info("Registered btlock D-Bus service")
        return True

    def _register_object(self) -> None:
        """Register the D-Bus object with method handlers."""
        from gi.repository import GLib, Gio

        xml = self._build_introspection()
        node = GLib.NodeInfo.new_for_xml(xml)

        vtable = Gio.DBusInterfaceVTable.new(
            self._on_method_call,
            self._on_get_property,
            self._on_set_property,
        )

        try:
            self._registration_id = self._connection.register_object(
                GLIB_NODE_PATH,
                node,
                vtable,
                None,
            )
        except Exception as e:
            logger.error("Failed to register D-Bus object: %s", e)

    def _build_introspection(self) -> str:
        """Build D-Bus introspection XML for the service interface."""
        return f"""
<node name="{GLIB_NODE_PATH}">
  <interface name="{DBUS_INTERFACE}">
    <method name="Lock">
      <arg type="b" direction="out" name="success"/>
    </method>
    <method name="Unlock">
      <arg type="b" direction="out" name="success"/>
    </method>
    <method name="GetStatus">
      <arg type="a{{sv}}" direction="out" name="status"/>
    </method>
    <method name="GetDevices">
      <arg type="aa{{sv}}" direction="out" name="devices"/>
    </method>
    <method name="SetEnrolled">
      <arg type="as" direction="in" name="addresses"/>
    </method>
    <method name="ReloadConfig"/>
    <signal name="DeviceFound">
      <arg type="s" name="address"/>
      <arg type="s" name="name"/>
      <arg type="n" name="rssi"/>
    </signal>
    <signal name="DeviceLost">
      <arg type="s" name="address"/>
    </signal>
    <signal name="LockStateChanged">
      <arg type="b" name="locked"/>
    </signal>
    <signal name="StatusChanged">
      <arg type="s" name="status"/>
    </signal>
  </interface>
</node>
"""

    def _on_method_call(
        self,
        connection: object,
        sender: str,
        path: str,
        interface_name: str,
        method_name: str,
        parameters: object,
        user_data: object,
    ) -> None:
        """Handle incoming D-Bus method calls."""
        from gi.repository import GLib

        reply = None
        if method_name == "Lock":
            success = self._on_lock()
            reply = GLib.Variant("(b)", (success,))
        elif method_name == "Unlock":
            success = self._on_unlock()
            reply = GLib.Variant("(b)", (success,))
        elif method_name == "GetStatus":
            status = self._get_status_dict()
            reply = GLib.Variant("(a{sv})", (status,))
        elif method_name == "GetDevices":
            devices = self._get_devices_list()
            reply = GLib.Variant("(aa{sv})", (devices,))
        elif method_name == "SetEnrolled":
            try:
                addresses, = parameters.unpack()
                self._config.enrolled_devices = set(addresses)
                self._monitor._enrolled = set(addresses)
            except Exception as e:
                logger.error("SetEnrolled failed: %s", e)
        elif method_name == "ReloadConfig":
            try:
                self._config.reload()
                self._monitor._enrolled = set(
                    self._config.enrolled_devices
                )
            except Exception as e:
                logger.error("ReloadConfig failed: %s", e)

        if reply is not None:
            connection.call_with_unix_fdlist(
                None,
                None,
                None,
                sender,
                GLIB_NODE_PATH,
                "org.freedesktop.DBus",
                "Method.Return",
                reply,
                -1,
                None,
            )

    def _on_get_property(
        self,
        connection: object,
        sender: str,
        path: str,
        interface_name: str,
        prop: str,
        error: object,
        user_data: object,
    ) -> object:
        """Handle property reads."""
        from gi.repository import GLib

        if prop == "Backend":
            return GLib.Variant(
                "s", self._desktop.get_backend_name()
            )
        if prop == "Locked":
            return GLib.Variant("b", self._desktop.is_locked())
        if prop == "AdapterPath":
            return GLib.Variant(
                "s", str(self._monitor._adapter_path or "")
            )
        return GLib.Variant("s", "")

    def _on_set_property(
        self,
        connection: object,
        sender: str,
        path: str,
        interface_name: str,
        prop: str,
        value: object,
        error: object,
        user_data: object,
    ) -> bool:
        """Handle property writes (all read-only)."""
        return False

    def _get_status_dict(self) -> dict:
        """Return current status as a dict."""
        return {
            "backend": self._desktop.get_backend_name(),
            "locked": self._desktop.is_locked(),
            "adapter_path": str(self._monitor._adapter_path or ""),
            "device_count": len(self._monitor.get_tracked_devices()),
            "running": self._monitor._running,
        }

    def _get_devices_list(self) -> list:
        """Return list of devices as dicts."""
        result = []
        for dev in self._monitor.get_tracked_devices():
            if isinstance(dev, DeviceInfo):
                result.append({
                    "address": dev.address,
                    "name": dev.name,
                    "rssi": dev.rssi,
                    "is_present": dev.is_present,
                })
        return result

    def emit_device_found(self, dev: DeviceInfo) -> None:
        """Emit a DeviceFound signal to D-Bus clients."""
        if self._connection is None:
            return
        from gi.repository import GLib

        try:
            self._connection.emit_signal(
                DBUS_SERVICE_NAME,
                GLIB_NODE_PATH,
                DBUS_INTERFACE,
                "DeviceFound",
                GLib.Variant(
                    "(ssn)", (dev.address, dev.name, dev.rssi)
                ),
            )
        except Exception as e:
            logger.debug("Failed to emit DeviceFound: %s", e)

    def emit_device_lost(self, address: str) -> None:
        """Emit a DeviceLost signal to D-Bus clients."""
        if self._connection is None:
            return
        from gi.repository import GLib

        try:
            self._connection.emit_signal(
                DBUS_SERVICE_NAME,
                GLIB_NODE_PATH,
                DBUS_INTERFACE,
                "DeviceLost",
                GLib.Variant("(s)", (address,)),
            )
        except Exception as e:
            logger.debug("Failed to emit DeviceLost: %s", e)

    def emit_lock_changed(self, locked: bool) -> None:
        """Emit a LockStateChanged signal."""
        if self._connection is None:
            return
        from gi.repository import GLib

        try:
            self._connection.emit_signal(
                DBUS_SERVICE_NAME,
                GLIB_NODE_PATH,
                DBUS_INTERFACE,
                "LockStateChanged",
                GLib.Variant("(b)", (locked,)),
            )
        except Exception as e:
            logger.debug("Failed to emit LockStateChanged: %s", e)

    def emit_status_changed(self, status: str) -> None:
        """Emit a StatusChanged signal."""
        if self._connection is None:
            return
        from gi.repository import GLib

        try:
            self._connection.emit_signal(
                DBUS_SERVICE_NAME,
                GLIB_NODE_PATH,
                DBUS_INTERFACE,
                "StatusChanged",
                GLib.Variant("(s)", (status,)),
            )
        except Exception as e:
            logger.debug("Failed to emit StatusChanged: %s", e)

    def shutdown(self) -> None:
        """Clean up D-Bus registration."""
        if self._registration_id is not None and \
                self._connection is not None:
            try:
                self._connection.unregister_object(
                    self._registration_id
                )
            except Exception:
                pass
            self._registration_id = None
        self._connection = None