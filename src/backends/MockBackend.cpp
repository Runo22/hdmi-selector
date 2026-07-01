#include "MockBackend.h"

#include <algorithm>
#include <unordered_set>

namespace hdmi {

namespace {
DisplayInfo makeDisplay(std::string id, std::string name, std::string connector, bool active,
                        bool primary, std::vector<DisplayMode> modes) {
    DisplayInfo d;
    d.id = std::move(id);
    d.name = std::move(name);
    d.connector = std::move(connector);
    d.active = active;
    d.primary = primary;
    d.modes = std::move(modes);
    if (active && !d.modes.empty()) {
        d.width = d.modes.front().width;
        d.height = d.modes.front().height;
        d.refreshHz = d.modes.front().hz;
    }
    return d;
}
}  // namespace

MockBackend::MockBackend() {
    displays_.push_back(makeDisplay(
        "monitor", "PC Monitor", "DisplayPort", /*active=*/true, /*primary=*/true,
        {{1920, 1080, 144}, {1920, 1080, 120}, {1920, 1080, 90}, {1920, 1080, 60},
         {2560, 1440, 120}, {2560, 1440, 60}, {3840, 2160, 60}}));
    displays_.push_back(makeDisplay(
        "tv", "Living Room TV", "HDMI", /*active=*/false, /*primary=*/false,
        {{3840, 2160, 60}, {3840, 2160, 30}, {1920, 1080, 60}}));
}

MockBackend::MockBackend(std::vector<DisplayInfo> displays)
    : displays_(std::move(displays)) {}

std::vector<DisplayInfo> MockBackend::list() {
    std::lock_guard<std::mutex> lock(mutex_);
    return displays_;
}

bool MockBackend::apply(const SwitchRequest& request, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::unordered_set<std::string> wanted(request.activeIds.begin(),
                                                 request.activeIds.end());

    // The primary is the first requested display; for Exclusive that is the
    // single active one, for Extend it is the left-most desktop.
    const std::string& primaryId = request.activeIds.front();

    int nextX = 0;
    for (auto& d : displays_) {
        const bool active = wanted.count(d.id) > 0;
        d.active = active;
        d.primary = active && d.id == primaryId;
        if (active) {
            // Adopt the best supported mode if this display had none yet.
            if (d.width == 0 && !d.modes.empty()) {
                d.width = d.modes.front().width;
                d.height = d.modes.front().height;
                d.refreshHz = d.modes.front().hz;
            }
            if (request.topology == Topology::Duplicate) {
                d.posX = 0;  // all cloned at the origin
                d.posY = 0;
            } else {
                d.posX = nextX;  // Exclusive (single) or Extend (tiled)
                d.posY = 0;
                nextX += d.width;
            }
        } else {
            d.posX = 0;  // keep last resolution, just drop it from the desktop
            d.posY = 0;
        }
    }
    (void)error;
    return true;
}

bool MockBackend::setMode(const std::string& id, int width, int height, int hz,
                          std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& d : displays_) {
        if (d.id != id) continue;

        // Resolve hz<=0 to the highest available at this resolution.
        int chosen = hz;
        bool supported = false;
        for (const auto& m : d.modes) {
            if (m.width == width && m.height == height) {
                if (hz <= 0) {
                    chosen = std::max(chosen, m.hz);
                    supported = true;
                } else if (m.hz == hz) {
                    supported = true;
                    break;
                }
            }
        }
        if (!supported) {
            if (error) *error = "unsupported mode for this display";
            return false;
        }
        d.width = width;
        d.height = height;
        d.refreshHz = chosen;
        return true;
    }
    if (error) *error = "unknown display id: " + id;
    return false;
}

}  // namespace hdmi
