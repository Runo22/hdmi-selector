#include "hdmi/ModeStore.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace hdmi {

using nlohmann::json;
namespace fs = std::filesystem;

ModeStore::ModeStore(std::string path) : path_(std::move(path)) { load(); }

bool ModeStore::get(const std::string& id, DisplayMode& out) const {
    auto it = modes_.find(id);
    if (it == modes_.end()) return false;
    out = it->second;
    return true;
}

void ModeStore::set(const std::string& id, const DisplayMode& mode) {
    modes_[id] = mode;
    save();
}

void ModeStore::load() {
    std::ifstream in(path_);
    if (!in) return;  // no config yet - callers fall back to the current mode
    try {
        json j;
        in >> j;
        for (auto it = j.begin(); it != j.end(); ++it) {
            DisplayMode m;
            m.width = it.value().value("width", 0);
            m.height = it.value().value("height", 0);
            m.hz = it.value().value("hz", 0);
            if (m.width > 0 && m.height > 0) modes_[it.key()] = m;
        }
    } catch (const std::exception&) {
        // Corrupt file: ignore and start fresh.
    }
}

void ModeStore::save() const {
    json j = json::object();
    for (const auto& [id, m] : modes_) {
        j[id] = {{"width", m.width}, {"height", m.height}, {"hz", m.hz}};
    }
    std::error_code ec;
    fs::create_directories(fs::path(path_).parent_path(), ec);
    std::ofstream out(path_, std::ios::trunc);
    if (out) out << j.dump(2);
}

std::string ModeStore::defaultPath() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    std::string base = appdata ? appdata : ".";
    return base + "\\HdmiSelector\\modes.json";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::string base;
    if (xdg && *xdg) base = xdg;
    else if (home && *home) base = std::string(home) + "/.config";
    else base = ".";
    return base + "/hdmi-selector/modes.json";
#endif
}

}  // namespace hdmi
