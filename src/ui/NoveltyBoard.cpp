#include "NoveltyBoard.hpp"
#include "NoveltyMarks.hpp"

#include <algorithm>

namespace ui::novelty {

namespace {

constexpr float kHeaderH = 52.f;
constexpr float kFooterH = 60.f;
constexpr float kGap = 12.f;
constexpr float kThumbH = 34.f;
constexpr float kGroupH = 34.f;

std::vector<std::string> wrapText(const gfx::IRenderer& r, std::string_view text, gfx::FontId font, float width) {
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

// "1.10.0" -> "1.10" (sans dependre du registre : ui ne connait pas help).
std::string shortOf(std::string v) {
    while (v.size() > 2 && v.compare(v.size() - 2, 2, ".0") == 0 && std::count(v.begin(), v.end(), '.') > 1) v.resize(v.size() - 2);
    return v;
}

const char* kShowMe = "\xE2\x86\x92 Me montrer";
const char* kShowAll = "Me montrer tout, une \xC3\xA0 une";
const char* kMarksBox = "Montrer les rep\xC3\xA8res orange \xC2\xAB NOUVEAU \xC2\xBB dans l'appli";

} // namespace

Board::Board(std::string id) : Widget(std::move(id)) { setFocusPolicy(true); }

void Board::setCards(std::vector<Card> cards, std::string current, std::string from, std::string caption) {
    cards_ = std::move(cards);
    current_ = std::move(current);
    from_ = std::move(from);
    caption_ = std::move(caption);
    scroll_ = 0.f;
    placed_.clear();
    open_.clear();
    invalidate();
}

void Board::setSeen(const std::string& id, bool seen) {
    for (auto& c : cards_)
        if (c.id == id) c.seen = seen;
    invalidate();
}

std::string Board::heading() const { return "Nouveaut\xC3\xA9s de la " + shortOf(current_); }

std::vector<std::string> Board::groups() const {
    std::vector<std::string> out;
    for (const auto& c : cards_)
        if (out.empty() || out.back() != shortOf(c.version)) out.push_back(shortOf(c.version));
    return out;
}

void Board::setGroupOpen(const std::string& v, bool open) {
    const auto it = std::find(open_.begin(), open_.end(), v);
    if (open && it == open_.end()) open_.push_back(v);
    if (!open && it != open_.end()) open_.erase(it);
    if (open) reveal_ = v;          // ses cartes sont plus bas : y defiler (place)
    invalidate();
}

bool Board::groupOpen(const std::string& v) const {
    const auto g = groups();
    if (!g.empty() && g.front() == v) return true;          // la version lancee : toujours ouverte
    return std::find(open_.begin(), open_.end(), v) != open_.end();
}

std::string Board::groupLine(const std::string& v) const {
    const auto n = static_cast<std::size_t>(std::count_if(cards_.begin(), cards_.end(), [&](const Card& c) { return shortOf(c.version) == v; }));
    std::string s = "Ce que tu as manqu\xC3\xA9 : la " + v;
    s += "  \xC2\xB7  ";
    if (!from_.empty()) s += "tu viens de la " + shortOf(from_) + " \xC2\xB7 ";
    s += std::to_string(n) + (n > 1 ? " nouveaut\xC3\xA9s" : " nouveaut\xC3\xA9");
    return s;
}

SizeHint Board::sizeHint() const {
    SizeHint h;
    h.preferred = {1000.f, 640.f};
    h.minimum = {420.f, 320.f};
    h.stretchX = h.stretchY = 1.f;
    return h;
}

void Board::place(const gfx::IRenderer& r, const Theme& t) {
    const auto b = bounds();
    body_ = {b.x, b.y + kHeaderH, b.w, std::max(0.f, b.h - kHeaderH - kFooterH)};
    // Quatre colonnes quand la fenetre est large (la maquette : les cartes de la
    // 1.10 et la 1.9 depliee tiennent sans defiler sur un ecran de 1920).
    const int cols = body_.w >= 1200.f ? 4 : body_.w >= 900.f ? 3 : body_.w >= 600.f ? 2 : 1;
    const float cw = (body_.w - kGap * static_cast<float>(cols + 1)) / static_cast<float>(cols);
    const float lh = r.lineHeight(t.font.ui), bh = r.lineHeight(t.font.uiBold);
    placed_.assign(cards_.size(), {});
    groups_.clear();
    float y = kGap;
    std::string group;
    std::size_t col = 0;
    float rowH = 0.f;
    bool shown = true;
    for (std::size_t i = 0; i < cards_.size(); ++i) {
        const auto& c = cards_[i];
        const std::string v = shortOf(c.version);
        if (v != group) {
            if (col != 0) { y += rowH + kGap; col = 0; rowH = 0.f; }
            if (!group.empty()) {
                // Une version manquee : sa ligne, repliee ou non.
                groups_.push_back({v, {kGap, y, body_.w - 2 * kGap, kGroupH}, true});
                y += kGroupH + (groupOpen(v) ? kGap : 4.f);
            } else {
                groups_.push_back({v, {}, false});
            }
            group = v;
            shown = groupOpen(v);
        }
        if (!shown) continue;
        const auto lines = wrapText(r, c.text, t.font.ui, cw - 28.f);
        // Le titre aussi va a la ligne (une carte etroite, un titre long).
        const auto titleLines = wrapText(r, c.title, t.font.uiBold, cw - 28.f);
        const float h = 12.f + kThumbH + 10.f + static_cast<float>(std::max<std::size_t>(1, titleLines.size())) * bh + 4.f
                      + static_cast<float>(lines.size()) * lh + 10.f + 28.f + 12.f;
        const float x = kGap + static_cast<float>(col) * (cw + kGap);
        placed_[i].card = {x, y, cw, h};
        placed_[i].shown = true;
        const float sw = r.measure(kShowMe, t.font.ui).width + 24.f;
        placed_[i].showMe = c.canShow ? gfx::Rect{x + 14.f, y + h - 12.f - 28.f, sw, 28.f} : gfx::Rect{};
        rowH = std::max(rowH, h);
        if (++col == static_cast<std::size_t>(cols)) { y += rowH + kGap; col = 0; rowH = 0.f; }
    }
    if (col != 0) y += rowH + kGap;
    // 1.10 (H) : les versions precedentes - les nouveautes de l'API du lot 8 et
    // leurs parcours (le menu Aide n'a plus d'entree pour elles).
    previous_ = {kGap, y, body_.w - 2 * kGap, kGroupH};
    y += kGroupH + kGap;
    contentH_ = y;
    if (!reveal_.empty()) {
        for (const auto& g : groups_)
            if (g.fold && g.version == reveal_) scroll_ = g.row.y - kGap;
        reveal_.clear();
    }
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, contentH_ - body_.h));
    // Le pied : la case a gauche ; Plus tard, Tout vu et Me montrer tout a droite.
    const float fy = b.bottom() - kFooterH + 14.f;
    const float sw = r.measure(kShowAll, t.font.uiBold).width + 28.f;
    const float aw = r.measure("Tout vu", t.font.ui).width + 28.f;
    const float lw = r.measure("Plus tard", t.font.ui).width + 28.f;
    showAll_ = {b.right() - 16.f - sw, fy, sw, 32.f};
    allSeen_ = {showAll_.x - 8.f - aw, fy, aw, 32.f};
    later_ = {allSeen_.x - 8.f - lw, fy, lw, 32.f};
    hide_ = {b.x + 16.f, fy, r.measure(kMarksBox, t.font.ui).width + 34.f, 32.f};
    close_ = {b.right() - 38.f, b.y + 13.f, 26.f, 26.f};
}

gfx::Rect Board::buttonRect(Button b) const noexcept {
    switch (b) {
        case Button::Later:     return later_;
        case Button::AllSeen:   return allSeen_;
        case Button::HideMarks: return hide_;
        case Button::ShowAll:   return showAll_;
        case Button::Close:     return close_;
        case Button::Previous:  return previous_.w > 0.f ? gfx::Rect{body_.x + previous_.x, body_.y + previous_.y - scroll_, previous_.w, previous_.h}
                                                         : gfx::Rect{};
    }
    return {};
}

gfx::Rect Board::showMeRect(std::size_t card) const noexcept {
    if (card >= placed_.size() || !placed_[card].shown || placed_[card].showMe.w <= 0.f) return {};
    const auto& s = placed_[card].showMe;
    return {body_.x + s.x, body_.y + s.y - scroll_, s.w, s.h};
}

gfx::Rect Board::groupRect(const std::string& v) const noexcept {
    for (const auto& g : groups_)
        if (g.fold && g.version == v) return {body_.x + g.row.x, body_.y + g.row.y - scroll_, g.row.w, g.row.h};
    return {};
}

void Board::press(Button b) {
    switch (b) {
        case Button::Later:
        case Button::Close:     laterRequested->emit(); break;
        case Button::AllSeen:   allSeenRequested->emit(); break;
        case Button::HideMarks: hideMarksRequested->emit(!marksHidden_); break;
        case Button::ShowAll:   showAllRequested->emit(); break;
        case Button::Previous:  previousRequested->emit(); break;
    }
}

bool Board::pressShowMe(const std::string& id) {
    for (const auto& c : cards_)
        if (c.id == id && c.canShow) {
            showMeRequested->emit(c.id);
            return true;
        }
    return false;
}

void Board::scrollBy(float dy) {
    scroll_ = std::clamp(scroll_ + dy, 0.f, std::max(0.f, contentH_ - body_.h));
    invalidate();
}

void Board::onPaint(const PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& t = ctx.theme;
    const auto& c = t.color;
    const auto o = orange(t);
    const gfx::Color white{255, 255, 255, 255};
    place(r, t);
    const auto b = bounds();
    r.fillRoundedRect({b.x + 2.f, b.y + 6.f, b.w, b.h}, gfx::Color{0, 0, 0, 70}, 10.f);
    r.fillRoundedRect(b, c.borderStrong, 10.f);
    r.fillRoundedRect({b.x + 1.f, b.y + 1.f, b.w - 2.f, b.h - 2.f}, c.panelBg, 9.f);
    // L'en-tete : le rond "?", le titre, ce qui ouvre la fenetre, la croix.
    {
        const float cy = b.y + kHeaderH / 2.f;
        r.fillRoundedRect({b.x + 18.f, cy - 9.f, 18.f, 18.f}, c.text, 9.f);
        r.fillRoundedRect({b.x + 19.5f, cy - 7.5f, 15.f, 15.f}, c.panelBg, 7.5f);
        const float qw = r.measure("?", t.font.caption).width;
        r.drawText({b.x + 27.f - qw / 2.f, cy - r.lineHeight(t.font.caption) / 2.f}, "?", t.font.caption, c.text);
        const auto h = heading();
        r.drawText({b.x + 46.f, cy - r.lineHeight(t.font.uiBold) / 2.f}, h, t.font.uiBold, c.text);
        const float hw = r.measure(h, t.font.uiBold).width;
        const std::string cap = caption_.empty() ? std::string("ce qui est nouveau dans cette version") : caption_;
        r.drawText({b.x + 46.f + hw + 12.f, cy - r.lineHeight(t.font.ui) / 2.f}, cap, t.font.ui, c.textMuted);
        if (hover_ == -6) r.fillRoundedRect(close_, t.brand.hover, 4.f);
        r.line({close_.x + 8.f, close_.y + 8.f}, {close_.right() - 8.f, close_.bottom() - 8.f}, c.textMuted, 1.5f);
        r.line({close_.right() - 8.f, close_.y + 8.f}, {close_.x + 8.f, close_.bottom() - 8.f}, c.textMuted, 1.5f);
        r.line({b.x, b.y + kHeaderH - 1.f}, {b.right(), b.y + kHeaderH - 1.f}, c.border, 1.f);
    }
    // Les cartes, dans le corps qui defile.
    r.pushClip(body_);
    const float top = body_.y - scroll_;
    for (const auto& g : groups_) {
        if (!g.fold) continue;
        const gfx::Rect gr{body_.x + g.row.x, top + g.row.y, g.row.w, g.row.h};
        r.line({gr.x, gr.y}, {gr.right(), gr.y}, c.border, 1.f);
        const bool open = groupOpen(g.version);
        const float ty = gr.y + (gr.h - r.lineHeight(t.font.uiBold)) / 2.f;
        r.drawText({gr.x + 4.f, ty}, open ? "\xE2\x96\xBE" : "\xE2\x96\xB8", t.font.ui, c.textMuted);
        const std::string line = groupLine(g.version);
        const auto dot = line.find("  \xC2\xB7  ");
        const std::string head = line.substr(0, dot), tail = dot == std::string::npos ? std::string{} : line.substr(dot + 6);
        r.drawText({gr.x + 22.f, ty}, head, t.font.uiBold, c.text);
        r.drawText({gr.x + 22.f + r.measure(head, t.font.uiBold).width + 12.f, ty}, tail, t.font.ui, c.textMuted);
    }
    // 1.10 (H) : la ligne des versions precedentes, comme celle d'une version manquee.
    if (previous_.w > 0.f) {
        const gfx::Rect pr{body_.x + previous_.x, top + previous_.y, previous_.w, previous_.h};
        if (hover_ == -7) r.fillRoundedRect(pr, t.brand.hover, 5.f);
        r.line({pr.x, pr.y}, {pr.right(), pr.y}, c.border, 1.f);
        const float ty = pr.y + (pr.h - r.lineHeight(t.font.uiBold)) / 2.f;
        r.drawText({pr.x + 4.f, ty}, "\xE2\x96\xB8", t.font.ui, c.textMuted);
        const std::string head = "Versions pr\xC3\xA9" "c\xC3\xA9" "dentes";
        const std::string tail = "La 1.8.0 : simulation, d\xC3\xA9" "bogage, fichiers \xE2\x80\x94 leurs parcours \xE2\x80\xBA";
        r.drawText({pr.x + 22.f, ty}, head, t.font.uiBold, c.text);
        r.drawText({pr.x + 22.f + r.measure(head, t.font.uiBold).width + 12.f, ty}, tail, t.font.ui, c.accent);
    }
    for (std::size_t i = 0; i < cards_.size() && i < placed_.size(); ++i) {
        const auto& card = cards_[i];
        const auto& p = placed_[i];
        if (!p.shown) continue;
        const gfx::Rect rc{body_.x + p.card.x, top + p.card.y, p.card.w, p.card.h};
        if (rc.bottom() < body_.y || rc.y > body_.bottom()) continue;
        r.fillRoundedRect(rc, card.seen ? c.border : c.borderStrong, 8.f);
        r.fillRoundedRect({rc.x + 1.f, rc.y + 1.f, rc.w - 2.f, rc.h - 2.f}, c.windowBg, 7.f);
        // La vignette : un petit dessin en texte, sur un bandeau.
        const gfx::Rect th{rc.x + 12.f, rc.y + 12.f, rc.w - 24.f, kThumbH};
        r.fillRoundedRect(th, c.headerBg, 5.f);
        const std::string picto = card.picto.empty() ? "NOUVEAU \xC2\xB7 " + shortOf(card.version) : card.picto;
        if (picto == kPillText) {
            drawPill(r, t, {th.x + (th.w - pillSize(r, t, picto).w) / 2.f, th.y + (th.h - pillSize(r, t, picto).h) / 2.f}, picto);
        } else {
            const float pw = r.measure(picto, t.font.mono).width;
            r.drawText({th.x + (th.w - pw) / 2.f, th.y + (th.h - r.lineHeight(t.font.mono)) / 2.f}, picto, t.font.mono,
                       card.picto.empty() ? o : c.text);
        }
        float y = th.bottom() + 10.f;
        for (const auto& l : wrapText(r, card.title, t.font.uiBold, rc.w - 28.f)) {
            r.drawText({rc.x + 14.f, y}, l, t.font.uiBold, c.text);
            y += r.lineHeight(t.font.uiBold);
        }
        y += 4.f;
        for (const auto& l : wrapText(r, card.text, t.font.ui, rc.w - 28.f)) {
            r.drawText({rc.x + 14.f, y}, l, t.font.ui, c.textMuted);
            y += r.lineHeight(t.font.ui);
        }
        if (card.canShow) {
            const auto s = showMeRect(i);
            const bool hot = hover_ == static_cast<int>(i);
            if (card.seen) {
                r.fillRoundedRect(s, hot ? t.brand.hover : c.panelBg, 5.f);
                r.strokeRect(s, c.border, 1.f);
            } else {
                r.fillRoundedRect(s, hot ? c.accentHover : c.accent, 5.f);
            }
            r.drawText({s.x + 12.f, s.y + (s.h - r.lineHeight(t.font.ui)) / 2.f}, kShowMe, t.font.ui, card.seen ? c.text : white);
            if (card.seen)
                r.drawText({s.right() + 10.f, s.y + (s.h - r.lineHeight(t.font.caption)) / 2.f}, "\xE2\x9C\x93 vu", t.font.caption, c.ok);
        }
    }
    // La barre de defilement, quand les cartes depassent : elle dit qu'il y en a d'autres.
    if (contentH_ > body_.h + 1.f) {
        const float track = body_.h - 8.f;
        const float th = std::max(24.f, track * body_.h / contentH_);
        const float ty = body_.y + 4.f + (track - th) * scroll_ / std::max(1.f, contentH_ - body_.h);
        r.fillRoundedRect({body_.right() - 7.f, ty, 4.f, th}, c.borderStrong, 2.f);
    }
    r.popClip();
    // Le pied : la case des reperes, ou revoir la fenetre, les boutons.
    r.line({b.x, b.bottom() - kFooterH}, {b.right(), b.bottom() - kFooterH}, c.border, 1.f);
    {
        const gfx::Rect box{hide_.x, hide_.y + (hide_.h - 16.f) / 2.f, 16.f, 16.f};
        if (!marksHidden_) {
            r.fillRoundedRect(box, c.accent, 3.f);
            r.line({box.x + 3.5f, box.y + 8.5f}, {box.x + 6.5f, box.y + 11.5f}, white, 2.f);
            r.line({box.x + 6.5f, box.y + 11.5f}, {box.x + 12.5f, box.y + 4.5f}, white, 2.f);
        } else {
            r.fillRoundedRect(box, c.inputBg, 3.f);
            r.strokeRect(box, c.borderStrong, 1.f);
        }
        r.drawText({box.right() + 8.f, hide_.y + (hide_.h - r.lineHeight(t.font.ui)) / 2.f}, kMarksBox, t.font.ui,
                   hover_ == -4 ? c.text : c.textMuted);
    }
    const std::string again = "Menu Aide \xE2\x80\xBA Nouveaut\xC3\xA9s\xE2\x80\xA6 pour la revoir";   // 1.10 (R2) : l'entree unique du menu
    const float agw = r.measure(again, t.font.caption).width;
    if (later_.x - 16.f - agw > hide_.right() + 12.f)
        r.drawText({later_.x - 16.f - agw, later_.y + (later_.h - r.lineHeight(t.font.caption)) / 2.f}, again, t.font.caption, c.textMuted);
    const auto plain = [&](const gfx::Rect& rc, const char* text, bool hot) {
        r.fillRoundedRect(rc, hot ? t.brand.hover : c.windowBg, 5.f);
        r.strokeRect(rc, c.borderStrong, 1.f);
        const float w = r.measure(text, t.font.ui).width;
        r.drawText({rc.x + (rc.w - w) / 2.f, rc.y + (rc.h - r.lineHeight(t.font.ui)) / 2.f}, text, t.font.ui, c.text);
    };
    plain(later_, "Plus tard", hover_ == -2);
    plain(allSeen_, "Tout vu", hover_ == -3);
    r.fillRoundedRect(showAll_, hover_ == -5 ? c.accentHover : c.accent, 5.f);
    const float sw = r.measure(kShowAll, t.font.uiBold).width;
    r.drawText({showAll_.x + (showAll_.w - sw) / 2.f, showAll_.y + (showAll_.h - r.lineHeight(t.font.uiBold)) / 2.f}, kShowAll,
               t.font.uiBold, white);
}

EventResult Board::onEvent(const InputEvent& ev) {
    const auto partAt = [&](gfx::Point p) {
        if (later_.contains(p)) return -2;
        if (allSeen_.contains(p)) return -3;
        if (hide_.contains(p)) return -4;
        if (showAll_.contains(p)) return -5;
        if (close_.contains(p)) return -6;
        if (body_.contains(p) && buttonRect(Button::Previous).contains(p)) return -7;
        if (body_.contains(p)) {
            for (std::size_t i = 0; i < cards_.size(); ++i)
                if (cards_[i].canShow && showMeRect(i).contains(p)) return static_cast<int>(i);
            for (std::size_t g = 0; g < groups_.size(); ++g)
                if (groups_[g].fold && groupRect(groups_[g].version).contains(p)) return -100 - static_cast<int>(g);
        }
        return -1;
    };
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        const int h = partAt(m->pos);
        if (h != hover_) { hover_ = h; invalidate(); }
        return EventResult::Ignored;
    }
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        scrollBy(-w->dy * 48.f);
        return EventResult::Consumed;
    }
    if (const auto* k = std::get_if<KeyDown>(&ev); k && k->key == Key::Escape) {
        press(Button::Later);
        return EventResult::Consumed;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev); d && d->button == MouseButton::Left) {
        const int part = partAt(d->pos);
        if (part == -2) press(Button::Later);
        else if (part == -3) press(Button::AllSeen);
        else if (part == -4) press(Button::HideMarks);
        else if (part == -5) press(Button::ShowAll);
        else if (part == -6) press(Button::Close);
        else if (part == -7) press(Button::Previous);
        else if (part <= -100) {
            const auto& g = groups_[static_cast<std::size_t>(-100 - part)];
            setGroupOpen(g.version, !groupOpen(g.version));
        } else if (part >= 0) showMeRequested->emit(cards_[static_cast<std::size_t>(part)].id);
        return part == -1 ? EventResult::Ignored : EventResult::Consumed;
    }
    return EventResult::Ignored;
}

} // namespace ui::novelty
