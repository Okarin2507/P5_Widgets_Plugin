cmake_minimum_required(VERSION 3.22)
project(persona-rice-cpp VERSION 2.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client wayland-cursor)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo pangocairo pangoft2 fontconfig gdk-pixbuf-2.0 gio-2.0 gio-unix-2.0)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WL_PROTOCOLS_DIR wayland-protocols pkgdatadir)

set(XDG_SHELL_XML "${WL_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
set(WLR_LAYER_SHELL_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml")

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h"
  COMMAND "${WAYLAND_SCANNER}" client-header "${XDG_SHELL_XML}" "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h"
  DEPENDS "${XDG_SHELL_XML}"
)
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c"
  COMMAND "${WAYLAND_SCANNER}" private-code "${XDG_SHELL_XML}" "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c"
  DEPENDS "${XDG_SHELL_XML}"
)
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h"
  COMMAND "${WAYLAND_SCANNER}" client-header "${WLR_LAYER_SHELL_XML}" "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h"
  DEPENDS "${WLR_LAYER_SHELL_XML}"
)
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c"
  COMMAND "${WAYLAND_SCANNER}" private-code "${WLR_LAYER_SHELL_XML}" "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c"
  DEPENDS "${WLR_LAYER_SHELL_XML}"
)

set(PROTOCOL_SOURCES
  "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h"
  "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c"
  "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h"
  "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c"
)

add_custom_target(p5-protocol-headers DEPENDS
  "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h"
  "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h"
)

# -----------------------------------------------------------------------------
# Modules
# -----------------------------------------------------------------------------
add_library(p5core STATIC
  src/core/common.cpp
  src/core/shm_buffer.cpp
  src/core/image_cache.cpp
  src/core/asset_config.cpp
  src/core/notification_launcher.cpp
  src/core/scratchpad_check.cpp
)
target_include_directories(p5core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(p5core PUBLIC PkgConfig::CAIRO)

add_library(p5weather STATIC
  src/weather/noctalia_weather.cpp
  src/weather/day_phase.cpp
)
target_include_directories(p5weather PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")

add_library(p5ipc STATIC
  src/ipc/dbus_notifications.cpp
  src/ipc/dbus_mpris.cpp
)
target_include_directories(p5ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(p5ipc PUBLIC PkgConfig::SYSTEMD PkgConfig::CURL PkgConfig::CAIRO PkgConfig::GLIB p5core)

add_library(p5notifications STATIC
  src/notifications/notification_surface.cpp
)
target_include_directories(p5notifications PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(p5notifications PUBLIC PkgConfig::CAIRO p5core)
add_dependencies(p5notifications p5-protocol-headers)

add_library(p5music STATIC
  src/music/music_surface.cpp
  src/music/jitter_text.cpp
)
target_include_directories(p5music PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(p5music PUBLIC PkgConfig::CAIRO p5core p5ipc)
add_dependencies(p5music p5-protocol-headers)

add_library(p5calendar STATIC
  src/calendar/calendar_surface.cpp
)
target_include_directories(p5calendar PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(p5calendar PUBLIC PkgConfig::CAIRO p5core p5weather)
add_dependencies(p5calendar p5-protocol-headers)

add_library(p5wallpaper STATIC
  src/wallpaper/wallpaper_automation.cpp
)
target_include_directories(p5wallpaper PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(p5wallpaper PUBLIC p5core p5weather)

# -----------------------------------------------------------------------------
# Executables
# -----------------------------------------------------------------------------
add_executable(p5-rice-daemon
  src/app/main.cpp
  ${PROTOCOL_SOURCES}
)
target_include_directories(p5-rice-daemon PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
add_dependencies(p5-rice-daemon p5-protocol-headers)
target_link_libraries(p5-rice-daemon PRIVATE
  PkgConfig::WAYLAND_CLIENT
  PkgConfig::CAIRO
  PkgConfig::SYSTEMD
  PkgConfig::CURL
  p5core
  p5weather
  p5ipc
  p5notifications
  p5music
  p5calendar
  p5wallpaper
)

add_executable(p5-rice-ctl src/app/ctl.cpp)
target_include_directories(p5-rice-ctl PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(p5-rice-ctl PRIVATE PkgConfig::SYSTEMD)

install(TARGETS p5-rice-daemon p5-rice-ctl RUNTIME DESTINATION bin)
