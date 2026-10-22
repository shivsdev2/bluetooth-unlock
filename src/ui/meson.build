# Python GTK4 UI - installed as a script
python = import('python').find_installation('python3')

ui_sources = [
  'btlock-ui.py',
  'btlockui/__init__.py',
  'btlockui/main.py',
  'btlockui/window.py',
  'btlockui/devices.py',
  'btlockui/settings.py',
  'btlockui/diagnostics.py',
]

# Install the UI package
foreach f : ui_sources
  install_data(f, install_dir: get_option('datadir') / 'btlock' / 'ui')
endforeach

# Install entry point script
conf = configuration_data()
conf.set('PYTHON', python.full_path())
conf.set('UI_SCRIPT', get_option('datadir') / 'btlock' / 'ui' / 'btlock-ui.py')
configure_file(
  input: 'btlock-ui.in',
  output: 'btlock-ui',
  configuration: conf,
  install: true,
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)
