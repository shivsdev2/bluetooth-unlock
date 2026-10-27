# Configuration Reference

## Config File Location

| Purpose          | Path                                        |
|------------------|---------------------------------------------|
| System default   | `/etc/btlock/config.json`                   |
| User override    | `~/.config/btlock/config.json`              |
| XDG fallback     | `$XDG_CONFIG_HOME/btlock/config.json`       |

The user config overrides the system config. Missing user config falls back
to the system config, then to compiled-in defaults.

## Generating Initial Config

```bash
btlockd --setup
```

Creates `~/.config/btlock/config.json` with safe defaults and `0600` file
permissions.

## JSON Schema

```jsonc
{
  "schema_version": 1,            // Must match BTLOCK_CONFIG_VERSION
  "enabled": true,                // Global enable/disable
  "enrolled_devices": [           // Trusted device MAC addresses
    "AA:BB:CC:DD:EE:FF"
  ],
  "lock_grace_sec": 15,           // Grace before locking after device lost
  "absent_threshold_dbm": -90,    // RSSI below this = device absent
  "present_threshold_dbm": -80,   // RSSI above this = device present
  "rssi_window": 5,               // Samples for RSSI filter
  "filter_type": "median",        // "median" | "moving_average"
  "min_disconnects": 3,           // Consecutive absent readings before state change
  "return_threshold_dbm": -75,    // RSSI above this = device returning
  "tick_interval_sec": 2,         // How often the main loop ticks
  "required_devices": 0,          // Lock policy: 0=any, N=lock when all N absent
  "auto_unlock_enabled": false,   // Allow auto-unlock when device returns
  "lock_confirm_required": true,  // Require session confirmation of lock
  "recovery_retry_sec": 5,        // Backoff between lock retries
  "max_retries": 3                // Max retries before error state
}
```

## Field Reference

### `enabled` (bool, default: `true`)
Master switch. If `false`, the daemon exits immediately on startup.

### `enrolled_devices` (array of string, default: `[]`)

List of trusted Bluetooth MAC addresses. Only devices in this list are
tracked — all other devices seen during discovery are ignored.

Format: `XX:XX:XX:XX:XX:XX` (uppercase or lowercase hex, colon-separated).

Find devices with:
```bash
bluetoothctl devices
```

### `lock_grace_sec` (int, range: 0–300, default: 15)

Seconds to wait after a trusted device is lost before requesting a lock.
Set to 0 for immediate lock on device loss (no grace period).

### `absent_threshold_dbm` (int8, range: -120 to 0, default: -90)

RSSI threshold below which a device is considered "absent." This is applied
to filtered RSSI, not raw readings.

### `present_threshold_dbm` (int8, range: -120 to 0, default: -80)

RSSI threshold above which a device is considered "present."
Must be strictly greater than `absent_threshold_dbm`.

### `rssi_window` (uint8, range: 1–50, default: 5)

Number of RSSI samples retained for filtering. Higher values smooth more
but add latency to state changes.

### `filter_type` (string, default: `"median"`)

RSSI filtering algorithm:

- `"median"`: Robust against outliers. Recommended for most environments.
- `"moving_average"`: Simple arithmetic mean. Smoother but slower.

### `min_disconnects` (uint8, range: 1–20, default: 3)

Number of consecutive "absent" readings required before the device is
considered lost. This adds a debounce layer on top of the grace period.

### `return_threshold_dbm` (int8, range: -120 to 0, default: -75)

RSSI threshold above which a returning device is considered back in range.
Must be greater than or equal to `present_threshold_dbm`.

### `tick_interval_sec` (uint32, range: 1–60, default: 2)

Interval at which the main loop sends TICK events to the state machine.
Lower values increase responsiveness but use more CPU.

### `required_devices` (uint8, range: 1–20 or 0, default: 0)

Multi-device lock policy:

- `0` (default): Lock when the last trusted device departs.
- `N > 0`: Lock only when all N enrolled devices are simultaneously absent.

> **Note**: If set to N but fewer than N devices are enrolled, the daemon
> will never lock. Ensure `len(enrolled_devices) >= required_devices`.

### `auto_unlock_enabled` (bool, default: `false`)

If `true`, the daemon will request an unlock when a trusted device returns.
Even when enabled, the session manager will prompt for authentication
(password, biometrics) — this is by design and cannot be bypassed.

### `lock_confirm_required` (bool, default: `true`)

If `true`, the state machine waits for confirmation from the session
manager after sending a lock request. If confirmation times out, the
machine enters `ERROR_DEGRADED` and retries up to `max_retries` times.

### `recovery_retry_sec` (int, range: 1–60, default: 5)

Backoff period between lock retry attempts when the session manager is
unavailable or the lock request fails.

### `max_retries` (uint8, range: 1–10, default: 3)

Maximum consecutive retry attempts for a lock operation. After exceeding
this count, the state machine enters `ERROR_DEGRADED`.

## Validation Rules

1. `schema_version` must equal `BTLOCK_CONFIG_VERSION` (currently 1).
2. `absent_threshold_dbm` must be strictly less than `present_threshold_dbm`.
3. `return_threshold_dbm` must be greater than or equal to `present_threshold_dbm`.
4. All numeric values must be within their documented ranges.
5. `enrolled_devices` entries must be valid MAC address format.

Violations raise `ConfigError` (Python) / `BTLOCK_CONFIG_ERR_*` (C).

## Environment Variables

| Variable          | Purpose                              |
|-------------------|--------------------------------------|
| `BTLOCK_LIB_PATH` | Override path to `libbtlock-core.so` |
