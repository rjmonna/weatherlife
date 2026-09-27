project('weather-life', 'c',
  version: '0.1.0',
  default_options: ['c_std=c99', 'warning_level=2'],
  license: 'MIT'
)

# Compiler setup
cc = meson.get_compiler('c')

# Platform detection
host_os = host_machine.system()
if host_os == 'windows'
  platform = 'windows'
  usb_dep = []
  win_deps = cc.find_library('setupapi')
  hid_dep = cc.find_library('hid')
  platform_files = ['src/usb_windows.c']
  platform_libs = [win_deps, hid_dep]
elif host_os == 'linux'
  platform = 'linux'
  hidapi_dep = cc.find_library('hidapi', required: true)
  platform_files = ['src/hid_transport.c', 'src/usb_linux.c']
  platform_libs = [hidapi_dep]
elif host_os == 'darwin'
  platform = 'macos'
  hidapi_dep = cc.find_library('hidapi', required: true)
  platform_files = ['src/hid_transport.c', 'src/usb_macos.c']
  platform_libs = [hidapi_dep]
else
  error('Unsupported platform: ' + host_os)
endif

message('Building for platform: ' + platform)

# Include directories
incdir = include_directories('src')

# Main USB library
usb_lib_sources = [
  'src/usb_common.c',
  'src/weather_response.c',
  'src/device_registration.c',
  'src/bit_packer.c',
  'src/weather_frame.c',
] + platform_files

usb_lib = static_library(
  'weather-life-usb',
  usb_lib_sources,
  include_directories: incdir,
  dependencies: platform_libs,
  install: true
)

# Device discovery executable
weather_discover = executable(
  'weather-life-discover',
  'src/main_discover.c',
  include_directories: incdir,
  link_with: usb_lib,
  dependencies: platform_libs,
  install: true
)

# Installation
install_headers('src/usb_device.h', 'src/weather_response.h', 'src/device_registration.h', 'src/bit_packer.h', 'src/weather_frame.h', subdir: 'weather-life')

# Tests (if any)
subdir('tests')

# Summary
message('---')
message('Weather-Life Configuration')
message('---')
message('Platform: @0@'.format(platform))
message('Compiler: @0@'.format(cc.get_id()))
message('C Standard: c99')
message('Build type: @0@'.format(get_option('buildtype')))
message('')
message('Executables:')
message('  - weather-life-discover')
message('')
message('To build:')
message('  meson compile -C builddir')
message('')
message('To install:')
message('  meson install -C builddir')
