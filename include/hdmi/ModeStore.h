#pragma once

#include <string>
#include <unordered_map>

#include "hdmi/Display.h"

namespace hdmi {

// Persists the chosen display mode (resolution + refresh) per monitor, keyed by
// the monitor's stable id. Backed by a small JSON file so both the GUI and the
// headless server remember selections across restarts.
//
// Only monitors the user has explicitly configured are stored; a monitor with
// no saved entry keeps whatever mode it currently has.
class ModeStore {
public:
    explicit ModeStore(std::string path);

    // Returns true and fills `out` if a mode is saved for `id`.
    bool get(const std::string& id, DisplayMode& out) const;

    // Saves (and immediately persists) the mode for `id`.
    void set(const std::string& id, const DisplayMode& mode);

    // Default per-user config file location for this application.
    static std::string defaultPath();

private:
    void load();
    void save() const;

    std::string path_;
    std::unordered_map<std::string, DisplayMode> modes_;
};

}  // namespace hdmi
