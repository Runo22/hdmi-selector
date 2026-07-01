#pragma once

#include <string>
#include <vector>

#include "hdmi/Display.h"

namespace hdmi {

// Abstraction over the OS-specific display-configuration API.
//
// Two implementations exist:
//   * WindowsBackend - real switching via the Win32 CCD API (Windows only).
//   * MockBackend    - in-memory fake used for tests and for running the
//                      REST server / core logic on non-Windows hosts.
//
// Keeping the OS calls behind this interface lets the core logic, REST
// server and GUI be developed and tested without a Windows machine.
class IDisplayBackend {
public:
    virtual ~IDisplayBackend() = default;

    // Enumerate all displays known to the OS (active and inactive).
    virtual std::vector<DisplayInfo> list() = 0;

    // Apply a switch request. Returns true on success. On failure, `error`
    // (if non-null) is filled with a human-readable reason.
    virtual bool apply(const SwitchRequest& request, std::string* error) = 0;

    // Change the mode of one display. If `hz <= 0`, the highest refresh rate
    // available at that resolution is chosen. Returns false (and fills `error`)
    // if the mode is unsupported or the change is rejected.
    virtual bool setMode(const std::string& id, int width, int height, int hz,
                         std::string* error) = 0;

    // Short name of the backend, e.g. "windows" or "mock".
    virtual const char* name() const = 0;
};

}  // namespace hdmi
