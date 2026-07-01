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
    DisplayCard(wxWindow* parent, const DisplayInfo& info, std::function<void()> onActivate);

private:
    void onPaint(wxPaintEvent&);
    void drawGlyph(wxGraphicsContext* gc, double cx, double top, const wxColour& colour);
    void animateTo(double target);      // start easing hover_ toward target

    DisplayInfo info_;
    std::function<void()> onActivate_;
    wxTimer anim_;
    double hover_ = 0.0;                 // animated hover amount, 0..1
    double hoverTarget_ = 0.0;
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
