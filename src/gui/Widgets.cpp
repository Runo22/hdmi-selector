#include "Widgets.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/settings.h>

#include <memory>
#include <string>

namespace hdmi::ui {

namespace {

// --- Palettes -------------------------------------------------------------
Theme makeLight() {
    Theme t;
    t.dark = false;
    t.windowBg = wxColour(245, 246, 248);
    t.cardBg = wxColour(255, 255, 255);
    t.cardHover = wxColour(250, 251, 252);
    t.cardBorder = wxColour(228, 230, 234);
    t.accent = wxColour(31, 168, 93);
    t.accentHover = wxColour(27, 150, 83);
    t.accentSoft = wxColour(233, 248, 239);
    t.textPrimary = wxColour(26, 28, 32);
    t.textGray = wxColour(139, 145, 154);
    t.badgeText = wxColour(255, 255, 255);
    t.glyphInactive = wxColour(176, 182, 190);
    t.shadowAlpha = 12;
    return t;
}

Theme makeDark() {
    Theme t;
    t.dark = true;
    t.windowBg = wxColour(30, 31, 34);
    t.cardBg = wxColour(43, 45, 49);
    t.cardHover = wxColour(50, 52, 57);
    t.cardBorder = wxColour(58, 61, 66);
    t.accent = wxColour(46, 190, 110);
    t.accentHover = wxColour(58, 205, 123);
    t.accentSoft = wxColour(30, 58, 42);
    t.textPrimary = wxColour(236, 237, 238);
    t.textGray = wxColour(154, 160, 166);
    t.badgeText = wxColour(255, 255, 255);
    t.glyphInactive = wxColour(107, 112, 120);
    t.shadowAlpha = 40;
    return t;
}

ThemeMode g_mode = ThemeMode::System;
Theme g_theme = makeLight();

// Draw text horizontally centred on cx at vertical position y.
void centeredText(wxGraphicsContext* gc, const wxString& s, const wxFont& font,
                  const wxColour& colour, double cx, double y) {
    gc->SetFont(font, colour);
    double tw = 0, th = 0, desc = 0, ext = 0;
    gc->GetTextExtent(s, &tw, &th, &desc, &ext);
    gc->DrawText(s, cx - tw / 2.0, y);
}
}  // namespace

bool systemIsDark() {
    return wxSystemSettings::GetAppearance().IsDark();
}

void setThemeMode(ThemeMode mode) {
    g_mode = mode;
    const bool dark = (mode == ThemeMode::Dark) ||
                      (mode == ThemeMode::System && systemIsDark());
    g_theme = dark ? makeDark() : makeLight();
}

ThemeMode themeMode() { return g_mode; }

const Theme& theme() { return g_theme; }

wxFont uiFont(int pointSize, wxFontWeight weight) {
    // FaceName is honoured where present (Segoe UI on Windows); otherwise
    // wxWidgets substitutes the platform default sans-serif.
    wxFont f(wxFontInfo(pointSize).FaceName("Segoe UI"));
    if (!f.IsOk()) f = wxFont(wxFontInfo(pointSize));
    f.SetWeight(weight);
    return f;
}

// ---------------------------------------------------------------------------
// DisplayCard
// ---------------------------------------------------------------------------

DisplayCard::DisplayCard(wxWindow* parent, const DisplayInfo& info,
                         std::function<void()> onActivate)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(170, 152)),
      info_(info),
      onActivate_(std::move(onActivate)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetMinSize(wxSize(170, 152));

    Bind(wxEVT_PAINT, &DisplayCard::onPaint, this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { hover_ = true; Refresh(); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hover_ = false; Refresh(); });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) { if (onActivate_) onActivate_(); });
}

void DisplayCard::drawGlyph(wxGraphicsContext* gc, double cx, double top,
                            const wxColour& colour) {
    const double sw = 48, sh = 30;  // screen
    gc->SetBrush(wxBrush(colour));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(cx - sw / 2, top, sw, sh, 4);
    // Neck + base of the stand.
    gc->DrawRectangle(cx - 4, top + sh, 8, 6);
    gc->DrawRoundedRectangle(cx - 12, top + sh + 6, 24, 4, 2);
    // A lighter "screen" inset for a bit of depth.
    wxColour inset(colour.Red(), colour.Green(), colour.Blue(), 90);
    gc->SetBrush(wxBrush(inset));
    gc->DrawRoundedRectangle(cx - sw / 2 + 4, top + 4, sw - 8, sh - 8, 2);
}

void DisplayCard::onPaint(wxPaintEvent&) {
    const Theme& th = theme();
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(th.windowBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const wxSize sz = GetClientSize();
    const double m = 4, r = 14;
    const double w = sz.GetWidth() - 2 * m, h = sz.GetHeight() - 2 * m;
    const double cx = sz.GetWidth() / 2.0;
    const bool active = info_.active;

    // Soft drop shadow.
    gc->SetBrush(wxBrush(wxColour(0, 0, 0, th.shadowAlpha)));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(m, m + 2, w, h, r);

    // Card body.
    wxColour body = active ? th.accentSoft : (hover_ ? th.cardHover : th.cardBg);
    gc->SetBrush(wxBrush(body));
    wxColour border = active ? th.accent : (hover_ ? th.accent : th.cardBorder);
    gc->SetPen(wxPen(border, active ? 2 : 1));
    gc->DrawRoundedRectangle(m, m, w, h, r);

    // Monitor glyph.
    drawGlyph(gc.get(), cx, m + 20, active ? th.accent : th.glyphInactive);

    // Active check badge (top-right).
    if (active) {
        const double bx = sz.GetWidth() - m - 18, by = m + 14, br = 9;
        gc->SetBrush(wxBrush(th.accent));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawEllipse(bx - br, by - br, br * 2, br * 2);
        wxGraphicsPath tick = gc->CreatePath();
        tick.MoveToPoint(bx - 4, by);
        tick.AddLineToPoint(bx - 1, by + 3);
        tick.AddLineToPoint(bx + 4, by - 3);
        gc->SetPen(wxPen(th.badgeText, 2));
        gc->StrokePath(tick);
    }

    // Text block. Backend strings are UTF-8, so decode them explicitly.
    centeredText(gc.get(), wxString::FromUTF8(info_.name), uiFont(12, wxFONTWEIGHT_BOLD),
                 th.textPrimary, cx, m + 74);

    if (!info_.connector.empty()) {
        centeredText(gc.get(), wxString::FromUTF8(info_.connector).Upper(), uiFont(8),
                     th.textGray, cx, m + 96);
    }

    wxString sub;
    if (active && info_.width > 0) {
        // "\xC3\x97" is the UTF-8 multiplication sign (×).
        sub = wxString::FromUTF8(std::to_string(info_.width) + " \xC3\x97 " +
                                 std::to_string(info_.height));
    } else if (!active) {
        sub = "Tap to activate";
    }
    if (!sub.empty()) {
        centeredText(gc.get(), sub, uiFont(8), active ? th.accent : th.textGray, cx, m + 116);
    }
}

// ---------------------------------------------------------------------------
// Chip
// ---------------------------------------------------------------------------

Chip::Chip(wxWindow* parent, const wxString& label, std::function<void()> onClick, bool filled)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 30)),
      label_(label),
      onClick_(std::move(onClick)),
      filled_(filled) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCursor(wxCURSOR_HAND));

    // Size to fit the label plus horizontal padding.
    wxClientDC dc(this);
    dc.SetFont(uiFont(9, wxFONTWEIGHT_MEDIUM));
    wxSize ext = dc.GetTextExtent(label_);
    SetMinSize(wxSize(ext.GetWidth() + 28, 30));

    Bind(wxEVT_PAINT, &Chip::onPaint, this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { hover_ = true; Refresh(); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { hover_ = false; Refresh(); });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) { if (onClick_) onClick_(); });
}

void Chip::onPaint(wxPaintEvent&) {
    const Theme& t = theme();
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(t.windowBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const wxSize sz = GetClientSize();
    const double r = sz.GetHeight() / 2.0;

    wxColour bg, fg, border;
    if (filled_) {
        bg = hover_ ? t.accentHover : t.accent;
        fg = t.badgeText;
        border = bg;
    } else {
        bg = hover_ ? t.cardHover : t.cardBg;
        fg = hover_ ? t.accent : t.textPrimary;
        border = hover_ ? t.accent : t.cardBorder;
    }

    gc->SetBrush(wxBrush(bg));
    gc->SetPen(wxPen(border, 1));
    gc->DrawRoundedRectangle(0.5, 0.5, sz.GetWidth() - 1, sz.GetHeight() - 1, r);

    gc->SetFont(uiFont(9, wxFONTWEIGHT_MEDIUM), fg);
    double tw = 0, th = 0, d = 0, e = 0;
    gc->GetTextExtent(label_, &tw, &th, &d, &e);
    gc->DrawText(label_, (sz.GetWidth() - tw) / 2.0, (sz.GetHeight() - th) / 2.0);
}

}  // namespace hdmi::ui
