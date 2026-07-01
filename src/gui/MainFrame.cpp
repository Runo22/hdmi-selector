#include "MainFrame.h"

#include <wx/config.h>
#include <wx/menu.h>

#include <string>
#include <vector>

#include "Widgets.h"
#include "../../resources/app.xpm"  // provides appicon_xpm
#include "hdmi/Autostart.h"

using namespace hdmi::ui;

namespace hdmi {

namespace {
enum {
    ID_Refresh = wxID_HIGHEST + 1,
    ID_ExtendBoth,
    ID_DuplicateBoth,
    ID_ToggleAutostart,
    ID_ThemeSystem,
    ID_ThemeLight,
    ID_ThemeDark,

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
    buildMenu();

    auto* root = new wxBoxSizer(wxVERTICAL);

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
                wxALIGN_CENTER_VERTICAL);

    root->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 18);

    // ---- Card row (cards are centred within this panel) ----
    cardRow_ = new wxPanel(this, wxID_ANY);
    cardSizer_ = new wxBoxSizer(wxHORIZONTAL);
    cardRow_->SetSizer(cardSizer_);
    root->Add(cardRow_, 1, wxEXPAND | wxLEFT | wxRIGHT, 14);

    // ---- Footer: subtle REST status with a live dot ----
    statusText_ = new wxStaticText(this, wxID_ANY, "");
    statusText_->SetFont(uiFont(8));
    root->Add(statusText_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM | wxTOP, 16);

    SetSizer(root);

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

    // Closing the window hides to tray rather than exiting; use File > Exit to quit.
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

void MainFrame::buildMenu() {
    auto* menuBar = new wxMenuBar();

    auto* fileMenu = new wxMenu();
    fileMenu->Append(ID_Refresh, "&Refresh\tF5", "Re-scan connected displays");
    if (autostart::isSupported()) {
        auto* item = fileMenu->AppendCheckItem(ID_ToggleAutostart, "Start with &Windows",
                                               "Launch automatically at login (to the tray)");
        item->Check(autostart::isEnabled());
    }
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "E&xit\tCtrl+Q");
    menuBar->Append(fileMenu, "&File");

    // Extend / Duplicate are deliberately kept out of the main view - they are
    // the rare alternatives to the default exclusive switch.
    auto* advanced = new wxMenu();
    advanced->Append(ID_ExtendBoth, "&Extend across all displays",
                     "Use every connected display as one large desktop");
    advanced->Append(ID_DuplicateBoth, "&Duplicate on all displays",
                     "Mirror the same image on every connected display");
    menuBar->Append(advanced, "&Advanced");

    // View > Theme (System / Light / Dark), reflecting the current selection.
    auto* view = new wxMenu();
    auto* themeMenu = new wxMenu();
    themeMenu->AppendRadioItem(ID_ThemeSystem, "&System", "Follow the OS light/dark setting");
    themeMenu->AppendRadioItem(ID_ThemeLight, "&Light");
    themeMenu->AppendRadioItem(ID_ThemeDark, "&Dark");
    switch (ui::themeMode()) {
        case ui::ThemeMode::System: themeMenu->Check(ID_ThemeSystem, true); break;
        case ui::ThemeMode::Light: themeMenu->Check(ID_ThemeLight, true); break;
        case ui::ThemeMode::Dark: themeMenu->Check(ID_ThemeDark, true); break;
    }
    view->AppendSubMenu(themeMenu, "&Theme");
    menuBar->Append(view, "&View");

    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { rebuildCards(); }, ID_Refresh);
    // Real quit: stop vetoing close, then destroy.
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Destroy(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyTopology(Topology::Extend); },
         ID_ExtendBoth);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyTopology(Topology::Duplicate); },
         ID_DuplicateBoth);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { toggleAutostart(); }, ID_ToggleAutostart);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setTheme(ui::ThemeMode::System); },
         ID_ThemeSystem);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setTheme(ui::ThemeMode::Light); }, ID_ThemeLight);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setTheme(ui::ThemeMode::Dark); }, ID_ThemeDark);
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
    refreshTree(this);
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
        showError(wxString::Format("Could not update startup setting: %s", err));
    }
    // Reflect the (possibly unchanged) real state back into the menu checkbox.
    if (wxMenuBar* bar = GetMenuBar()) {
        if (wxMenuItem* item = bar->FindItem(ID_ToggleAutostart)) {
            item->Check(autostart::isEnabled());
        }
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
    for (const auto& d : displays) {
        const std::string id = d.id;
        auto* card = new DisplayCard(cardRow_, d, [this, id] { switchExclusive(id); });
        cardSizer_->Add(card, 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);
    }
    cardSizer_->AddStretchSpacer();

    cardRow_->Layout();
    Layout();
}

void MainFrame::switchExclusive(const std::string& id) {
    std::string err;
    if (!manager_.activateExclusive(id, &err)) {
        showError(wxString::Format("Could not switch: %s", err));
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
        showError(wxString::Format("Could not apply %s: %s", topologyToString(topology), err));
    }
    rebuildCards();
}

void MainFrame::showError(const wxString& message) {
    wxMessageBox(message, "HDMI Selector", wxOK | wxICON_ERROR, this);
}

// ---------------------------------------------------------------------------
// TrayIcon
// ---------------------------------------------------------------------------

TrayIcon::TrayIcon(MainFrame* frame) : frame_(frame) {
    Bind(wxEVT_TASKBAR_LEFT_DCLICK, &TrayIcon::onLeftDClick, this);

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

void TrayIcon::onLeftDClick(wxTaskBarIconEvent&) {
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
