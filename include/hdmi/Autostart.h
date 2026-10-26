#pragma once

#include <string>

namespace hdmi::autostart {

// Whether launch-at-login is supported on this platform (Windows only).
bool isSupported();

// True if the app is currently registered to start automatically at login.
bool isEnabled();

// Register (enable=true) or unregister (enable=false) launch-at-login for the
// current user. Uses the per-user "Run" registry key on Windows, so no admin
// rights are required. Returns false and fills `error` on failure.
//
// The registered command carries kTrayFlag, so a login-time launch starts
// straight to the tray instead of popping the window open.
bool setEnabled(bool enable, std::string* error);

// Command-line flag meaning "start hidden, to the tray".
inline constexpr const char* kTrayFlag = "--tray";

// Re-write the registered command if launch-at-login is enabled, so entries
// written by an older version (or before the exe moved) pick up the current
// path and flags. No-op when it isn't enabled.
void refresh();

}  // namespace hdmi::autostart
