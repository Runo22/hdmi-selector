#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hdmi {

// A supported display mode: resolution + (integer) refresh rate.
struct DisplayMode {
    int width = 0;
    int height = 0;
    int hz = 0;
};

// How the desktop should be laid out across the selected displays.
enum class Topology {
    Exclusive,   // exactly one display active (the 99% use case)
    Extend,      // multiple displays, each its own desktop region
    Duplicate    // multiple displays showing the same (cloned) image
};

// A single physical display/output as discovered from the OS.
struct DisplayInfo {
    // Stable identifier used by the API and GUI to refer to this output.
    // On Windows this encodes the CCD adapter LUID + source id; the Mock
    // backend uses a simple synthetic id.
    std::string id;

    // Friendly, human-readable name (monitor model / EDID name when known).
    std::string name;

    // Physical connector/port type, e.g. "HDMI" or "DisplayPort". Shown in the
    // UI beneath the name. May be empty if the OS doesn't report it.
    std::string connector;

    // Display label for the connector, numbered when several displays share the
    // same type (e.g. "HDMI 1", "HDMI 2"). Filled by DisplayManager; falls back
    // to `connector` when unique. UI should prefer this.
    std::string connectorLabel;

    bool active = false;    // currently part of the Windows desktop
    bool primary = false;   // is the primary display

    // Current mode / placement (only meaningful when active).
    int width = 0;
    int height = 0;
    int refreshHz = 0;
    int posX = 0;
    int posY = 0;

    // Supported modes, for the resolution / refresh-rate pickers.
    std::vector<DisplayMode> modes;

    // The monitor's EDID-reported preferred (native) resolution, when known
    // (0 if not). Used to restore a display to its own best resolution after
    // it's re-activated, instead of leaving whatever the OS carried over from
    // whichever display was previously using the same output.
    int nativeWidth = 0;
    int nativeHeight = 0;
};

// A request to change which displays are active and how they are arranged.
struct SwitchRequest {
    Topology topology = Topology::Exclusive;
    // Displays that should end up active. For Exclusive this must contain
    // exactly one id; for Extend/Duplicate it should contain two or more.
    std::vector<std::string> activeIds;
};

inline const char* topologyToString(Topology t) {
    switch (t) {
        case Topology::Exclusive: return "exclusive";
        case Topology::Extend:    return "extend";
        case Topology::Duplicate: return "duplicate";
    }
    return "exclusive";
}

// Returns false if the string is not a recognised topology.
inline bool topologyFromString(const std::string& s, Topology& out) {
    if (s == "exclusive") { out = Topology::Exclusive; return true; }
    if (s == "extend")    { out = Topology::Extend;    return true; }
    if (s == "duplicate") { out = Topology::Duplicate; return true; }
    return false;
}

// Friendly label for common resolutions ("1080p", "2K", "4K", ...), else
// "WxH". Shared by the GUI's resolution picker and the REST API's status
// messages so both describe a mode the same way.
inline std::string resolutionLabel(int width, int height) {
    if (width == 7680 && height == 4320) return "8K";
    if (width == 3840 && height == 2160) return "4K";
    if (width == 3440 && height == 1440) return "UW 2K";
    if (width == 2560 && height == 1440) return "2K";
    if (width == 2560 && height == 1080) return "UW 1080p";
    if (width == 1920 && height == 1080) return "1080p";
    if (width == 1600 && height == 900) return "900p";
    if (width == 1366 && height == 768) return "768p";
    if (width == 1280 && height == 720) return "720p";
    return std::to_string(width) + "\xC3\x97" + std::to_string(height);  // "x" is U+00D7 in UTF-8
}

}  // namespace hdmi
