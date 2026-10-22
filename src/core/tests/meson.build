# C unit tests for the core library
test_sources = [
  'test_state_machine.c',
  'test_filter.c',
  'test_policy.c',
  'test_config.c',
]

foreach test_name : test_sources
  test_bin = executable(
    test_name.split('.')[0],
    test_name,
    include_directories: btlock_core_inc,
    link_with: btlock_core_lib,
    dependencies: [dep_glib],
    install: false,
  )
  test(
    test_name.split('.')[0],
    test_bin,
    workdir: meson.current_build_dir(),
  )
endforeach
