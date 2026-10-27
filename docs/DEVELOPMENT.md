# Development Guide

## Prerequisites

### Build dependencies

| Dependency    | Version  | Purpose                  |
|---------------|----------|--------------------------|
| meson         | >=0.60   | Build system             |
| ninja         | any      | Build backend            |
| gcc/clang     | C11      | C compiler               |
| pkg-config    | any      | Dependency discovery     |
| glib-2.0      | >=2.76   | GObject, GLib utils      |
| gio-2.0       | >=2.76   | D-Bus, GIO               |
| gobject-2.0   | >=2.76   | GObject                  |

### Python dependencies (daemon + UI + tests)

| Package      | Required version | Purpose                |
|--------------|------------------|------------------------|
| python3      | >=3.10           | Runtime                |
| PyGObject    | >=3.42           | GTK4 bindings          |
| PyGObject-Stubs | latest       | Type hints             |

### System dependencies

- BlueZ (for `bluetoothctl` and D-Bus interface)
- logind (systemd) or a compatible session manager
- A D-Bus session bus

## Build

```bash
meson setup builddir
cd builddir
ninja
```

## Run Tests

### C unit tests

```bash
cd builddir
meson test -v
```

Tests:
- `test_state_machine` — state transition logic
- `test_filter` — RSSI filtering (median, moving average)
- `test_policy` — multi-device policy evaluation
- `test_config` — configuration validation and defaults

### Python unit tests

```bash
# From repository root, with C library built
PYTHONPATH="src/daemon" python3 tests/python/run_tests.py -v

# Or with unittest discovery
PYTHONPATH="src/daemon" python3 -m unittest discover -s tests/python -p "test_*.py" -v
```

Python tests cover:
- `test_config.py` — Config dataclass, validation, file I/O, permissions
- `test_bluez_monitor.py` — RSSI filtering, device tracking, callbacks
- `test_state_machine.py` — NullStateBridge state transitions
- `test_desktop_adapter.py` — Backend detection, lock/unlock
- `test_dbus_service.py` — D-Bus service methods, status dict

### Simulation mode

Tests use `BluezMonitor` in simulation mode (no real BlueZ or D-Bus). The
`NullStateBridge` is used when `libbtlock-core.so` is not installed.

## Development Workflow

### Making changes to the C core

1. Edit source in `src/core/src/`.
2. Header changes go in `src/core/include/btlock/`.
3. Run C tests: `cd builddir && meson test`.
4. Python `NullStateBridge` mirrors the C state machine — update both.

### Making changes to the daemon

1. Edit source in `src/daemon/btlock/`.
2. Run Python tests: `PYTHONPATH="src/daemon" python3 tests/python/run_tests.py`.
3. If changing public API, update `docs/DBUS.md`.

### Making changes to the UI

1. Edit source in `src/ui/btlockui/`.
2. Run UI smoke test (manual): `python3 src/ui/btlock-ui.py`.

## Adding a New State

1. Add state to `BtlockState` enum in `btlock_common.h`.
2. Add state to `btlock_state_name()` in `btlock_common.c`.
3. Implement transition logic in `btlock_state.c`.
4. Add state to `NullStateBridge` in `src/daemon/btlock/state_bridge.py`.
5. Add state name to `get_state_name()` in `state_bridge.py`.
6. Add C unit tests in `src/core/tests/test_state_machine.c`.
7. Add Python unit tests in `tests/python/test_state_machine.py`.
8. Update state table in `docs/DESIGN.md`.

## Adding a New Configuration Field

1. Add field to `BtlockConfig` struct in `btlock_config.h`.
2. Set default in `btlock_config_init_defaults()` in `btlock_config.c`.
3. Add validation in `btlock_config_validate()`.
4. Add field to Python `BtlockConfig` dataclass in `src/daemon/btlock/config.py`.
5. Add field to `to_dict()` / `from_dict()` methods.
6. Add validation in Python `validate()` method.
7. Add tests for the new field in both C and Python test suites.
8. Document in `docs/CONFIG.md`.

## Release Process

1. Bump version in `meson.build`.
2. Update changelog (if applicable).
3. `meson setup builddir && cd builddir && ninja && meson test`
4. Tag release: `git tag -s v<version>`
5. Push tag: `git push origin v<version>`

## Code Style

### C

- Follow the existing style (4-space indentation, braces on new lines).
- Use `BTLOCK_STATIC_ASSERT` for compile-time checks.
- Document all public functions with `/** ... */` Doxygen-style comments.
- Security: never log full MAC addresses at non-DEBUG levels.

### Python

- Follow PEP 8.
- Use `from __future__ import annotations` for type hints.
- Use dataclasses for configuration and device state.
- Use `logging` module, not `print()`.
- Type hints required on all function signatures.

### Git Commit Messages

Use the imperative mood ("add filter", not "added filter"). Keep the
subject line under 72 characters. Reference the relevant docs file if
the change is architectural.
