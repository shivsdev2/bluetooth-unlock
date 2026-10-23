"""
btlock daemon package - Bluetooth proximity locking daemon.

Copyright (C) 2025 btlock contributors
SPDX-License-Identifier: GPL-3.0-or-later

This package provides:
- Configuration management with schema validation
- BlueZ D-Bus integration for Bluetooth monitoring
- Desktop session locking adapters
- D-Bus service for UI communication
- A bridge to the C core state machine via ctypes
"""

__version__ = "0.1.0"
__all__ = ["config", "state_bridge", "bluez_monitor", "desktop_adapter",
           "daemon", "dbus_service"]

