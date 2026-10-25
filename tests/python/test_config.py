"""Tests for BtlockConfig validation."""

import json
import os
import tempfile
import unittest

from btlock.config import (
    BtlockConfig, ConfigError, DEFAULTS, BOUNDS, load_config,
    save_config, get_config_path,
)


class TestBtlockConfig(unittest.TestCase):
    """Test configuration dataclass and validation."""

    def setUp(self):
        """Create a fresh config for each test."""
        self.config = BtlockConfig()

    def test_defaults(self):
        """Config should have correct default values."""
        self.assertEqual(self.config.schema_version, 1)
        self.assertEqual(self.config.lock_grace_sec, 15)
        self.assertEqual(self.config.absent_threshold_dbm, -90)
        self.assertEqual(self.config.present_threshold_dbm, -80)
        self.assertEqual(self.config.rssi_window, 5)
        self.assertEqual(self.config.filter_type, "median")
        self.assertTrue(self.config.enabled)

    def test_validate_passes(self):
        """Valid config should pass validation."""
        self.config.validate()

    def test_validate_enrolled_devices(self):
        """Enrolled devices should be a list of MAC strings."""
        self.config.enrolled_devices = ["AA:BB:CC:DD:EE:FF"]
        self.config.validate()

    def test_validate_bad_filter_type(self):
        """Invalid filter_type should raise ConfigError."""
        self.config.filter_type = "bogus"
        with self.assertRaises(ConfigError):
            self.config.validate()

    def test_validate_out_of_range(self):
        """Values outside bounds should raise ConfigError."""
        self.config.lock_grace_sec = 9999
        with self.assertRaises(ConfigError):
            self.config.validate()

    def test_validate_bad_rssi_window(self):
        """Invalid rssi_window should raise ConfigError."""
        self.config.rssi_window = 0
        with self.assertRaises(ConfigError):
            self.config.validate()

    def test_from_dict(self):
        """Config should load from a dict."""
        data = {
            "enrolled_devices": ["DE:AD:BE:EF:00:01"],
            "lock_grace_sec": 30,
            "filter_type": "moving_average",
        }
        config = BtlockConfig.from_dict(data)
        self.assertEqual(config.lock_grace_sec, 30)
        self.assertEqual(config.enrolled_devices, ["DE:AD:BE:EF:00:01"])
        self.assertEqual(config.filter_type, "moving_average")

    def test_to_dict_roundtrip(self):
        """Config should roundtrip through dict."""
        self.config.lock_grace_sec = 60
        self.config.enrolled_devices = ["AA:BB:CC:DD:EE:FF"]
        d = self.config.to_dict()
        config2 = BtlockConfig.from_dict(d)
        self.assertEqual(config2.lock_grace_sec, 60)
        self.assertEqual(config2.enrolled_devices, ["AA:BB:CC:DD:EE:FF"])


class TestConfigFileIO(unittest.TestCase):
    """Test config file read/write with atomic writes and permissions."""

    def setUp(self):
        """Create a temporary directory for config files."""
        self.tmpdir = tempfile.mkdtemp()
        self.config_path = os.path.join(self.tmpdir, "config.json")

    def tearDown(self):
        """Clean up temp directory."""
        import shutil
        shutil.rmtree(self.tmpdir, ignore_errors=True)

    def test_save_and_load(self):
        """Config should save and load correctly."""
        config = BtlockConfig()
        config.enrolled_devices = ["AA:BB:CC:DD:EE:FF"]
        config.lock_grace_sec = 30
        save_config(config, Path(self.config_path))

        loaded = load_config_from_path(self.config_path)
        self.assertEqual(loaded.lock_grace_sec, 30)
        self.assertEqual(len(loaded.enrolled_devices), 1)

    def test_save_permissions(self):
        """Config file should have 0600 permissions."""
        config = BtlockConfig()
        save_config(config, Path(self.config_path))

        mode = os.stat(self.config_path).st_mode & 0o777
        self.assertEqual(mode, 0o600)

    def test_load_missing_file(self):
        """Loading non-existent config should return defaults."""
        path = os.path.join(self.tmpdir, "nonexistent.json")
        config = load_config_from_path(path)
        self.assertEqual(config.schema_version, 1)

    def test_load_corrupted_file(self):
        """Loading corrupted JSON should raise ConfigError."""
        with open(self.config_path, "w") as f:
            f.write("{invalid json!!")

        with self.assertRaises(ConfigError):
            load_config_from_path(self.config_path)


def load_config_from_path(path):
    """Helper: load config from a specific path (for testing)."""
    from pathlib import Path
    from btlock.config import BtlockConfig

    config_path = Path(path)
    if not config_path.exists():
        return BtlockConfig()

    import json
    raw = config_path.read_text(encoding="utf-8")
    data = json.loads(raw)
    config = BtlockConfig.from_dict(data)
    config.validate()
    return config


if __name__ == "__main__":
    unittest.main()