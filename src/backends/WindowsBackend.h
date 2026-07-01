#pragma once

#ifdef _WIN32

#include <string>
#include <vector>

#include "hdmi/Display.h"
#include "hdmi/IDisplayBackend.h"

namespace hdmi {

// Real display backend for Windows, implemented with the Connecting and
// Configuring Displays (CCD) API: QueryDisplayConfig / SetDisplayConfig plus
// DisplayConfigGetDeviceInfo for friendly names.
//
// Stable display ids are the monitor device instance path
// (DISPLAYCONFIG_TARGET_DEVICE_NAME::monitorDevicePath), which persists across
// reboots even though the adapter LUIDs do not.
class WindowsBackend : public IDisplayBackend {
public:
    std::vector<DisplayInfo> list() override;
    bool apply(const SwitchRequest& request, std::string* error) override;
    const char* name() const override { return "windows"; }
};

}  // namespace hdmi

#endif  // _WIN32
