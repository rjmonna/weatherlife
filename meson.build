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
  usb_dep = dependency('libusb-1.0', required: true)
  platform_files = ['src/usb_linux.c']
  platform_libs = [usb_dep]
elif host_os == 'darwin'
  platform = 'macos'
  usb_dep = dependency('libusb-1.0', required: true)
  iokit_dep = cc.find_library('IOKit')
  corefoundation_dep = cc.find_library('CoreFoundation')
  platform_files = ['src/usb_macos.c']
  platform_libs = [usb_dep, iokit_dep, corefoundation_dep]
else
  error('Unsupported platform: ' + host_os)
endif

message('Building for platform: ' + platform)

# Include directories
incdir = include_directories('src')

# Main USB library
usb_lib_sources = [
  'src/usb_common.c',
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
install_headers('src/usb_device.h', subdir: 'weather-life')

# Tests (if any)
subdir('tests', if_found: [] )

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
