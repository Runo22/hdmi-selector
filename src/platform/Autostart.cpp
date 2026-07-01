#include "hdmi/Autostart.h"

#ifdef _WIN32
#include <windows.h>

#include <string>
#include <vector>
#endif

namespace hdmi::autostart {

#ifdef _WIN32

namespace {
constexpr const wchar_t* kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kValueName = L"HdmiSelector";

// Full path to the running executable, quoted for the Run entry.
std::wstring quotedExePath() {
    std::vector<wchar_t> buf(MAX_PATH);
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    while (len == buf.size()) {  // path was truncated; grow and retry
        buf.resize(buf.size() * 2);
        len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    }
    std::wstring path(buf.data(), len);
    return L"\"" + path + L"\"";
}
}  // namespace

bool isSupported() { return true; }

bool isEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0, size = 0;
    LONG rc = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

bool setEnabled(bool enable, std::string* error) {
    HKEY key;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                              &key, nullptr);
    if (rc != ERROR_SUCCESS) {
        if (error) *error = "could not open Run registry key";
        return false;
    }

    if (enable) {
        const std::wstring cmd = quotedExePath();
        rc = RegSetValueExW(key, kValueName, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kValueName);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;  // already absent
    }
    RegCloseKey(key);

    if (rc != ERROR_SUCCESS) {
        if (error) *error = "could not update Run registry value";
        return false;
    }
    return true;
}

#else  // Non-Windows: stubs so the GUI compiles/tests on other platforms.

bool isSupported() { return false; }
bool isEnabled() { return false; }
bool setEnabled(bool, std::string* error) {
    if (error) *error = "launch-at-login is only supported on Windows";
    return false;
}

#endif  // _WIN32

}  // namespace hdmi::autostart
