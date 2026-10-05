// =============================================================================
//  ui/widgets/ScrollBar.cpp - 1.11.4 : une barre de defilement qu'on tire
//  (voir ScrollBar.hpp)
// =============================================================================
#include "ScrollBar.hpp"
#include "../Theme.hpp"

#include <algorithm>

namespace ui {

gfx::Rect ScrollAxis::thumb(float offset) const noexcept {
    const float len = length();
    if (len <= 0.f || content <= 0.f) return {};
    const float frac = std::clamp(viewport / content, 0.f, 1.f);
    const float size = std::min(len, std::max(minThumb, len * frac));
    const float range = maxOffset();
    const float t = range > 0.f ? std::clamp(offset / range, 0.f, 1.f) : 0.f;
    const float at = t * (len - size);
    return horizontal ? gfx::Rect{track.x + at, track.y, size, track.h} : gfx::Rect{track.x, track.y + at, track.w, size};
}

bool overThumb(const ScrollAxis& a, float offset, gfx::Point p) noexcept {
    return a.needed() && a.thumb(offset).contains(p);
}

bool ScrollBarDrag::press(Widget& owner, const ScrollAxis& a, gfx::Point p, float& offset) {
    if (!a.needed() || !a.track.contains(p)) return false;
    const gfx::Rect th = a.thumb(offset);
    if (th.contains(p)) {
        active_ = true;
        grab_ = a.horizontal ? p.x - th.x : p.y - th.y;
        owner.captureMouse();
        return true;
    }
    // La gouttiere : une page vers le clic.
    const bool before = a.horizontal ? p.x < th.x : p.y < th.y;
    const float page = std::max(1.f, a.viewport * 0.9f);
    offset = std::clamp(offset + (before ? -page : page), 0.f, a.maxOffset());
    return true;
}

bool ScrollBarDrag::move(const ScrollAxis& a, gfx::Point p, float& offset) const {
    if (!active_) return false;
    const gfx::Rect th = a.thumb(offset);
    const float size = a.horizontal ? th.w : th.h;
    const float room = a.length() - size;
    if (room <= 0.f) return false;
    const float start = (a.horizontal ? p.x - a.track.x : p.y - a.track.y) - grab_;
    const float next = std::clamp(start / room, 0.f, 1.f) * a.maxOffset();
    if (std::abs(next - offset) < 0.01f) return false;
    offset = next;
    return true;
}

bool ScrollBarDrag::release(Widget& owner) {
    if (!active_) return false;
    active_ = false;
    owner.releaseMouse();
    return true;
}

bool ScrollBarDrag::handle(Widget& owner, const ScrollAxis& a, const InputEvent& ev, float& offset, bool* changed) {
    const float was = offset;
    bool taken = false;
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (d->button == MouseButton::Left) taken = press(owner, a, d->pos, offset);
    } else if (const auto* m = std::get_if<MouseMove>(&ev)) {
        if (active_) {
            (void)move(a, m->pos, offset);
            taken = true;
        }
    } else if (std::holds_alternative<MouseUp>(ev)) {
        taken = release(owner);
    }
    if (changed) *changed = std::abs(offset - was) > 0.001f;
    return taken;
}

void paintScrollBar(const PaintContext& ctx, const ScrollAxis& a, float offset, bool hot, bool dragging) {
    if (!a.needed()) return;
    const auto& c = ctx.theme.color;
    // La gouttiere, a peine marquee ; le pouce, inscrit avec une marge de 2 px.
    ctx.r.fillRoundedRect(a.track, c.scrollbar.withAlpha(static_cast<std::uint8_t>(c.scrollbar.a / 4)), 4.f);
    gfx::Rect th = a.thumb(offset);
    if (a.horizontal) th = {th.x, th.y + 2.f, th.w, std::max(2.f, th.h - 4.f)};
    else th = {th.x + 2.f, th.y, std::max(2.f, th.w - 4.f), th.h};
    ctx.r.fillRoundedRect(th, hot || dragging ? c.scrollbarHover : c.scrollbar, 3.f);
}

// ---- la barre au bord d'une zone ---------------------------------------------------
ScrollAxis EdgeScrollBar::axis(gfx::Rect area, float content, float viewport) const noexcept {
    ScrollAxis a;
    a.horizontal = horizontal_;
    a.content = content;
    a.viewport = viewport;
    a.track = horizontal_ ? gfx::Rect{area.x, area.bottom() - kScrollBarThickness, area.w, kScrollBarThickness}
                          : gfx::Rect{area.right() - kScrollBarThickness, area.y, kScrollBarThickness, area.h};
    return a;
}

float EdgeScrollBar::space(gfx::Rect area, float content, float viewport) const noexcept {
    return axis(area, content, viewport).needed() ? kScrollBarThickness + 2.f : 0.f;
}

void EdgeScrollBar::paint(const PaintContext& ctx, gfx::Rect area, float content, float viewport, float offset) const {
    paintScrollBar(ctx, axis(area, content, viewport), offset, hot_, drag_.active());
}

bool EdgeScrollBar::handle(Widget& owner, const InputEvent& ev, gfx::Rect area, float content, float viewport, float& offset) {
    const ScrollAxis a = axis(area, content, viewport);
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        const bool hot = drag_.active() || overThumb(a, offset, m->pos);
        if (hot != hot_) {
            hot_ = hot;
            owner.invalidate();
        }
    }
    bool changed = false;
    if (!drag_.handle(owner, a, ev, offset, &changed)) return false;
    owner.invalidate();
    return true;
}

// ---- le widget --------------------------------------------------------------------
ScrollBar::ScrollBar(std::string id, bool horizontal) : Widget(std::move(id)), horizontal_(horizontal) {}

void ScrollBar::setRange(float content, float viewport, float offset) {
    if (content == content_ && viewport == viewport_ && offset == offset_) return;
    content_ = content;
    viewport_ = viewport;
    offset_ = offset;
    invalidate();
}

bool ScrollBar::needed() const noexcept { return axis().needed(); }

ScrollAxis ScrollBar::axis() const noexcept {
    ScrollAxis a;
    a.track = bounds();
    a.content = content_;
    a.viewport = viewport_;
    a.horizontal = horizontal_;
    return a;
}

void ScrollBar::onPaint(const PaintContext& ctx) {
    // Opaque : le contenu passe dessous (un panneau plus large que sa vue).
    if (needed()) ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
    paintScrollBar(ctx, axis(), offset_, hot_, drag_.active());
}

EventResult ScrollBar::onEvent(const InputEvent& ev) {
    const ScrollAxis a = axis();
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        const bool hot = overThumb(a, offset_, m->pos);
        if (hot != hot_) { hot_ = hot; invalidate(); }
    }
    float off = offset_;
    bool changed = false;
    if (!drag_.handle(*this, a, ev, off, &changed)) return EventResult::Ignored;
    if (changed) {
        offset_ = off;
        invalidate();
        if (onScroll) onScroll(off);
    } else {
        invalidate();
    }
    return EventResult::Consumed;
}

} // namespace ui
