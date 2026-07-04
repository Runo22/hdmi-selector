#include "MainFrame.h"

#include <wx/config.h>
#include <wx/menu.h>

#include <string>
#include <vector>

#include "Widgets.h"
#include "../../resources/app.xpm"  // provides appicon_xpm
#include "hdmi/Autostart.h"

#ifdef __WXMSW__
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {
// SetPreferredAppMode/FlushMenuThemes are undocumented uxtheme.dll exports
// (ordinals 135/136, stable since Windows 10 1809) that switch the classic
// Win32 menu bar and its dropdowns to dark; there is no public API for this.
enum PreferredAppMode { kDefault, kAllowDark, kForceDark, kForceLight, kMax };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using FlushMenuThemesFn = void(WINAPI*)();

void applyWin32MenuDarkMode(bool dark) {
    static HMODULE uxtheme =
        LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) return;
    static auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    static auto flushMenuThemes =
        reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (setPreferredAppMode) setPreferredAppMode(dark ? kForceDark : kForceLight);
    if (flushMenuThemes) flushMenuThemes();
}
}  // namespace
#endif

using namespace hdmi::ui;

namespace hdmi {

namespace {
enum {
    ID_Refresh = wxID_HIGHEST + 1,  // kept as an accelerator target (F5); no menu item owns it

    // Tray menu ids.
    ID_TrayOpen,
    ID_TrayExtend,
    ID_TrayDuplicate,
    ID_TrayAutostart,
    ID_TrayExit,
    ID_TrayDisplayBase = wxID_HIGHEST + 100,  // + display index
};

constexpr int kPollIntervalMs = 1500;       // display-change detection cadence
}  // namespace

// ---------------------------------------------------------------------------
// MainFrame
// ---------------------------------------------------------------------------

MainFrame::MainFrame(DisplayManager& manager, const RestConfig& restConfig)
    : wxFrame(nullptr, wxID_ANY, "HDMI Selector", wxDefaultPosition, wxSize(620, 340)),
      manager_(manager),
      restConfig_(restConfig),
      pollTimer_(this) {
    SetIcon(wxICON(appicon));

    // No native menu bar (Windows can't theme its top strip dark; see the
    // class comment). F5/Ctrl+Q still work via this accelerator table.
    wxAcceleratorEntry accel[2];
    accel[0].Set(wxACCEL_NORMAL, WXK_F5, ID_Refresh);
    accel[1].Set(wxACCEL_CTRL, static_cast<int>('Q'), wxID_EXIT);
    SetAcceleratorTable(wxAcceleratorTable(2, accel));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { rebuildCards(); }, ID_Refresh);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Destroy(); }, wxID_EXIT);

    auto* leftCol = new wxBoxSizer(wxVERTICAL);

    // ---- Header: title + subtitle on the left, inline action chips right ----
    auto* header = new wxBoxSizer(wxHORIZONTAL);

    auto* titles = new wxBoxSizer(wxVERTICAL);
    title_ = new wxStaticText(this, wxID_ANY, "Displays");
    title_->SetFont(uiFont(16, wxFONTWEIGHT_BOLD));
    subtitle_ = new wxStaticText(this, wxID_ANY, "Choose the screen you want to use");
    subtitle_->SetFont(uiFont(9));
    titles->Add(title_);
    titles->Add(subtitle_, 0, wxTOP, 2);

    header->Add(titles, 1, wxALIGN_CENTER_VERTICAL);
    header->AddStretchSpacer();
    // Secondary options live here as small chips instead of buried in a menu.
    header->Add(new Chip(this, "Extend", [this] { applyTopology(Topology::Extend); }), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    header->Add(new Chip(this, "Duplicate", [this] { applyTopology(Topology::Duplicate); }), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    header->Add(new Chip(this, "Refresh", [this] { rebuildCards(); }), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    header->Add(new Chip(this, "Settings", [this] { openSettings(); }), 0,
                wxALIGN_CENTER_VERTICAL);

    leftCol->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 18);

    // ---- Card row (cards are centred within this panel) ----
    cardRow_ = new wxPanel(this, wxID_ANY);
    cardSizer_ = new wxBoxSizer(wxHORIZONTAL);
    cardRow_->SetSizer(cardSizer_);
    leftCol->Add(cardRow_, 1, wxEXPAND | wxLEFT | wxRIGHT, 14);

    // ---- Footer: subtle REST status with a live dot ----
    statusText_ = new wxStaticText(this, wxID_ANY, "");
    statusText_->SetFont(uiFont(8));
    leftCol->Add(statusText_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM | wxTOP, 16);

    // Collapsible options drawer on the right; hidden until a card's ••• is used.
    drawer_ = new ui::OptionsPanel(this);

    auto* outer = new wxBoxSizer(wxHORIZONTAL);
    outer->Add(leftCol, 1, wxEXPAND);
    outer->Add(drawer_, 0, wxEXPAND);
    SetSizer(outer);

    // Re-follow the OS when it flips light/dark, if we're in System mode.
    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& e) {
        if (ui::themeMode() == ui::ThemeMode::System) {
            ui::setThemeMode(ui::ThemeMode::System);
            applyTheme();
        }
        e.Skip();
    });

    // Start the REST server alongside the GUI so the app can be driven over LAN.
    // Note: build UTF-8 text explicitly (FromUTF8). Passing non-ASCII directly
    // through wxString::Format is locale-dependent and unsafe.
    server_ = std::make_unique<RestServer>(manager_, restConfig_);
    if (server_->start()) {
        const std::string s = "\xE2\x97\x8F  REST API  \xC2\xB7  http://" + restConfig_.host +
                              ":" + std::to_string(restConfig_.port) + "  \xC2\xB7  backend: " +
                              manager_.backendName();
        statusText_->SetLabel(wxString::FromUTF8(s));
    } else {
        statusText_->SetLabel(wxString::FromUTF8("\xE2\x97\x8F  REST API failed to bind " +
                                                 restConfig_.host + ":" +
                                                 std::to_string(restConfig_.port)));
    }

    // Live refresh: rebuild the view whenever the display set changes.
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { refreshIfChanged(); });
    pollTimer_.Start(kPollIntervalMs);

    // Tray icon mirroring the controls.
    tray_ = new TrayIcon(this);
    tray_->SetIcon(wxICON(appicon), "HDMI Selector");

    // Closing the window hides to tray rather than exiting; use Settings > Exit
    // App (or the tray menu) to quit.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        if (e.CanVeto()) {
            Hide();
            e.Veto();
        } else {
            e.Skip();
        }
    });

    applyTheme();
    rebuildCards();
}

MainFrame::~MainFrame() {
    if (tray_) {
        tray_->RemoveIcon();
        delete tray_;  // wxTaskBarIcon is a wxEvtHandler, not a wxWindow
        tray_ = nullptr;
    }
}

namespace {
// Recursively repaint a window and all of its descendants.
void refreshTree(wxWindow* w) {
    w->Refresh();
    for (wxWindow* child : w->GetChildren()) refreshTree(child);
}
}  // namespace

void MainFrame::applyTheme() {
    const Theme& th = theme();
    SetBackgroundColour(th.windowBg);
    if (cardRow_) cardRow_->SetBackgroundColour(th.windowBg);
    if (title_) title_->SetForegroundColour(th.textPrimary);
    if (subtitle_) subtitle_->SetForegroundColour(th.textGray);
    if (statusText_) statusText_->SetForegroundColour(th.textGray);
    if (drawer_) drawer_->applyTheme();
    refreshTree(this);

#ifdef __WXMSW__
    // wxWidgets only paints the client area; the title bar is drawn by DWM
    // and needs this explicit opt-in to switch to a dark caption.
    BOOL dark = th.dark ? TRUE : FALSE;
    HWND hwnd = static_cast<HWND>(GetHandle());
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    // There's no frame menu bar to theme, but this also darkens the tray
    // icon's right-click popup menu (a real Win32 popup, unlike a menu bar
    // strip, so it does support dark mode via this opt-in).
    applyWin32MenuDarkMode(th.dark);
#endif
}

void MainFrame::setTheme(ui::ThemeMode mode) {
    ui::setThemeMode(mode);
    wxConfig config("HdmiSelector");
    config.Write("ThemeMode", static_cast<int>(mode));
    applyTheme();
}

void MainFrame::toggleAutostart() {
    const bool enable = !autostart::isEnabled();
    std::string err;
    if (!autostart::setEnabled(enable, &err)) {
        showError("Could not update startup setting: " + wxString::FromUTF8(err));
    }
}

std::string MainFrame::signatureOf(const std::vector<DisplayInfo>& displays) {
    std::string sig;
    for (const auto& d : displays) {
        sig += d.id;
        sig += d.active ? "|A" : "|-";
        sig += d.primary ? "P;" : ";";
    }
    return sig;
}

void MainFrame::refreshIfChanged() {
    const std::string sig = signatureOf(manager_.displays());
    if (sig != lastSignature_) {
        rebuildCards();  // updates lastSignature_
    }
}

void MainFrame::rebuildCards() {
    cardSizer_->Clear(/*delete_windows=*/true);

    const auto displays = manager_.displays();
    lastSignature_ = signatureOf(displays);

    // Leading + trailing stretch spacers centre the content horizontally; the
    // per-item ALIGN_CENTER_VERTICAL centres it in the row's height.
    cardSizer_->AddStretchSpacer();
    if (displays.empty()) {
        auto* empty = new wxStaticText(cardRow_, wxID_ANY, "No displays detected");
        empty->SetFont(uiFont(10));
        empty->SetForegroundColour(theme().textGray);
        cardSizer_->Add(empty, 0, wxALIGN_CENTER_VERTICAL | wxALL, 12);
    }
    bool drawerDisplayActive = false;
    for (const auto& d : displays) {
        const std::string id = d.id;
        auto* card = new DisplayCard(cardRow_, d, [this, id] { switchExclusive(id); },
                                     [this, id] { openOptionsFor(id); });
        cardSizer_->Add(card, 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);
        if (id == drawerId_ && d.active) drawerDisplayActive = true;
    }
    cardSizer_->AddStretchSpacer();

    // Keep the drawer in sync: refresh its contents, or close it if its display
    // is gone/inactive.
    if (drawer_ && drawer_->isOpen() && !drawerId_.empty()) {
        if (drawerDisplayActive) configureDrawer(drawerId_);
        else { drawer_->close(); drawerId_.clear(); }
    }

    cardRow_->Layout();
    Layout();
}

void MainFrame::switchExclusive(const std::string& id) {
    std::string err;
    if (!manager_.activateExclusive(id, &err)) {
        showError("Could not switch: " + wxString::FromUTF8(err));
    }
    rebuildCards();
}

void MainFrame::applyTopology(Topology topology) {
    // Apply across every currently-connected display.
    SwitchRequest req;
    req.topology = topology;
    for (const auto& d : manager_.displays()) req.activeIds.push_back(d.id);

    if (req.activeIds.size() < 2) {
        showError("Need at least two connected displays for this mode.");
        return;
    }

    std::string err;
    if (!manager_.apply(req, &err)) {
        showError("Could not apply " + wxString::FromUTF8(topologyToString(topology)) + ": " +
                  wxString::FromUTF8(err));
    }
    rebuildCards();
}

void MainFrame::configureDrawer(const std::string& id) {
    // Find the fresh DisplayInfo for `id`.
    DisplayInfo info;
    bool found = false;
    for (const auto& d : manager_.displays()) {
        if (d.id == id) { info = d; found = true; break; }
    }
    if (!found) { drawer_->close(); drawerId_.clear(); return; }

    drawer_->configure(
        info,
        [this, id](int w, int h, int hz) { changeMode(id, w, h, hz); },
        [this] { drawer_->close(); drawerId_.clear(); });
}

void MainFrame::openOptionsFor(const std::string& id) {
    drawerId_ = id;
    configureDrawer(id);
    drawer_->open();
}

void MainFrame::openSettings() {
    drawerId_.clear();
    drawer_->configureSettings(
        ui::themeMode(), autostart::isSupported(), autostart::isEnabled(),
        [this](ui::ThemeMode mode) {
            setTheme(mode);
            // Rebuild after this event finishes: setTheme->applyTheme() would
            // otherwise re-theme (but not re-select) the chip mid-click.
            CallAfter([this] { openSettings(); });
        },
        [this] {
            toggleAutostart();
            CallAfter([this] { openSettings(); });
        },
        [this] { Destroy(); }, [this] { drawer_->close(); });
    drawer_->open();
}

void MainFrame::changeMode(std::string id, int w, int h, int hz) {
    std::string err;
    if (!manager_.setMode(id, w, h, hz, &err)) {
        showError("Could not apply mode: " + wxString::FromUTF8(err));
    }
    // Rebuild after the current event finishes: this callback runs from a chip
    // inside the drawer, and rebuildCards() re-populates (deletes) that chip.
    // setMode already verified the change actually took effect.
    CallAfter([this] { rebuildCards(); });
}

void MainFrame::showError(const wxString& message) {
    wxMessageBox(message, "HDMI Selector", wxOK | wxICON_ERROR, this);
}

// ---------------------------------------------------------------------------
// TrayIcon
// ---------------------------------------------------------------------------

TrayIcon::TrayIcon(MainFrame* frame) : frame_(frame) {
    // A single left click restores the window (matches the common tray-icon
    // convention on Windows); right click still shows the actions menu.
    Bind(wxEVT_TASKBAR_LEFT_UP, &TrayIcon::onLeftUp, this);

    // A single handler dispatches every menu id to the right action.
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
        const int id = e.GetId();
        if (id == ID_TrayOpen) {
            frame_->Show(true);
            frame_->Raise();
        } else if (id == ID_TrayExtend) {
            frame_->applyTopology(Topology::Extend);
        } else if (id == ID_TrayDuplicate) {
            frame_->applyTopology(Topology::Duplicate);
        } else if (id == ID_TrayAutostart) {
            frame_->toggleAutostart();
        } else if (id == ID_TrayExit) {
            frame_->Destroy();
        } else if (id >= ID_TrayDisplayBase) {
            const size_t idx = static_cast<size_t>(id - ID_TrayDisplayBase);
            if (idx < menuDisplayIds_.size()) {
                frame_->switchExclusive(menuDisplayIds_[idx]);
            }
        }
    });
}

void TrayIcon::onLeftUp(wxTaskBarIconEvent&) {
    frame_->Show(true);
    frame_->Raise();
}

wxMenu* TrayIcon::CreatePopupMenu() {
    auto* menu = new wxMenu();

    // One radio-style entry per display; the active one is checked. Selecting
    // an entry performs an exclusive switch.
    menuDisplayIds_.clear();
    const auto displays = frame_->displays();
    for (const auto& d : displays) {
        const int id = ID_TrayDisplayBase + static_cast<int>(menuDisplayIds_.size());
        wxString label = "Switch to " + d.name;
        auto* item = menu->AppendCheckItem(id, label);
        item->Check(d.active);
        menuDisplayIds_.push_back(d.id);
    }

    menu->AppendSeparator();
    auto* advanced = new wxMenu();
    advanced->Append(ID_TrayExtend, "Extend all displays");
    advanced->Append(ID_TrayDuplicate, "Duplicate all displays");
    menu->AppendSubMenu(advanced, "Advanced");

    menu->AppendSeparator();
    if (autostart::isSupported()) {
        auto* item = menu->AppendCheckItem(ID_TrayAutostart, "Start with Windows");
        item->Check(autostart::isEnabled());
    }
    menu->Append(ID_TrayOpen, "Open HDMI Selector");
    menu->Append(ID_TrayExit, "Exit");
    return menu;
}

}  // namespace hdmi
