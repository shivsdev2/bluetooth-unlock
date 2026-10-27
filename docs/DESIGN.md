# Btlock Design Document

## Overview

btlock is a Bluetooth proximity-based workstation locking tool for Linux. When
a trusted Bluetooth device (e.g., smartphone, fitness tracker) moves out of
range, the workstation locks automatically. When the device returns, the user
is prompted for authentication to unlock.

The system is built as a multi-layer architecture:

```
┌──────────────────────────────────────────────────┐
│                    Desktop Session                │
│  (GNOME / KDE / Sway — logind, D-Bus, screensaver) │
└──────────┬──────────────────────────┬─────────────┘
           │ logind D-Bus             │ screensaver D-Bus
           ▼                          ▼
┌──────────────────────────────────────────────────┐
│                  btlockd (daemon)                  │
│                                                    │
│  D-Bus service  │  State machine  │  Bluetooth     │
│    (org.btlock) │  (C core)      │  monitor       │
│                 │                │  (BlueZ D-Bus) │
└─────────────────┴────────────────┴────────────────┘
                   │ ctypes           │ D-Bus
                   ▼                  ▼
┌──────────────────────────────────────────────────┐
│              libbtlock-core (C library)           │
│                                                   │
│  State machine  │  Filter  │  Policy  │  Config     │
└──────────────────────────────────────────────────┘
                   │
                   ▼
           Config file (~/.config/btlock/config.json)
```

## Architecture Layers

### 1. C Core Library (`libbtlock-core`)

**Location**: `src/core/`

**Responsibility**: Pure-logic state machine, RSSI filtering, policy engine,
configuration validation. No I/O dependencies (no D-Bus, no Bluetooth).

**Design rationale**: The state machine and filtering logic are the most
error-prone parts of the system. By isolating them in a pure C library with
no I/O dependencies, they can be tested deterministically with unit tests
that do not require hardware or a desktop session.

**ABI stability**: The headers in `include/btlock/` form the public ABI.
The library is versioned with soversion 0. Python bindings are via ctypes,
so symbol names must remain stable.

### 2. State Machine (`btlock_state.c`)

The state machine implements a deterministic finite automaton. All time
decisions use monotonic milliseconds (`guint64`) to be immune to clock
changes, NTP adjustments, or timezone shifts.

**States**:

| State                  | Meaning                                         |
|------------------------|-------------------------------------------------|
| `UNINITIALIZED`        | Not yet started                                 |
| `DISCOVERING`          | Scanning for Bluetooth devices                  |
| `DEVICE_PRESENT`       | At least one trusted device nearby              |
| `DEVICE_ABSENT`        | No device detected, no grace period             |
| `DEPARTURE_GRACE`      | Device lost, grace period before lock          |
| `LOCK_REQUESTED`       | Lock command sent, awaiting confirmation        |
| `LOCK_CONFIRMED`       | Lock confirmed by session manager               |
| `RETURNING`            | Device returned during departure               |
| `RECONNECTING`         | Device reconnecting after lock                  |
| `BT_UNAVAILABLE`       | Bluetooth subsystem not available               |
| `ADAPTER_UNAVAILABLE`  | No Bluetooth adapter found                      |
| `SESSION_UNAVAILABLE`  | No session manager for locking                  |
| `AUTH_REQUIRED`        | User authentication needed for unlock           |
| `ERROR_DEGRADED`       | Recoverable error, degraded operation           |
| `SHUTTING_DOWN`        | Application is shutting down                     |

**Decisions** (`BtlockAction`):

| Action              | Meaning                                  |
|---------------------|------------------------------------------|
| `NONE`              | No action needed                         |
| `REQUEST_LOCK`      | Send lock request to session manager     |
| `REQUEST_UNLOCK`    | Request unlock (manual auth required)    |
| `CANCEL_LOCK`       | Cancel pending lock (device returned)    |
| `NOTIFY`            | Emit notification to UI                  |
| `START_DISCOVERY`   | Begin Bluetooth discovery                |
| `STOP_DISCOVERY`    | Stop discovery (power saving)            |

### 2.1 State Transition Flow

```
START
  │
  ▼
UNINITIALIZED ──START──> DISCOVERING ──device found──> DEVICE_PRESENT
                                            │                    │
                                            │ device lost        │ device lost
                                            ▼                    ▼
                                      DEPARTURE_GRACE ──grace expired──> LOCK_REQUESTED
                                                          │
                                            device found ┌──┘
                                                         ▼
                                                   RETURNING ──confirmed──> DEVICE_PRESENT
                                                         │
                                            LOCK_CONFIRMED ┌─┘
                                                         ▼
                                                   LOCK_CONFIRMED
```

### 3. RSSI Filter (`btlock_filter.c`)

Bluetooth signal strength (RSSI) is inherently noisy. Direct use of raw
RSSI would cause flickering state transitions (false lock/unlock).

Two filter algorithms are available:

1. **Moving Average** (`BTLOCK_FILTER_MOVING_AVERAGE`): Simple arithmetic
   mean over the last N samples. Good for smoothly varying signals.

2. **Median Filter** (`BTLOCK_FILTER_MEDIAN`): Robust against outliers.
   Better for environments with sudden signal spikes/dips.

Both maintain a sliding window of `rssi_window` samples (default 5).

### 4. Policy Engine (`btlock_policy.c`)

The policy engine tracks multiple Bluetooth devices and evaluates the
collective state to decide whether to lock.

**Single-device policy** (`required_devices = 0`): Lock when the only
trusted device is absent for the grace period.

**Multi-device policy** (`required_devices > 0`): Lock only when all
`required_devices` enlisted devices are simultaneously absent. This
prevents locking when the user has their phone in one pocket and
headphones in another.

### 5. Python Daemon (`btlockd`)

**Location**: `src/daemon/`

The daemon orchestrates the C core, BlueZ D-Bus monitoring, and desktop
session management.

#### 5.1 `BluezMonitor`

Monitors BlueZ via D-Bus. Tracks `Device1` objects, filters RSSI, and
maintains device state. Only devices in the `enrolled_devices` config
list trigger state machine events.

#### 5.2 `DesktopAdapter`

Abstracts desktop session management. Detects and uses the available
backend:

- `org.freedesktop.login1` (logind) — sends `Lock`/`Unlock` D-Bus calls.
- `loginctl` CLI fallback.
- `org.gnome.ScreenSaver` / freedesktop screensaver D-Bus (fallback).

#### 5.3 `StateBridge` / `NullStateBridge`

`StateBridge` loads `libbtlock-core.so` via ctypes and delegates to the C
state machine. `NullStateBridge` is a Python reimplementation used as a
fallback if the C library is not available.

#### 5.4 `BtlockDBusService`

Exposes a D-Bus service on the session bus at `org.btlock` with interface
`org.btlock.Daemon`. Provides:
- `Lock()` / `Unlock()` — manual lock/unlock requests.
- `GetStatus()` — current state, device list, config values.
- `Scan()` — trigger an immediate Bluetooth scan.
- `ReloadConfig()` — reload configuration without restart.

#### 5.5 `btlockd.py`

Entry point. Initializes all components, runs the main loop (tick-based),
and handles signals (SIGTERM, SIGINT).

### 6. GTK4 UI (`btlock-ui`)

**Location**: `src/ui/`

A Python GTK4 application providing a GUI for:
- Viewing tracked device status.
- Editing enrolled device list.
- Adjusting configuration parameters.
- Viewing diagnostics (RSSI history, state machine state, logs).

### 7. Configuration

**Default location**: `~/.config/btlock/config.json`

Generated by `btlockd --setup`. The config schema is mirrored between C
(`BtlockConfig` struct) and Python (`BtlockConfig` dataclass) for
cross-language consistency.

Config files are written with `0600` permissions (owner read/write only)
because they contain device MAC addresses which can be used for tracking.

## Security Model

### Trust Boundary: Enrollment

Only devices explicitly listed in `enrolled_devices` are tracked.
Non-enrolled devices are filtered at the discovery layer (`BluezMonitor`)
and do not reach the state machine. This prevents tracking arbitrary
devices that simply enter Bluetooth range.

### Trust Boundary: Configuration

All configuration values are validated before use. Defaults are safe:
- `auto_unlock_enabled` defaults to `false`.
- `lock_confirm_required` defaults to `true`.

### Privacy: Logging

Logs never contain credentials, passwords, or authentication tokens.
Device MAC addresses are truncated to the manufacturer prefix
(`AA:BB:CC:xx:xx:xx`) in non-debug log levels. Debug logs contain full
addresses but require explicit opt-in.

### Unlock Security

btlock does not implement its own unlocking mechanism. It requests
unlocks through the session manager (logind or screensaver D-Bus), which
requires the user to enter their password or use biometrics. Even with
`auto_unlock_enabled=true`, the user must authenticate via the desktop
session's unlock dialog.