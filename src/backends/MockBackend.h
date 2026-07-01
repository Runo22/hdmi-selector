#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "hdmi/Display.h"
#include "hdmi/IDisplayBackend.h"

namespace hdmi {

// In-memory backend that simulates a machine with a fixed set of displays.
// Used by unit tests and when running the server on a non-Windows host so the
// core logic and REST API can be exercised end-to-end without real hardware.
class MockBackend : public IDisplayBackend {
public:
    // Creates a default two-display setup: a "PC Monitor" (active/primary)
    // and a "Living Room TV" (inactive), mirroring the target use case.
    MockBackend();

    // Creates a backend seeded with an explicit display list.
    explicit MockBackend(std::vector<DisplayInfo> displays);

    std::vector<DisplayInfo> list() override;
    bool apply(const SwitchRequest& request, std::string* error) override;
    const char* name() const override { return "mock"; }

private:
    std::vector<DisplayInfo> displays_;
    std::mutex mutex_;
};

}  // namespace hdmi
