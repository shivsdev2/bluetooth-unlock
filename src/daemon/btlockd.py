#!/usr/bin/env python3
"""
btlockd - Bluetooth proximity screen locking daemon.

Copyright (C) 2025 btlock contributors
SPDX-License-Identifier: GPL-3.0-or-later

Main daemon entry point. Monitors Bluetooth devices via BlueZ D-Bus
and locks the desktop session when trusted devices move out of range.
"""

from __future__ import annotations

import logging
import signal
import sys
import time

from pathlib import Path

_SRC_DIR = Path(__file__).parent
if str(_SRC_DIR) not in sys.path:
    sys.path.insert(0, str(_SRC_DIR))

from btlock.config import load_config, BtlockConfig
from btlock.bluez_monitor import BluezMonitor
from btlock.desktop_adapter import DesktopAdapter
from btlock.state_bridge import (
    StateBridge, NullStateBridge,
    BtlockEvent, BtlockState, BtlockAction,
)
from btlock.dbus_service import BtlockDBusService

logger = logging.getLogger("btlockd")


class BtlockDaemon:
    """
    Orchestrates Bluetooth monitoring, state machine, and desktop
    locking. Integrates BluezMonitor, DesktopAdapter, StateBridge
    (C or fallback), and BtlockDBusService.
    """

    def __init__(self) -> None:
        self._config = None
        self._monitor: BluezMonitor | None = None
        self._desktop: DesktopAdapter | None = None
        self._state_bridge = None
        self._dbus_service: BtlockDBusService | None = None
        self._running = False
        self._last_tick = 0

    def initialize(self) -> bool:
        """Initialize all components. Returns True on success."""
        try:
            self._config = load_config()
        except Exception as e:
            logger.error("Failed to load config: %s", e)
            self._config = BtlockConfig()

        if not self._config.enabled:
            logger.info("btlock is disabled in config, exiting")
            return False

        self._init_state_bridge()
        self._desktop = DesktopAdapter()

        self._monitor = BluezMonitor(
            config=self._config,
            on_device_found=self._on_device_found,
            on_device_lost=self._on_device_lost,
            on_rssi_update=self._on_rssi_update,
        )

        self._dbus_service = BtlockDBusService(
            config=self._config,
            monitor=self._monitor,
            desktop=self._desktop,
            on_lock=self._on_lock_request,
            on_unlock=self._on_unlock_request,
        )
        return True

    def _init_state_bridge(self) -> None:
        """Initialize state machine (C library or Python fallback)."""
        try:
            self._state_bridge = StateBridge(self._config)
            logger.info("Using C core library for state machine")
        except Exception as e:
            logger.warning(
                "C library unavailable (%s); using fallback", e
            )
            self._state_bridge = NullStateBridge(self._config)

    def _on_device_found(self, address: str, device) -> None:
        """Bluetooth device appeared - forward to state machine."""
        now_ms = int(time.monotonic() * 1000)
        result = self._state_bridge.process_event(
            BtlockEvent.DEVICE_FOUND,
            address=address,
            name=device.name,
            rssi=device.rssi,
            monotonic_now_ms=now_ms,
        )
        self._handle_state_result(result)

        if self._dbus_service:
            self._dbus_service.emit_device_found(device)

    def _on_device_lost(self, address: str) -> None:
        """Bluetooth device disappeared - forward to state machine."""
        now_ms = int(time.monotonic() * 1000)
        result = self._state_bridge.process_event(
            BtlockEvent.DEVICE_LOST,
            address=address,
            monotonic_now_ms=now_ms,
        )
        self._handle_state_result(result)

        if self._dbus_service:
            self._dbus_service.emit_device_lost(address)

    def _on_rssi_update(self, address: str, rssi: int) -> None:
        """RSSI changed - update state machine if needed."""
        pass

    def _on_lock_request(self) -> bool:
        """Lock requested by proximity or remote call."""
        logger.info("Lock requested")
        success = self._desktop.lock()
        if self._dbus_service:
            self._dbus_service.emit_lock_changed(True)
        return success

    def _on_unlock_request(self) -> bool:
        """Unlock requested."""
        logger.info("Unlock requested")
        success = self._desktop.unlock()
        if self._dbus_service:
            self._dbus_service.emit_lock_changed(False)
        return success

    def _handle_state_result(self, result) -> None:
        """Handle a state machine result, performing requested actions."""
        if result.action == BtlockAction.REQUEST_LOCK:
            self._on_lock_request()
        elif result.action == BtlockAction.REQUEST_UNLOCK:
            self._on_unlock_request()
        elif result.action == BtlockAction.ABORT_LOCK:
            logger.info("Lock aborted - device returned")

        if result.state_changed:
            state_name = BtlockState(result.new_state).name
            logger.info("State transition: %s", state_name)
            if self._dbus_service:
                self._dbus_service.emit_status_changed(state_name)

    def start(self) -> bool:
        """Start the daemon components."""
        if not self.initialize():
            return False

        if not self._monitor.start():
            logger.error("Failed to start BlueZ monitor")
            return False

        if not self._dbus_service.register():
            logger.warning("D-Bus service registration failed")

        # Send initial START event to state machine
        now_ms = int(time.monotonic() * 1000)
        result = self._state_bridge.process_event(
            BtlockEvent.START, monotonic_now_ms=now_ms
        )
        self._handle_state_result(result)

        self._running = True
        logger.info("btlockd started")
        return True

    def run(self) -> int:
        """Run the main loop until stopped."""
        if not self.start():
            return 1

        signal.signal(signal.SIGTERM, self._handle_signal)
        signal.signal(signal.SIGINT, self._handle_signal)

        tick_interval = self._config.tick_interval_sec

        try:
            while self._running:
                now_ms = int(time.monotonic() * 1000)

                # Send periodic TICK events to state machine
                elapsed = (now_ms - self._last_tick) / 1000.0
                if elapsed >= tick_interval:
                    self._last_tick = now_ms
                    result = self._state_bridge.process_event(
                        BtlockEvent.TICK, monotonic_now_ms=now_ms
                    )
                    self._handle_state_result(result)

                # Trigger a scan to refresh RSSI readings
                self._monitor.scan()

                time.sleep(0.1)

        except Exception as e:
            logger.exception("Fatal error in main loop: %s", e)
        finally:
            self.stop()

        return 0

    def _handle_signal(self, signum, frame) -> None:
        """Handle shutdown signals."""
        logger.info("Received signal %d, shutting down", signum)
        self._running = False

    def stop(self) -> None:
        """Stop the daemon and clean up resources."""
        self._running = False

        if self._state_bridge:
            self._state_bridge.process_event(BtlockEvent.STOP)

        if self._monitor:
            self._monitor.stop()

        if self._dbus_service:
            self._dbus_service.shutdown()

        logger.info("btlockd stopped")


def setup_logging(verbose: bool = False) -> None:
    """Configure logging."""
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )


def main() -> int:
    """Entry point for the btlock daemon."""
    verbose = "-v" in sys.argv or "--verbose" in sys.argv
    setup_logging(verbose)

    logger.info("Starting btlockd v%s", "0.1.0")
    daemon = BtlockDaemon()
    return daemon.run()


if __name__ == "__main__":
    sys.exit(main())
