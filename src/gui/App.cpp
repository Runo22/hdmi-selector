#include <wx/wx.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "MainFrame.h"
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
