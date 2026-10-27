# D-Bus API Reference

btlock exposes two D-Bus interfaces:

1. **Session bus**: `org.btlock` — control and status interface (daemon ↔ UI).
2. **System bus**: `org.freedesktop.login1` — session lock/unlock (daemon ↔ desktop).

## org.btlock / org.btlock.Daemon

**Bus name**: `org.btlock`
**Object path**: `/org/btlock`
**Interface**: `org.btlock.Daemon`

### Methods

#### `Lock` (no arguments)

Request an immediate lock, regardless of device state.

```
→ Lock()
← (no return value on success)
```

**Errors**:
- `org.btlock.Error.LockFailed` — session manager unavailable or lock failed.

---

#### `Unlock` (no arguments)

Request unlock. The session manager will prompt for authentication.

```
→ Unlock()
← (no return value on success)
```

**Notes**:
- Unlock always requires user authentication at the lock screen.
- btlock cannot bypass the session manager's unlock dialog.

---

#### `GetStatus` → `a{sv}` (dict)

Returns the current daemon status as a dictionary.

```
→ GetStatus()
← {
    "state":           "DEVICE_PRESENT",
    "state_code":      4,
    "locked":          false,
    "running":         true,
    "devices":         [{...}, ...],
    "config":          {...}
}
```

**Dict keys**:

| Key           | Type     | Description                              |
|---------------|----------|------------------------------------------|
| `state`       | string   | Human-readable state name                |
| `state_code`  | int32    | Numeric state code (matches `BtlockState`)|
| `locked`      | bool     | Whether the session is currently locked   |
| `running`     | bool     | Whether the daemon is running             |
| `devices`     | `aa{sv}` | List of tracked device dicts (see below)  |
| `config`      | `a{sv}`  | Active configuration values               |

**Device dict keys**:

| Key            | Type   | Description                            |
|----------------|--------|----------------------------------------|
| `address`      | string | Bluetooth MAC address                  |
| `name`         | string | Device name (may be empty)             |
| `rssi`         | int16  | Current filtered RSSI in dBm           |
| `present`      | bool   | Whether device is currently present    |
| `last_seen_ms` | uint64 | Monotonic time of last detection       |

---

#### `Scan` (no arguments)

Trigger an immediate Bluetooth discovery scan.

```
→ Scan()
← true (on success)
```

---

#### `ReloadConfig` (no arguments)

Reload the configuration file without restarting the daemon.

```
→ ReloadConfig()
← true (on success)
```

---

#### `GetDevices` → `aa{sv}` (array of device dicts)

Returns the list of tracked devices. Same format as the `devices` field in
`GetStatus`.

### Signals

#### `DeviceFound (s address, s name, n rssi)`

Emitted when a trusted device is first detected after being absent.

#### `DeviceLost (s address)`

Emitted when a trusted device has been absent for the configured duration.

#### `StateChanged (s old_state, s new_state)`

Emitted whenever the state machine transitions to a new state.

#### `LockRequested`

Emitted when a lock decision is made (after grace period expiry).

#### `ConfigReloaded (s path)`

Emitted after `ReloadConfig` completes. Contains the config file path.

## login1 Integration

The daemon interacts with the desktop session via logind:

### Interface: `org.freedesktop.login1.Manager`

**Object path**: `/org/freedesktop/login1`
**Bus name**: `org.freedesktop.login1`

| Method         | Arguments | Effect                              |
|----------------|-----------|-------------------------------------|
| `Lock`          | none      | Lock the session                    |
| `Unlock`        | none      | Request unlock (prompts for auth)   |
| `ScheduleShutdown` | `s mode`, `s timespec` | Schedule system shutdown |

### Properties

| Property           | Type | Description                       |
|--------------------|------|-----------------------------------|
| `PrepareForSleep`  | bool | True when system is suspending    |
| `SessionExists`    | bool | True if a user session is active  |

### Screen Saver Fallback

If logind is unavailable, the daemon falls back to the freedesktop screensaver
D-Bus interface:

**Bus name**: `org.gnome.ScreenSaver`
**Object path**: `/org/gnome/ScreenSaver`
**Interface**: `org.gnome.ScreenSaver`

| Method     | Arguments | Effect         |
|------------|-----------|----------------|
| `Lock`      | none      | Lock the screen |
| `SetActive` | `b active`| Set active state|

## D-Bus Permission Policy (default)

The session bus service file (`btlock.service`) typically does not require
special D-Bus permissions for method calls since they run on the user's own
session bus. The `ReloadConfig` and `Scan` methods may be restricted via
D-Bus policy rules in production deployments.

## Using gdbus CLI

```bash
# Get status
gdbus call --session --dest org.btlock --object-path /org/btlock \
  --method org.btlock.Daemon.GetStatus

# Lock now
gdbus call --session --dest org.btlock --object-path /org/btlock \
  --method org.btlock.Daemon.Lock

# Trigger scan
gdbus call --session --dest org.btlock --object-path /org/btlock \
  --method org.btlock.Daemon.Scan

# Monitor signals
gdbus monitor --session --dest org.btlock --object-path /org/btlock
```
