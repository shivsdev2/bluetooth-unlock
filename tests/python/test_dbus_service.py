"""Tests for BtlockDBusService."""

import unittest

from btlock.config import BtlockConfig
from btlock.bluez_monitor import BluezMonitor
from btlock.desktop_adapter import DesktopAdapter
from btlock.dbus_service import (
    BtlockDBusService,
    DBUS_SERVICE_NAME,
    DBUS_OBJECT_PATH,
    DBUS_INTERFACE,
)


class TestBtlockDBusService(unittest.TestCase):
    """Test D-Bus service constants and interface definition."""

    def test_constants(self):
        """D-Bus constants should have expected values."""
        self.assertEqual(DBUS_SERVICE_NAME, "org.btlock.Service")
        self.assertEqual(DBUS_OBJECT_PATH, "/org/btlock")
        self.assertEqual(DBUS_INTERFACE, "org.btlock.Service")

    def test_status_dict(self):
        """_get_status_dict should return a dict with expected keys."""
        config = BtlockConfig()
        monitor = BluezMonitor(
            config=config,
            on_device_found=lambda a, d: None,
            on_device_lost=lambda a: None,
            on_rssi_update=lambda a, r: None,
        )
        monitor.start()

        service = BtlockDBusService(
            config=config,
            monitor=monitor,
            desktop=DesktopAdapter(),
            on_lock=lambda: True,
            on_unlock=lambda: True,
        )

        status = service._get_status_dict()
        self.assertIn("backend", status)
        self.assertIn("locked", status)
        self.assertIn("adapter_path", status)
        self.assertIn("device_count", status)
        self.assertIn("running", status)

        monitor.stop()

    def test_devices_list(self):
        """_get_devices_list should return list of device dicts."""
        config = BtlockConfig()
        config.enrolled_devices = ["AA:BB:CC:DD:EE:FF"]
        monitor = BluezMonitor(
            config=config,
            on_device_found=lambda a, d: None,
            on_device_lost=lambda a: None,
            on_rssi_update=lambda a, r: None,
        )
        monitor.start()
        monitor.inject_device("AA:BB:CC:DD:EE:FF", "TestPhone", -70)

        service = BtlockDBusService(
            config=config,
            monitor=monitor,
            desktop=DesktopAdapter(),
            on_lock=lambda: True,
            on_unlock=lambda: True,
        )

        devices = service._get_devices_list()
        self.assertEqual(len(devices), 1)
        self.assertEqual(devices[0]["address"], "AA:BB:CC:DD:EE:FF")
        self.assertEqual(devices[0]["name"], "TestPhone")
        self.assertEqual(devices[0]["rssi"], -70)

        monitor.stop()

    def test_introspection_xml(self):
        """Introspection XML should include all interface elements."""
        service = BtlockDBusService(
            config=BtlockConfig(),
            monitor=None,
            desktop=DesktopAdapter(),
            on_lock=lambda: True,
            on_unlock=lambda: True,
        )
        xml = service._build_introspection()
        self.assertIn(DBUS_INTERFACE, xml)
        self.assertIn("method name=\"Lock\"", xml)
        self.assertIn("method name=\"Unlock\"", xml)
        self.assertIn("method name=\"GetStatus\"", xml)
        self.assertIn("signal name=\"DeviceFound\"", xml)
        self.assertIn("signal name=\"DeviceLost\"", xml)

    def test_shutdown_clean(self):
        """shutdown should not raise even if not registered."""
        service = BtlockDBusService(
            config=BtlockConfig(),
            monitor=None,
            desktop=DesktopAdapter(),
            on_lock=lambda: True,
            on_unlock=lambda: True,
        )
        service.shutdown()  # Should not raise


if __name__ == "__main__":
    unittest.main()