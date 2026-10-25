# btlock - Bluetooth Proximity Screen Locker

**Bluetooth proximity workstation locking for Linux.**

When a paired/enrolled Bluetooth device (e.g. your phone) moves out of
range, btlock automatically locks your desktop session. When it returns,
the state machine can optionally unlock (if configured).

## Quick Start

### Prerequisites

- Fedora Linux (29+) or any Linux with BlueZ
- Python 3.9+
- Install dependencies:

```bash
sudo dnf install python3-gobject python3-dbus-bluez bluez-tools
```

### Build and Install

```bash
meson setup builddir
ninja -C builddir
sudo ninja -C builddir install
```

### Configure

```bash
python3 src/daemon/btlockd.py --setup
# Edit the config
nano ~/.config/btlock/config.json
```

Add your device MAC addresses:

```json
{
  "enrolled_devices": ["AA:BB:CC:DD:EE:FF"],
  "enabled": true
}
```

Find your device MAC:
```bash
bluetoothctl devices
```

### Run the Daemon

```bash
# Direct run (debug mode)
python3 src/daemon/btlockd.py -v

# Or as a systemd user service
systemctl --user enable --now btlockd
```

### Verify It's Working

```bash
# Check daemon is running
pgrep -f btlockd

# View live logs (debug mode)
python3 src/daemon/btlockd.py -v
```

## Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | bool | `true` | Master enable/disable |
| `enrolled_devices` | list | `[]` | MAC addresses of trusted devices |
| `absent_threshold_dbm` | int | `-90` | RSSI threshold to consider device "absent" |
| `present_threshold_dbm` | int | `-80` | RSSI threshold to consider device "present" |
| `return_threshold_dbm` | int | `-75` | RSSI to consider device "returned" |
| `lock_grace_sec` | int | `15` | Seconds to wait before locking after device leaves |
| `min_disconnects` | int | `3` | Number of missed scans before lock |
| `tick_interval_sec` | int | `2` | How often to check RSSI (seconds) |
| `rssi_window` | int | `5` | Number of RSSI samples for filtering |
| `filter_type` | str | `"median"` | `"median"` or `"moving_average"` |
| `required_devices` | int | `0` | Minimum devices that must be present to stay unlocked |
| `auto_unlock_enabled` | bool | `false` | Auto-unlock when device returns |
| `lock_confirm_required` | bool | `true` | Require user confirmation on lock |
| `recovery_retry_sec` | int | `5` | Retry interval after lock failure |
| `max_retries` | int | `3` | Maximum lock retry attempts |
| `start_at_login` | bool | `true` | Start daemon on login |

## How It Works

```
┌─────────────┐    D-Bus Signals    ┌─────────────┐
│   BlueZ     │────────────────────>│  BluezMonitor│
│   (Linux)   │                     │  (Python)   │
└─────────────┘                     └──────┬──────┘
                                           │ RSSI
                                           ▼
┌─────────────┐                     ┌──────────────┐
│  Desktop    │<────────────────────│ StateBridge  │
│  (Lock)     │  Lock/Unlock        │  (C/Fallback) │
└─────────────┘                     └──────┬───────┘
                                           │
┌─────────────┐                     ┌──────┴───────┐
│  UI Client  │<────────────────────│ DbusService  │
│  (D-Bus)    │  Status/Events      │              │
└─────────────┘                     └──────────────┘
```

The daemon polls RSSI at `tick_interval_sec` and applies a filter
(`median` or `moving_average` over `rssi_window` samples). When the
signal drops below `absent_threshold_dbm` for `lock_grace_sec` seconds,
the state machine transitions to `LOCK_REQUESTED` and the desktop is locked.

### State Machine

```
START → DEVICE_PRESENT → DEPARTURE_GRACE ─(timeout)→ LOCK_REQUESTED
                                          ↓
                                   (device returns)
                                          ↓
                                   ←─── DEVICE_PRESENT
```

## D-Bus API

The daemon exposes `org.btlock.Service` at `/org/btlock`:

### Methods
- `Lock()` → `b` (success)
- `Unlock()` → `b` (success)
- `GetStatus()` → `a{sv}` (status dict)
- `GetDevices()` → `aa{sv}` (device list)
- `SetEnrolled(as addresses)` — update enrolled devices
- `ReloadConfig()` — reload from disk

### Signals
- `DeviceFound(s address, s name, n rssi)`
- `DeviceLost(s address)`
- `LockStateChanged(b locked)`
- `StatusChanged(s status)`

## Development

### Running Tests

```bash
# C core tests
cd builddir && meson test

# Python tests (if pytest is available)
python3 -m pytest tests/python/
```

### Debug Mode

```bash
python3 src/daemon/btlockd.py -v
```

### Simulation Mode

When `pygobject` is unavailable (e.g., in a headless environment), the
BlueZ monitor falls back to simulation mode. You can inject fake devices
for testing:

```python
from btlock.bluez_monitor import BluezMonitor
monitor = BluezMonitor(config, callback, callback, callback)
monitor.start()
monitor.inject_device("AA:BB:CC:DD:EE:FF", "Test Phone", -65)
```

## License

GPL-3.0-or-later - See COPYING file.