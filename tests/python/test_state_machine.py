"""Tests for the NullStateBridge state machine."""

import time
import unittest

from btlock.config import BtlockConfig
from btlock.state_bridge import (
    NullStateBridge, BtlockEvent, BtlockState,
    BtlockAction, BtlockEventResult,
)


class TestNullStateBridge(unittest.TestCase):
    """Test the Python fallback state machine."""

    def setUp(self):
        """Create a fresh state bridge for each test."""
        self.config = BtlockConfig()
        self.config.lock_grace_sec = 5
        self.bridge = NullStateBridge(self.config)

    def _process(self, event, **kwargs):
        """Helper to process event with monotonic time."""
        kwargs.setdefault("monotonic_now_ms", 0)
        return self.bridge.process_event(event, **kwargs)

    def test_start(self):
        """START event should transition to DEVICE_PRESENT."""
        result = self._process(BtlockEvent.START)
        self.assertEqual(result.new_state, BtlockState.DEVICE_PRESENT)
        self.assertTrue(result.state_changed)

    def test_device_found(self):
        """DEVICE_FOUND should transition to DEVICE_PRESENT."""
        self._process(BtlockEvent.START)
        result = self._process(BtlockEvent.DEVICE_FOUND)
        self.assertEqual(result.new_state, BtlockState.DEVICE_PRESENT)
        self.assertEqual(result.action, BtlockAction.NONE)

    def test_device_lost(self):
        """DEVICE_LOST should enter DEPARTURE_GRACE."""
        self._process(BtlockEvent.START)
        result = self._process(BtlockEvent.DEVICE_LOST,
                               monotonic_now_ms=1000)
        self.assertEqual(result.new_state, BtlockState.DEPARTURE_GRACE)

    def test_grace_period_not_expired(self):
        """Stays in DEPARTURE_GRACE during grace period."""
        self._process(BtlockEvent.START)
        self._process(BtlockEvent.DEVICE_LOST, monotonic_now_ms=0)
        result = self._process(BtlockEvent.TICK,
                               monotonic_now_ms=2000)  # 2s < 5s grace
        self.assertEqual(result.new_state, BtlockState.DEPARTURE_GRACE)
        self.assertEqual(result.action, BtlockAction.NONE)

    def test_grace_period_expired(self):
        """Transitions to LOCK_REQUESTED after grace period."""
        self._process(BtlockEvent.START)
        self._process(BtlockEvent.DEVICE_LOST, monotonic_now_ms=0)
        result = self._process(BtlockEvent.TICK,
                               monotonic_now_ms=6000)  # 6s > 5s grace
        self.assertEqual(result.new_state, BtlockState.LOCK_REQUESTED)
        self.assertEqual(result.action, BtlockAction.REQUEST_LOCK)

    def test_device_returns_during_grace(self):
        """Device found during grace should go back to DEVICE_PRESENT."""
        self._process(BtlockEvent.START)
        self._process(BtlockEvent.DEVICE_LOST, monotonic_now_ms=0)
        self._process(BtlockEvent.TICK,
                      monotonic_now_ms=2000)
        result = self._process(BtlockEvent.DEVICE_FOUND,
                               monotonic_now_ms=3000,
                               address="AA:BB:CC:DD:EE:FF",
                               name="Phone",
                               rssi=-70)
        self.assertEqual(result.new_state, BtlockState.DEVICE_PRESENT)

    def test_lock_confirmed(self):
        """LOCK_CONFIRMED transitions from LOCK_REQUESTED to LOCKED."""
        self._process(BtlockEvent.START)
        self._process(BtlockEvent.DEVICE_LOST, monotonic_now_ms=0)
        self._process(BtlockEvent.TICK,
                      monotonic_now_ms=6000)  # Enter LOCK_REQUESTED
        result = self._process(BtlockEvent.LOCK_CONFIRMED)
        self.assertEqual(result.new_state, BtlockState.LOCKED)

    def test_stop_event(self):
        """STOP event should not crash."""
        self._process(BtlockEvent.START)
        result = self._process(BtlockEvent.STOP)
        # State may change depending on implementation
        self.assertIsNotNone(result)

    def test_lock_ack(self):
        """LOCK_ACK should transition from LOCK_REQUESTED to LOCKING."""
        self._process(BtlockEvent.START)
        self._process(BtlockEvent.DEVICE_LOST, monotonic_now_ms=0)
        self._process(BtlockEvent.TICK,
                      monotonic_now_ms=6000)  # Enter LOCK_REQUESTED
        result = self._process(BtlockEvent.LOCK_ACK)
        self.assertEqual(result.new_state, BtlockState.LOCKING)

    def test_event_result_fields(self):
        """BtlockEventResult should have all required fields."""
        result = self._process(BtlockEvent.START)
        self.assertIn("action", result.__dict__)
        self.assertIn("state_changed", result.__dict__)
        self.assertIn("new_state", result.__dict__)


class TestBtlockStateEnum(unittest.TestCase):
    """Test BtlockState enum values."""

    def test_state_values(self):
        """BtlockState should have correct integer values."""
        self.assertEqual(int(BtlockState.UNINITIALIZED), 0)
        self.assertEqual(int(BtlockState.INIT_SEQUENCE), 1)
        self.assertEqual(int(BtlockState.DEVICE_PRESENT), 4)
        self.assertEqual(int(BtlockState.DEPARTURE_GRACE), 5)
        self.assertEqual(int(BtlockState.LOCK_REQUESTED), 6)
        self.assertEqual(int(BtlockState.LOCKING), 7)
        self.assertEqual(int(BtlockState.LOCKED), 8)
        self.assertEqual(int(BtlockState.LOCK_FAILED), 9)


class TestBtlockActionEnum(unittest.TestCase):
    """Test BtlockAction enum values."""

    def test_action_values(self):
        """BtlockAction should have correct integer values."""
        self.assertEqual(int(BtlockAction.NONE), 0)
        self.assertEqual(int(BtlockAction.REQUEST_LOCK), 1)
        self.assertEqual(int(BtlockAction.REQUEST_UNLOCK), 2)
        self.assertEqual(int(BtlockAction.ABORT_LOCK), 3)


if __name__ == "__main__":
    unittest.main()