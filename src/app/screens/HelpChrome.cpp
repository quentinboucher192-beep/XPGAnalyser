#include "HelpChrome.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace app {
namespace {

// Le survol arrive en fondu, et repart en fondu : 0 -> 1 sur hoverMs.
float hoverAlpha(const ui::PaintContext& ctx, bool hovered, double since, bool& animating) {
    if (since < 0.0) return hovered ? 1.f : 0.f;
    const float t = std::clamp(static_cast<float>((ctx.time - since) * 1000.0)
                               / std::max(1.f, ctx.theme.motion.hoverMs), 0.f, 1.f);
    animating = t < 1.f;
    return hovered ? t : 1.f - t;
}

gfx::Color mixColor(gfx::Color a, gfx::Color b, float t) {
    const auto m = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(static_cast<float>(x) + (static_cast<float>(y) - x) * t);
    };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Retire le dernier caractere UTF-8 entier : effacer un octet d'un "e" laisse
// une sequence cassee, que la police dessine en boite.
void popUtf8(std::string& s) {
    if (s.empty()) return;
    std::size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    s.erase(i);
}

} // namespace

// =============================================================== PillButton ===
PillButton::PillButton(std::string text, ui::Icon icon, Kind kind, std::string id)
    : ui::Widget(std::move(id)), text_(std::move(text)), icon_(icon), kind_(kind) {
    setFocusPolicy(true);
}

void PillButton::setText(std::string t) {
    if (t == text_) return;
    text_ = std::move(t);
    invalidateLayout();
}

void PillButton::setCompact(bool c) {
    if (c == compact_) return;
    compact_ = c;
    invalidateLayout();
}

ui::SizeHint PillButton::sizeHint() const {
    ui::SizeHint h;
    const float height = 32.f;
    float w = height;                                   // compact : un cercle
    if (!compact()) {
        w = 14.f + ui::measureWidth(text_, font_) + 14.f;
        if (icon_ != ui::Icon::None || glyph_ != Glyph::None) w += 16.f + 7.f;
        if (text_.empty()) w = height;
    }
    h.preferred = {w, height};
    h.minimum   = {height, height};
    return h;
}

void PillButton::onPaint(const ui::PaintContext& ctx) {
    const auto& th = ctx.theme;
    const auto& c  = th.color;
    const auto  b  = bounds();
    const float h  = std::min(b.h, 32.f);
    const gfx::Rect r{b.x, b.y + (b.h - h) * 0.5f, b.w, h};
    const float radius = th.metric.radius > 0.f ? h * 0.5f : 0.f;
    if (th.font.ui.v != font_.v) {
        font_ = th.font.ui;
        invalidateLayout();                  // la barre se refait a la bonne largeur
        if (parent() != nullptr) parent()->invalidateLayout();
    }

    if (hovered() != wasHovered_) { wasHovered_ = hovered(); hoverSince_ = ctx.time; }
    // Relache hors du bouton : Widget::dispatch ne lui envoie pas le MouseUp,
    // et l'aspect " enfonce " restait colle. La souris partie, il se releve.
    if (pressed_ && !hovered()) pressed_ = false;
    bool animating = false;
    const float hov = enabled() ? hoverAlpha(ctx, hovered(), hoverSince_, animating) : 0.f;
    if (animating) invalidate();

    // ---- le fond -------------------------------------------------------------
    gfx::Color fg = enabled() ? c.text : c.textDisabled;
    switch (kind_) {
        case Kind::Primary: {
            gfx::Color bg = mixColor(c.accent, c.accentHover, hov);
            if (pressed_) bg = c.accentPressed;
            if (!enabled()) bg = c.border;
            ctx.r.fillRoundedRect(r, bg, radius);
            fg = enabled() ? c.textInverted : c.textDisabled;
            break;
        }
        case Kind::Subtle:
            ctx.r.fillRoundedRect(r, c.border, radius);
            ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, c.rowAltBg,
                                  std::max(0.f, radius - 1.f));
            [[fallthrough]];
        case Kind::Ghost:
            if (hov > 0.f) {
                auto veil = th.brand.hover;
                veil.a = static_cast<std::uint8_t>(veil.a * hov * (pressed_ ? 2.f : 1.f));
                ctx.r.fillRoundedRect(r, veil, radius);
            }
            break;
    }

    // ---- le clavier : un anneau, pour savoir ou l'on est ----------------------
    if (focused())
        ctx.r.strokeRect({r.x - 2.f, r.y - 2.f, r.w + 4.f, r.h + 4.f}, th.brand.focusRing, 2.f);

    // ---- l'icone et le libelle --------------------------------------------------
    const float side = 16.f;
    gfx::Color iconColour = fg;
    if (kind_ != Kind::Primary && enabled())
        iconColour = iconTone_ != ui::Tone::None ? th.tone(iconTone_, c.textMuted) : c.textMuted;
    const auto drawMark = [&](gfx::Rect box) {
        if (glyph_ != Glyph::None) {
            // Un chevron, epais : la fleche d'un navigateur.
            const float cx = box.x + box.w * 0.5f, cy = box.y + box.h * 0.5f, k = 4.5f;
            const float dir = glyph_ == Glyph::Back ? 1.f : -1.f;
            ctx.r.line({cx + dir * k * 0.55f, cy - k}, {cx - dir * k * 0.55f, cy}, iconColour, 2.f);
            ctx.r.line({cx - dir * k * 0.55f, cy}, {cx + dir * k * 0.55f, cy + k}, iconColour, 2.f);
        } else if (icon_ != ui::Icon::None) {
            ui::drawIcon(ctx.r, icon_, box, iconColour);
        }
    };
    const bool hasMark = icon_ != ui::Icon::None || glyph_ != Glyph::None;
    if (compact() || text_.empty()) {
        if (hasMark) drawMark({r.x + (r.w - side) * 0.5f, r.y + (h - side) * 0.5f, side, side});
    } else {
        float x = r.x + 14.f;
        if (hasMark) {
            drawMark({x, r.y + (h - side) * 0.5f, side, side});
            x += side + 7.f;
        }
        const float ty = r.y + (h - ctx.r.lineHeight(th.font.ui)) * 0.5f;
        ctx.r.drawText({x, ty}, text_, th.font.ui, fg);
        if (kind_ == Kind::Primary) ctx.r.drawText({x + 0.6f, ty}, text_, th.font.ui, fg);
    }

    // ---- la pastille ---------------------------------------------------------------
    if (dot_ != ui::Tone::None) {
        const float d = 8.f;
        const gfx::Rect dot{r.right() - d - 3.f, r.y + 2.f, d, d};
        ctx.r.fillRoundedRect({dot.x - 2.f, dot.y - 2.f, d + 4.f, d + 4.f}, c.headerBg, (d + 4.f) * 0.5f);
        ctx.r.fillRoundedRect(dot, th.tone(dot_, c.accent), d * 0.5f);
    }
}

ui::EventResult PillButton::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (!bounds().contains(d->pos) || d->button != ui::MouseButton::Left)
            return ui::EventResult::Ignored;
        pressed_ = true;
        invalidate();
        return ui::EventResult::Consumed;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev)) {
        if (!pressed_) return ui::EventResult::Ignored;
        pressed_ = false;
        invalidate();
        // Relacher HORS du bouton annule : c'est le geste qu'on fait quand on a
        // appuye par erreur, et il doit marcher partout. (Avec Widget::dispatch
        // tel qu'il est, un MouseUp hors du bouton n'arrive meme pas ici ; le
        // test reste pour un hote qui capturerait la souris - la mutation qui
        // le retire est equivalente aujourd'hui, et c'est su.)
        if (bounds().contains(u->pos) && enabled()) clicked->emit();
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && focused() && !k->repeat
            && (k->key == ui::Key::Space || k->key == ui::Key::Return)) {
        if (enabled()) clicked->emit();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ================================================================== HelpBar ===
HelpBar::HelpBar(std::string id) : ui::Widget(std::move(id)) {
    setPadding({8.f, 10.f, 8.f, 10.f});
}

PillButton& HelpBar::add(std::unique_ptr<PillButton> b) {
    auto& ref = static_cast<PillButton&>(addChild(std::move(b)));
    slots_.push_back({SlotKind::Button, &ref, 0.f});
    invalidateLayout();
    return ref;
}

void HelpBar::addSeparator() { slots_.push_back({SlotKind::Separator, nullptr, 0.f}); invalidateLayout(); }
void HelpBar::addSpacer()    { slots_.push_back({SlotKind::Spacer, nullptr, 0.f}); invalidateLayout(); }

ui::SizeHint HelpBar::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {400.f, 48.f};
    h.minimum   = {100.f, 48.f};
    h.stretchX  = 1.f;
    return h;
}

void HelpBar::onLayout() {
    const auto area = contentRect();
    constexpr float kGap = 4.f, kSep = 17.f;

    // Les deux cotes du ressort, chacun avec ses separateurs UTILES : un
    // separateur n'est pose qu'entre deux boutons visibles. Un groupe dont tous
    // les boutons sont caches ne laisse ni trou ni double trait.
    struct Placed { Slot* slot; bool separator; };
    std::vector<Placed> left, right;
    bool afterSpacer = false;
    for (auto& s : slots_) {
        auto& side = afterSpacer ? right : left;
        switch (s.kind) {
            case SlotKind::Spacer:    afterSpacer = true; break;
            case SlotKind::Separator:
                if (!side.empty() && !side.back().separator) side.push_back({&s, true});
                break;
            case SlotKind::Button:
                if (s.button->visibility() != ui::Visibility::Collapsed) side.push_back({&s, false});
                break;
        }
    }
    for (auto* side : {&left, &right})
        while (!side->empty() && side->back().separator) side->pop_back();

    const auto widthOf = [&](const std::vector<Placed>& side) {
        float w = 0.f;
        for (const auto& p : side)
            w += p.separator ? kSep : p.slot->button->sizeHint().preferred.w + kGap;
        return w;
    };

    // Trop etroit : les libelles tombent, les icones restent - sauf l'action
    // principale, qui est la raison d'etre de la barre.
    compact_ = false;
    for (auto& s : slots_) if (s.button) s.button->setCompact(false);
    if (widthOf(left) + widthOf(right) > area.w) {
        compact_ = true;
        for (auto& s : slots_)
            if (s.button && s.button->kind() != PillButton::Kind::Primary) s.button->setCompact(true);
    }

    const auto place = [&](std::vector<Placed>& side, float x) {
        for (auto& p : side) {
            if (p.separator) { p.slot->x = x + kSep * 0.5f; x += kSep; continue; }
            const float w = p.slot->button->sizeHint().preferred.w;
            p.slot->button->setBounds({x, area.y, w, area.h});
            x += w + kGap;
        }
    };
    for (auto& s : slots_) if (s.kind == SlotKind::Separator) s.x = -1.f;   // non pose
    place(left, area.x);
    place(right, area.right() - widthOf(right) + kGap);
}

void HelpBar::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto  b = bounds();
    ctx.r.fillRect(b, c.headerBg);
    ctx.r.line({b.x, b.bottom() - 0.5f}, {b.right(), b.bottom() - 0.5f}, c.border, 1.f);
    for (const auto& s : slots_)
        if (s.kind == SlotKind::Separator && s.x >= 0.f)
            ctx.r.line({s.x, b.y + 14.f}, {s.x, b.bottom() - 14.f}, c.border, 1.f);
}

// ============================================================= HelpTabStrip ===
namespace {
constexpr float kTabPadX = 16.f, kTabIcon = 16.f, kTabIconGap = 8.f, kTabGap = 2.f;

ui::Icon tabIcon(int tab) {
    switch (tab) {
        case HelpTabStrip::General: return ui::Icon::Document;
        case HelpTabStrip::Macros:  return ui::Icon::Play;
        case HelpTabStrip::Blocks:  return ui::Icon::FunctionBlock;
        case HelpTabStrip::Hmi:     return ui::Icon::Screen;
        default:                    return ui::Icon::None;
    }
}
} // namespace

HelpTabStrip::HelpTabStrip(int current, std::string id) : ui::Widget(std::move(id)) {
    current_ = std::clamp(current, 0, kTabCount - 1);
}

std::string_view HelpTabStrip::label(int tab) noexcept {
    switch (tab) {
        case General: return "Aide";
        case Macros:  return "Macros";
        case Blocks:  return "Blocs DFB / DDT";
        case Hmi:     return "IHM";
        default:      return {};
    }
}

std::string_view HelpTabStrip::menuId(int tab) noexcept {
    switch (tab) {
        case General: return "help";
        case Macros:  return "help.macros";
        case Blocks:  return "help.blocs";
        case Hmi:     return "help.hmi";
        default:      return {};
    }
}

std::string_view HelpTabStrip::tooltipOf(int tab) noexcept {
    switch (tab) {
        case General: return "L'aide g\xC3\xA9n\xC3\xA9rale : les \xC3\xA9" "crans, les formats, ce qu'il faut remplir";
        case Macros:  return "Chaque macro : ce qu'elle demande, ce qu'elle lit, ce qu'elle produit";
        case Blocks:  return "Les blocs (DFB) et les types (DDT) de la biblioth\xC3\xA8que, param\xC3\xA8tre par param\xC3\xA8tre";
        case Hmi:     return "L'IHM : les vues, les popups, la programmation, chaque objet et son exemple anim\xC3\xA9";
        default:      return {};
    }
}

int HelpTabStrip::tabOfLabel(std::string_view text) noexcept {
    for (int t = 0; t < kTabCount; ++t)
        if (label(t) == text) return t;
    return -1;
}

gfx::Rect HelpTabStrip::tabRect(int tab) const noexcept {
    return tab >= 0 && tab < kTabCount ? rects_[tab] : gfx::Rect{};
}

ui::SizeHint HelpTabStrip::sizeHint() const {
    ui::SizeHint h;
    h.preferred = {600.f, 42.f};
    h.minimum   = {200.f, 42.f};
    h.stretchX  = 1.f;
    return h;
}

void HelpTabStrip::onLayout() {
    const auto b = bounds();
    float x = b.x + 10.f;
    for (int t = 0; t < kTabCount; ++t) {
        const float w = kTabPadX + kTabIcon + kTabIconGap + ui::measureWidth(label(t), font_) + kTabPadX;
        rects_[t] = {x, b.y, w, b.h};
        x += w + kTabGap;
    }
    const float cw = 14.f + kTabIcon + 7.f + ui::measureWidth("Fermer", font_) + 14.f;
    close_ = {b.right() - cw - 10.f, b.y + (b.h - 32.f) * 0.5f, cw, 32.f};
}

int HelpTabStrip::partAt(gfx::Point p) const noexcept {
    for (int t = 0; t < kTabCount; ++t)
        if (rects_[t].contains(p)) return t;
    if (close_.contains(p)) return kTabCount;
    return -1;
}

void HelpTabStrip::clickForTest(int tab) {
    if (tab == kTabCount) { closeRequested->emit(); return; }
    if (tab >= 0 && tab < kTabCount && tab != current_) chosen->emit(tab);
}

void HelpTabStrip::onPaint(const ui::PaintContext& ctx) {
    const auto& th = ctx.theme;
    const auto& c  = th.color;
    const auto  b  = bounds();
    if (th.font.ui.v != font_.v) { font_ = th.font.ui; invalidateLayout(); }
    if (!hovered()) hover_ = -1;          // la souris est partie sans MouseMove ici

    ctx.r.fillRect(b, c.railBg);
    ctx.r.line({b.x, b.bottom() - 0.5f}, {b.right(), b.bottom() - 0.5f}, c.border, 1.f);

    for (int t = 0; t < kTabCount; ++t) {
        const auto r = rects_[t];
        const bool active = t == current_;
        if (active) {
            // L'onglet courant se detache : le fond de la page, et un trait d'accent.
            ctx.r.fillRect({r.x, r.y + 4.f, r.w, r.h - 4.f}, c.headerBg);
            ctx.r.line({r.x, r.y + 4.f}, {r.x, r.bottom()}, c.border, 1.f);
            ctx.r.line({r.right(), r.y + 4.f}, {r.right(), r.bottom()}, c.border, 1.f);
            ctx.r.fillRect({r.x, r.bottom() - 3.f, r.w, 3.f}, c.accent);
        } else if (hover_ == t) {
            ctx.r.fillRoundedRect({r.x + 2.f, r.y + 6.f, r.w - 4.f, r.h - 10.f}, th.brand.hover, 6.f);
        }
        const gfx::Color fg = active ? c.text : c.textMuted;
        const float iy = r.y + (r.h - kTabIcon) * 0.5f + (active ? 1.f : 0.f);
        ui::drawIcon(ctx.r, tabIcon(t), {r.x + kTabPadX, iy, kTabIcon, kTabIcon}, active ? c.accent : c.textMuted);
        const float tx = r.x + kTabPadX + kTabIcon + kTabIconGap;
        const float ty = r.y + (r.h - ctx.r.lineHeight(font_)) * 0.5f + (active ? 1.f : 0.f);
        const std::string text(label(t));
        ctx.r.drawText({tx, ty}, text, font_, fg);
        if (active) ctx.r.drawText({tx + 0.6f, ty}, text, font_, fg);   // un faux gras
    }

    // Fermer, a droite : ramene a l'ecran d'ou l'aide a ete ouverte.
    {
        const auto r = close_;
        if (hover_ == kTabCount)
            ctx.r.fillRoundedRect(r, th.brand.hover, r.h * 0.5f);
        ctx.r.strokeRect(r, c.border, 1.f);
        ui::drawIcon(ctx.r, ui::Icon::Close, {r.x + 14.f, r.y + (r.h - kTabIcon) * 0.5f, kTabIcon, kTabIcon}, c.textMuted);
        ctx.r.drawText({r.x + 14.f + kTabIcon + 7.f, r.y + (r.h - ctx.r.lineHeight(font_)) * 0.5f}, "Fermer", font_, c.text);
    }
}

ui::EventResult HelpTabStrip::onEvent(const ui::InputEvent& ev) {
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = bounds().contains(m->pos) ? partAt(m->pos) : -1;
        if (h != hover_) {
            hover_ = h;
            setTooltip(h >= 0 && h < kTabCount ? std::string(tooltipOf(h))
                       : h == kTabCount ? std::string("Revenir \xC3\xA0 l'\xC3\xA9" "cran d'o\xC3\xB9 l'aide a \xC3\xA9t\xC3\xA9 ouverte (\xC3\x89" "chap)")
                                        : std::string{});
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (d->button != ui::MouseButton::Left || !bounds().contains(d->pos)) return ui::EventResult::Ignored;
        pressed_ = partAt(d->pos);
        return pressed_ >= 0 ? ui::EventResult::Consumed : ui::EventResult::Ignored;
    }
    if (const auto* u = std::get_if<ui::MouseUp>(&ev)) {
        if (pressed_ < 0) return ui::EventResult::Ignored;
        const int was = pressed_;
        pressed_ = -1;
        if (partAt(u->pos) == was) clickForTest(was);
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ================================================================== palette ===
int paletteScore(std::string_view query, const PaletteItem& item) {
    if (query.empty()) return 1;
    const auto q = lowerAscii(query);
    const auto t = lowerAscii(item.title);

    if (t.rfind(q, 0) == 0) return 1000 - static_cast<int>(std::min<std::size_t>(t.size(), 200));

    // Un mot du titre qui commence par la requete : apres _ . espace /, ou a
    // une majuscule qui suit une minuscule (FaultCount -> "count").
    for (std::size_t i = 1; i < item.title.size(); ++i) {
        const char p = item.title[i - 1], ch = item.title[i];
        const bool boundary = p == '_' || p == '.' || p == ' ' || p == '/' || p == '-'
                           || (std::islower(static_cast<unsigned char>(p))
                               && std::isupper(static_cast<unsigned char>(ch)));
        if (boundary && t.compare(i, q.size(), q) == 0)
            return 700 - static_cast<int>(std::min<std::size_t>(t.size(), 200));
    }
    if (const auto at = t.find(q); at != std::string::npos)
        return 500 - static_cast<int>(std::min<std::size_t>(at, 200));
    if (lowerAscii(item.detail).find(q) != std::string::npos
        || lowerAscii(item.keywords).find(q) != std::string::npos) return 200;

    // Les lettres dans l'ordre, trous permis : plus les trous sont courts,
    // mieux c'est. "dfbmot" -> DFB_EQ_MOTOR.
    std::size_t at = 0;
    int gaps = 0;
    for (const char ch : q) {
        if (ch == ' ') continue;
        const auto found = t.find(ch, at);
        if (found == std::string::npos) return 0;
        gaps += static_cast<int>(found - at);
        at = found + 1;
    }
    return std::max(1, 100 - gaps);
}

std::vector<std::size_t> paletteFilter(std::string_view query,
                                       const std::vector<PaletteItem>& items, std::size_t limit) {
    std::vector<std::pair<int, std::size_t>> scored;
    scored.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
        if (const int s = paletteScore(query, items[i]); s > 0) scored.emplace_back(s, i);
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < scored.size() && i < limit; ++i) out.push_back(scored[i].second);
    return out;
}

std::string fitText(const gfx::IRenderer& r, const std::string& text, gfx::FontId font, float maxWidth) {
    if (maxWidth <= 0.f || text.empty()) return {};
    // Le demi-pixel : une place calculee pour ce texte meme sort parfois des
    // arrondis de la mise en page plus courte d'un rien.
    if (r.measure(text, font).width <= maxWidth + 0.5f) return text;
    const float dots = r.measure("...", font).width;
    if (maxWidth < dots) return {};
    std::size_t keep = std::min(text.size(), r.fitCharacters(text, font, maxWidth - dots));
    while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80) --keep;
    return text.substr(0, keep) + "...";
}

CommandPalette::CommandPalette(std::string id) : ui::Widget(std::move(id)) {
    setVisibility(ui::Visibility::Collapsed);
}

void CommandPalette::open(std::vector<PaletteItem> items) {
    items_ = std::move(items);
    query_.clear();
    current_ = first_ = 0;
    hover_ = -1;
    openedAt_ = -1.0;
    refilter();
    setVisibility(ui::Visibility::Visible);
    invalidate();
}

void CommandPalette::close() {
    setVisibility(ui::Visibility::Collapsed);
    invalidate();
}

void CommandPalette::typeForTest(std::string text) {
    query_ += text;
    refilter();
}

void CommandPalette::refilter() {
    results_ = paletteFilter(query_, items_);
    current_ = 0;
    first_ = 0;
    // La premiere entree DISPONIBLE : Entree sur une commande grisee ne ferait
    // rien, et on croirait la palette cassee.
    while (current_ < results_.size() && !items_[results_[current_]].enabled) ++current_;
    if (current_ >= results_.size()) current_ = 0;
    invalidate();
}

void CommandPalette::choose(std::size_t resultIndex) {
    if (resultIndex >= results_.size()) return;
    const auto& item = items_[results_[resultIndex]];
    if (!item.enabled) return;
    const std::string target = item.target;      // copie : close() peut tout vider plus tard
    close();
    chosen->emit(target);
}

int CommandPalette::rowAt(gfx::Point p) const noexcept {
    if (!panel_.contains(p) || p.y < listTop_) return -1;
    const auto row = static_cast<std::size_t>((p.y - listTop_) / rowH_) + first_;
    if (row >= results_.size() || row >= first_ + visibleRows_) return -1;
    return static_cast<int>(row);
}

void CommandPalette::onPaint(const ui::PaintContext& ctx) {
    const auto& th = ctx.theme;
    const auto& c  = th.color;
    const auto  b  = bounds();

    if (openedAt_ < 0.0) openedAt_ = ctx.time;
    const float p = std::clamp(static_cast<float>((ctx.time - openedAt_) * 1000.0)
                               / std::max(1.f, th.motion.pageFadeMs), 0.f, 1.f);
    if (p < 1.f) invalidate();

    // Le fond s'assombrit : ce qui est dessous existe toujours, mais ce n'est
    // plus la qu'on tape.
    ctx.r.fillRect(b, gfx::Color{0, 0, 0, static_cast<std::uint8_t>((th.isDark() ? 120.f : 70.f) * p)});

    const float w = std::min(680.f, b.w - 40.f);
    rowH_ = 46.f;
    visibleRows_ = static_cast<std::size_t>(std::clamp((b.h * 0.6f - 110.f) / rowH_, 3.f, 9.f));
    const std::size_t shown = std::min(visibleRows_, results_.size() - std::min(results_.size(), first_));
    const float inputH = 58.f, footH = 34.f;
    const float listH = (results_.empty() ? rowH_ : static_cast<float>(shown) * rowH_) + 8.f;
    const float h = inputH + listH + footH;
    panel_ = {b.x + (b.w - w) * 0.5f, b.y + std::max(24.f, b.h * 0.12f) - (1.f - p) * 10.f, w, h};

    const float radius = th.metric.radius > 0.f ? 12.f : 0.f;
    ctx.r.fillRoundedRect({panel_.x, panel_.y + 4.f, panel_.w, panel_.h}, th.brand.cardShadow, radius);
    ctx.r.fillRoundedRect({panel_.x - 1.f, panel_.y - 1.f, panel_.w + 2.f, panel_.h + 2.f},
                          th.brand.cardBorder, radius + 1.f);
    ctx.r.fillRoundedRect(panel_, th.brand.card, radius);

    // ---- le champ ------------------------------------------------------------------
    const float ix = panel_.x + 18.f;
    ui::drawIcon(ctx.r, ui::Icon::Search, {ix, panel_.y + (inputH - 18.f) * 0.5f, 18.f, 18.f},
                 c.accent);
    const auto font = th.font.lead;
    const float ty = panel_.y + (inputH - ctx.r.lineHeight(font)) * 0.5f;
    const float tx = ix + 30.f;
    if (query_.empty()) {
        ctx.r.drawText({tx, ty}, "Aller \xC3\xA0 : un bloc, un dossier, une commande...", font,
                       c.textMuted);
    } else {
        ctx.r.drawText({tx, ty}, query_, font, c.text);
    }
    // Le curseur clignote : une demi-seconde allume, une demi-seconde eteint.
    if (std::fmod(ctx.time, 1.0) < 0.55) {
        const float cx = tx + (query_.empty() ? 0.f : ctx.r.measure(query_, font).width) + 1.f;
        ctx.r.fillRect({cx, ty + 2.f, 2.f, ctx.r.lineHeight(font) - 4.f}, c.accent);
    }
    invalidate();                       // le curseur vit
    ctx.r.line({panel_.x, panel_.y + inputH}, {panel_.right(), panel_.y + inputH}, c.border, 1.f);

    // ---- la liste ------------------------------------------------------------------
    listTop_ = panel_.y + inputH + 4.f;
    if (results_.empty()) {
        ctx.r.drawText({panel_.x + 22.f, listTop_ + (rowH_ - ctx.r.lineHeight(th.font.ui)) * 0.5f},
                       "Rien pour \xC2\xAB " + query_ + " \xC2\xBB", th.font.ui, c.textMuted);
    }
    for (std::size_t k = 0; k < shown; ++k) {
        const std::size_t ri = first_ + k;
        const auto& it = items_[results_[ri]];
        const gfx::Rect row{panel_.x + 8.f, listTop_ + static_cast<float>(k) * rowH_,
                            panel_.w - 16.f, rowH_};
        if (ri == current_) {
            ctx.r.fillRoundedRect(row, c.selectionBg, 8.f);
            ctx.r.fillRoundedRect({row.x + 2.f, row.y + 10.f, 3.f, row.h - 20.f}, c.accent, 1.5f);
        } else if (static_cast<int>(ri) == hover_) {
            ctx.r.fillRoundedRect(row, th.brand.hover, 8.f);
        }
        const bool on = it.enabled;
        const float side = 18.f;
        if (it.icon != ui::Icon::None)
            ui::drawIcon(ctx.r, it.icon, {row.x + 14.f, row.y + (rowH_ - side) * 0.5f, side, side},
                         on ? th.tone(it.tone, c.textMuted) : c.textDisabled);
        const float x = row.x + 46.f;
        // Le texte s'arrete avant le raccourci, et un resume trop long se coupe
        // au lieu de deborder du panneau.
        const float textRight = row.right() - 12.f
            - (it.shortcut.empty() ? 0.f : ctx.r.measure(it.shortcut, th.font.caption).width + 26.f);
        const float textW = std::max(0.f, textRight - x);
        const auto textColour = ri == current_ ? c.selectionText : (on ? c.text : c.textDisabled);
        ctx.r.drawText({x, row.y + 5.f}, fitText(ctx.r, it.title, th.font.ui, textW), th.font.ui, textColour);

        // Ce qui correspond, souligne : on voit POURQUOI la ligne est la.
        if (!query_.empty()) {
            const auto low = lowerAscii(it.title);
            const auto at = low.find(lowerAscii(query_));
            if (at != std::string::npos) {
                const float x0 = x + ctx.r.measure(std::string_view(it.title).substr(0, at), th.font.ui).width;
                const float mw = std::min(ctx.r.measure(std::string_view(it.title).substr(at, query_.size()),
                                                        th.font.ui).width, textRight - x0);
                if (mw > 0.f)
                    ctx.r.fillRect({x0, row.y + 5.f + ctx.r.lineHeight(th.font.ui), mw, 2.f}, c.accent);
            }
        }
        if (!it.detail.empty())
            ctx.r.drawText({x, row.y + 25.f}, fitText(ctx.r, it.detail, th.font.caption, textW),
                           th.font.caption, ri == current_ ? c.selectionText : c.textMuted);
        if (!it.shortcut.empty()) {
            const auto sf = th.font.caption;
            const float sw = ctx.r.measure(it.shortcut, sf).width + 14.f;
            const float sh = ctx.r.lineHeight(sf) + 6.f;
            const gfx::Rect pill{row.right() - sw - 12.f, row.y + (rowH_ - sh) * 0.5f, sw, sh};
            ctx.r.fillRoundedRect(pill, c.border, 5.f);
            ctx.r.fillRoundedRect({pill.x + 1.f, pill.y + 1.f, pill.w - 2.f, pill.h - 2.f},
                                  c.rowAltBg, 4.f);
            ctx.r.drawText({pill.x + 7.f, pill.y + 3.f}, it.shortcut, sf, c.textMuted);
        }
    }

    // ---- le pied : les touches -------------------------------------------------------
    const float fy = panel_.bottom() - footH;
    ctx.r.line({panel_.x, fy}, {panel_.right(), fy}, c.border, 1.f);
    const std::string aide = std::to_string(results_.size())
        + (results_.size() > 1 ? " r\xC3\xA9sultats" : " r\xC3\xA9sultat")
        + "   \xC2\xB7   Haut / Bas pour choisir   \xC2\xB7   Entr\xC3\xA9" "e pour ouvrir   \xC2\xB7   \xC3\x89" "chap pour fermer";
    ctx.r.drawText({panel_.x + 18.f, fy + (footH - ctx.r.lineHeight(th.font.caption)) * 0.5f},
                   aide, th.font.caption, c.textMuted);
}

ui::EventResult CommandPalette::onEvent(const ui::InputEvent& ev) {
    // MODALE : tant qu'elle est ouverte, rien ne passe dessous. Une touche F5
    // qui lancerait l'essai sous la palette, ou un clic qui changerait l'item
    // de l'arbre derriere elle, est un geste qu'on n'a pas fait.
    if (const auto* t = std::get_if<ui::TextInput>(&ev)) {
        query_ += t->utf8;
        refilter();
        return ui::EventResult::Consumed;
    }
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        switch (k->key) {
            case ui::Key::Escape:    close(); break;
            case ui::Key::Backspace: popUtf8(query_); refilter(); break;
            case ui::Key::Return:    choose(current_); break;
            case ui::Key::Down:
            case ui::Key::Up: {
                if (results_.empty()) break;
                const bool down = k->key == ui::Key::Down;
                std::size_t i = current_;
                for (std::size_t n = 0; n < results_.size(); ++n) {
                    i = down ? (i + 1) % results_.size() : (i + results_.size() - 1) % results_.size();
                    if (items_[results_[i]].enabled) break;
                }
                current_ = i;
                if (current_ < first_) first_ = current_;
                if (current_ >= first_ + visibleRows_) first_ = current_ + 1 - visibleRows_;
                invalidate();
                break;
            }
            default: break;
        }
        return ui::EventResult::Consumed;
    }
    if (const auto* m = std::get_if<ui::MouseMove>(&ev)) {
        const int h = rowAt(m->pos);
        if (h != hover_) { hover_ = h; invalidate(); }
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
        if (!panel_.contains(d->pos)) { close(); return ui::EventResult::Consumed; }
        if (const int r = rowAt(d->pos); r >= 0) choose(static_cast<std::size_t>(r));
        return ui::EventResult::Consumed;
    }
    if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
        if (results_.size() > visibleRows_) {
            const auto maxFirst = results_.size() - visibleRows_;
            if (w->dy < 0.f) first_ = std::min(maxFirst, first_ + 1);
            else if (first_ > 0) --first_;
            invalidate();
        }
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Consumed;
}

} // namespace app
