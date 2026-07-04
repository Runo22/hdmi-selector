#ifdef _WIN32

#include "WindowsBackend.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
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

// GDI device name ("\\.\DISPLAY1") for a path's source, needed by the classic
// EnumDisplaySettings / ChangeDisplaySettingsEx mode APIs.
std::wstring sourceGdiName(const DISPLAYCONFIG_PATH_INFO& p) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME s = {};
    s.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    s.header.size = sizeof(s);
    s.header.adapterId = p.sourceInfo.adapterId;
    s.header.id = p.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&s.header) != ERROR_SUCCESS) return L"";
    return s.viewGdiDeviceName;
}

// All distinct 32-bpp modes the GDI device reports.
std::vector<DisplayMode> enumModes(const std::wstring& gdi) {
    std::vector<DisplayMode> out;
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(gdi.c_str(), i, &dm); ++i) {
        const bool interlaced = (dm.dmDisplayFlags & DM_INTERLACED) != 0;
        // Keep only progressive, 32-bpp modes with a real refresh rate (0/1 are
        // "use hardware default" placeholders Windows may return).
        if (dm.dmBitsPerPel >= 32 && !interlaced && dm.dmDisplayFrequency > 1) {
            DisplayMode m{static_cast<int>(dm.dmPelsWidth), static_cast<int>(dm.dmPelsHeight),
                          static_cast<int>(dm.dmDisplayFrequency)};
            const bool dup = std::any_of(out.begin(), out.end(), [&](const DisplayMode& e) {
                return e.width == m.width && e.height == m.height && e.hz == m.hz;
            });
            if (!dup) out.push_back(m);
        }
        dm = {};
        dm.dmSize = sizeof(dm);
    }
    return out;
}

int currentHz(const std::wstring& gdi) {
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(gdi.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        return static_cast<int>(dm.dmDisplayFrequency);
    }
    return 0;
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

        // Enumerate supported modes + current refresh via the GDI device name.
        if (info.active) {
            std::wstring gdi = sourceGdiName(p);
            if (!gdi.empty()) {
                info.modes = enumModes(gdi);
                info.refreshHz = currentHz(gdi);
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

bool WindowsBackend::setMode(const std::string& id, int width, int height, int hz,
                             std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    if (!queryAllPaths(paths, modes)) return fail("QueryDisplayConfig failed");

    // The mode APIs act on the GDI device of the currently-active source.
    std::wstring gdi;
    for (const auto& p : paths) {
        if (pathActive(p) && queryTarget(p).devicePath == id) {
            gdi = sourceGdiName(p);
            break;
        }
    }
    if (gdi.empty()) return fail("display is not active: " + id);

    // Resolve hz<=0 to the highest rate available at this resolution.
    int freq = hz;
    if (freq <= 0) {
        for (const auto& m : enumModes(gdi)) {
            if (m.width == width && m.height == height) freq = std::max(freq, m.hz);
        }
        if (freq <= 0) return fail("resolution not supported by this display");
    }

    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    dm.dmPelsWidth = static_cast<DWORD>(width);
    dm.dmPelsHeight = static_cast<DWORD>(height);
    dm.dmDisplayFrequency = static_cast<DWORD>(freq);

    // Validate before committing.
    if (ChangeDisplaySettingsExW(gdi.c_str(), &dm, nullptr, CDS_TEST, nullptr) !=
        DISP_CHANGE_SUCCESSFUL) {
        return fail("mode not supported (validation failed)");
    }
    if (ChangeDisplaySettingsExW(gdi.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY, nullptr) !=
        DISP_CHANGE_SUCCESSFUL) {
        return fail("failed to apply mode");
    }
    return true;
}

}  // namespace hdmi

#endif  // _WIN32
