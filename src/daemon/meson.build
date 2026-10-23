# Python daemon - installed as a script
# The daemon uses the C core library via ctypes
python = import('python').find_installation('python3')

daemon_sources = [
  'btlockd.py',
  'btlock/__init__.py',
  'btlock/config.py',
  'btlock/bluez_monitor.py',
  'btlock/desktop_adapter.py',
  'btlock/state_bridge.py',
  'btlock/dbus_service.py',
]

# Install the daemon package
foreach f : daemon_sources
  install_data(f, install_dir: get_option('datadir') / 'btlock' / 'daemon')
endforeach

# Install entry point script
conf = configuration_data()
conf.set('PYTHON', python.full_path())
conf.set('DAEMON_SCRIPT', get_option('datadir') / 'btlock' / 'daemon' / 'btlockd.py')
configure_file(
  input: 'btlockd.in',
  output: 'btlockd',
  configuration: conf,
  install: true,
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)
