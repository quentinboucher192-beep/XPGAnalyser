// =============================================================================
//  ui/widgets/GlobeLists.cpp - 1.47.0 : les calques et les villes du globe
//  (voir GlobeLists.hpp ; les mesures de globe.css)
// =============================================================================
#include "GlobeLists.hpp"

#include "../Shapes.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace ui {

namespace {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;

// Les couleurs de la maquette (ds.css).
constexpr Color kInk{15, 23, 42, 255};          // --ink
constexpr Color kInk2{51, 65, 85, 255};         // --ink2
constexpr Color kMuted{100, 116, 139, 255};     // --muted
constexpr Color kFaint{148, 163, 184, 255};     // --faint
constexpr Color kLine{227, 232, 239, 255};      // --line
constexpr Color kLine2{213, 220, 230, 255};     // --line2
constexpr Color kSurface2{248, 250, 252, 255};  // --surface2
constexpr Color kViolet{109, 74, 255, 255};     // --violet
constexpr Color kViolet50{241, 237, 255, 255};  // --violet-50
constexpr Color kWhite{255, 255, 255, 255};

std::string fitText(const gfx::IRenderer& r, const std::string& s, FontId f, float w) {
    if (w <= 6.f) return {};
    if (r.measure(s, f).width <= w) return s;
    const float dots = r.measure("\xE2\x80\xA6", f).width;
    auto k = r.fitCharacters(s, f, std::max(0.f, w - dots));
    while (k > 0 && k < s.size() && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) --k;
    return s.substr(0, k) + "\xE2\x80\xA6";
}

float midY(const gfx::IRenderer& r, FontId f, float top, float h) { return std::round(top + (h - r.lineHeight(f)) / 2.f); }

} // namespace

// ============================================================ les calques ====
GlobeLayerList::GlobeLayerList(std::string id) : Widget(std::move(id)) {}

void GlobeLayerList::setLayers(globe::LayerSet on) {
    on_ = on;
    invalidate();
}
void GlobeLayerList::setTotals(const std::array<int, globe::kLayerCount>& totals) {
    totals_ = totals;
    invalidate();
}
void GlobeLayerList::setHint(globe::Layer l, std::string hint) {
    hints_[globe::index(l)] = std::move(hint);
    invalidate();
}
void GlobeLayerList::setNameSuffix(globe::Layer l, std::string suffix) {
    suffixes_[globe::index(l)] = std::move(suffix);
    invalidate();
}
bool GlobeLayerList::toggle(globe::Layer l) {
    if (on_.has(l) && on_.count() <= 1) return false;   // un calque reste coche
    on_.set(l, !on_.has(l));
    invalidate();
    layersChanged->emit(on_);
    return true;
}

int GlobeLayerList::rowAt(gfx::Point p) const {
    const Rect b = bounds();
    if (p.x < b.x || p.x >= b.right()) return -1;
    const float y = p.y - (b.y + 12.f + kHeader);
    if (y < 0.f) return -1;
    const int i = static_cast<int>(y / kRow);
    return i < static_cast<int>(globe::kLayerCount) ? i : -1;
}

SizeHint GlobeLayerList::sizeHint() const {
    const float h = 12.f + kHeader + kRow * static_cast<float>(globe::kLayerCount) + kFooter;
    return SizeHint{{256.f, h}, {200.f, h}, 0.f, 1.f};
}

std::optional<cursor::Shape> GlobeLayerList::cursorAt(gfx::Point local) const {
    return rowAt(local) >= 0 ? std::optional<cursor::Shape>(cursor::Shape::Pointer) : std::nullopt;
}

EventResult GlobeLayerList::onEvent(const InputEvent& ev) {
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        const int h = bounds().contains(m->pos) ? rowAt(m->pos) : -1;
        if (h != hot_) {
            hot_ = h;
            invalidate();
        }
        return EventResult::Ignored;
    }
    if (const auto* m = std::get_if<MouseDown>(&ev)) {
        if (m->button != MouseButton::Left || !bounds().contains(m->pos)) return EventResult::Ignored;
        const int i = rowAt(m->pos);
        if (i < 0) return EventResult::Ignored;
        toggle(globe::styles()[static_cast<std::size_t>(i)].layer);
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

void GlobeLayerList::onPaint(const PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const Rect b = bounds();
    if (!hovered()) hot_ = -1;
    r.fillRect(b, kSurface2);
    r.fillRect({b.right() - 1.f, b.y, 1.f, b.h}, kLine);
    const float x0 = b.x + 12.f, x1 = b.right() - 11.f;
    // L'en-tete : CALQUES, << 3 sur 7 >>
    const FontId fOver = gfx::semibold(FontId{11});
    float y = b.y + 12.f;
    r.drawText({x0 + 4.f, midY(r, fOver, y, 18.f)}, "CALQUES", fOver, kFaint);
    const std::string n = std::to_string(on_.count()) + " sur " + std::to_string(globe::kLayerCount);
    r.drawText({x1 - 4.f - r.measure(n, fOver).width, midY(r, fOver, y, 18.f)}, n, fOver, kFaint);
    y += kHeader;
    // Les calques
    const FontId fName = gfx::semibold(FontId{12}), fSmall{11}, fNum = gfx::semibold(FontId{12}), fNumOff{12};
    for (std::size_t i = 0; i < globe::kLayerCount; ++i) {
        const auto& st = globe::styles()[i];
        const bool on = on_.has(st.layer);
        const Rect row{x0, y, x1 - x0, kRow - 2.f};
        if (hot_ == static_cast<int>(i)) shapes::fillFramedRect(r, row, kWhite, kLine, 9.f);
        // la case
        const Rect ck{row.x + 6.f, row.y + 8.f, 16.f, 16.f};
        if (on) {
            r.fillRoundedRect(ck, st.color, 5.f);
            r.line({ck.x + 4.f, ck.y + 8.3f}, {ck.x + 6.8f, ck.y + 11.f}, kWhite, 2.f);
            r.line({ck.x + 6.8f, ck.y + 11.f}, {ck.x + 12.3f, ck.y + 5.f}, kWhite, 2.f);
        } else {
            shapes::fillFramedRect(r, ck, kWhite, kLine2, 5.f);
        }
        // le glyphe, le nom, ce qu'il compte, le total
        GlobeView::drawGlyph(r, st.layer, {ck.right() + 8.f, row.y + 9.f, 14.f, 14.f}, on);
        const float tx = ck.right() + 30.f;
        const std::string total = std::to_string(totals_[i]);
        const FontId fN = on ? fNum : fNumOff;
        const float nw = r.measure(total, fN).width;
        const float room = row.right() - 6.f - nw - 8.f - tx;
        const std::string name = fitText(r, st.name, fName, room);
        r.drawText({tx, row.y + 7.f}, name, fName, on ? kInk : kMuted);
        if (!suffixes_[i].empty()) {
            const float sx = tx + r.measure(name, fName).width;
            r.drawText({sx, row.y + 7.f}, fitText(r, suffixes_[i], FontId{12}, row.right() - 6.f - nw - 8.f - sx), FontId{12}, kFaint);
        }
        const std::string& hint = hints_[i].empty() ? std::string(st.hint) : hints_[i];
        r.drawText({tx, row.y + 24.f}, fitText(r, hint, fSmall, row.right() - 6.f - tx), fSmall, on ? kMuted : kFaint);
        r.drawText({row.right() - 6.f - nw, row.y + 8.f}, total, fN, on ? kInk2 : kFaint);
        y += kRow;
    }
    // La legende des tailles, en bas.
    const float fy = std::max(y + 8.f, b.bottom() - kFooter);
    r.fillRect({x0, fy, x1 - x0, 1.f}, kLine);
    const FontId fLeg{11}, fTick = gfx::semibold(FontId{10});
    r.drawText({x0 + 4.f, fy + 9.f}, "La taille d'une marque suit le nombre", fLeg, kMuted);
    const float base = fy + 50.f;   // le bas des ronds
    float cx = x0 + 10.f;
    const std::pair<float, const char*> sizes[] = {{8.f, "1"}, {11.f, "4"}, {14.f, "9"}, {18.f, "18+"}};
    for (const auto& [d, label] : sizes) {
        const Rect dot{cx - d / 2.f, base - d, d, d};
        r.fillRoundedRect(dot.inset(-2.5f, -2.5f), kLine2, d / 2.f + 2.5f);
        r.fillRoundedRect(dot.inset(-2.f, -2.f), kWhite, d / 2.f + 2.f);
        r.fillRoundedRect(dot, globe::style(globe::Layer::Clients).color, d / 2.f);
        const float lw = r.measure(label, fTick).width;
        r.drawText({std::round(cx - lw / 2.f), base + 3.f}, label, fTick, kMuted);
        cx += d / 2.f + 24.f;
    }
    const char* l1 = "forme + couleur :";
    const char* l2 = "lisibles sans les couleurs";
    r.drawText({x1 - 4.f - r.measure(l2, fLeg).width, base - 22.f}, l1, fLeg, kFaint);
    r.drawText({x1 - 4.f - r.measure(l2, fLeg).width, base - 8.f}, l2, fLeg, kFaint);
}

// ============================================================ les villes ====
GlobeCityList::GlobeCityList(std::string id) : Widget(std::move(id)) {}

void GlobeCityList::setPlaces(const std::vector<globe::Place>& places) {
    entries_.clear();
    for (const auto& p : places) entries_.push_back({p.label(), p.counts, p.country});
    if (selected_ >= static_cast<int>(entries_.size())) selected_ = -1;
    rebuild();
}
void GlobeCityList::setLayer(globe::Layer by) {
    by_ = by;
    scroll_ = 0.f;
    rebuild();
}
void GlobeCityList::setSelected(int placeIndex) {
    selected_ = placeIndex >= 0 && placeIndex < static_cast<int>(entries_.size()) ? placeIndex : -1;
    invalidate();
}

void GlobeCityList::rebuild() {
    order_.clear();
    std::set<std::string> countries;
    const std::size_t k = globe::index(by_);
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].counts[k] > 0) {
            order_.push_back(static_cast<int>(i));
            countries.insert(entries_[i].country);
        }
    // La plus grande d'abord ; a egalite, la plus grande en tout (tous les calques : un client par ville, la ville
    // de 7 PC avant celle d'un seul), puis par nom.
    const auto all = [this](int i) {
        int n = 0;
        for (int c : entries_[static_cast<std::size_t>(i)].counts) n += c;
        return n;
    };
    std::stable_sort(order_.begin(), order_.end(), [&](int a, int b) {
        const int na = entries_[static_cast<std::size_t>(a)].counts[k], nb = entries_[static_cast<std::size_t>(b)].counts[k];
        if (na != nb) return na > nb;
        const int ta = all(a), tb = all(b);
        if (ta != tb) return ta > tb;
        return entries_[static_cast<std::size_t>(a)].label < entries_[static_cast<std::size_t>(b)].label;
    });
    countries_ = static_cast<int>(countries.size());
    invalidate();
}

int GlobeCityList::placeAt(gfx::Point p) const {
    const Rect b = bounds();
    if (!b.contains(p)) return -1;
    const float y = p.y - b.y + scroll_;
    const int i = static_cast<int>(std::floor(y / kRow));
    return i >= 0 && i < static_cast<int>(order_.size()) ? order_[static_cast<std::size_t>(i)] : -1;
}

SizeHint GlobeCityList::sizeHint() const { return SizeHint{{326.f, 300.f}, {200.f, 3.f * kRow}, 0.f, 1.f}; }

std::optional<cursor::Shape> GlobeCityList::cursorAt(gfx::Point local) const {
    return placeAt(local) >= 0 ? std::optional<cursor::Shape>(cursor::Shape::Pointer) : std::nullopt;
}

EventResult GlobeCityList::onEvent(const InputEvent& ev) {
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        const int h = placeAt(m->pos);
        if (h != hot_) {
            hot_ = h;
            invalidate();
        }
        return EventResult::Ignored;
    }
    if (const auto* m = std::get_if<MouseDown>(&ev)) {
        if (m->button != MouseButton::Left) return EventResult::Ignored;
        const int i = placeAt(m->pos);
        if (i < 0) return EventResult::Ignored;
        placeChosen->emit(i);
        return EventResult::Consumed;
    }
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos)) return EventResult::Ignored;
        const float most = std::max(0.f, static_cast<float>(order_.size()) * kRow - bounds().h + 8.f);
        scroll_ = std::clamp(scroll_ - w->dy * kRow * 2.f, 0.f, most);
        invalidate();
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

void GlobeCityList::onPaint(const PaintContext& ctx) {
    gfx::IRenderer& r = ctx.r;
    const Rect b = bounds();
    if (!hovered()) hot_ = -1;
    const std::size_t k = globe::index(by_);
    const Color bar = globe::style(by_).color;
    int most = 1;
    for (int i : order_) most = std::max(most, entries_[static_cast<std::size_t>(i)].counts[k]);
    const FontId fName{12}, fSel = gfx::semibold(FontId{12}), fNum = gfx::semibold(FontId{12});
    if (order_.empty()) {
        r.drawText({b.x + 8.f, b.y + 12.f}, "Aucune ville pour ce calque.", fName, kFaint);
        return;
    }
    for (std::size_t n = 0; n < order_.size(); ++n) {
        const float y = b.y + static_cast<float>(n) * kRow - scroll_;
        if (y + kRow < b.y) continue;
        if (y > b.bottom()) break;
        const int i = order_[n];
        const auto& e = entries_[static_cast<std::size_t>(i)];
        const Rect row{b.x, y, b.w, kRow};
        const bool sel = i == selected_;
        if (sel) {
            r.fillRoundedRect(row, kViolet50, 8.f);
            r.fillRoundedRect({row.x, row.y + 1.f, 3.f, row.h - 2.f}, kViolet, 1.5f);
        } else if (i == hot_) {
            r.fillRoundedRect(row, kSurface2, 8.f);
        }
        // << Lyon, FR >> (112 px), la barre, le nombre (30 px) - globe.css, .vr
        const FontId f = sel ? fSel : fName;
        r.drawText({row.x + 8.f, midY(r, f, row.y, row.h)}, fitText(r, e.label, f, 112.f), f, kInk);
        const float bx = row.x + 8.f + 112.f + 10.f, bw = std::max(0.f, row.right() - 8.f - 30.f - 10.f - bx);
        const float w = std::max(3.f, bw * static_cast<float>(e.counts[k]) / static_cast<float>(most));
        r.fillRoundedRect({bx, row.y + (row.h - 8.f) / 2.f, w, 8.f}, bar, 3.f);
        r.fillRect({bx, row.y + (row.h - 8.f) / 2.f, std::min(w, 3.f), 8.f}, bar);   // le bout gauche droit
        const std::string num = globe::formatCount(e.counts[k]);
        r.drawText({row.right() - 8.f - r.measure(num, fNum).width, midY(r, fNum, row.y, row.h)}, num, fNum, kInk2);
    }
    // Le bas s'efface (globe.css : un masque en degrade sur 28 px) quand la liste continue.
    if (static_cast<float>(order_.size()) * kRow - scroll_ > b.h) {
        for (int s = 0; s < 14; ++s)
            r.fillRect({b.x, b.bottom() - 28.f + static_cast<float>(s) * 2.f, b.w, 2.f}, kWhite.withAlpha(static_cast<std::uint8_t>(18 * s)));
    }
}

} // namespace ui
