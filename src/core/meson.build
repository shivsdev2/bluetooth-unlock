# Core C library: libbtlock-core
# Pure logic: state machine, signal filtering, policy engine
# No I/O dependencies - fully testable and language-agnostic

pkgconfig = import('pkgconfig')

btlock_core_sources = [
  'src/btlock_common.c',
  'src/btlock_state.c',
  'src/btlock_filter.c',
  'src/btlock_policy.c',
  'src/btlock_config.c',
  'src/btlock_log.c',
]

btlock_core_headers = [
  'include/btlock/btlock_state.h',
  'include/btlock/btlock_filter.h',
  'include/btlock/btlock_policy.h',
  'include/btlock/btlock_config.h',
  'include/btlock/btlock_log.h',
  'include/btlock/btlock_common.h',
]

btlock_core_inc = include_directories('include')

btlock_core_lib = library(
  'btlock-core',
  btlock_core_sources,
  install: true,
  version: meson.project_version(),
  soversion: '0',
  include_directories: btlock_core_inc,
  dependencies: [dep_glib, dep_gio],
  install_dir: get_option('libdir'),
)

btlock_core_dep = declare_dependency(
  include_directories: btlock_core_inc,
  link_with: btlock_core_lib,
  dependencies: [dep_glib],
)

# Install headers
install_headers(
  btlock_core_headers,
  subdir: 'btlock',
)

# pkg-config file
pkgconfig.generate(
  btlock_core_lib,
  filebase: 'btlock-core-0.1',
  description: 'Bluetooth proximity workstation locking - core library',
)

# --- Core tests ---
subdir('tests')