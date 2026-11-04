#pragma once

#include <cstdint>
#include <string>

namespace p5 {

// True when the app that raised a notification currently has at least one
// window hidden in Umbriel's scratchpad. Clicking such a banner must not open
// the app (it would yank a deliberately-stowed window back onto the visible
// stack); the banner just dismisses instead.
//
// sender_pid comes from the notification's D-Bus sender (resolved at parse
// time). Matching against hidden scratchpad windows:
//   * app_id equality (case-insensitive, [Xwayland] prefix and .desktop
//     suffix normalized),
//   * title contains the identity,
//   * sender process ancestry: the window owns the sender's PID (GUI apps)
//     or the sender runs inside the window's process tree (terminal-hosted
//     CLIs like opencode inside kitty), also via /proc comm of ancestors.
// A revealed window loses the scratchpad flag and never blocks.
bool sender_window_in_scratchpad(const std::string& desktop_entry,
                                 const std::string& app_name,
                                 uint32_t sender_pid);

} // namespace p5
