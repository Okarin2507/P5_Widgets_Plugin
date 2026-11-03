project(
  'persona-rice-cpp',
  ['cpp', 'c'],
  version: '2.1.0',
  license: 'MIT',
  default_options: [
    'cpp_std=c++23',
    'c_std=c11',
    'warning_level=2',
    'optimization=2',
  ],
)

wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
wayland_protocols = dependency('wayland-protocols')
wayland_scanner_dep = dependency('wayland-scanner', native: true, required: false)
wayland_scanner = find_program(
  wayland_scanner_dep.found() ? wayland_scanner_dep.get_variable('wayland_scanner') : 'wayland-scanner',
  native: true,
)

cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
pangoft2 = dependency('pangoft2')
fontconfig = dependency('fontconfig')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
gio = dependency('gio-2.0')
glib = dependency('glib-2.0')
libsystemd = dependency('libsystemd')
libcurl = dependency('libcurl')

src_inc = include_directories('src')

wl_protocols_dir = wayland_protocols.get_variable('pkgdatadir')
xdg_shell_xml = wl_protocols_dir / 'stable/xdg-shell/xdg-shell.xml'
wlr_layer_shell_xml = files('protocols/wlr-layer-shell-unstable-v1.xml')

xdg_shell_header = custom_target(
  'xdg-shell-client-protocol.h',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

xdg_shell_code = custom_target(
  'xdg-shell-protocol.c',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

wlr_layer_shell_header = custom_target(
  'wlr-layer-shell-unstable-v1-client-protocol.h',
  input: wlr_layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

wlr_layer_shell_code = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.c',
  input: wlr_layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

protocol_sources = [
  xdg_shell_header,
  xdg_shell_code,
  wlr_layer_shell_header,
  wlr_layer_shell_code,
]

# -----------------------------------------------------------------------------
# Modules
# -----------------------------------------------------------------------------
core_lib = static_library(
  'p5core',
  [
    'src/core/common.cpp',
    'src/core/shm_buffer.cpp',
    'src/core/image_cache.cpp',
    'src/core/asset_config.cpp',
  ],
  include_directories: src_inc,
  dependencies: [cairo, pangocairo, pangoft2, fontconfig, gdk_pixbuf, gio],
)

weather_lib = static_library(
  'p5weather',
  ['src/weather/noctalia_weather.cpp', 'src/weather/day_phase.cpp'],
  include_directories: src_inc,
)

ipc_lib = static_library(
  'p5ipc',
  [
    'src/ipc/dbus_notifications.cpp',
    'src/ipc/dbus_mpris.cpp',
  ],
  include_directories: src_inc,
  dependencies: [libsystemd, libcurl, cairo, glib],
  link_with: core_lib,
)

notifications_lib = static_library(
  'p5notifications',
  ['src/notifications/notification_surface.cpp', wlr_layer_shell_header],
  include_directories: src_inc,
  dependencies: [cairo, pangocairo],
  link_with: core_lib,
)

music_lib = static_library(
  'p5music',
  [
    'src/music/music_surface.cpp',
    'src/music/jitter_text.cpp',
    wlr_layer_shell_header,
  ],
  include_directories: src_inc,
  dependencies: [cairo, pangocairo],
  link_with: [core_lib, ipc_lib],
)

calendar_lib = static_library(
  'p5calendar',
  ['src/calendar/calendar_surface.cpp', wlr_layer_shell_header],
  include_directories: src_inc,
  dependencies: [cairo, pangocairo],
  link_with: [core_lib, weather_lib],
)

wallpaper_lib = static_library(
  'p5wallpaper',
  ['src/wallpaper/wallpaper_automation.cpp'],
  include_directories: src_inc,
  link_with: [core_lib, weather_lib],
)

# -----------------------------------------------------------------------------
# Executables
# -----------------------------------------------------------------------------
executable(
  'p5-rice-daemon',
  ['src/app/main.cpp', protocol_sources],
  include_directories: src_inc,
  dependencies: [
    wayland_client,
    wayland_cursor,
    cairo,
    pangocairo,
    pangoft2,
    fontconfig,
    gdk_pixbuf,
    gio,
    libsystemd,
    libcurl,
  ],
  link_with: [
    core_lib,
    weather_lib,
    ipc_lib,
    notifications_lib,
    music_lib,
    calendar_lib,
    wallpaper_lib,
  ],
  install: true,
)

executable(
  'p5-rice-ctl',
  ['src/app/ctl.cpp'],
  dependencies: [libsystemd],
  install: true,
)
