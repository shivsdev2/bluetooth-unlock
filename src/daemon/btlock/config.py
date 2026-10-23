"""
btlock configuration management.

Copyright (C) 2025 btlock contributors
SPDX-License-Identifier: GPL-3.0-or-later

Manages the user configuration for btlock. Configuration is stored as
JSON in the XDG config directory. This module mirrors the C
BtlockConfig struct and adds:

- File I/O with atomic writes
- Schema versioning and migration
- Validation of all values
- Secure file permissions (0600)
- Error reporting for corrupted/invalid configs

Configuration file: $XDG_CONFIG_HOME/btlock/config.json
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any

# Schema version - must match C BTLOCK_CONFIG_VERSION
SCHEMA_VERSION = 1

# Default values - must match C defaults
DEFAULTS = {
    "lock_grace_sec": 15,
    "absent_threshold_dbm": -90,
    "present_threshold_dbm": -80,
    "rssi_window": 5,
    "min_disconnects": 3,
    "return_threshold_dbm": -75,
    "tick_interval_sec": 2,
    "required_devices": 0,
    "auto_unlock_enabled": False,
    "lock_confirm_required": True,
    "recovery_retry_sec": 5,
    "max_retries": 3,
    "enrolled_devices": [],
    "filter_type": "median",
    "start_at_login": True,
    "enabled": True,
}

# Validation bounds - must match C limits
BOUNDS = {
    "lock_grace_sec": (0, 300),
    "absent_threshold_dbm": (-120, 0),
    "present_threshold_dbm": (-120, 0),
    "rssi_window": (1, 50),
    "min_disconnects": (1, 20),
    "return_threshold_dbm": (-120, 0),
    "tick_interval_sec": (1, 60),
    "required_devices": (0, 20),
    "recovery_retry_sec": (1, 60),
    "max_retries": (1, 20),
}

# Type annotations for validation
TYPES = {
    "lock_grace_sec": int,
    "absent_threshold_dbm": int,
    "present_threshold_dbm": int,
    "rssi_window": int,
    "min_disconnects": int,
    "return_threshold_dbm": int,
    "tick_interval_sec": int,
    "required_devices": int,
    "auto_unlock_enabled": bool,
    "lock_confirm_required": bool,
    "recovery_retry_sec": int,
    "max_retries": int,
    "enrolled_devices": list,
    "filter_type": str,
    "start_at_login": bool,
    "enabled": bool,
}


class ConfigError(Exception):
    """Raised when configuration is invalid or cannot be loaded."""


@dataclass
class BtlockConfig:
    """Configuration dataclass mirroring the C BtlockConfig struct."""

    schema_version: int = 1
    lock_grace_sec: int = 15
    absent_threshold_dbm: int = -90
    present_threshold_dbm: int = -80
    rssi_window: int = 5
    min_disconnects: int = 3
    return_threshold_dbm: int = -75
    tick_interval_sec: int = 2
    required_devices: int = 0
    auto_unlock_enabled: bool = False
    lock_confirm_required: bool = True
    recovery_retry_sec: int = 5
    max_retries: int = 3
    enrolled_devices: list = None
    filter_type: str = "median"
    start_at_login: bool = True
    enabled: bool = True

    def __post_init__(self):
        if self.enrolled_devices is None:
            self.enrolled_devices = []

    def to_dict(self):
        d = asdict(self)
        return d

    @classmethod
    def from_dict(cls, data):
        """Create config from dict, validating types and ranges."""
        config = cls()
        if "schema_version" in data:
            version = data["schema_version"]
            if version != 1:
                raise ConfigError(
                    f"Unsupported schema version {version}, expected 1"
                )
            config.schema_version = version
        for key, expected_type in TYPES.items():
            if key in data:
                val = data[key]
                if not isinstance(val, expected_type):
                    if expected_type == bool and isinstance(val, int):
                        val = bool(val)
                    elif expected_type == int and isinstance(val, bool):
                        pass
                    elif not isinstance(val, expected_type):
                        raise ConfigError(
                            f"Config key '{key}' expected "
                            f"{expected_type.__name__}, got {type(val).__name__}"
                        )
                setattr(config, key, val)
        if config.enrolled_devices is None:
            config.enrolled_devices = []
        return config

    def validate(self):
        """Validate all configuration values. Raises ConfigError on invalid."""
        for key, (lo, hi) in BOUNDS.items():
            val = getattr(self, key, None)
            if val is not None and isinstance(val, int) and not isinstance(val, bool):
                if val < lo or val > hi:
                    raise ConfigError(
                        f"Config value '{key}' must be between {lo} and {hi} "
                        f"(got {val})"
                    )
        if self.absent_threshold_dbm >= self.present_threshold_dbm:
            raise ConfigError(
                f"absent_threshold_dbm ({self.absent_threshold_dbm}) must be "
                f"less than present_threshold_dbm ({self.present_threshold_dbm})"
            )
        if self.return_threshold_dbm < self.present_threshold_dbm:
            raise ConfigError(
                f"return_threshold_dbm ({self.return_threshold_dbm}) must be "
                f">= present_threshold_dbm ({self.present_threshold_dbm})"
            )
        if self.filter_type not in ("median", "moving_average"):
            raise ConfigError(
                f"filter_type must be 'median' or 'moving_average'"
            )


def _is_valid_bt_address(addr: str) -> bool:
    """Validate a Bluetooth MAC address (XX:XX:XX:XX:XX:XX)."""
    if not isinstance(addr, str) or len(addr) != 17:
        return False
    parts = addr.split(":")
    if len(parts) != 6:
        return False
    for part in parts:
        if len(part) != 2:
            return False
        try:
            int(part, 16)
        except ValueError:
            return False
    return True


def get_config_path() -> Path:
    """Get the path to the user configuration file."""
    xdg_config = os.environ.get("XDG_CONFIG_HOME") or \
        os.path.join(os.path.expanduser("~"), ".config")
    return Path(xdg_config) / "btlock" / "config.json"


def get_state_dir() -> Path:
    """Get the runtime state directory."""
    xdg_runtime = os.environ.get("XDG_RUNTIME_DIR") or \
        os.path.join(os.path.expanduser("~"), ".local", "share", "btlock")
    return Path(xdg_runtime) / "btlock"


def load_config() -> BtlockConfig:
    """
    Load configuration from disk.

    Returns defaults if no config file exists.
    Raises ConfigError if the file is corrupted or invalid.
    """
    config_path = get_config_path()

    if not config_path.exists():
        return BtlockConfig()

    try:
        raw = config_path.read_text(encoding="utf-8")
        data = json.loads(raw)
    except json.JSONDecodeError as e:
        raise ConfigError(f"Configuration file is corrupted (invalid JSON): {e}")
    except OSError as e:
        raise ConfigError(f"Cannot read configuration file: {e}")

    if not isinstance(data, dict):
        raise ConfigError("Configuration file must contain a JSON object")

    if data.get("schema_version", 0) != 1:
        data = _migrate_config(data, config_path)

    config = BtlockConfig.from_dict(data)
    config.validate()
    return config


def _migrate_config(data: dict, original_path: Path) -> dict:
    """Migrate configuration from an older schema version."""
    version = data.get("schema_version", 0)

    if version == 0:
        for key, default_val in DEFAULTS.items():
            data.setdefault(key, default_val)
        data["schema_version"] = 1
        return data

    raise ConfigError(
        f"Cannot migrate config from version {version} to 1. "
        f"Original config preserved at {original_path}"
    )


def save_config(config: BtlockConfig, config_path: Path | None = None) -> None:
    """
    Save configuration atomically with secure permissions.

    Uses atomic rename to prevent corruption on crash.
    File permissions are set to 0600 (owner read/write only).
    """
    if config_path is None:
        config_path = get_config_path()

    config_path.parent.mkdir(parents=True, exist_ok=True)
    config.validate()

    contents = json.dumps(config.to_dict(), indent=2, sort_keys=True)

    fd = os.open(
        str(config_path) + ".tmp",
        os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
        0o600,
    )
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(contents)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
    except Exception:
        try:
            os.unlink(str(config_path) + ".tmp")
        except OSError:
            pass
        raise

    os.rename(str(config_path) + ".tmp", str(config_path))
    os.chmod(config_path, 0o600)


def delete_config(config_path: Path | None = None) -> bool:
    """Delete the configuration file. Returns True if deleted."""
    if config_path is None:
        config_path = get_config_path()
    if config_path.exists():
        config_path.unlink()
        return True
    return False


def get_default_config() -> BtlockConfig:
    """Return a configuration with all default values."""
    return BtlockConfig()
