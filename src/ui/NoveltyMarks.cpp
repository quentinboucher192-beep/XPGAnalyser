#include "NoveltyMarks.hpp"

#include <algorithm>

namespace ui::novelty {

gfx::Color orange(const Theme& t) noexcept {
    // Un orange franc, lisible sur le clair comme sur le sombre (pas l'ambre
    // des avertissements : un repere de nouveaute n'est pas un probleme).
    return t.isDark() ? gfx::Color{255, 152, 40, 255} : gfx::Color{240, 118, 0, 255};
}

void drawFrame(gfx::IRenderer& r, const Theme& t, const gfx::Rect& rect, float thickness) {
    const auto c = orange(t);
    const gfx::Rect o{rect.x - 3.f, rect.y - 3.f, rect.w + 6.f, rect.h + 6.f};
    // Un halo pale, puis le trait : il se voit sur un fond charge.
    r.strokeRect({o.x - 2.f, o.y - 2.f, o.w + 4.f, o.h + 4.f}, c.withAlpha(70), 2.f);
    r.strokeRect(o, c, thickness);
}

gfx::Size pillSize(const gfx::IRenderer& r, const Theme& t, std::string_view text) {
    const float w = r.measure(text, t.font.caption).width;
    return {w + 12.f, r.lineHeight(t.font.caption) + 4.f};
}

void drawPill(gfx::IRenderer& r, const Theme& t, gfx::Point at, std::string_view text) {
    const auto s = pillSize(r, t, text);
    r.fillRoundedRect({at.x, at.y, s.w, s.h}, orange(t), s.h / 2.f);
    r.drawText({at.x + 6.f, at.y + 2.f}, text, t.font.caption, gfx::Color{255, 255, 255, 255});
}

bool visibleRect(const Widget& w, gfx::Rect& out) {
    gfx::Rect rect = w.bounds();
    for (const Widget* p = &w; p; p = p->parent()) {
        if (!p->visible()) return false;
        if (p != &w) rect = rect.intersect(p->bounds());
        if (rect.w < 1.f || rect.h < 1.f) return false;
    }
    // Hors de la fenetre (un panneau defile tres loin) : pas a l'ecran.
    if (const auto s = surfaceSize(); s.w > 0.f && s.h > 0.f) {
        rect = rect.intersect({0.f, 0.f, s.w, s.h});
        if (rect.w < 1.f || rect.h < 1.f) return false;
    }
    out = rect;
    return true;
}

bool idMatches(std::string_view id, std::string_view pattern) noexcept {
    if (pattern.find('*') == std::string_view::npos) return id == pattern;
    // Le joker classique : on revient a la derniere etoile quand ca ne colle plus.
    std::size_t i = 0, p = 0, star = std::string_view::npos, mark = 0;
    while (i < id.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = i;
        } else if (p < pattern.size() && pattern[p] == id[i]) {
            ++p;
            ++i;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            i = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

Widget* findVisible(Widget& root, std::string_view id) {
    gfx::Rect r;
    if (idMatches(root.id(), id) && visibleRect(root, r)) return &root;
    if (!root.visible()) return nullptr;
    for (const auto& c : root.children())
        if (auto* f = findVisible(*c, id)) return f;
    return nullptr;
}

namespace {
PartFinder& partFinder() {
    static PartFinder f;
    return f;
}
} // namespace

void setPartFinder(PartFinder f) { partFinder() = std::move(f); }

namespace {
SinceLabeler& sinceLabeler() {
    static SinceLabeler f;
    return f;
}
} // namespace

void setSinceLabeler(SinceLabeler f) { sinceLabeler() = std::move(f); }

std::string sinceLabel(std::string_view since) {
    if (since.empty() || !sinceLabeler()) return {};
    return sinceLabeler()(since);
}

bool targetRect(Widget& root, std::string_view spec, gfx::Rect& out) {
    const auto hash = spec.find('#');
    auto* w = findVisible(root, spec.substr(0, hash));
    gfx::Rect r;
    if (!w || !visibleRect(*w, r)) return false;
    if (hash == std::string_view::npos) {
        out = r;
        return true;
    }
    gfx::Rect part;
    // 1.10 (H) : la partie introuvable (un bouton passe dans le menu >> d'une barre
    // trop etroite, un selecteur hors de la vue) : le widget entier, qui la contient.
    if (!partFinder() || !partFinder()(*w, spec.substr(hash + 1), part)) {
        out = r;
        return true;
    }
    part = part.intersect(r);
    if (part.w < 1.f || part.h < 1.f) {
        out = r;
        return true;
    }
    out = part;
    return true;
}

const std::vector<Marks::Mark>& Marks::collect(Widget* root) {
    marks_.clear();
    if (!root) return marks_;
    for (const auto& t : targets_) {
        if (t.widget.empty()) continue;
        if (std::any_of(marks_.begin(), marks_.end(), [&](const Mark& m) { return m.widget == t.widget; })) continue;
        if (gfx::Rect r; targetRect(*root, t.widget, r)) marks_.push_back({t.key, t.widget, r});
    }
    return marks_;
}

std::string Marks::hit(gfx::Point p) const {
    for (const auto& m : marks_)
        if (m.rect.contains(p)) return m.key;
    return {};
}

void Marks::paint(gfx::IRenderer& r, const Theme& t) const {
    for (const auto& m : marks_) {
        drawFrame(r, t, m.rect);
        // La pastille sur le coin haut droit, a cheval sur le cadre ; dans la
        // fenetre si l'element touche son bord.
        const auto s = pillSize(r, t, kPillText);
        float x = m.rect.right() - s.w + 6.f;
        float y = m.rect.y - s.h / 2.f - 3.f;
        if (const auto surf = surfaceSize(); surf.w > 0.f) x = std::min(x, surf.w - s.w - 2.f);
        x = std::max(2.f, x);
        y = std::max(2.f, y);
        drawPill(r, t, {x, y}, kPillText);
    }
}

// ------------------------------------------------------------------ Spotlight --
void Spotlight::show(std::string title, std::string text, std::size_t index, std::size_t count, std::string caption) {
    active_ = true;
    title_ = std::move(title);
    text_ = std::move(text);
    caption_ = std::move(caption);
    note_.clear();
    index_ = index;
    count_ = count;
    hover_ = -1;
}

void Spotlight::close() {
    active_ = false;
    target_.reset();
    bubble_ = next_ = close_ = help_ = end_ = {};
}

namespace {
// Les lignes d'un texte qui tient dans `width` (coupe aux espaces).
std::vector<std::string> wrap(const gfx::IRenderer& r, std::string_view text, gfx::FontId font, float width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {
        auto sp = text.find(' ', i);
        if (sp == std::string_view::npos) sp = text.size();
        const std::string word(text.substr(i, sp - i));
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && r.measure(candidate, font).width > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        i = sp + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}
} // namespace

void Spotlight::layout(const gfx::IRenderer& r, const Theme& t, gfx::Size surface) {
    const float w = std::min(340.f, std::max(240.f, surface.w - 40.f));
    const float lh = r.lineHeight(t.font.ui);
    const float ch = r.lineHeight(t.font.caption);
    const auto lines = wrap(r, text_, t.font.ui, w - 28.f);
    const auto noteLines = note_.empty() ? std::vector<std::string>{} : wrap(r, note_, t.font.caption, w - 28.f);
    const float h = 12.f + ch + 6.f + r.lineHeight(t.font.uiBold) + 6.f + static_cast<float>(lines.size()) * lh
                  + (noteLines.empty() ? 0.f : 6.f + static_cast<float>(noteLines.size()) * ch) + 12.f + 28.f + 12.f;
    float x = (surface.w - w) / 2.f, y = (surface.h - h) / 2.f;
    if (target_) {
        const auto& tg = *target_;
        // Sous l'element s'il y a la place, sinon au-dessus, sinon a cote.
        x = std::clamp(tg.x, 10.f, std::max(10.f, surface.w - w - 10.f));
        if (tg.bottom() + 14.f + h < surface.h) y = tg.bottom() + 14.f;
        else if (tg.y - 14.f - h > 0.f) y = tg.y - 14.f - h;
        else {
            y = std::clamp(tg.y, 10.f, std::max(10.f, surface.h - h - 10.f));
            x = tg.right() + 14.f + w < surface.w ? tg.right() + 14.f : std::max(10.f, tg.x - 14.f - w);
        }
    }
    bubble_ = {x, y, w, h};
    const float by = y + h - 12.f - 28.f;
    const bool last = index_ + 1 >= count_;
    const char* nx = last ? "Revoir la liste" : "Suivante \xE2\x80\xBA";
    const float nw = r.measure(nx, t.font.ui).width + 24.f;
    next_ = {x + w - 12.f - nw, by, nw, 28.f};
    const float ew = r.measure("Terminer", t.font.ui).width + 24.f;
    end_ = {next_.x - 8.f - ew, by, ew, 28.f};
    const float hw = r.measure("Voir l'aide", t.font.ui).width + 16.f;
    help_ = helpOffered_ ? gfx::Rect{end_.x - 6.f - hw, by, hw, 28.f} : gfx::Rect{};
    close_ = {x + w - 28.f, y + 8.f, 20.f, 20.f};
}

gfx::Rect Spotlight::partRect(Part p) const noexcept {
    switch (p) {
        case Part::Next:  return next_;
        case Part::Close: return close_;
        case Part::Help:  return help_;
        case Part::End:   return end_;
    }
    return {};
}

void Spotlight::press(Part p) {
    if (!active_) return;
    switch (p) {
        case Part::Next:
            if (index_ + 1 >= count_) boardRequested->emit();
            else nextRequested->emit();
            break;
        case Part::Close:
        case Part::End:   closeRequested->emit(); break;
        case Part::Help:  if (helpOffered_) helpRequested->emit(); break;
    }
}

void Spotlight::paint(gfx::IRenderer& r, const Theme& t, gfx::Size surface) {
    if (!active_) return;
    layout(r, t, surface);
    const auto c = orange(t);
    const gfx::Color white{255, 255, 255, 255};
    // Le voile : tout l'ecran sauf l'element (la maquette).
    const gfx::Color veil{0, 0, 0, static_cast<std::uint8_t>(t.isDark() ? 120 : 90)};
    if (target_) {
        const gfx::Rect h{target_->x - 6.f, target_->y - 6.f, target_->w + 12.f, target_->h + 12.f};
        r.fillRect({0.f, 0.f, surface.w, std::max(0.f, h.y)}, veil);
        r.fillRect({0.f, h.bottom(), surface.w, std::max(0.f, surface.h - h.bottom())}, veil);
        r.fillRect({0.f, h.y, std::max(0.f, h.x), h.h}, veil);
        r.fillRect({h.right(), h.y, std::max(0.f, surface.w - h.right()), h.h}, veil);
        drawFrame(r, t, *target_, 3.f);
    } else {
        r.fillRect({0.f, 0.f, surface.w, surface.h}, veil);
    }
    // La bulle : une carte au lisere orange, teintee.
    const auto& b = bubble_;
    r.fillRoundedRect({b.x + 2.f, b.y + 5.f, b.w, b.h}, gfx::Color{0, 0, 0, 80}, 10.f);
    r.fillRoundedRect(b, c, 10.f);
    r.fillRoundedRect({b.x + 1.5f, b.y + 1.5f, b.w - 3.f, b.h - 3.f}, t.color.panelBg, 9.f);
    r.fillRoundedRect({b.x + 1.5f, b.y + 1.5f, b.w - 3.f, b.h - 3.f}, c.withAlpha(t.isDark() ? 34 : 22), 9.f);
    float y = b.y + 12.f;
    const std::string pill = caption_.empty() ? std::string(kPillText) : caption_;
    drawPill(r, t, {b.x + 14.f, y - 2.f}, pill);
    y += r.lineHeight(t.font.caption) + 6.f;
    r.drawText({b.x + 14.f, y}, title_, t.font.uiBold, t.color.text);
    y += r.lineHeight(t.font.uiBold) + 6.f;
    for (const auto& l : wrap(r, text_, t.font.ui, b.w - 28.f)) {
        r.drawText({b.x + 14.f, y}, l, t.font.ui, t.color.text);
        y += r.lineHeight(t.font.ui);
    }
    if (!note_.empty()) {
        y += 6.f;
        for (const auto& l : wrap(r, note_, t.font.caption, b.w - 28.f)) {
            r.drawText({b.x + 14.f, y}, l, t.font.caption, t.color.textMuted);
            y += r.lineHeight(t.font.caption);
        }
    }
    // La croix.
    {
        const auto& x = close_;
        if (hover_ == 1) r.fillRoundedRect(x, t.color.border, 4.f);
        r.line({x.x + 5.f, x.y + 5.f}, {x.right() - 5.f, x.bottom() - 5.f}, t.color.textMuted, 1.5f);
        r.line({x.right() - 5.f, x.y + 5.f}, {x.x + 5.f, x.bottom() - 5.f}, t.color.textMuted, 1.5f);
    }
    // Le rang (en haut, a gauche de la croix : en bas, il heurterait Voir l'aide), Terminer, Suivante.
    if (count_ > 1) {
        const std::string rank = std::to_string(index_ + 1) + " / " + std::to_string(count_);
        r.drawText({close_.x - 8.f - r.measure(rank, t.font.caption).width, close_.y + (close_.h - r.lineHeight(t.font.caption)) / 2.f},
                   rank, t.font.caption, t.color.textMuted);
    }
    const auto plain = [&](const gfx::Rect& rc, const char* text, bool hot) {
        r.fillRoundedRect(rc, hot ? t.color.border : t.color.panelBg, 5.f);
        r.strokeRect(rc, t.color.borderStrong, 1.f);
        const float w = r.measure(text, t.font.ui).width;
        r.drawText({rc.x + (rc.w - w) / 2.f, rc.y + (rc.h - r.lineHeight(t.font.ui)) / 2.f}, text, t.font.ui, t.color.text);
    };
    plain(end_, "Terminer", hover_ == 3);
    if (helpOffered_) plain(help_, "Voir l'aide", hover_ == 2);
    r.fillRoundedRect(next_, hover_ == 0 ? c.withAlpha(220) : c, 5.f);
    const char* nx = index_ + 1 >= count_ ? "Revoir la liste" : "Suivante \xE2\x80\xBA";
    const float nw = r.measure(nx, t.font.ui).width;
    r.drawText({next_.x + (next_.w - nw) / 2.f, next_.y + (next_.h - r.lineHeight(t.font.ui)) / 2.f}, nx, t.font.ui, white);
}

EventResult Spotlight::handle(const InputEvent& ev) {
    if (!active_) return EventResult::Ignored;
    if (const auto* k = std::get_if<KeyDown>(&ev)) {
        if (k->key == Key::Escape) {
            closeRequested->emit();
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        hover_ = next_.contains(m->pos) ? 0 : close_.contains(m->pos) ? 1 : (helpOffered_ && help_.contains(m->pos)) ? 2
               : end_.contains(m->pos) ? 3 : -1;
        return EventResult::Ignored;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (!bubble_.contains(d->pos)) return EventResult::Ignored;
        if (next_.contains(d->pos)) press(Part::Next);
        else if (close_.contains(d->pos)) press(Part::Close);
        else if (end_.contains(d->pos)) press(Part::End);
        else if (helpOffered_ && help_.contains(d->pos)) press(Part::Help);
        return EventResult::Consumed;
    }
    if (const auto* u = std::get_if<MouseUp>(&ev)) return bubble_.contains(u->pos) ? EventResult::Consumed : EventResult::Ignored;
    return EventResult::Ignored;
}

} // namespace ui::novelty
