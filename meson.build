project(
  'btlock',
  'c',
  version: '0.1.0',
  license: 'GPL-3.0-or-later',
    default_options: [
    'c_std=gnu23',
    'warning_level=3',
    'werror=true',
    'b_pie=true',
  ],
)

project_description = 'Bluetooth proximity workstation locking for Linux'
project_homepage = 'https://github.com/btlock/btlock'
project_license = 'GPL-3.0-or-later'

# --- Required system libraries ---
dep_glib = dependency('glib-2.0', version: '>=2.76')
dep_gio = dependency('gio-2.0', version: '>=2.76')
dep_gobject = dependency('gobject-2.0', version: '>=2.76')

# --- pkg-config metadata for the core library ---
pkgconfig_data = configuration_data()
pkgconfig_data.set('prefix', get_option('prefix'))
pkgconfig_data.set('exec_prefix', get_option('prefix'))
pkgconfig_data.set('libdir', get_option('libdir'))
pkgconfig_data.set('includedir', get_option('includedir'))
pkgconfig_data.set('VERSION', meson.project_version())
pkgconfig_data.set('DESCRIPTION', project_description)

# --- Core C library: state machine, filtering, policy ---
subdir('src/core')

# --- Python daemon and UI are installed as scripts/data ---
subdir('src/daemon')
subdir('src/ui')