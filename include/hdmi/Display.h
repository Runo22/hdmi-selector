#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hdmi {

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

    bool active = false;    // currently part of the Windows desktop
    bool primary = false;   // is the primary display

    // Current mode / placement (only meaningful when active).
    int width = 0;
    int height = 0;
    int posX = 0;
    int posY = 0;
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

}  // namespace hdmi
