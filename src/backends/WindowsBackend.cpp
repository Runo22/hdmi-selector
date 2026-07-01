#ifdef _WIN32

#include "WindowsBackend.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace hdmi {
namespace {

// UTF-16 -> UTF-8 for surfacing friendly names through the JSON/GUI layer.
std::string toUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

// Retrieve the full QDC_ALL_PATHS configuration (active *and* inactive paths).
bool queryAllPaths(std::vector<DISPLAYCONFIG_PATH_INFO>& paths,
                   std::vector<DISPLAYCONFIG_MODE_INFO>& modes) {
    UINT32 numPaths = 0, numModes = 0;
    LONG rc = GetDisplayConfigBufferSizes(QDC_ALL_PATHS, &numPaths, &numModes);
    if (rc != ERROR_SUCCESS) return false;

    paths.resize(numPaths);
    modes.resize(numModes);
    rc = QueryDisplayConfig(QDC_ALL_PATHS, &numPaths, paths.data(), &numModes, modes.data(),
                            nullptr);
    if (rc != ERROR_SUCCESS) return false;
    paths.resize(numPaths);
    modes.resize(numModes);
    return true;
}

// Human-readable connector type for the UI ("HDMI", "DisplayPort", ...).
std::string connectorName(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY tech) {
    switch (tech) {
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI: return "HDMI";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL:
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED: return "DisplayPort";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI: return "DVI";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15: return "VGA";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL: return "Internal";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SVIDEO: return "S-Video";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPOSITE_VIDEO: return "Composite";
        default: return "";
    }
}

// Resolved identity + display attributes for a path's target.
struct TargetInfo {
    std::string devicePath;  // stable id
    std::string friendly;    // monitor name
    std::string connector;   // port type
};

TargetInfo queryTarget(const DISPLAYCONFIG_PATH_INFO& path) {
    DISPLAYCONFIG_TARGET_DEVICE_NAME name = {};
    name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    name.header.size = sizeof(name);
    name.header.adapterId = path.targetInfo.adapterId;
    name.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS) return {};

    TargetInfo t;
    t.devicePath = toUtf8(name.monitorDevicePath);
    std::string friendly = toUtf8(name.monitorFriendlyDeviceName);
    t.friendly = friendly.empty() ? "Display" : friendly;
    t.connector = connectorName(name.outputTechnology);
    return t;
}

bool pathActive(const DISPLAYCONFIG_PATH_INFO& p) {
    return (p.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
}

}  // namespace

std::vector<DisplayInfo> WindowsBackend::list() {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    std::vector<DisplayInfo> result;
    if (!queryAllPaths(paths, modes)) return result;

    // A monitor may be reachable via several paths; keep one DisplayInfo per
    // device path, preferring an active path so its geometry is reported.
    std::unordered_map<std::string, size_t> byId;

    for (const auto& p : paths) {
        TargetInfo t = queryTarget(p);
        if (t.devicePath.empty()) continue;
        const std::string& id = t.devicePath;

        DisplayInfo info;
        info.id = id;
        info.name = t.friendly;
        info.connector = t.connector;
        info.active = pathActive(p);

        if (info.active && p.sourceInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
            p.sourceInfo.modeInfoIdx < modes.size()) {
            const auto& sm = modes[p.sourceInfo.modeInfoIdx];
            if (sm.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
                info.width = static_cast<int>(sm.sourceMode.width);
                info.height = static_cast<int>(sm.sourceMode.height);
                info.posX = sm.sourceMode.position.x;
                info.posY = sm.sourceMode.position.y;
                info.primary = (info.posX == 0 && info.posY == 0);
            }
        }

        auto it = byId.find(id);
        if (it == byId.end()) {
            byId[id] = result.size();
            result.push_back(info);
        } else if (info.active && !result[it->second].active) {
            result[it->second] = info;  // upgrade to the active representation
        }
    }
    return result;
}

bool WindowsBackend::apply(const SwitchRequest& request, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    if (!queryAllPaths(paths, modes)) return fail("QueryDisplayConfig failed");

    // Map each requested device path to the first candidate path index.
    std::unordered_map<std::string, size_t> firstPathForId;
    for (size_t i = 0; i < paths.size(); ++i) {
        std::string id = queryTarget(paths[i]).devicePath;
        if (!id.empty() && firstPathForId.find(id) == firstPathForId.end()) {
            firstPathForId[id] = i;
        }
    }
    for (const auto& id : request.activeIds) {
        if (firstPathForId.find(id) == firstPathForId.end()) {
            return fail("display not found on this system: " + id);
        }
    }

    // Start from a clean slate: deactivate every path and let Windows recompute
    // the modes for the ones we re-enable (SDC_ALLOW_CHANGES).
    for (auto& p : paths) {
        p.flags &= ~DISPLAYCONFIG_PATH_ACTIVE;
        p.sourceInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
        p.targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    }

    // Assign each requested display a source framebuffer:
    //   * Duplicate -> all targets share source id 0 (clone).
    //   * Extend    -> each target gets its own incrementing source id.
    //   * Exclusive -> a single target on source id 0.
    UINT32 nextSourceId = 0;
    for (const auto& id : request.activeIds) {
        auto& p = paths[firstPathForId[id]];
        p.flags |= DISPLAYCONFIG_PATH_ACTIVE;
        if (request.topology == Topology::Duplicate) {
            p.sourceInfo.id = 0;
        } else {
            p.sourceInfo.id = nextSourceId++;
        }
    }

    UINT32 flags = SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES |
                   SDC_SAVE_TO_DATABASE;
    LONG rc = SetDisplayConfig(static_cast<UINT32>(paths.size()), paths.data(),
                               static_cast<UINT32>(modes.size()), modes.data(), flags);
    if (rc != ERROR_SUCCESS) {
        return fail("SetDisplayConfig failed (code " + std::to_string(rc) + ")");
    }
    return true;
}

}  // namespace hdmi

#endif  // _WIN32
