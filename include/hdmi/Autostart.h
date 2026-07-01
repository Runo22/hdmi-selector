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
bool setEnabled(bool enable, std::string* error);

}  // namespace hdmi::autostart
