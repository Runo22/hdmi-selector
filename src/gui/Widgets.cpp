#include "Widgets.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/settings.h>

#include <algorithm>
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

// Linear interpolation between two colours (t in 0..1).
wxColour lerpColour(const wxColour& a, const wxColour& b, double t) {
    auto mix = [&](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(x + (static_cast<int>(y) - x) * t + 0.5);
    };
    return wxColour(mix(a.Red(), b.Red()), mix(a.Green(), b.Green()), mix(a.Blue(), b.Blue()));
}

// Smoothstep easing for a softer feel than a linear ramp.
double ease(double t) { return t * t * (3.0 - 2.0 * t); }

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

wxString resolutionLabel(int width, int height) {
    if (width == 7680 && height == 4320) return "8K";
    if (width == 3840 && height == 2160) return "4K";
    if (width == 3440 && height == 1440) return "UW 2K";
    if (width == 2560 && height == 1440) return "2K";
    if (width == 2560 && height == 1080) return "UW 1080p";
    if (width == 1920 && height == 1080) return "1080p";
    if (width == 1600 && height == 900) return "900p";
    if (width == 1366 && height == 768) return "768p";
    if (width == 1280 && height == 720) return "720p";
    // "\xC3\x97" is × in UTF-8; build via FromUTF8 (locale-safe).
    return wxString::FromUTF8(std::to_string(width) + "\xC3\x97" + std::to_string(height));
}

// ---------------------------------------------------------------------------
// DisplayCard
// ---------------------------------------------------------------------------

DisplayCard::DisplayCard(wxWindow* parent, const DisplayInfo& info,
                         std::function<void()> onActivate,
                         std::function<void(int, int, int)> onSetMode,
                         std::function<void()> onMaxHz)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(172, 164)),
      info_(info),
      onActivate_(std::move(onActivate)),
      onSetMode_(std::move(onSetMode)),
      onMaxHz_(std::move(onMaxHz)),
      anim_(this) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetMinSize(wxSize(172, 164));

    Bind(wxEVT_PAINT, &DisplayCard::onPaint, this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { animateTo(1.0); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { animateTo(0.0); });
    Bind(wxEVT_LEFT_UP, &DisplayCard::onLeftUp, this);
    // Right-click anywhere on an active card opens the options menu.
    Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent& e) {
        if (info_.active) showOptions(e.GetPosition());
    });

    // ~60 fps easing of the hover amount toward its target.
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        const double step = 0.16;
        if (hover_ < hoverTarget_) hover_ = std::min(hoverTarget_, hover_ + step);
        else if (hover_ > hoverTarget_) hover_ = std::max(hoverTarget_, hover_ - step);
        Refresh();
        if (hover_ == hoverTarget_) anim_.Stop();
    });
}

void DisplayCard::animateTo(double target) {
    hoverTarget_ = target;
    if (!anim_.IsRunning()) anim_.Start(16);
}

wxRect DisplayCard::optionsHotspot() const {
    const wxSize sz = GetClientSize();
    return wxRect(sz.GetWidth() - 42, sz.GetHeight() - 36, 34, 26);
}

void DisplayCard::onLeftUp(wxMouseEvent& e) {
    // On an active card, the bottom-right "•••" opens options; elsewhere (and
    // on inactive cards) a click switches to this display.
    if (info_.active && optionsHotspot().Contains(e.GetPosition())) {
        showOptions(e.GetPosition());
        return;
    }
    if (onActivate_) onActivate_();
}

void DisplayCard::showOptions(const wxPoint& pos) {
    // Unique resolutions (highest refresh each), largest first.
    std::vector<DisplayMode> res;
    for (const auto& m : info_.modes) {
        auto it = std::find_if(res.begin(), res.end(), [&](const DisplayMode& r) {
            return r.width == m.width && r.height == m.height;
        });
        if (it == res.end()) res.push_back({m.width, m.height, m.hz});
        else it->hz = std::max(it->hz, m.hz);
    }
    std::sort(res.begin(), res.end(), [](const DisplayMode& a, const DisplayMode& b) {
        return static_cast<long>(a.width) * a.height > static_cast<long>(b.width) * b.height;
    });

    // Highest refresh available at the current resolution.
    int maxHzHere = 0;
    for (const auto& m : info_.modes) {
        if (m.width == info_.width && m.height == info_.height) {
            maxHzHere = std::max(maxHzHere, m.hz);
        }
    }

    wxMenu menu;
    const int kResBase = 1000, kMaxHz = 2000;
    for (size_t i = 0; i < res.size(); ++i) {
        const auto& r = res[i];
        // Build with FromUTF8 (× is non-ASCII and unsafe in a Format string).
        wxString dims = wxString::FromUTF8(std::to_string(r.width) + "\xC3\x97" +
                                           std::to_string(r.height));
        wxString label = resolutionLabel(r.width, r.height) + "   (" + dims + ")";
        auto* item = menu.AppendRadioItem(kResBase + static_cast<int>(i), label);
        if (r.width == info_.width && r.height == info_.height) item->Check(true);
    }
    if (maxHzHere > 0) {
        menu.AppendSeparator();
        menu.Append(kMaxHz, wxString::Format("Set max refresh rate (%d Hz)", maxHzHere));
    }

    const int sel = GetPopupMenuSelectionFromUser(menu, pos);
    if (sel == wxID_NONE) return;
    if (sel == kMaxHz) {
        if (onMaxHz_) onMaxHz_();
    } else if (sel >= kResBase && sel < kMaxHz) {
        const auto& r = res[static_cast<size_t>(sel - kResBase)];
        if (onSetMode_) onSetMode_(r.width, r.height, 0);  // 0 => best refresh
    }
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
    const double t = ease(hover_);       // eased hover amount
    const double r = 14;
    const double mx = 6;                 // side margin
    const double top = 6 - 3.0 * t;      // card rises up to 3px on hover
    const double w = sz.GetWidth() - 2 * mx;
    const double h = sz.GetHeight() - 12 - 6;  // leave room below for shadow
    const double cx = sz.GetWidth() / 2.0;
    const bool active = info_.active;

    // Soft drop shadow that deepens as the card lifts.
    const int shadowAlpha = th.shadowAlpha + static_cast<int>(22 * t);
    const double shadowOff = 2 + 5.0 * t;
    gc->SetBrush(wxBrush(wxColour(0, 0, 0, shadowAlpha)));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(mx, top + shadowOff, w, h, r);

    // Card body: hover eases the fill and border toward the accent.
    wxColour body = active ? th.accentSoft : lerpColour(th.cardBg, th.cardHover, t);
    gc->SetBrush(wxBrush(body));
    wxColour border = active ? th.accent : lerpColour(th.cardBorder, th.accent, t);
    gc->SetPen(wxPen(border, active ? 2.0 : 1.0 + t));
    gc->DrawRoundedRectangle(mx, top, w, h, r);

    // Monitor glyph.
    drawGlyph(gc.get(), cx, top + 16, active ? th.accent : th.glyphInactive);

    // Active check badge (top-right).
    if (active) {
        const double bx = sz.GetWidth() - mx - 18, by = top + 10, br = 9;
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
                 th.textPrimary, cx, top + 74);

    if (!info_.connector.empty()) {
        centeredText(gc.get(), wxString::FromUTF8(info_.connector).Upper(), uiFont(8),
                     th.textGray, cx, top + 96);
    }

    wxString sub;
    if (active && info_.width > 0) {
        // "\xC3\x97" is × and "\xC2\xB7" is · in UTF-8.
        std::string s = std::to_string(info_.width) + " \xC3\x97 " + std::to_string(info_.height);
        if (info_.refreshHz > 0) s += "  \xC2\xB7  " + std::to_string(info_.refreshHz) + " Hz";
        sub = wxString::FromUTF8(s);
    } else if (!active) {
        sub = "Tap to activate";
    }
    if (!sub.empty()) {
        centeredText(gc.get(), sub, uiFont(8), active ? th.accent : th.textGray, cx, top + 116);
    }

    // Options affordance ("•••") bottom-right on active cards, following the
    // card's lifted position.
    if (active) {
        const double dotY = top + h - 16;
        const double dotCx = sz.GetWidth() - mx - 16;
        gc->SetBrush(wxBrush(th.textGray));
        gc->SetPen(*wxTRANSPARENT_PEN);
        for (int i = -1; i <= 1; ++i) {
            gc->DrawEllipse(dotCx + i * 6 - 1.5, dotY - 1.5, 3, 3);
        }
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
