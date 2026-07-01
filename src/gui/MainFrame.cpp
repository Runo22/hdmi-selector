#include "MainFrame.h"

#include <wx/menu.h>

#include <string>
#include <vector>

#include "../../resources/app.xpm"  // provides appicon_xpm
#include "hdmi/Autostart.h"

namespace hdmi {

namespace {
enum {
    ID_Refresh = wxID_HIGHEST + 1,
    ID_ExtendBoth,
    ID_DuplicateBoth,
    ID_ToggleAutostart,

    // Tray menu ids.
    ID_TrayOpen,
    ID_TrayExtend,
    ID_TrayDuplicate,
    ID_TrayAutostart,
    ID_TrayExit,
    ID_TrayDisplayBase = wxID_HIGHEST + 100,  // + display index
};

// Colours used to distinguish the active display card from inactive ones.
const wxColour kActiveBg(46, 125, 50);      // green
const wxColour kActiveFg(*wxWHITE);
const wxColour kInactiveBg(60, 63, 65);     // dark grey
const wxColour kInactiveFg(220, 220, 220);

constexpr int kPollIntervalMs = 1500;       // display-change detection cadence
}  // namespace

// ---------------------------------------------------------------------------
// MainFrame
// ---------------------------------------------------------------------------

MainFrame::MainFrame(DisplayManager& manager, const RestConfig& restConfig)
    : wxFrame(nullptr, wxID_ANY, "HDMI Selector", wxDefaultPosition, wxSize(640, 260)),
      manager_(manager),
      restConfig_(restConfig),
      pollTimer_(this) {
    SetIcon(wxICON(appicon));
    buildMenu();

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* heading = new wxStaticText(this, wxID_ANY, "Click a display to switch to it");
    wxFont hf = heading->GetFont();
    hf.SetPointSize(hf.GetPointSize() + 2);
    heading->SetFont(hf);
    root->Add(heading, 0, wxALL, 12);

    cardRow_ = new wxPanel(this, wxID_ANY);
    cardSizer_ = new wxBoxSizer(wxHORIZONTAL);
    cardRow_->SetSizer(cardSizer_);
    root->Add(cardRow_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    SetSizer(root);

    // Start the REST server alongside the GUI so the app can be driven over LAN.
    server_ = std::make_unique<RestServer>(manager_, restConfig_);
    wxString status;
    if (server_->start()) {
        status = wxString::Format("REST API: http://%s:%d   (backend: %s)",
                                  restConfig_.host, restConfig_.port, manager_.backendName());
    } else {
        status = wxString::Format("REST API failed to bind %s:%d",
                                  restConfig_.host, restConfig_.port);
    }
    CreateStatusBar();
    SetStatusText(status);

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

    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { rebuildCards(); }, ID_Refresh);
    // Real quit: stop vetoing close, then destroy.
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Destroy(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyTopology(Topology::Extend); },
         ID_ExtendBoth);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { applyTopology(Topology::Duplicate); },
         ID_DuplicateBoth);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { toggleAutostart(); }, ID_ToggleAutostart);
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

    if (displays.empty()) {
        cardSizer_->Add(new wxStaticText(cardRow_, wxID_ANY, "No displays detected"), 0,
                        wxALL, 8);
    }

    for (const auto& d : displays) {
        auto* card = new wxButton(cardRow_, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxSize(150, 110), wxBORDER_NONE);
        const bool active = d.active;
        card->SetBackgroundColour(active ? kActiveBg : kInactiveBg);
        card->SetForegroundColour(active ? kActiveFg : kInactiveFg);

        wxString label = d.name;
        if (active) {
            label += d.primary ? "\n\n[ ACTIVE - primary ]" : "\n\n[ ACTIVE ]";
        } else {
            label += "\n\n(click to switch)";
        }
        card->SetLabel(label);

        const std::string id = d.id;
        card->Bind(wxEVT_BUTTON, [this, id](wxCommandEvent&) { switchExclusive(id); });

        cardSizer_->Add(card, 0, wxALL, 8);
    }
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
