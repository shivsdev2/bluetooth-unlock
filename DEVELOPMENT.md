# Developer Guide

## Project Layout

```
bluetooth proximity/
├── meson.build                 # Top-level build definition
├── README.md                   # User documentation
├── DEVELOPMENT.md              # This file
├── doc/                        # Design documents
│   ├── architecture.md
│   └── state_machine.md
├── src/
│   ├── core/                   # C library: state machine, filtering, policy
│   │   ├── meson.build
│   │   ├── include/btlock/     # Public headers
│   │   ├── src/                # Implementation
│   │   └── tests/              # C unit tests
│   ├── daemon/                 # Python daemon
│   │   ├── meson.build
│   │   ├── btlockd.py          # Entry point
│   │   ├── btlockd.in          # Script wrapper template
│   │   ├── btlockd.service.in   # Systemd service template
│   │   └── btlock/
│   │       ├── __init__.py
│   │       ├── config.py       # Config management (mirrors C struct)
│   │       ├── bluez_monitor.py # BlueZ D-Bus monitoring
│   │       ├── desktop_adapter.py # Desktop locking backends
│   │       ├── dbus_service.py  # D-Bus service for UI
│   │       └── state_bridge.py # ctypes bridge to C core
│   └── ui/                     # GUI/CLI clients
│       └── meson.build
└── tests/                      # Integration tests
```

## Architecture

The project is divided into three layers:

### 1. C Core Library (`src/core/`)
The state machine, RSSI filtering, and lock policy logic are implemented
in C for performance and reliability. This library is consumable from
both C and Python (via ctypes).

**Key files:**
- `btlock_state.c` / `.h` — Finite state machine with 10 states
- `btlock_filter.c` / `.h` — Median and moving average filters
- `btlock_policy.c` / `.h` — Lock/unlock threshold logic
- `btlock_config.c` / `.h` — C struct mirroring Python config
- `btlock_log.c` / `.h` — Logging abstraction

### 2. Python Daemon (`src/daemon/`)
The daemon (`btlockd.py`) orchestrates all components:
- Loads config (JSON file with schema validation)
- Drives the state machine via `StateBridge` (ctypes)
- Monitors Bluetooth via `BluezMonitor` (D-Bus)
- Locks/unlocks the desktop via `DesktopAdapter`
- Exposes D-Bus API via `BtlockDBusService`

### 3. UI Layer (`src/ui/`)
Planned for a future stage - GUI app and CLI tool.

## Building

```bash
# Configure
meson setup builddir

# Build
ninja -C builddir

# Run tests
meson test -C builddir

# Install (requires root)
sudo ninja -C builddir install
```

## Running Tests

### C Tests
```bash
meson test -C builddir
```

Available tests:
- `test_config` — Config validation and bounds checking
- `test_filter` — RSSI filter (median, moving average) unit tests
- `test_policy` — Lock policy threshold logic
- `test_state_machine` — State machine transitions and edge cases

### Python
```bash
python3 -m pytest tests/python/
```

## Development Workflow

1. Make changes to C core in `src/core/src/`
2. Update headers in `src/core/include/btlock/`
3. Add/update tests in `src/core/tests/`
4. Rebuild: `ninja -C builddir`
5. Run tests: `meson test -C builddir`
6. For Python changes, run `python3 -m py_compile` on changed files

## Key Design Decisions

- **ctypes over cffi**: The Python bridge uses ctypes (in `state_bridge.py`)
  to avoid an external dependency, making it easy to package and deploy.

- **Simulation mode**: When PyGObject is unavailable, `BluezMonitor`
  falls back to simulation mode. This allows testing the Python daemon
  without Bluetooth hardware or a D-Bus system bus.

- **Filter types**: Median filtering is the default for noise robustness.
  Moving average is available as an alternative via config.

- **State machine in C**: The core state machine is in C so it can be
  reused by other frontends (e.g., a C-only agent).

## D-Bus Interface

The daemon registers as `org.btlock.Service` on the system D-Bus.
See the README for full method/signal definitions.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Run all tests
6. Submit a pull request