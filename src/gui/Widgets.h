#pragma once

#include <wx/wx.h>
#include <wx/timer.h>

#include <functional>

#include "hdmi/Display.h"

namespace hdmi::ui {

// ---- Theming -------------------------------------------------------------
// The palette is chosen at runtime so the app can follow the OS light/dark
// setting (or a manual override). Widgets read theme() at paint time.
enum class ThemeMode { System, Light, Dark };

struct Theme {
    wxColour windowBg, cardBg, cardHover, cardBorder;
    wxColour accent, accentHover, accentSoft;
    wxColour textPrimary, textGray, badgeText, glyphInactive;
    int shadowAlpha = 12;
    bool dark = false;
};

// True if the OS is currently using a dark appearance.
bool systemIsDark();

// Resolve `mode` (System consults the OS) and make it the active theme.
void setThemeMode(ThemeMode mode);
ThemeMode themeMode();

// The currently active, resolved theme.
const Theme& theme();

// A UI font, preferring Segoe UI (Windows) and falling back gracefully.
wxFont uiFont(int pointSize, wxFontWeight weight = wxFONTWEIGHT_NORMAL);

// ---- DisplayCard ---------------------------------------------------------
// A custom-drawn, clickable card representing one display. Shows a monitor
// glyph, the display name (dark, bold), the connector/port type (gray), and,
// when active, an accent treatment plus current resolution. Clicking it invokes
// the supplied callback (an exclusive switch).
class DisplayCard : public wxWindow {
public:
    // onActivate: exclusive-switch to this display.
    // onOptions: open the resolution/refresh side panel for this display
    //            (only reachable on an active card).
    DisplayCard(wxWindow* parent, const DisplayInfo& info, std::function<void()> onActivate,
                std::function<void()> onOptions);

private:
    void onPaint(wxPaintEvent&);
    void onLeftUp(wxMouseEvent&);
    void drawGlyph(wxGraphicsContext* gc, double cx, double top, const wxColour& colour);
    void animateTo(double target);      // start easing hover_ toward target
    wxRect optionsHotspot() const;      // clickable "options" region (active only)

    DisplayInfo info_;
    std::function<void()> onActivate_;
    std::function<void()> onOptions_;
    wxTimer anim_;
    double hover_ = 0.0;                 // animated hover amount, 0..1
    double hoverTarget_ = 0.0;
};

// Friendly label for common resolutions: "1080p", "2K", "4K", ... else WxH.
wxString resolutionLabel(int width, int height);

// A themed, collapsible side panel that slides in from the right. Shows
// either a display's resolution/refresh-rate picker or the app's settings
// (theme, autostart, exit), depending on which `configure*` call populated it.
class OptionsPanel : public wxPanel {
public:
    explicit OptionsPanel(wxWindow* parent);

    // Populate for a display. onSetMode(w,h,hz): hz>0 exact, 0 = best at size.
    void configure(const DisplayInfo& info, std::function<void(int, int, int)> onSetMode,
                   std::function<void()> onClose);

    // Populate for the app-settings panel (theme picker + autostart + exit).
    void configureSettings(ThemeMode mode, bool autostartSupported, bool autostartEnabled,
                           std::function<void(ThemeMode)> onSetTheme,
                           std::function<void()> onToggleAutostart, std::function<void()> onExit,
                           std::function<void()> onClose);

    void open();       // animate into view
    void close();      // animate out, then hide
    bool isOpen() const { return targetWidth_ > 0; }
    void applyTheme();

    static constexpr int kFullWidth = 236;

private:
    enum class Kind { Display, Settings };

    void rebuild();
    void rebuildDisplay();
    void rebuildSettings();
    void relayout();

    wxPanel* content_ = nullptr;
    wxBoxSizer* contentSizer_ = nullptr;
    Kind kind_ = Kind::Display;

    // Display-mode picker state.
    DisplayInfo info_;
    std::function<void(int, int, int)> onSetMode_;

    // Settings-panel state.
    ThemeMode settingsMode_ = ThemeMode::System;
    bool autostartSupported_ = false;
    bool autostartEnabled_ = false;
    std::function<void(ThemeMode)> onSetTheme_;
    std::function<void()> onToggleAutostart_;
    std::function<void()> onExit_;

    std::function<void()> onClose_;
    wxTimer anim_;
    double width_ = 0.0;
    double targetWidth_ = 0.0;
};

// ---- Chip ----------------------------------------------------------------
// A small, flat, rounded action button used for secondary/inline actions
// (Extend, Duplicate, Refresh). Either "ghost" (outlined) or "filled".
class Chip : public wxWindow {
public:
    Chip(wxWindow* parent, const wxString& label, std::function<void()> onClick,
         bool filled = false);

private:
    void onPaint(wxPaintEvent&);

    wxString label_;
    std::function<void()> onClick_;
    bool filled_ = false;
    bool hover_ = false;
};

}  // namespace hdmi::ui
