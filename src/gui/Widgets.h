#pragma once

#include <wx/wx.h>

#include <functional>

#include "hdmi/Display.h"

namespace hdmi::ui {

// ---- Palette -------------------------------------------------------------
// A light, modern theme. Accent green matches the app icon.
inline const wxColour kWindowBg(245, 246, 248);
inline const wxColour kCardBg(255, 255, 255);
inline const wxColour kCardHover(250, 251, 252);
inline const wxColour kCardBorder(228, 230, 234);
inline const wxColour kAccent(31, 168, 93);
inline const wxColour kAccentSoft(233, 248, 239);
inline const wxColour kTextDark(26, 28, 32);
inline const wxColour kTextGray(139, 145, 154);
inline const wxColour kWhite(255, 255, 255);

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

    DisplayInfo info_;
    std::function<void()> onActivate_;
    bool hover_ = false;
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
