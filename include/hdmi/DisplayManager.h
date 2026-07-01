#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hdmi/Display.h"
#include "hdmi/IDisplayBackend.h"
#include "hdmi/ModeStore.h"

namespace hdmi {

// Thread-safe orchestration layer over an IDisplayBackend.
//
// The GUI and the REST server both talk to a single shared DisplayManager,
// so all access is guarded by a mutex. This is where request validation and
// convenience helpers live, keeping the backends thin.
class DisplayManager {
public:
    // `store` (optional) persists per-monitor mode selections. When present,
    // saved modes are re-applied after switches and via applySavedModes().
    explicit DisplayManager(std::unique_ptr<IDisplayBackend> backend,
                            std::shared_ptr<ModeStore> store = nullptr);

    // Re-apply any saved per-monitor modes to the currently-active displays.
    // Monitors without a saved entry are left on their current mode.
    void applySavedModes();

    // Current list of displays (re-queried from the backend each call).
    std::vector<DisplayInfo> displays();

    // Validate and apply a switch request. `error` is filled on failure.
    bool apply(const SwitchRequest& request, std::string* error);

    // Convenience for the primary use case: make `id` the only active display.
    bool activateExclusive(const std::string& id, std::string* error);

    // Stateless toggle: exclusively activate the next display after the current
    // primary (cycles; for two displays this flips between them). No id needed.
    // On success, `activatedId`/`activatedName` (if non-null) name the result.
    bool toggle(std::string* activatedId, std::string* activatedName, std::string* error);

    // Change a display's mode. hz<=0 selects the highest refresh at that size.
    bool setMode(const std::string& id, int width, int height, int hz, std::string* error);

    // Raise a display to the highest refresh rate at its current resolution.
    bool setMaxRefresh(const std::string& id, std::string* error);

    const char* backendName() const;

private:
    // Ensures the request is well-formed given the currently known displays.
    // Must be called with mutex_ held.
    bool validateLocked(const SwitchRequest& request, std::string* error);

    // Re-apply saved modes to active displays. Must hold mutex_.
    void applySavedModesLocked();

    // Save the mode currently in effect for `id` to the store. Must hold mutex_.
    void persistCurrentLocked(const std::string& id);

    std::unique_ptr<IDisplayBackend> backend_;
    std::shared_ptr<ModeStore> store_;
    std::mutex mutex_;
};

}  // namespace hdmi
