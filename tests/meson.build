native_tests = executable(
  'weather-life-native-tests',
  'test_native.c',
  include_directories: incdir,
  link_with: usb_lib,
  dependencies: platform_libs,
)

test('native-unit-tests', native_tests)

python = import('python').find_installation()
test('python-unit-tests', python, args: ['-m', 'unittest', 'discover', '-s', meson.project_source_root() / 'tests', '-p', 'test_python.py'],
  env: ['PYTHONPATH=' + meson.project_source_root() / 'python' + ':' + meson.project_source_root() / 'scripts'])