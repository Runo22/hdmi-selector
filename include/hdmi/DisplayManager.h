#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hdmi/Display.h"
#include "hdmi/IDisplayBackend.h"

namespace hdmi {

// Thread-safe orchestration layer over an IDisplayBackend.
//
// The GUI and the REST server both talk to a single shared DisplayManager,
// so all access is guarded by a mutex. This is where request validation and
// convenience helpers live, keeping the backends thin.
class DisplayManager {
public:
    explicit DisplayManager(std::unique_ptr<IDisplayBackend> backend);

    // Current list of displays (re-queried from the backend each call).
    std::vector<DisplayInfo> displays();

    // Validate and apply a switch request. `error` is filled on failure.
    bool apply(const SwitchRequest& request, std::string* error);

    // Convenience for the primary use case: make `id` the only active display.
    bool activateExclusive(const std::string& id, std::string* error);

    const char* backendName() const;

private:
    // Ensures the request is well-formed given the currently known displays.
    // Must be called with mutex_ held.
    bool validateLocked(const SwitchRequest& request, std::string* error);

    std::unique_ptr<IDisplayBackend> backend_;
    std::mutex mutex_;
};

}  // namespace hdmi
