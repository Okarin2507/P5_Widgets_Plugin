#pragma once

#include <string>

namespace p5 {

// Launch the application that raised a notification, so clicking a banner's
// text can direct the user to the sender ("click-to-open").
//
// Resolution order:
//   1. FreeDesktop "desktop-entry" hint (exact .desktop id).
//   2. app_name matched against installed .desktop files
//      (Name / GenericName / StartupWMClass / Exec first token).
//   3. app_name as an executable name found on PATH.
//
// Returns true when a launch was issued; false when no target could be
// resolved (the banner then behaves like a plain dismiss-on-click).
bool launch_notification_app(const std::string& desktop_entry, const std::string& app_name);

} // namespace p5
