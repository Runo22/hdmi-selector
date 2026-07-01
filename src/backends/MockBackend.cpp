#include "MockBackend.h"

#include <algorithm>
#include <unordered_set>

namespace hdmi {

namespace {
DisplayInfo makeDisplay(std::string id, std::string name, std::string connector, bool active,
                        bool primary) {
    DisplayInfo d;
    d.id = std::move(id);
    d.name = std::move(name);
    d.connector = std::move(connector);
    d.active = active;
    d.primary = primary;
    d.width = active ? 1920 : 0;
    d.height = active ? 1080 : 0;
    return d;
}
}  // namespace

MockBackend::MockBackend() {
    displays_.push_back(makeDisplay("monitor", "PC Monitor", "DisplayPort",
                                    /*active=*/true, /*primary=*/true));
    displays_.push_back(makeDisplay("tv", "Living Room TV", "HDMI",
                                    /*active=*/false, /*primary=*/false));
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
            d.width = 1920;
            d.height = 1080;
            if (request.topology == Topology::Duplicate) {
                d.posX = 0;  // all cloned at the origin
                d.posY = 0;
            } else {
                d.posX = nextX;  // Exclusive (single) or Extend (tiled)
                d.posY = 0;
                nextX += d.width;
            }
        } else {
            d.width = 0;
            d.height = 0;
            d.posX = 0;
            d.posY = 0;
        }
    }
    (void)error;
    return true;
}

}  // namespace hdmi
