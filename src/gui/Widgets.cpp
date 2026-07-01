#include "Widgets.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include <memory>
#include <string>

namespace hdmi::ui {

namespace {
// Draw text horizontally centred on cx at vertical position y.
void centeredText(wxGraphicsContext* gc, const wxString& s, const wxFont& font,
                  const wxColour& colour, double cx, double y) {
    gc->SetFont(font, colour);
    double tw = 0, th = 0, desc = 0, ext = 0;
    gc->GetTextExtent(s, &tw, &th, &desc, &ext);
    gc->DrawText(s, cx - tw / 2.0, y);
}
}  // namespace

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
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kWindowBg));
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
    gc->SetBrush(wxBrush(wxColour(0, 0, 0, 12)));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(m, m + 2, w, h, r);

    // Card body.
    wxColour body = active ? kAccentSoft : (hover_ ? kCardHover : kCardBg);
    gc->SetBrush(wxBrush(body));
    wxColour border = active ? kAccent : (hover_ ? kAccent : kCardBorder);
    gc->SetPen(wxPen(border, active ? 2 : 1));
    gc->DrawRoundedRectangle(m, m, w, h, r);

    // Monitor glyph.
    drawGlyph(gc.get(), cx, m + 20, active ? kAccent : wxColour(176, 182, 190));

    // Active check badge (top-right).
    if (active) {
        const double bx = sz.GetWidth() - m - 18, by = m + 14, br = 9;
        gc->SetBrush(wxBrush(kAccent));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawEllipse(bx - br, by - br, br * 2, br * 2);
        wxGraphicsPath tick = gc->CreatePath();
        tick.MoveToPoint(bx - 4, by);
        tick.AddLineToPoint(bx - 1, by + 3);
        tick.AddLineToPoint(bx + 4, by - 3);
        gc->SetPen(wxPen(kWhite, 2));
        gc->StrokePath(tick);
    }

    // Text block. Backend strings are UTF-8, so decode them explicitly.
    centeredText(gc.get(), wxString::FromUTF8(info_.name), uiFont(12, wxFONTWEIGHT_BOLD),
                 kTextDark, cx, m + 74);

    if (!info_.connector.empty()) {
        centeredText(gc.get(), wxString::FromUTF8(info_.connector).Upper(), uiFont(8),
                     kTextGray, cx, m + 96);
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
        centeredText(gc.get(), sub, uiFont(8), active ? kAccent : kTextGray, cx, m + 116);
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
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(kWindowBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const wxSize sz = GetClientSize();
    const double r = sz.GetHeight() / 2.0;

    wxColour bg, fg, border;
    if (filled_) {
        bg = hover_ ? wxColour(27, 150, 83) : kAccent;
        fg = kWhite;
        border = bg;
    } else {
        bg = hover_ ? kCardHover : kCardBg;
        fg = hover_ ? kAccent : kTextDark;
        border = hover_ ? kAccent : kCardBorder;
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
