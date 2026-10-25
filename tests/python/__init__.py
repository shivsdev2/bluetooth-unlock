"""
Tests for btlock Python modules.

Run with: python3 -m pytest tests/python/ -v
Or:       python3 -m unittest discover -s tests/python -v
"""

import sys
import os
from pathlib import Path

# Ensure the daemon package is importable
_DAEMON_DIR = Path(__file__).parent.parent / "src" / "daemon"
sys.path.insert(0, str(_DAEMON_DIR))

from btlock.config import BtlockConfig, ConfigError
from btlock.bluez_monitor import BluezMonitor, DeviceInfo
from btlock.state_bridge import (
    NullStateBridge, BtlockEvent, BtlockState, BtlockAction,
)
from btlock.desktop_adapter import DesktopAdapter, LockBackend
from btlock.dbus_service import BtlockDBusService


__all__ = ["*"]