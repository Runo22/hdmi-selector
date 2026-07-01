#include "Widgets.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/settings.h>
#include <wx/wrapsizer.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

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

// Trim `s` with a trailing ellipsis so it fits within maxWidth for `font`.
wxString ellipsize(wxGraphicsContext* gc, const wxString& s, const wxFont& font, double maxWidth) {
    gc->SetFont(font, *wxBLACK);
    double w = 0, h = 0, d = 0, e = 0;
    gc->GetTextExtent(s, &w, &h, &d, &e);
    if (w <= maxWidth) return s;
    const wxString ell = wxString::FromUTF8("\xE2\x80\xA6");  // …
    wxString out = s;
    while (!out.empty()) {
        out.RemoveLast();
        gc->GetTextExtent(out + ell, &w, &h, &d, &e);
        if (w <= maxWidth) return out + ell;
    }
    return ell;
}

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
                         std::function<void()> onActivate, std::function<void()> onOptions)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(172, 164)),
      info_(info),
      onActivate_(std::move(onActivate)),
      onOptions_(std::move(onOptions)),
      anim_(this) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetMinSize(wxSize(172, 164));

    Bind(wxEVT_PAINT, &DisplayCard::onPaint, this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { animateTo(1.0); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { animateTo(0.0); });
    Bind(wxEVT_LEFT_UP, &DisplayCard::onLeftUp, this);
    // Right-click anywhere on an active card with modes opens the options panel.
    Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent&) {
        if (info_.active && !info_.modes.empty() && onOptions_) onOptions_();
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
    return wxRect(sz.GetWidth() - 40, 2, 34, 34);  // top-right corner
}

void DisplayCard::onLeftUp(wxMouseEvent& e) {
    // On an active card with modes, the top-right "•••" opens the options panel;
    // elsewhere (and on inactive cards) a click switches to this display.
    if (info_.active && !info_.modes.empty() && optionsHotspot().Contains(e.GetPosition())) {
        if (onOptions_) onOptions_();
        return;
    }
    if (onActivate_) onActivate_();
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
    wxColour pbg = GetParent() ? GetParent()->GetBackgroundColour() : th.windowBg;
    dc.SetBackground(wxBrush(pbg));
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

    // Active check badge (top-left, clear of the centred text and the ••• menu).
    if (active) {
        const double bx = mx + 16, by = top + 14, br = 9;
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

    // Text block. Backend strings are UTF-8, so decode them explicitly. The
    // name is ellipsized so a long monitor name can't overflow the card.
    const wxFont nameFont = uiFont(12, wxFONTWEIGHT_BOLD);
    wxString name = ellipsize(gc.get(), wxString::FromUTF8(info_.name), nameFont, w - 20);
    centeredText(gc.get(), name, nameFont, th.textPrimary, cx, top + 74);

    const std::string& conn =
        !info_.connectorLabel.empty() ? info_.connectorLabel : info_.connector;
    if (!conn.empty()) {
        centeredText(gc.get(), wxString::FromUTF8(conn).Upper(), uiFont(8), th.textGray, cx,
                     top + 96);
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

    // Options affordance ("•••") top-right on active cards that have adjustable
    // modes to offer.
    if (active && !info_.modes.empty()) {
        const double dotY = top + 14;
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
    // Clear with the parent's background so the chip blends on any surface.
    wxColour pbg = GetParent() ? GetParent()->GetBackgroundColour() : t.windowBg;
    dc.SetBackground(wxBrush(pbg));
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

// ---------------------------------------------------------------------------
// OptionsPanel (collapsible side drawer)
// ---------------------------------------------------------------------------

namespace {
// Unique resolutions in `modes`, largest first.
std::vector<DisplayMode> uniqueResolutions(const std::vector<DisplayMode>& modes) {
    std::vector<DisplayMode> res;
    for (const auto& m : modes) {
        auto it = std::find_if(res.begin(), res.end(), [&](const DisplayMode& r) {
            return r.width == m.width && r.height == m.height;
        });
        if (it == res.end()) res.push_back({m.width, m.height, m.hz});
        else it->hz = std::max(it->hz, m.hz);
    }
    // Ascending by pixel count so the row reads smallest -> 4K.
    std::sort(res.begin(), res.end(), [](const DisplayMode& a, const DisplayMode& b) {
        return static_cast<long>(a.width) * a.height < static_cast<long>(b.width) * b.height;
    });
    return res;
}

// Distinct refresh rates available at a given resolution, ascending.
std::vector<int> refreshRatesAt(const std::vector<DisplayMode>& modes, int w, int h) {
    std::vector<int> hz;
    for (const auto& m : modes) {
        if (m.width == w && m.height == h &&
            std::find(hz.begin(), hz.end(), m.hz) == hz.end()) {
            hz.push_back(m.hz);
        }
    }
    std::sort(hz.begin(), hz.end());
    return hz;
}
}  // namespace

OptionsPanel::OptionsPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY), anim_(this) {
    content_ = new wxPanel(this, wxID_ANY);
    contentSizer_ = new wxBoxSizer(wxVERTICAL);
    content_->SetSizer(contentSizer_);
    SetMinSize(wxSize(0, -1));
    Hide();

    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { relayout(); e.Skip(); });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        width_ += (targetWidth_ - width_) * 0.35;
        if (std::abs(targetWidth_ - width_) < 1.0) {
            width_ = targetWidth_;
            anim_.Stop();
            if (targetWidth_ == 0.0) Hide();
        }
        SetMinSize(wxSize(static_cast<int>(width_), -1));
        if (GetParent()) GetParent()->Layout();
        relayout();
    });
}

void OptionsPanel::relayout() {
    if (!content_) return;
    const wxSize sz = GetClientSize();
    // Pin the fixed-width content to the right edge so it slides in/out.
    content_->SetSize(kFullWidth, sz.GetHeight());
    content_->Move(sz.GetWidth() - kFullWidth, 0);
}

void OptionsPanel::configure(const DisplayInfo& info,
                             std::function<void(int, int, int)> onSetMode,
                             std::function<void()> onClose) {
    info_ = info;
    onSetMode_ = std::move(onSetMode);
    onClose_ = std::move(onClose);
    rebuild();
}

void OptionsPanel::open() {
    Show(true);
    targetWidth_ = kFullWidth;
    if (!anim_.IsRunning()) anim_.Start(16);
}

void OptionsPanel::close() {
    targetWidth_ = 0.0;
    if (!anim_.IsRunning()) anim_.Start(16);
}

void OptionsPanel::applyTheme() {
    SetBackgroundColour(theme().windowBg);
    if (content_) content_->SetBackgroundColour(theme().cardBg);
    rebuild();
}

void OptionsPanel::rebuild() {
    if (!content_) return;
    contentSizer_->Clear(/*delete_windows=*/true);

    const Theme& th = theme();
    auto addLabel = [&](const wxString& text, int pt, wxFontWeight w, const wxColour& c) {
        auto* t = new wxStaticText(content_, wxID_ANY, text);
        t->SetFont(uiFont(pt, w));
        t->SetForegroundColour(c);
        return t;
    };

    contentSizer_->AddSpacer(16);

    // Header: display name (ellipsized so it can't overflow) + a Close chip.
    auto* head = new wxBoxSizer(wxHORIZONTAL);
    auto* nameLbl = new wxStaticText(content_, wxID_ANY, wxString::FromUTF8(info_.name),
                                     wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    nameLbl->SetFont(uiFont(12, wxFONTWEIGHT_BOLD));
    nameLbl->SetForegroundColour(th.textPrimary);
    head->Add(nameLbl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    head->Add(new Chip(content_, "Close", [this] { if (onClose_) onClose_(); }), 0,
              wxALIGN_CENTER_VERTICAL);
    contentSizer_->Add(head, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);

    const std::string& conn =
        !info_.connectorLabel.empty() ? info_.connectorLabel : info_.connector;
    if (!conn.empty()) {
        contentSizer_->Add(
            addLabel(wxString::FromUTF8(conn).Upper(), 8, wxFONTWEIGHT_NORMAL, th.textGray), 0,
            wxLEFT | wxRIGHT | wxTOP, 16);
    }

    // No adjustable modes reported: say so instead of empty sections.
    if (info_.modes.empty()) {
        contentSizer_->AddSpacer(16);
        contentSizer_->Add(
            addLabel("No adjustable display modes", 9, wxFONTWEIGHT_NORMAL, th.textGray), 0,
            wxLEFT | wxRIGHT, 16);
        contentSizer_->AddStretchSpacer();
        content_->Layout();
        return;
    }

    // Resolution section (smallest -> 4K). Selecting a resolution keeps the
    // current refresh rate when that rate exists at the new size, else best.
    contentSizer_->AddSpacer(14);
    contentSizer_->Add(addLabel("RESOLUTION", 8, wxFONTWEIGHT_BOLD, th.textGray), 0,
                       wxLEFT | wxRIGHT, 16);
    contentSizer_->AddSpacer(6);
    auto* resWrap = new wxWrapSizer(wxHORIZONTAL);
    for (const auto& r : uniqueResolutions(info_.modes)) {
        const bool sel = (r.width == info_.width && r.height == info_.height);
        const int w = r.width, h = r.height;
        auto* chip = new Chip(content_, resolutionLabel(w, h), [this, w, h] {
            if (!onSetMode_) return;
            int keep = 0;  // 0 => backend picks the highest at this resolution
            for (const auto& m : info_.modes) {
                if (m.width == w && m.height == h && m.hz == info_.refreshHz) {
                    keep = info_.refreshHz;
                    break;
                }
            }
            onSetMode_(w, h, keep);
        }, sel);
        resWrap->Add(chip, 0, wxALL, 3);
    }
    contentSizer_->Add(resWrap, 0, wxEXPAND | wxLEFT | wxRIGHT, 13);

    // Refresh-rate section (rates available at the current resolution).
    contentSizer_->AddSpacer(14);
    contentSizer_->Add(addLabel("REFRESH RATE", 8, wxFONTWEIGHT_BOLD, th.textGray), 0,
                       wxLEFT | wxRIGHT, 16);
    contentSizer_->AddSpacer(6);
    auto* hzWrap = new wxWrapSizer(wxHORIZONTAL);
    for (int hz : refreshRatesAt(info_.modes, info_.width, info_.height)) {
        const bool sel = (hz == info_.refreshHz);
        const int w = info_.width, h = info_.height;
        auto* chip = new Chip(content_, wxString::Format("%d Hz", hz),
                              [this, w, h, hz] { if (onSetMode_) onSetMode_(w, h, hz); }, sel);
        hzWrap->Add(chip, 0, wxALL, 3);
    }
    contentSizer_->Add(hzWrap, 0, wxEXPAND | wxLEFT | wxRIGHT, 13);

    contentSizer_->AddStretchSpacer();
    content_->Layout();
}

}  // namespace hdmi::ui
