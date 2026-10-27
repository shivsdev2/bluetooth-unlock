"""Tests for DesktopAdapter lock backend detection."""

import unittest
from unittest.mock import patch, MagicMock

from btlock.desktop_adapter import DesktopAdapter, LockBackend


class TestDesktopAdapter(unittest.TestCase):
    """Test DesktopAdapter backend detection and lock/unlock."""

    def test_backend_detected(self):
        """Backend should be detected (not UNKNOWN)."""
        adapter = DesktopAdapter()
        self.assertNotEqual(adapter.backend, LockBackend.UNKNOWN)

    def test_get_backend_name(self):
        """get_backend_name should return a string."""
        adapter = DesktopAdapter()
        name = adapter.get_backend_name()
        self.assertIsInstance(name, str)
        self.assertIn(name, ["screensaver", "login1", "loginctl", "unknown"])

    def test_unlock_always_returns_true(self):
        """Unlock should always return True (user enters password)."""
        adapter = DesktopAdapter()
        self.assertTrue(adapter.unlock())

    @patch("subprocess.run")
    def test_lock_loginctl(self, mock_run):
        """lock() should call loginctl when using LOGINDCTL backend."""
        mock_run.return_value = MagicMock(returncode=0, stderr="")
        with patch.object(DesktopAdapter, "_detect_backend") as mock_detect:
            mock_detect.return_value = None
            # Force loginctl backend
            adapter = DesktopAdapter.__new__(DesktopAdapter)
            adapter._backend = LockBackend.LOGINDCTL
            adapter._proxy = None
            adapter._session_id = None

            # Re-init for DesktopAdapter
            adapter.__init__()
            adapter._backend = LockBackend.LOGINDCTL
            mock_detect.return_value = None

            result = adapter.lock()
            self.assertTrue(result)
            mock_run.assert_called()

    def test_is_locked_returns_bool(self):
        """is_locked should return a bool."""
        adapter = DesktopAdapter()
        result = adapter.is_locked()
        self.assertIsInstance(result, bool)


if __name__ == "__main__":
    unittest.main()