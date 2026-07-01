#include "hdmi/DisplayManager.h"

#include <algorithm>
#include <unordered_set>

namespace hdmi {

DisplayManager::DisplayManager(std::unique_ptr<IDisplayBackend> backend,
                               std::shared_ptr<ModeStore> store)
    : backend_(std::move(backend)), store_(std::move(store)) {}

std::vector<DisplayInfo> DisplayManager::displays() {
    std::lock_guard<std::mutex> lock(mutex_);
    return backend_->list();
}

const char* DisplayManager::backendName() const {
    return backend_->name();
}

bool DisplayManager::validateLocked(const SwitchRequest& request, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    if (request.activeIds.empty()) {
        return fail("no displays selected");
    }

    // Reject duplicate ids in the request.
    std::unordered_set<std::string> seen;
    for (const auto& id : request.activeIds) {
        if (!seen.insert(id).second) {
            return fail("display '" + id + "' listed more than once");
        }
    }

    if (request.topology == Topology::Exclusive && request.activeIds.size() != 1) {
        return fail("exclusive mode requires exactly one display");
    }
    if (request.topology != Topology::Exclusive && request.activeIds.size() < 2) {
        return fail(std::string(topologyToString(request.topology)) +
                    " mode requires at least two displays");
    }

    // Every requested id must correspond to a known display.
    const auto known = backend_->list();
    for (const auto& id : request.activeIds) {
        const bool exists = std::any_of(known.begin(), known.end(),
                                        [&](const DisplayInfo& d) { return d.id == id; });
        if (!exists) {
            return fail("unknown display id: " + id);
        }
    }
    return true;
}

bool DisplayManager::apply(const SwitchRequest& request, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validateLocked(request, error)) {
        return false;
    }
    if (!backend_->apply(request, error)) {
        return false;
    }
    // Newly-activated monitors adopt their saved mode (if any).
    applySavedModesLocked();
    return true;
}

bool DisplayManager::activateExclusive(const std::string& id, std::string* error) {
    SwitchRequest req;
    req.topology = Topology::Exclusive;
    req.activeIds = {id};
    return apply(req, error);
}

bool DisplayManager::setMode(const std::string& id, int width, int height, int hz,
                             std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (width <= 0 || height <= 0) {
        if (error) *error = "invalid resolution";
        return false;
    }
    if (!backend_->setMode(id, width, height, hz, error)) return false;

    // Verify the change actually took effect (some drivers silently ignore).
    for (const auto& d : backend_->list()) {
        if (d.id != id) continue;
        const bool resOk = (d.width == width && d.height == height);
        const bool hzOk = (hz <= 0) || (d.refreshHz == hz);
        if (!resOk || !hzOk) {
            if (error) {
                *error = "mode was not applied (display is at " + std::to_string(d.width) +
                         "x" + std::to_string(d.height) + "@" + std::to_string(d.refreshHz) +
                         ")";
            }
            return false;
        }
        break;
    }
    persistCurrentLocked(id);
    return true;
}

bool DisplayManager::setMaxRefresh(const std::string& id, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& d : backend_->list()) {
        if (d.id != id) continue;
        if (d.width <= 0 || d.height <= 0) {
            if (error) *error = "display has no current resolution";
            return false;
        }
        // hz<=0 lets the backend pick the highest rate at this resolution.
        if (!backend_->setMode(id, d.width, d.height, 0, error)) return false;
        persistCurrentLocked(id);
        return true;
    }
    if (error) *error = "unknown display id: " + id;
    return false;
}

void DisplayManager::persistCurrentLocked(const std::string& id) {
    if (!store_) return;
    // Save the resolved mode actually in effect (backend may have picked hz).
    for (const auto& d : backend_->list()) {
        if (d.id == id) {
            store_->set(id, DisplayMode{d.width, d.height, d.refreshHz});
            return;
        }
    }
}

void DisplayManager::applySavedModes() {
    std::lock_guard<std::mutex> lock(mutex_);
    applySavedModesLocked();
}

void DisplayManager::applySavedModesLocked() {
    if (!store_) return;
    for (const auto& d : backend_->list()) {
        if (!d.active) continue;
        DisplayMode m;
        if (!store_->get(d.id, m)) continue;  // no saved config -> keep current
        if (m.width == d.width && m.height == d.height && m.hz == d.refreshHz) continue;
        std::string err;
        backend_->setMode(d.id, m.width, m.height, m.hz, &err);  // best effort
    }
}

}  // namespace hdmi
