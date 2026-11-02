cmake_minimum_required(VERSION 3.22)
project(persona-rice-cpp VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client wayland-cursor)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo pangocairo pangoft2 fontconfig gdk-pixbuf-2.0 gio-2.0)
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

add_executable(p5-rice-daemon
  src/main.cpp
  src/common.cpp
  src/shm_buffer.cpp
  src/jitter_text.cpp
  src/dbus_notifications.cpp
  src/dbus_mpris.cpp
  src/p5notify_surface.cpp
  src/p5music_surface.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h"
  "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c"
  "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h"
  "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c"
)

target_include_directories(p5-rice-daemon PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" src)
target_link_libraries(p5-rice-daemon PRIVATE
  PkgConfig::WAYLAND_CLIENT
  PkgConfig::CAIRO
  PkgConfig::SYSTEMD
  PkgConfig::CURL
)

add_executable(p5-rice-ctl src/ctl.cpp)
target_link_libraries(p5-rice-ctl PRIVATE PkgConfig::SYSTEMD)

install(TARGETS p5-rice-daemon p5-rice-ctl RUNTIME DESTINATION bin)
