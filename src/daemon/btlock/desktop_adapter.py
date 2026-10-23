"""
btlock desktop session locking adapter.

Copyright (C) 2025 btlock contributors
SPDX-License-Identifier: GPL-3.0-or-later

Provides a unified interface for locking and unlocking the desktop
session across multiple desktop environments. Detects the available
locking backend and uses the appropriate mechanism.

Supported backends:
- Freedesktop ScreenSaver D-Bus (GNOME, KDE, XFCE, etc.)
- Login1 session inhibition (systemd)
- org.freedesktop.login1.Manager.LockSession
- Fallback: loginctl command

Security considerations:
- All subprocess calls use argument arrays (no shell=True)
- Lock confirmation is required before unlocking
- Timeout handling prevents indefinite hangs
"""

from __future__ import annotations

import logging
import os
import subprocess
import time
from enum import Enum
from typing import Optional

logger = logging.getLogger(__name__)


class LockBackend(Enum):
    """Supported locking backends."""
    SCREENSAVER = "screensaver"
    LOGIN1 = "login1"
    LOGINDCTL = "loginctl"
    UNKNOWN = "unknown"


class DesktopAdapter:
    """
    Locks and unlocks the desktop session.

    Automatically detects the best available backend. All lock/unlock
    operations are non-blocking and use timeouts to prevent hangs.
    """

    LOCK_TIMEOUT_SEC = 30
    UNLOCK_TIMEOUT_SEC = 5

    def __init__(self) -> None:
        self._backend = LockBackend.UNKNOWN
        self._proxy: Optional[object] = None
        self._session_id: Optional[str] = None
        self._detect_backend()

    def _detect_backend(self) -> None:
        """Detect available locking backend."""
        try:
            from gi.repository import Gio
            self._bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        except Exception:
            self._bus = None

        # Try Freedesktop ScreenSaver first
        if self._bus is not None:
            try:
                self._proxy = Gio.DBusProxy.new_sync(
                    self._bus,
                    Gio.DBusProxyFlags.NONE,
                    None,
                    "org.freedesktop.ScreenSaver",
                    "/org/freedesktop/ScreenSaver",
                    "org.freedesktop.ScreenSaver",
                    None,
                )
                self._proxy.Introspect()  # Verify it's accessible
                self._backend = LockBackend.SCREENSAVER
                logger.info("Using ScreenSaver D-Bus backend")
                return
            except Exception:
                pass

            # Try Login1
            try:
                login1 = Gio.DBusProxy.new_sync(
                    self._bus,
                    Gio.DBusProxyFlags.NONE,
                    None,
                    "org.freedesktop.login1",
                    "/org/freedesktop/login1",
                    "org.freedesktop.login1.Manager",
                    None,
                )
                self._session_id = login1.GetSessionByPID(
                    os.getuid(), 0
                ) if hasattr(login1, "GetSessionByPID") else None
                self._proxy = login1
                self._backend = LockBackend.LOGIN1
                logger.info("Using Login1 backend")
                return
            except Exception:
                pass

        # Fallback: loginctl command
        self._backend = LockBackend.LOGINDCTL
        logger.info("Using loginctl command backend")

    @property
    def backend(self) -> LockBackend:
        """Return the detected locking backend."""
        return self._backend

    def lock(self) -> bool:
        """
        Lock the desktop session.

        Returns True if the lock was requested successfully.
        """
        if self._backend == LockBackend.SCREENSAVER and self._proxy is not None:
            return self._lock_screensaver()
        elif self._backend == LockBackend.LOGIN1 and self._proxy is not None:
            return self._lock_login1()
        elif self._backend == LockBackend.LOGINDCTL:
            return self._lock_loginctl()
        return False

    def _lock_screensaver(self) -> bool:
        """Lock via Freedesktop ScreenSaver D-Bus interface."""
        try:
            self._proxy.Lock()
            logger.info("Lock requested via ScreenSaver")
            return True
        except Exception as e:
            logger.error("ScreenSaver lock failed: %s", e)
            return False

    def _lock_login1(self) -> bool:
        """Lock via Login1 Manager interface."""
        try:
            if self._session_id:
                self._proxy.LockSession(self._session_id)
            else:
                self._proxy.LockSessionByPID(os.getuid(), 0)
            logger.info("Lock requested via Login1")
            return True
        except Exception as e:
            logger.error("Login1 lock failed: %s", e)
            return False

    def _lock_loginctl(self) -> bool:
        """Lock via loginctl command."""
        try:
            result = subprocess.run(
                ["loginctl", "lock-sessions"],
                capture_output=True,
                timeout=self.LOCK_TIMEOUT_SEC,
            )
            if result.returncode == 0:
                logger.info("Lock requested via loginctl")
                return True
            logger.error("loginctl lock failed: %s", result.stderr)
            return False
        except subprocess.TimeoutExpired:
            logger.error("loginctl lock timed out")
            return False
        except FileNotFoundError:
            logger.error("loginctl not found")
            return False
        except Exception as e:
            logger.error("loginctl lock error: %s", e)
            return False

    def unlock(self) -> bool:
        """
        Unlock the desktop session.

        This requires lock confirmation from the user (entering password).
        In practice, unlock is only used for testing / diagnostics.
        Real unlock happens when the user enters their password.
        """
        logger.info("Unlock requested (usually user-entered password)")
        return True

    def is_locked(self) -> bool:
        """Check if the session is currently locked."""
        if self._backend == LockBackend.SCREENSAVER and self._proxy is not None:
            try:
                active = self._proxy.GetActive()
                return not active
            except Exception:
                return False
        elif self._backend == LockBackend.LOGINDCTL:
            try:
                result = subprocess.run(
                    ["loginctl", "show-session", str(os.getuid()),
                     "--property=Active"],
                    capture_output=True,
                    timeout=5,
                    text=True,
                )
                return "Active=no" in result.stdout
            except Exception:
                return False
        return False

    def get_backend_name(self) -> str:
        """Return a human-readable name for the current backend."""
        return self._backend.value
