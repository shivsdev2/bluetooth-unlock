"""Tests for BluezMonitor RSSI filtering and device management."""

import time
import unittest

from btlock.config import BtlockConfig
from btlock.bluez_monitor import BluezMonitor, DeviceInfo


class TestDeviceInfo(unittest.TestCase):
    """Test DeviceInfo dataclass."""

    def test_creation(self):
        """DeviceInfo should store all fields correctly."""
        dev = DeviceInfo(
            address="AA:BB:CC:DD:EE:FF",
            name="Test",
            rssi=-65,
            last_seen_ms=1000,
            is_present=True,
            rssi_samples=[-65, -67, -63],
        )
        self.assertEqual(dev.address, "AA:BB:CC:DD:EE:FF")
        self.assertEqual(dev.name, "Test")
        self.assertEqual(dev.rssi, -65)
        self.assertTrue(dev.is_present)
        self.assertEqual(len(dev.rssi_samples), 3)


class TestBluezMonitor(unittest.TestCase):
    """Test BluezMonitor filtering and callback behavior."""

    def setUp(self):
        """Create a monitor in simulation mode."""
        self.found_calls = []
        self.lost_calls = []
        self.rssi_calls = []

        config = BtlockConfig()
        config.enrolled_devices = ["AA:BB:CC:DD:EE:FF", "11:22:33:44:55:66"]
        config.rssi_window = 5
        config.filter_type = "median"

        self.monitor = BluezMonitor(
            config=config,
            on_device_found=self._on_found,
            on_device_lost=self._on_lost,
            on_rssi_update=self._on_rssi,
        )
        self.monitor.start()

    def tearDown(self):
        """Stop the monitor."""
        self.monitor.stop()

    def _on_found(self, address, dev):
        self.found_calls.append((address, dev.name, dev.rssi))

    def _on_lost(self, address):
        self.lost_calls.append(address)

    def _on_rssi(self, address, rssi):
        self.rssi_calls.append((address, rssi))

    def test_device_found_callback(self):
        """inject_device should trigger on_device_found for new devices."""
        self.monitor.inject_device("AA:BB:CC:DD:EE:FF", "Phone", -65)
        self.assertEqual(len(self.found_calls), 1)
        self.assertEqual(self.found_calls[0][0], "AA:BB:CC:DD:EE:FF")

    def test_device_lost_callback(self):
        """mark_absent should trigger on_device_lost."""
        self.monitor.inject_device("AA:BB:CC:DD:EE:FF", "Phone", -65)
        self.monitor.mark_absent("AA:BB:CC:DD:EE:FF")
        self.assertEqual(len(self.lost_calls), 1)

    def test_rssi_update_callback(self):
        """Updating RSSI for existing device should trigger on_rssi_update."""
        self.monitor.inject_device("AA:BB:CC:DD:EE:FF", "Phone", -70)
        self.monitor.inject_device("AA:BB:CC:DD:EE:FF", "Phone", -65)
        self.assertEqual(len(self.rssi_calls), 1)

    def test_rssi_filtering_median(self):
        """Median filter should produce correct result."""
        dev = DeviceInfo(
            address="AA:BB:CC:DD:EE:FF",
            name="Test", rssi=-65, last_seen_ms=0,
            is_present=True, rssi_samples=[],
        )
        samples = [-70, -65, -60, -75, -68]
        for s in samples:
            result = self.monitor._apply_filter(dev, s)
        self.assertEqual(result, -68)

    def test_rssi_filtering_moving_average(self):
        """Moving average filter should produce correct result."""
        self.monitor._filter_type = "moving_average"
        dev = DeviceInfo(
            address="AA:BB:CC:DD:EE:FF",
            name="Test", rssi=-65, last_seen_ms=0,
            is_present=True, rssi_samples=[],
        )
        samples = [-70, -65, -60, -75, -68]
        result = None
        for s in samples:
            result = self.monitor._apply_filter(dev, s)
        expected = sum(samples) // len(samples)
        self.assertEqual(result, expected)

    def test_filter_window_size(self):
        """RSSI samples should be limited to rssi_window."""
        dev = DeviceInfo(
            address="AA:BB:CC:DD:EE:FF",
            name="Test", rssi=-65, last_seen_ms=0,
            is_present=True, rssi_samples=[],
        )
        self.monitor._config.rssi_window = 3
        for rssi in [-70, -65, -60, -75, -68, -55]:
            self.monitor._apply_filter(dev, rssi)
        self.assertEqual(len(dev.rssi_samples), 3)
        self.assertEqual(dev.rssi_samples, [-75, -68, -55])

    def test_get_tracked_devices(self):
        """get_tracked_devices should return all tracked devices."""
        self.monitor.inject_device("AA:BB:CC:DD:EE:FF", "Phone", -65)
        self.monitor.inject_device("11:22:33:44:55:66", "Tablet", -80)
        devices = self.monitor.get_tracked_devices()
        self.assertEqual(len(devices), 2)

    def test_is_device_present(self):
        """is_device_present should detect tracked devices."""
        self.monitor.inject_device("AA:BB:CC:DD:EE:FF", "Phone", -65)
        self.assertTrue(self.monitor.is_device_present("AA:BB:CC:DD:EE:FF"))
        self.assertFalse(self.monitor.is_device_present("00:00:00:00:00:00"))

    def test_non_enrolled_device_ignored(self):
        """Non-enrolled devices should not trigger callbacks."""
        self.monitor.inject_device("00:11:22:33:44:55", "Unknown", -65)
        self.assertEqual(len(self.found_calls), 0)

    def test_start_stop(self):
        """Monitor should start and stop cleanly."""
        from btlock.bluez_monitor import BluezMonitor
        monitor = BluezMonitor(
            config=BtlockConfig(),
            on_device_found=lambda a, d: None,
            on_device_lost=lambda a: None,
            on_rssi_update=lambda a, r: None,
        )
        self.assertTrue(monitor.start())
        self.assertTrue(monitor._running)
        monitor.stop()
        self.assertFalse(monitor._running)


if __name__ == "__main__":
    unittest.main()