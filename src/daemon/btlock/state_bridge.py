"""
btlock state machine bridge - ctypes bindings to the C core library.

Copyright (C) 2025 btlock contributors
SPDX-License-Identifier: GPL-3.0-or-later

This module provides a Python interface to the C core state machine
via ctypes. It loads libbtlock-core.so and provides methods for
creating, configuring, and driving the state machine.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
from dataclasses import dataclass
from enum import IntEnum, auto
from pathlib import Path
from typing import Optional

from .config import BtlockConfig

def _find_library() -> Optional[str]:
    """Find libbtlock-core.so in standard locations and build dirs."""
    explicit = os.environ.get("BTLOCK_LIB_PATH")
    if explicit and os.path.isfile(explicit):
        return explicit

    pkg_dir = Path(__file__).parent
    search_paths = [
        pkg_dir.parent.parent.parent / "builddir" / "src" / "core",
        Path("/usr/lib"),
        Path("/usr/local/lib"),
        Path("/usr/lib64"),
        Path("/usr/local/lib64"),
    ]

    for lib_dir in search_paths:
        for name in ("libbtlock-core.so.0", "libbtlock-core.so"):
            lib_path = lib_dir / name
            if lib_path.exists():
                return str(lib_path)

    found = ctypes.util.find_library("btlock-core")
    if found:
        return found

    return None


class BtlockState(IntEnum):
    """C BtlockState enum values."""
    UNINITIALIZED = 0
    INIT_SEQUENCE = 1
    ADVERTISING = 2
    DISCOVERING = 3
    DEVICE_PRESENT = 4
    DEPARTURE_GRACE = 5
    LOCK_REQUESTED = 6
    LOCKING = 7
    LOCKED = 8
    LOCK_FAILED = 9
    SHUTTING_DOWN = 255


class BtlockEvent(IntEnum):
    """C BtlockEvent enum values."""
    START = 0
    STOP = 1
    DEVICE_FOUND = 2
    DEVICE_LOST = 3
    TICK = 4
    LOCK_ACK = 5
    LOCK_FAILED = 6
    LOCK_CONFIRMED = 7


class BtlockAction(IntEnum):
    """C BtlockAction enum values."""
    NONE = 0
    REQUEST_LOCK = 1
    REQUEST_UNLOCK = 2
    ABORT_LOCK = 3


@dataclass
class BtlockEventResult:
    """Result from processing a state machine event."""
    action: BtlockAction
    state_changed: bool
    new_state: BtlockState


class CEventResult(ctypes.Structure):
    """C struct matching BtlockEventResult."""
    _fields_ = [
        ("action", ctypes.c_int),
        ("state_changed", ctypes.c_bool),
        ("new_state", ctypes.c_int),
    ]


class CConfig(ctypes.Structure):
    """C struct matching BtlockConfig."""
    _fields_ = [
        ("schema_version", ctypes.c_uint32),
        ("lock_grace_sec", ctypes.c_uint32),
        ("absent_threshold_dbm", ctypes.c_int8),
        ("present_threshold_dbm", ctypes.c_int8),
        ("rssi_window", ctypes.c_uint8),
        ("min_disconnects", ctypes.c_uint8),
        ("return_threshold_dbm", ctypes.c_int8),
        ("tick_interval_sec", ctypes.c_uint32),
        ("required_devices", ctypes.c_uint8),
        ("auto_unlock_enabled", ctypes.c_bool),
        ("lock_confirm_required", ctypes.c_bool),
        ("recovery_retry_sec", ctypes.c_uint32),
        ("max_retries", ctypes.c_uint8),
        ("enrolled_devices", ctypes.c_char * 256),
        ("device_count", ctypes.c_uint8),
        ("_reserved", ctypes.c_char * 128),
    ]


class CDevice(ctypes.Structure):
    """C struct for a Bluetooth device."""
    _fields_ = [
        ("address", ctypes.c_char * 18),
        ("name", ctypes.c_char * 256),
        ("rssi", ctypes.c_int8),
        ("present", ctypes.c_bool),
    ]



class StateBridge:
    """
    Python bridge to the C state machine.

    Usage:
        bridge = StateBridge(config)
        result = bridge.process_event(BtlockEvent.DEVICE_FOUND, ...)
        bridge.free()
    """

    def __init__(self, config: BtlockConfig):
        self._config = config
        self._lib: Optional[ctypes.CDLL] = None
        self._sm = None

        lib_path = _find_library()
        if lib_path is None:
            raise RuntimeError(
                "libbtlock-core.so not found. Set BTLOCK_LIB_PATH or "
                "install the btlock-core package."
            )

        self._lib = ctypes.CDLL(lib_path)
        self._setup_functions()

        c_config = self._to_c_config(config)
        self._sm = self._lib.btlock_state_machine_new(ctypes.byref(c_config))

        if not self._sm:
            raise RuntimeError("Failed to create state machine instance")

    def _setup_functions(self) -> None:
        """Set up ctypes function signatures."""
        L = self._lib

        L.btlock_state_machine_new.argtypes = [ctypes.POINTER(CConfig)]
        L.btlock_state_machine_new.restype = ctypes.c_void_p

        L.btlock_state_machine_free.argtypes = [ctypes.c_void_p]
        L.btlock_state_machine_free.restype = None

        L.btlock_state_machine_process_event.argtypes = [
            ctypes.c_void_p,    # sm
            ctypes.c_int,       # event
            ctypes.c_void_p,    # device (can be NULL)
            ctypes.c_uint64,   # monotonic_now_ms
        ]
        L.btlock_state_machine_process_event.restype = CEventResult

        L.btlock_state_machine_get_state.argtypes = [ctypes.c_void_p]
        L.btlock_state_machine_get_state.restype = ctypes.c_int

    def _to_c_config(self, config: BtlockConfig) -> CConfig:
        """Convert Python config to C struct."""
        c = CConfig()
        c.schema_version = config.schema_version
        c.lock_grace_sec = config.lock_grace_sec
        c.absent_threshold_dbm = config.absent_threshold_dbm
        c.present_threshold_dbm = config.present_threshold_dbm
        c.rssi_window = config.rssi_window
        c.min_disconnects = config.min_disconnects
        c.return_threshold_dbm = config.return_threshold_dbm
        c.tick_interval_sec = config.tick_interval_sec
        c.required_devices = config.required_devices
        c.auto_unlock_enabled = config.auto_unlock_enabled
        c.lock_confirm_required = config.lock_confirm_required
        c.recovery_retry_sec = config.recovery_retry_sec
        c.max_retries = config.max_retries

        addr_str = ",".join(config.enrolled_devices)
        c.enrolled_devices = addr_str.encode("ascii")[:255]
        c.device_count = len(config.enrolled_devices)

        return c

    def _to_c_device(
        self, address: str, name: str, rssi: int
    ) -> CDevice:
        """Convert device info to C struct."""
        d = CDevice()
        d.address = address.encode("ascii")[:17]
        d.name = name.encode("utf-8")[:255]
        d.rssi = rssi
        d.present = True
        return d

    def process_event(
        self,
        event: BtlockEvent,
        address: Optional[str] = None,
        name: str = "",
        rssi: int = 0,
        monotonic_now_ms: int = 0,
    ) -> BtlockEventResult:
        """Process a state machine event."""
        if not self._sm:
            raise RuntimeError("State machine not initialized")

        device_ptr: ctypes.c_void_p = None
        device_data: Optional[CDevice] = None

        if address is not None:
            device_data = self._to_c_device(address, name, rssi)
            device_ptr = ctypes.byref(device_data)

        raw_result = self._lib.btlock_state_machine_process_event(
            self._sm,
            ctypes.c_int(event.value),
            device_ptr,
            ctypes.c_uint64(monotonic_now_ms),
        )

        return BtlockEventResult(
            action=BtlockAction(raw_result.action),
            state_changed=raw_result.state_changed,
            new_state=BtlockState(raw_result.new_state),
        )

    def get_state(self) -> BtlockState:
        """Get the current state of the state machine."""
        if not self._sm:
            return BtlockState.UNINITIALIZED

        state_val = self._lib.btlock_state_machine_get_state(self._sm)
        try:
            return BtlockState(state_val)
        except ValueError:
            return BtlockState.UNINITIALIZED

    @staticmethod
    def get_state_name(state: BtlockState) -> str:
        """Get a human-readable name for a state."""
        names = {
            BtlockState.UNINITIALIZED: "UNINITIALIZED",
            BtlockState.INIT_SEQUENCE: "INIT_SEQUENCE",
            BtlockState.ADVERTISING: "ADVERTISING",
            BtlockState.DISCOVERING: "DISCOVERING",
            BtlockState.DEVICE_PRESENT: "DEVICE_PRESENT",
            BtlockState.DEPARTURE_GRACE: "DEPARTURE_GRACE",
            BtlockState.LOCK_REQUESTED: "LOCK_REQUESTED",
            BtlockState.LOCKING: "LOCKING",
            BtlockState.LOCKED: "LOCKED",
            BtlockState.LOCK_FAILED: "LOCK_FAILED",
            BtlockState.SHUTTING_DOWN: "SHUTTING_DOWN",
        }
        return names.get(state, f"UNKNOWN({int(state)})")

    def free(self) -> None:
        """Free the C state machine instance."""
        if self._sm and self._lib:
            self._lib.btlock_state_machine_free(self._sm)
            self._sm = None

    def __del__(self):
        try:
            if hasattr(self, "_sm") and self._sm:
                self._lib.btlock_state_machine_free(self._sm)
        except Exception:
            pass


class NullStateBridge:
    """Fallback state bridge for when C library is unavailable."""

    def __init__(self, config: BtlockConfig):
        self._config = config
        self._state = BtlockState.UNINITIALIZED
        self._last_device_lost_ms = 0
        self._grace_end_ms = 0
        self._is_present = False

    def process_event(
        self,
        event: BtlockEvent,
        address: Optional[str] = None,
        name: str = "",
        rssi: int = 0,
        monotonic_now_ms: int = 0,
    ) -> BtlockEventResult:
        old_state = self._state
        action = BtlockAction.NONE

        if event == BtlockEvent.START:
            self._state = BtlockState.DEVICE_PRESENT

        elif event == BtlockEvent.DEVICE_FOUND:
            self._is_present = True
            if self._state in (
                BtlockState.UNINITIALIZED,
                BtlockState.DEPARTURE_GRACE,
                BtlockState.LOCK_REQUESTED,
                BtlockState.LOCK_FAILED,
            ):
                self._state = BtlockState.DEVICE_PRESENT

        elif event == BtlockEvent.DEVICE_LOST:
            self._is_present = False
            self._last_device_lost_ms = monotonic_now_ms
            self._grace_end_ms = monotonic_now_ms + (
                self._config.lock_grace_sec * 1000
            )
            if self._state == BtlockState.DEVICE_PRESENT:
                self._state = BtlockState.DEPARTURE_GRACE

        elif event == BtlockEvent.TICK:
            if (
                self._state == BtlockState.DEPARTURE_GRACE
                and monotonic_now_ms >= self._grace_end_ms
            ):
                self._state = BtlockState.LOCK_REQUESTED
                action = BtlockAction.REQUEST_LOCK
            elif (
                self._state == BtlockState.LOCKING
                and monotonic_now_ms >= self._grace_end_ms
            ):
                self._state = BtlockState.LOCK_FAILED

        elif event == BtlockEvent.LOCK_ACK:
            if self._state == BtlockState.LOCK_REQUESTED:
                self._state = BtlockState.LOCKING

        elif event == BtlockEvent.LOCK_CONFIRMED:
            if self._state in (BtlockState.LOCK_REQUESTED, BtlockState.LOCKING):
                self._state = BtlockState.LOCKED

        elif event == BtlockEvent.LOCK_FAILED:
            if self._state in (
                BtlockState.LOCK_REQUESTED,
                BtlockState.LOCKING,
            ):
                self._state = BtlockState.LOCK_FAILED

        elif event == BtlockEvent.STOP:
            self._state = BtlockState.SHUTTING_DOWN

        state_changed = self._state != old_state

        return BtlockEventResult(
            action=action,
            state_changed=state_changed,
            new_state=self._state,
        )

    def get_state(self) -> BtlockState:
        return self._state

    @staticmethod
    def get_state_name(state: BtlockState) -> str:
        return StateBridge.get_state_name(state)

    def free(self) -> None:
        pass

    def __del__(self):
        pass
