#include "hdmi/DisplayManager.h"

#include <algorithm>
#include <unordered_set>

namespace hdmi {

DisplayManager::DisplayManager(std::unique_ptr<IDisplayBackend> backend)
    : backend_(std::move(backend)) {}

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
    return backend_->apply(request, error);
}

bool DisplayManager::activateExclusive(const std::string& id, std::string* error) {
    SwitchRequest req;
    req.topology = Topology::Exclusive;
    req.activeIds = {id};
    return apply(req, error);
}

}  // namespace hdmi
