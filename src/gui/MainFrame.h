#pragma once

#include <wx/wx.h>
#include <wx/taskbar.h>
#include <wx/timer.h>

#include <memory>
#include <string>
#include <vector>

#include "Widgets.h"
#include "hdmi/Display.h"
#include "hdmi/DisplayManager.h"
#include "hdmi/RestServer.h"

namespace hdmi {

class TrayIcon;

// Main application window / control center.
//
// The primary interaction is a horizontal row of display "cards"; clicking one
// makes it the sole active display (Exclusive switch) - the 99% use case.
// Extend / Duplicate are secondary header chips so they don't compete
// visually with the main action. There is no native menu bar: Windows can't
// theme it dark, so app settings (theme, autostart, exit) live in the same
// custom-drawn side drawer used for per-display resolution/refresh options.
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
    void setTheme(ui::ThemeMode mode);   // switch + persist the theme
    std::vector<DisplayInfo> displays() { return manager_.displays(); }

private:
    void rebuildCards();                 // re-query displays and redraw the row
    void refreshIfChanged();             // timer tick: rebuild only on change
    void applyTheme();                   // push current theme colours into widgets
    void openOptionsFor(const std::string& id);   // open/refresh the side drawer
    void configureDrawer(const std::string& id);  // (re)populate drawer for a display
    void openSettings();                 // open/refresh the settings side drawer
    // `id` is taken by value on purpose: applying a mode rebuilds the drawer,
    // which replaces the very callback that invoked this, freeing a captured id.
    void changeMode(std::string id, int w, int h, int hz);  // apply + verify
    static std::string signatureOf(const std::vector<DisplayInfo>& displays);
    void showError(const wxString& message);

    DisplayManager& manager_;
    RestConfig restConfig_;
    std::unique_ptr<RestServer> server_;

    wxPanel* cardRow_ = nullptr;         // holds the horizontal card sizer
    wxBoxSizer* cardSizer_ = nullptr;
    wxStaticText* title_ = nullptr;      // header widgets, recoloured on theme change
    wxStaticText* subtitle_ = nullptr;
    wxStaticText* statusText_ = nullptr; // footer REST status
    ui::OptionsPanel* drawer_ = nullptr; // collapsible resolution/refresh/settings panel
    std::string drawerId_;               // display the drawer is showing; empty if closed
                                          // or showing the app-settings panel instead

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
    void onLeftUp(wxTaskBarIconEvent&);
    MainFrame* frame_;
    std::vector<std::string> menuDisplayIds_;  // menu item index -> display id
};

}  // namespace hdmi
