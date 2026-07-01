#pragma once

#include <wx/wx.h>
#include <wx/taskbar.h>
#include <wx/timer.h>

#include <memory>
#include <string>
#include <vector>

#include "hdmi/Display.h"
#include "hdmi/DisplayManager.h"
#include "hdmi/RestServer.h"

namespace hdmi {

class TrayIcon;

// Main application window / control center.
//
// The primary interaction is a horizontal row of display "cards"; clicking one
// makes it the sole active display (Exclusive switch) - the 99% use case.
// Extend / Duplicate live in the "Advanced" menu so they don't compete
// visually with the main action.
//
// A lightweight timer polls the display configuration and refreshes the view
// automatically when displays are plugged in/out or changed elsewhere, giving
// the app a live "control center" feel.
class MainFrame : public wxFrame {
public:
    MainFrame(DisplayManager& manager, const RestConfig& restConfig);
    ~MainFrame() override;

    // Shared entry points used by both the window and the tray menu.
    void switchExclusive(const std::string& id);
    void applyTopology(Topology topology);
    void toggleAutostart();              // enable/disable launch-at-login
    std::vector<DisplayInfo> displays() { return manager_.displays(); }

private:
    void buildMenu();
    void rebuildCards();                 // re-query displays and redraw the row
    void refreshIfChanged();             // timer tick: rebuild only on change
    static std::string signatureOf(const std::vector<DisplayInfo>& displays);
    void showError(const wxString& message);

    DisplayManager& manager_;
    RestConfig restConfig_;
    std::unique_ptr<RestServer> server_;

    wxPanel* cardRow_ = nullptr;         // holds the horizontal card sizer
    wxBoxSizer* cardSizer_ = nullptr;
    wxStaticText* statusText_ = nullptr; // footer REST status

    wxTimer pollTimer_;
    std::string lastSignature_;          // detects display-config changes

    TrayIcon* tray_ = nullptr;           // owned by wx; destroyed via RemoveIcon
};

// System-tray icon with a right-click menu mirroring the main actions, so the
// user can switch displays without opening the window.
class TrayIcon : public wxTaskBarIcon {
public:
    explicit TrayIcon(MainFrame* frame);
    wxMenu* CreatePopupMenu() override;

private:
    void onLeftDClick(wxTaskBarIconEvent&);
    MainFrame* frame_;
    std::vector<std::string> menuDisplayIds_;  // menu item index -> display id
};

}  // namespace hdmi
