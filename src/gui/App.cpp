#include <wx/wx.h>

#include <wx/config.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "MainFrame.h"
#include "Widgets.h"
#include "hdmi/DisplayManager.h"
#include "hdmi/RestServer.h"
#include "hdmi/backend_factory.h"

namespace {

std::string envOr(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

}  // namespace

// wxWidgets application object. Owns the DisplayManager for the whole process;
// the MainFrame borrows a reference to it.
class HdmiApp : public wxApp {
public:
    bool OnInit() override {
        SetVendorName("HdmiSelector");
        SetAppName("HdmiSelector");

        // Restore the saved theme preference (default: follow the OS). An
        // optional HDMI_THEME=system|light|dark env var overrides it.
        wxConfig config("HdmiSelector");
        long mode = static_cast<long>(hdmi::ui::ThemeMode::System);
        config.Read("ThemeMode", &mode, mode);
        const std::string envTheme = envOr("HDMI_THEME", "");
        if (envTheme == "light") mode = static_cast<long>(hdmi::ui::ThemeMode::Light);
        else if (envTheme == "dark") mode = static_cast<long>(hdmi::ui::ThemeMode::Dark);
        else if (envTheme == "system") mode = static_cast<long>(hdmi::ui::ThemeMode::System);
        hdmi::ui::setThemeMode(static_cast<hdmi::ui::ThemeMode>(mode));

        manager_ = std::make_unique<hdmi::DisplayManager>(hdmi::makeDefaultBackend());

        hdmi::RestConfig cfg;
        // Local-only by default. Set HDMI_HOST=0.0.0.0 (plus a token) to expose
        // the API to the LAN.
        cfg.host = envOr("HDMI_HOST", "127.0.0.1");
        cfg.port = std::atoi(envOr("HDMI_PORT", "8420").c_str());
        cfg.token = envOr("HDMI_TOKEN", "");

        auto* frame = new hdmi::MainFrame(*manager_, cfg);
        frame->Show(true);
        return true;
    }

private:
    std::unique_ptr<hdmi::DisplayManager> manager_;
};

wxIMPLEMENT_APP(HdmiApp);
