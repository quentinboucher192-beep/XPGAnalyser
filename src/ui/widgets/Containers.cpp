// =============================================================================
//  ui/widgets/Containers.cpp
// =============================================================================
#include "Containers.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>
#include <cmath>
#include <numeric>

namespace ui {

// ============================================================== GroupBox ====
GroupBox::GroupBox(std::string title, std::string id)
    : Widget(std::move(id)), title_(std::move(title)) {
    auto body = std::make_unique<BoxLayout>(Orientation::Vertical, this->id() + ".body");
    body_ = &addChild(std::move(body));
    setPadding({22.f, 8.f, 8.f, 8.f});
}

void GroupBox::setTitle(std::string t) { title_ = std::move(t); invalidate(); }

void GroupBox::setCollapsed(bool c) {
    if (!collapsible_ || collapsed_ == c) return;
    collapsed_ = c;
    body_->setVisibility(c ? Visibility::Collapsed : Visibility::Visible);
    invalidateLayout();
}

SizeHint GroupBox::sizeHint() const {
    SizeHint h = body_ ? body_->sizeHint() : SizeHint{};
    h.preferred.h += 30.f;
    h.minimum.h   += 30.f;
    return h;
}

void GroupBox::onLayout() {
    if (body_ && !collapsed_) body_->setBounds(contentRect());
}

void GroupBox::onPaint(const PaintContext& ctx) {
    // UNE CARTE, PAS UN CADRE. L'ancien bandeau gris plein sur toute la largeur
    // pesait autant que le contenu ; ici la surface est surelevee (bordure fine,
    // ombre d'un pixel), et le titre est une legende discrete en tete.
    const auto& th = ctx.theme;
    const auto& c  = th.color;
    const auto  r  = bounds();
    const float radius = th.metric.radius > 0.f ? 8.f : 0.f;

    ctx.r.fillRect(r, c.windowBg);
    const gfx::Rect card{r.x + 1.f, r.y + 1.f, std::max(0.f, r.w - 2.f), std::max(0.f, r.h - 3.f)};
    ctx.r.fillRoundedRect({card.x, card.y + 1.f, card.w, card.h}, th.brand.cardShadow, radius);
    ctx.r.fillRoundedRect(card, th.brand.cardBorder, radius);
    ctx.r.fillRoundedRect({card.x + 1.f, card.y + 1.f, std::max(0.f, card.w - 2.f),
                           std::max(0.f, card.h - 2.f)},
                          th.brand.card, std::max(0.f, radius - 1.f));

    float textX = r.x + 10.f;
    const float headerMid = r.y + 12.f;
    if (collapsible_) {
        // Un chevron, comme l'arbre : la meme forme veut dire la meme chose.
        const float cx = r.x + 12.f, cy = headerMid, k = 3.5f;
        const gfx::Color m = c.textMuted;
        if (collapsed_) {
            ctx.r.line({cx - k * 0.5f, cy - k}, {cx + k * 0.5f, cy}, m, 1.5f);
            ctx.r.line({cx + k * 0.5f, cy}, {cx - k * 0.5f, cy + k}, m, 1.5f);
        } else {
            ctx.r.line({cx - k, cy - k * 0.5f}, {cx, cy + k * 0.5f}, m, 1.5f);
            ctx.r.line({cx, cy + k * 0.5f}, {cx + k, cy - k * 0.5f}, m, 1.5f);
        }
        textX += 12.f;
    }
    const auto font = th.font.caption;
    const float ty = headerMid - ctx.r.lineHeight(font) * 0.5f;
    ctx.r.drawText({textX, ty}, title_, font, c.textMuted);
    ctx.r.drawText({textX + 0.5f, ty}, title_, font, c.textMuted);   // faux gras
}

EventResult GroupBox::onEvent(const InputEvent& ev) {
    if (!collapsible_) return EventResult::Ignored;
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        const gfx::Rect header{bounds().x, bounds().y, bounds().w, 20.f};
        if (header.contains(d->pos)) { setCollapsed(!collapsed_); return EventResult::Consumed; }
    }
    return EventResult::Ignored;
}

// ============================================================== Splitter ====
Splitter::Splitter(Orientation o, std::string id)
    : Widget(std::move(id)), orientation_(o) {}

Widget& Splitter::addPane(WidgetPtr w, float ratio, float minExtent) {
    Widget& ref = addChild(std::move(w));
    panes_.push_back(Pane{&ref, ratio, minExtent, false});
    invalidateLayout();
    return ref;
}

void Splitter::setRatios(const std::vector<float>& r) {
    for (std::size_t i = 0; i < panes_.size() && i < r.size(); ++i) panes_[i].ratio = r[i];
    invalidateLayout();
}

std::vector<float> Splitter::ratios() const {
    std::vector<float> out;
    out.reserve(panes_.size());
    for (const auto& p : panes_) out.push_back(p.ratio);
    return out;
}

void Splitter::collapsePane(std::size_t index, bool collapsed) {
    if (index >= panes_.size()) return;
    panes_[index].collapsed = collapsed;
    panes_[index].w->setVisibility(collapsed ? Visibility::Collapsed : Visibility::Visible);
    invalidateLayout();
}

void Splitter::onLayout() {
    const auto area = contentRect();
    if (panes_.empty()) return;

    const bool horizontal = orientation_ == Orientation::Horizontal;
    const float thickness = 5.f;
    const float total = (horizontal ? area.w : area.h)
                      - thickness * static_cast<float>(panes_.size() - 1);

    float ratioSum = 0.f;
    for (const auto& p : panes_) if (!p.collapsed) ratioSum += p.ratio;
    if (ratioSum <= 0.f) ratioSum = 1.f;

    float cursor = horizontal ? area.x : area.y;
    for (auto& p : panes_) {
        float extent = p.collapsed ? 0.f
                                   : std::max(p.minExtent, total * (p.ratio / ratioSum));
        extent = std::min(extent, std::max(0.f, (horizontal ? area.right() : area.bottom()) - cursor));
        if (horizontal) p.w->setBounds({cursor, area.y, extent, area.h});
        else            p.w->setBounds({area.x, cursor, area.w, extent});
        cursor += extent + thickness;
    }
}

int Splitter::handleAt(gfx::Point p) const {
    const bool horizontal = orientation_ == Orientation::Horizontal;
    for (std::size_t i = 0; i + 1 < panes_.size(); ++i) {
        const auto b = panes_[i].w->bounds();
        const gfx::Rect handle = horizontal ? gfx::Rect{b.right(), b.y, 5.f, b.h}
                                            : gfx::Rect{b.x, b.bottom(), b.w, 5.f};
        if (handle.contains(p)) return static_cast<int>(i);
    }
    return -1;
}

void Splitter::onPaint(const PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(bounds(), c.windowBg);
    const bool horizontal = orientation_ == Orientation::Horizontal;
    for (std::size_t i = 0; i + 1 < panes_.size(); ++i) {
        const auto b = panes_[i].w->bounds();
        const gfx::Rect handle = horizontal ? gfx::Rect{b.right(), b.y, 5.f, b.h}
                                            : gfx::Rect{b.x, b.bottom(), b.w, 5.f};
        ctx.r.fillRect(handle, dragHandle_ == static_cast<int>(i) ? c.accent : c.windowBg);
        ctx.r.line(horizontal ? gfx::Point{handle.x + 2.f, handle.y}
                              : gfx::Point{handle.x, handle.y + 2.f},
                   horizontal ? gfx::Point{handle.x + 2.f, handle.bottom()}
                              : gfx::Point{handle.right(), handle.y + 2.f},
                   c.border, 1.f);
    }
}

EventResult Splitter::onEvent(const InputEvent& ev) {
    const bool horizontal = orientation_ == Orientation::Horizontal;

    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        const int h = handleAt(d->pos);
        if (h >= 0) {
            dragHandle_ = h;
            dragOrigin_ = horizontal ? d->pos.x : d->pos.y;
            invalidate();
            return EventResult::Consumed;
        }
    }
    if (const auto* m = std::get_if<MouseMove>(&ev); m && dragHandle_ >= 0) {
        const auto i = static_cast<std::size_t>(dragHandle_);
        const float delta = (horizontal ? m->pos.x : m->pos.y) - dragOrigin_;
        const float total = horizontal ? contentRect().w : contentRect().h;
        if (total > 0.f && i + 1 < panes_.size()) {
            // Ratios move as a pair, so the panes on either side of the handle
            // trade space and everything else stays where the user put it.
            //
            // Rapportes a la somme des panneaux MONTRES : onLayout partage la
            // place entre eux seuls. Divise par la largeur totale, le pas etait
            // trop grand des qu'un panneau etait masque, et le separateur
            // partait plus loin que la souris.
            float shown = 0.f;
            for (const auto& p : panes_) if (!p.collapsed) shown += p.ratio;
            if (shown <= 0.f) shown = 1.f;
            const float step = delta / total * shown;
            const float a = panes_[i].ratio + step;
            const float b = panes_[i + 1].ratio - step;
            if (a > 0.03f && b > 0.03f) {
                panes_[i].ratio = a;
                panes_[i + 1].ratio = b;
                dragOrigin_ = horizontal ? m->pos.x : m->pos.y;
                invalidateLayout();
            }
        }
        return EventResult::Consumed;
    }
    if (std::get_if<MouseUp>(&ev) && dragHandle_ >= 0) {
        dragHandle_ = -1;
        invalidate();
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

// ============================================================ TabControl ====
TabControl::TabControl(std::string id) : Widget(std::move(id)) {
    // Lot 19 : la liste de tous les onglets (le bouton a droite de la bande).
    auto list = std::make_unique<PopupMenu>(this->id() + ".tabList");
    tabList_ = &static_cast<PopupMenu&>(addChild(std::move(list)));
    links_ += tabList_->itemChosen->connect([this](int index) {
        if (index >= 0) setCurrentIndex(static_cast<std::size_t>(index));
    });
}

std::size_t TabControl::addTab(Tab meta, WidgetPtr page) {
    Widget& ref = addChild(std::move(page));
    tabs_.push_back(Entry{std::move(meta), &ref, {}, false, false});
    if (tabs_.size() > 1) ref.setVisibility(Visibility::Collapsed);
    invalidateLayout();
    return tabs_.size() - 1;
}

void TabControl::removeTab(std::size_t index) {
    (void)takeTab(index);
}

WidgetPtr TabControl::takeTab(std::size_t index) {
    if (index >= tabs_.size()) return nullptr;
    const bool wasCurrent = index == current_;
    WidgetPtr page = removeChild(*tabs_[index].page);
    if (page) page->setVisibility(Visibility::Visible);
    tabs_.erase(tabs_.begin() + static_cast<long>(index));
    if (tabs_.empty()) { current_ = 0; invalidateLayout(); return page; }
    // UN ONGLET A GAUCHE DU COURANT : le courant garde SA page, son indice
    // recule. Sans cela l'indice designait la page voisine, restee repliee, et
    // la zone des onglets restait vide jusqu'au prochain changement d'onglet.
    if (index < current_) --current_;
    // L'onglet courant ferme : son voisin de droite (ou le dernier) le remplace,
    // et se montre.
    if (current_ >= tabs_.size()) current_ = tabs_.size() - 1;
    for (std::size_t i = 0; i < tabs_.size(); ++i)
        tabs_[i].page->setVisibility(i == current_ ? Visibility::Visible : Visibility::Collapsed);
    revealCurrent_ = true;
    invalidateLayout();
    if (wasCurrent) currentChanged->emit(current_);
    return page;
}

void TabControl::setCurrentIndex(std::size_t index) {
    if (index >= tabs_.size() || index == current_) return;
    current_ = index;
    revealCurrent_ = true;
    for (std::size_t i = 0; i < tabs_.size(); ++i)
        tabs_[i].page->setVisibility(i == current_ ? Visibility::Visible : Visibility::Collapsed);
    invalidateLayout();
    currentChanged->emit(index);
}

void TabControl::setTabModified(std::size_t index, bool modified) {
    if (index < tabs_.size() && tabs_[index].meta.modified != modified) {
        tabs_[index].meta.modified = modified;
        invalidateLayout();          // le point prend de la place
    }
}

void TabControl::setTabTitle(std::size_t index, std::string title) {
    if (index >= tabs_.size() || tabs_[index].meta.title == title) return;
    tabs_[index].meta.title = std::move(title);
    invalidateLayout();              // la largeur de l'en-tete change
}

void TabControl::setTabIcon(std::size_t index, Icon icon) {
    if (index >= tabs_.size() || tabs_[index].meta.icon == icon) return;
    tabs_[index].meta.icon = icon;
    invalidateLayout();
}

void TabControl::setTabBadge(std::size_t index, std::string badge, Tone tone) {
    if (index >= tabs_.size()) return;
    auto& m = tabs_[index].meta;
    if (m.badge == badge && m.badgeTone == tone) return;
    m.badge = std::move(badge);
    m.badgeTone = tone;
    invalidateLayout();
}

void TabControl::setTabLive(std::size_t index, bool live) {
    if (index >= tabs_.size() || tabs_[index].meta.live == live) return;
    tabs_[index].meta.live = live;
    invalidateLayout();
}

void TabControl::setTabHidden(std::size_t index, bool hidden) {
    if (index >= tabs_.size() || tabs_[index].hidden == hidden) return;
    tabs_[index].hidden = hidden;
    // L'onglet ouvert qui se cache : le premier montre prend sa place.
    if (hidden && index == current_)
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            if (!tabs_[i].hidden) {
                setCurrentIndex(i);
                break;
            }
    invalidateLayout();
    invalidate();
}

std::string TabControl::shortBadge(std::string_view badge) {
    std::string out;
    for (const char ch : badge) {
        const auto u = static_cast<unsigned char>(ch);
        if (std::isdigit(u) || ch == '+' || ch == '/' || ch == '.' || ch == ',' || ch == ' ' || ch == '%' || ch == '-')
            out += ch;
        else
            break;
    }
    out.erase(std::remove(out.begin(), out.end(), ' '), out.end());
    while (!out.empty() && (out.back() == '+' || out.back() == '/' || out.back() == '-' || out.back() == '.' || out.back() == ','))
        out.pop_back();
    return out;
}

bool TabControl::tabListOpen() const noexcept { return tabList_ && tabList_->isOpen(); }

void TabControl::openTabList() {
    if (!tabList_ || tabs_.empty()) return;
    std::vector<PopupMenu::Item> items;
    items.reserve(tabs_.size());
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].hidden) continue;           // 1.12.0 : un onglet cache n'est pas dans la liste
        PopupMenu::Item it;
        it.label = (i == current_ ? "\xE2\x80\xA2 " : "") + tabs_[i].meta.title;
        it.shortcut = tabs_[i].meta.badge;
        it.icon = tabs_[i].meta.icon;
        it.id = static_cast<int>(i);
        items.push_back(std::move(it));
    }
    tabList_->setItems(std::move(items));
    const gfx::Rect at = listButton_.w > 0.f ? listButton_ : gfx::Rect{bounds().right() - 40.f, bounds().y, 40.f, tabHeight_};
    gfx::Size surface = surface_;
    if (surface.w <= 0.f) surface = {bounds().right() + 400.f, bounds().bottom() + 400.f};
    tabList_->openAt({std::max(0.f, at.right() - 300.f), at.bottom()}, surface);
    invalidate();
}

bool TabControl::hitTest(gfx::Point local) const {
    lastPointer_ = local;
    return Widget::hitTest(local);
}

namespace {
// Les largeurs d'un onglet, calculees au meme endroit pour la mise en page et
// la peinture : deux calculs qui divergent, et le titre deborde sur le voisin.
constexpr float kTabPadX    = 14.f;
constexpr float kTabPadCompact = 8.f;
constexpr float kTabIconGap = 7.f;
constexpr float kTabDot     = 12.f;   // le point « modifie », marge comprise
constexpr float kTabLive    = 14.f;   // la LED « en cours »
constexpr float kListW      = 58.f;   // le bouton de la liste
constexpr float kArrowW     = 22.f;   // une fleche
constexpr float kCompactTitleMax = 90.f;   // un onglet sans icone, reduit : son titre coupe

float badgeWidth(std::string_view badge) {
    return badge.empty() ? 0.f : measureWidth(badge, gfx::FontId{13}) + 14.f + 8.f;
}
// La pastille montree : entiere, courte, ou un point (rien a garder).
std::string shownBadge(const TabControl::Tab& t, bool shortened) {
    if (!shortened || t.badge.empty()) return t.badge;
    return TabControl::shortBadge(t.badge);
}
// La pastille d'un en-tete (onglet ou barre de tuile), a `x` ; rend la largeur
// prise. Une fonction et plus un lambda de onPaint : la tuile (lot 7,
// disposition) dessine la meme.
float paintTabBadge(const PaintContext& ctx, const TabControl::Tab& t, const std::string& text, float x,
                    const gfx::Rect& hr, float th) {
    const auto& theme = ctx.theme;
    const auto& c = theme.color;
    const bool toned = t.badgeTone != Tone::None;
    const auto col = theme.tone(t.badgeTone, c.textMuted);
    if (text.empty()) {
        // Un point : il reste quelque chose a dire (l'infobulle et la liste le disent).
        const float d = 7.f;
        ctx.r.fillRoundedRect({x + 4.f, hr.y + (th - d) * 0.5f, d, d}, toned ? col : c.textMuted, d * 0.5f);
        return 12.f;
    }
    const auto font = theme.font.caption;
    const float blh = ctx.r.lineHeight(font);
    const float bw = ctx.r.measure(text, font).width + 14.f;
    const float bh = std::min(th - 10.f, blh + 4.f);
    const gfx::Rect pill{x + 8.f, hr.y + (th - bh) * 0.5f, bw, bh};
    if (toned) {
        ctx.r.fillRoundedRect(pill, col.withAlpha(theme.isDark() ? 60 : 36), bh * 0.5f);
    } else {
        ctx.r.fillRoundedRect(pill, c.border, bh * 0.5f);
        ctx.r.fillRoundedRect({pill.x + 1.f, pill.y + 1.f, pill.w - 2.f, pill.h - 2.f},
                              c.rowAltBg, bh * 0.5f - 1.f);
    }
    ctx.r.drawText({pill.x + 7.f, pill.y + (bh - blh) * 0.5f}, text, font,
                   toned ? theme.onSurface(col) : c.textMuted);
    return bw + 8.f;
}
// Lot 7 : un cadre en tirets (le renderer ne trace que des traits pleins) -
// l'onglet que vise un menu ouvert.
void dashedRect(gfx::IRenderer& r, const gfx::Rect& box, gfx::Color col) {
    constexpr float kDash = 4.f, kGap = 3.f;
    for (float x = box.x; x < box.right(); x += kDash + kGap) {
        const float w = std::min(kDash, box.right() - x);
        r.fillRect({x, box.y, w, 1.f}, col);
        r.fillRect({x, box.bottom() - 1.f, w, 1.f}, col);
    }
    for (float y = box.y; y < box.bottom(); y += kDash + kGap) {
        const float h = std::min(kDash, box.bottom() - y);
        r.fillRect({box.x, y, 1.f, h}, col);
        r.fillRect({box.right() - 1.f, y, 1.f, h}, col);
    }
}
} // namespace

void TabControl::onLayout() {
    const auto  area = contentRect();
    const float th = barHeight();
    const std::size_t n = tabs_.size();
    if (tabList_) tabList_->setBounds(bounds());      // la liste peint par-dessus tout

    // Lot 7 (disposition) : une tuile - un seul en-tete, sur toute la largeur.
    // Les autres (il ne devrait pas y en avoir) n'ont pas de place : ni clic,
    // ni menu ne les atteignent par erreur.
    if (tile_) {
        listButton_ = arrowLeft_ = arrowRight_ = {};
        viewport_ = {area.x, area.y, area.w, th};
        fit_ = Fit::Full;
        scrollOffset_ = 0.f;
        stripWidth_ = area.w;
        for (std::size_t i = 0; i < n; ++i) {
            tabs_[i].compact = false;
            tabs_[i].shortBadge = false;
            tabs_[i].headerRect = {area.x, area.y, i == current_ ? area.w : 0.f, th};
        }
        if (current_ < n) tabs_[current_].page->setBounds({area.x, area.y + th, area.w, std::max(0.f, area.h - th)});
        return;
    }

    const auto width = [&](const Entry& t, bool shortened, bool compact) {
        float w = 0.f;
        if (t.hidden) return 0.f;               // 1.12.0 : pas de place
        if (compact) {
            w = 2 * kTabPadCompact;
            if (t.meta.icon != Icon::None) w += 16.f;
            else w += std::min(kCompactTitleMax, measureWidth(t.meta.title, tabFont_));
        } else {
            w = measureWidth(t.meta.title, tabFont_) + 2 * kTabPadX;
            if (t.meta.icon != Icon::None) w += 16.f + kTabIconGap;
        }
        if (t.meta.closable) w += 20.f;
        if (t.meta.modified) w += kTabDot;
        if (t.meta.live)     w += kTabLive;
        if (!t.meta.badge.empty()) {
            const auto b = shownBadge(t.meta, shortened || compact);
            w += b.empty() ? 12.f : badgeWidth(b) - (compact ? 4.f : 0.f);
        }
        return w;
    };

    std::vector<float> widths(n, 0.f);
    float total = 0.f;
    for (std::size_t i = 0; i < n; ++i) {
        tabs_[i].compact = false;
        tabs_[i].shortBadge = false;
        widths[i] = width(tabs_[i], false, false);
        total += widths[i];
    }
    stripWidth_ = total;
    listButton_ = arrowLeft_ = arrowRight_ = {};
    viewport_ = {area.x, area.y, area.w, th};
    fit_ = Fit::Full;
    if (total > area.w && n > 1) {
        const float avail = std::max(40.f, area.w - kListW);
        listButton_ = {area.right() - kListW, area.y, kListW, th};
        viewport_.w = avail;
        float shortTotal = 0.f;
        for (std::size_t i = 0; i < n; ++i) shortTotal += width(tabs_[i], true, false);
        if (shortTotal <= avail) {
            fit_ = Fit::ShortBadges;
            total = 0.f;
            for (std::size_t i = 0; i < n; ++i) {
                tabs_[i].shortBadge = true;
                widths[i] = width(tabs_[i], true, false);
                total += widths[i];
            }
        } else {
            total = 0.f;
            for (std::size_t i = 0; i < n; ++i) {
                const bool compact = i != current_;
                tabs_[i].compact = compact;
                tabs_[i].shortBadge = compact;
                widths[i] = width(tabs_[i], compact, compact);
                total += widths[i];
            }
            fit_ = total <= avail ? Fit::Compact : Fit::Scroll;
            if (fit_ == Fit::Scroll) {
                arrowLeft_ = {area.x, area.y, kArrowW, th};
                arrowRight_ = {area.x + avail - kArrowW, area.y, kArrowW, th};
                viewport_ = {area.x + kArrowW, area.y, std::max(20.f, avail - 2 * kArrowW), th};
            }
        }
        stripWidth_ = total;
    }

    // Trop d'onglets meme reduits : la barre defile (fleches, molette), et
    // l'onglet qu'on vient de choisir est toujours amene en vue - de meme quand
    // la largeur change (un volet qu'on retrecit ne doit pas le cacher).
    if (std::abs(area.w - lastWidth_) > 0.5f) revealCurrent_ = true;
    lastWidth_ = area.w;
    float shift = fit_ == Fit::Scroll || fit_ == Fit::Full ? -scrollOffset_ : 0.f;
    if (revealCurrent_ && current_ < n) {
        float left = 0.f;
        for (std::size_t i = 0; i < current_; ++i) left += widths[i];
        const float right = left + widths[current_];
        if (left - shift < 0.f) shift = left;
        if (right - shift > viewport_.w) shift = right - viewport_.w;
        revealCurrent_ = false;
    }
    shift = std::clamp(shift, 0.f, std::max(0.f, total - viewport_.w));
    scrollOffset_ = -shift;
    float x = viewport_.x - shift;
    for (std::size_t i = 0; i < n; ++i) {
        tabs_[i].headerRect = {x, area.y, widths[i], th};
        x += widths[i];
    }
    const gfx::Rect page{area.x, area.y + th, area.w, std::max(0.f, area.h - th)};
    for (std::size_t i = 0; i < n; ++i)
        if (i == current_) tabs_[i].page->setBounds(page);
}

void TabControl::onPaint(const PaintContext& ctx) {
    const auto& theme = ctx.theme;
    const auto& c = theme.color;
    const auto  r = bounds();
    surface_ = ctx.r.surfaceSize();
    if (tabHeight_ != theme.metric.tabHeight || tabFont_.v != theme.font.ui.v) {
        // La hauteur ET la police : le theme a fort contraste ecrit en 22 px,
        // et des onglets mesures en 16 se chevauchaient.
        tabHeight_ = theme.metric.tabHeight;
        tabFont_   = theme.font.ui;
        const_cast<TabControl*>(this)->invalidateLayout();
    }
    if (tile_) { paintTile(ctx); return; }   // lot 7 (disposition) : une barre de titre
    const float th = tabHeight_;

    // L'onglet sous la souris (d'apres le dernier evenement de souris recu :
    // une page qui prend le deplacement ne laisse pas un survol perime).
    const gfx::Point mouse{r.x + lastPointer_.x, r.y + lastPointer_.y};
    const bool inStrip = hovered() && lastPointer_.y >= 0.f && lastPointer_.y < th;
    int hover = -1;
    if (inStrip)
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            if (tabs_[i].headerRect.contains(mouse) && viewport_.contains(mouse)) hover = static_cast<int>(i);
    hoverTab_ = hover;
    // L'infobulle : le nom et la pastille entiere d'un onglet reduit ; la liste.
    std::string tip;
    if (inStrip && hover >= 0) {
        const auto& t = tabs_[static_cast<std::size_t>(hover)];
        if (t.compact || (t.shortBadge && !t.meta.badge.empty()))
            tip = t.meta.title + (t.meta.badge.empty() ? std::string() : " \xE2\x80\x94 " + t.meta.badge)
                + "\nCtrl+Tab : onglet suivant \xC2\xB7 Ctrl+Maj+Tab : pr\xC3\xA9" "c\xC3\xA9" "dent";
    } else if (inStrip && listButton_.contains(mouse)) {
        tip = "Tous les onglets (" + std::to_string(tabs_.size()) + ")\nCtrl+Tab : onglet suivant \xC2\xB7 Ctrl+Maj+Tab : pr\xC3\xA9" "c\xC3\xA9" "dent";
    } else if (inStrip && (arrowLeft_.contains(mouse) || arrowRight_.contains(mouse))) {
        tip = "Faire d\xC3\xA9" "filer les onglets (la molette aussi)";
    }
    if (tip != tooltip()) setTooltip(tip);

    ctx.r.fillRect(r, c.panelBg);
    ctx.r.fillRect({r.x, r.y, r.w, th}, c.headerBg);
    ctx.r.line({r.x, r.y + th - 0.5f}, {r.right(), r.y + th - 0.5f}, c.border, 1.f);
    ctx.r.pushClip(viewport_.intersect({r.x, r.y, r.w, th}));      // des en-tetes qui debordent : coupes au bord

    const float lh = ctx.r.lineHeight(theme.font.ui);
    const auto drawBadge = [&](const Tab& t, const std::string& text, float x, const gfx::Rect& hr) -> float {
        return paintTabBadge(ctx, t, text, x, hr, th);
    };

    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        const auto& t = tabs_[i];
        const bool active = i == current_;
        const bool hoverThis = !active && hoverTab_ == static_cast<int>(i);
        const auto& hr = t.headerRect;
        if (t.hidden || hr.w <= 0.f) continue;  // 1.12.0 : un onglet cache
        if (hr.right() < viewport_.x || hr.x > viewport_.right()) continue;
        if (active) {
            ctx.r.fillRect({hr.x, hr.y, hr.w, th}, c.panelBg);
            ctx.r.line({hr.x, hr.y}, {hr.x, hr.y + th}, c.border, 1.f);
            ctx.r.line({hr.right(), hr.y}, {hr.right(), hr.y + th}, c.border, 1.f);
        } else if (hoverThis) {
            ctx.r.fillRoundedRect({hr.x + 3.f, hr.y + 4.f, hr.w - 6.f, th - 8.f}, theme.brand.hover, 6.f);
        }
        const auto fg = active || hoverThis ? c.text : c.textMuted;
        float x = hr.x + (t.compact ? kTabPadCompact : kTabPadX);

        if (t.meta.live) {
            // La LED « ca tourne », qui respire : une seconde par cycle, assez
            // lent pour ne pas attirer l'oeil, assez vivant pour qu'on la voie.
            const float pulse = 0.55f + 0.45f * static_cast<float>(
                0.5 + 0.5 * std::sin(ctx.time * 6.2831853));
            const float d = 8.f;
            const auto led = theme.brand.led;
            ctx.r.fillRoundedRect({x - 2.f, hr.y + (th - d) * 0.5f - 2.f, d + 4.f, d + 4.f},
                                  led.withAlpha(static_cast<std::uint8_t>(60.f * pulse)), (d + 4.f) * 0.5f);
            ctx.r.fillRoundedRect({x, hr.y + (th - d) * 0.5f, d, d},
                                  led.withAlpha(static_cast<std::uint8_t>(255.f * pulse)), d * 0.5f);
            x += kTabLive;
            invalidate();
        }
        if (t.meta.icon != Icon::None) {
            const float side = std::min(th - 12.f, 16.f);
            // 1.8.0 : une icone au choix garde sa couleur.
            const int code = codeIconIndex(t.meta.icon);
            drawIcon(ctx.r, t.meta.icon, {x, hr.y + (th - side) * 0.5f, side, side},
                     code >= 0 ? codeIconColor(code) : active && groupFocus_ ? c.accent : fg);
            x += side + (t.compact ? 0.f : kTabIconGap);
        }
        if (!t.compact) {
            ctx.r.drawText({x, hr.y + (th - lh) * 0.5f}, t.meta.title, theme.font.ui, fg);
            if (active) ctx.r.drawText({x + 0.6f, hr.y + (th - lh) * 0.5f}, t.meta.title, theme.font.ui, fg);
            x += ctx.r.measure(t.meta.title, theme.font.ui).width;
        } else if (t.meta.icon == Icon::None) {
            // Reduit sans icone : son titre, coupe.
            std::string title = t.meta.title;
            if (ctx.r.measure(title, theme.font.ui).width > kCompactTitleMax) {
                auto k = ctx.r.fitCharacters(title, theme.font.ui, kCompactTitleMax - 10.f);
                while (k > 0 && k < title.size() && (static_cast<unsigned char>(title[k]) & 0xC0) == 0x80) --k;
                title = title.substr(0, k) + "\xE2\x80\xA6";
            }
            ctx.r.drawText({x, hr.y + (th - lh) * 0.5f}, title, theme.font.ui, fg);
            x += std::min(kCompactTitleMax, ctx.r.measure(title, theme.font.ui).width);
        }

        if (t.meta.modified) {
            // Un point, pas un « * » colle au titre : il se voit sans se lire.
            const float d = 7.f;
            ctx.r.fillRoundedRect({x + 5.f, hr.y + (th - d) * 0.5f, d, d}, c.accent, d * 0.5f);
            x += kTabDot;
        }
        if (!t.meta.badge.empty())
            x += drawBadge(t.meta, shownBadge(t.meta, t.shortBadge || t.compact), t.compact ? x - 4.f : x, hr);

        if (t.meta.closable) {
            const float side = std::min(th - 12.f, 12.f);
            drawIcon(ctx.r, Icon::Close,
                     {hr.right() - side - 8.f, hr.y + (th - side) * 0.5f, side, side},
                     c.textMuted);
        }
    }

    // Lot 7 : l'onglet que vise le menu ouvert (clic droit), entoure de tirets -
    // ce n'est pas forcement l'onglet ouvert. Le menu ferme, la marque s'efface.
    if (markMenu_ && markMenu_->isOpen()) {
        if (const int at = indexOf(markPage_); at >= 0) {
            const auto& hr = tabs_[static_cast<std::size_t>(at)].headerRect;
            if (hr.w > 8.f) dashedRect(ctx.r, {hr.x + 3.f, hr.y + 3.f, hr.w - 6.f, th - 6.f}, c.accent);
        }
    } else {
        markMenu_ = nullptr;
        markPage_ = nullptr;
    }

    // Le soulignement de l'onglet courant : trois pixels d'accent, arrondis,
    // qui GLISSENT jusqu'a leur place. Un saut sec ne dit pas d'ou l'on vient.
    if (current_ < tabs_.size()) {
        const auto& hr = tabs_[current_].headerRect;
        const float tx = hr.x + 8.f, tw = std::max(0.f, hr.w - 16.f);
        if (underlineX_ < 0.f) { underlineX_ = tx; underlineW_ = tw; }
        const float ease = theme.motion.scrollEase;
        if (std::abs(underlineX_ - tx) > 0.5f || std::abs(underlineW_ - tw) > 0.5f) {
            underlineX_ += (tx - underlineX_) * ease * 1.4f;
            underlineW_ += (tw - underlineW_) * ease * 1.4f;
            invalidate();
        } else {
            underlineX_ = tx;
            underlineW_ = tw;
        }
        // Lot 7 (disposition) : en gris dans un groupe qui n'est pas celui ou
        // s'ouvrent les nouveaux onglets - on voit ou l'on travaille.
        ctx.r.fillRoundedRect({underlineX_, r.y + th - 3.f, underlineW_, 3.f},
                              groupFocus_ ? c.accent : c.textMuted, 1.5f);
    }
    // D'autres onglets hors de la barre : un fondu au bord qui le dit.
    const float shift = -scrollOffset_;
    for (int k = 0; k < 3; ++k) {
        const float w = 8.f, a = 70.f + 60.f * static_cast<float>(k);
        if (shift > 0.5f)
            ctx.r.fillRect({viewport_.x + w * static_cast<float>(2 - k), r.y, w, th - 1.f}, c.headerBg.withAlpha(static_cast<std::uint8_t>(a)));
        if (stripWidth_ - shift > viewport_.w + 0.5f)
            ctx.r.fillRect({viewport_.right() - w * static_cast<float>(3 - k), r.y, w, th - 1.f}, c.headerBg.withAlpha(static_cast<std::uint8_t>(a)));
    }
    ctx.r.popClip();

    // Les fleches et le bouton de la liste (lot 19).
    const auto arrow = [&](const gfx::Rect& a, bool right, bool enabled) {
        if (a.w <= 0.f) return;
        const bool hot = inStrip && a.contains(mouse) && enabled;
        ctx.r.fillRect(a, hot ? theme.brand.hover : c.headerBg);
        const float cx = a.x + a.w * 0.5f, cy = a.y + th * 0.5f;
        const auto col = enabled ? c.text : c.textDisabled;
        const float d = right ? -1.f : 1.f;
        ctx.r.line({cx + 3.f * d, cy - 5.f}, {cx - 2.f * d, cy}, col, 1.6f);
        ctx.r.line({cx - 2.f * d, cy}, {cx + 3.f * d, cy + 5.f}, col, 1.6f);
    };
    arrow(arrowLeft_, false, shift > 0.5f);
    arrow(arrowRight_, true, stripWidth_ - shift > viewport_.w + 0.5f);
    if (listButton_.w > 0.f) {
        const bool hot = (inStrip && listButton_.contains(mouse)) || tabListOpen();
        ctx.r.fillRect(listButton_, hot ? theme.brand.hover : c.headerBg);
        ctx.r.line({listButton_.x, listButton_.y + 5.f}, {listButton_.x, listButton_.y + th - 5.f}, c.border, 1.f);
        // Trois traits (la liste), le nombre d'onglets, un chevron.
        const float lx = listButton_.x + 10.f, ly = listButton_.y + th * 0.5f;
        for (int k = -1; k <= 1; ++k)
            ctx.r.fillRect({lx, ly + 4.f * static_cast<float>(k) - 0.75f, 10.f, 1.5f}, c.text);
        const auto count = std::to_string(tabs_.size());
        const auto cf = theme.font.caption;
        ctx.r.drawText({lx + 14.f, ly - ctx.r.lineHeight(cf) * 0.5f}, count, cf, c.textMuted);
        const float vx = lx + 18.f + ctx.r.measure(count, cf).width;
        ctx.r.line({vx, ly - 2.f}, {vx + 3.5f, ly + 2.f}, c.text, 1.4f);
        ctx.r.line({vx + 3.5f, ly + 2.f}, {vx + 7.f, ly - 2.f}, c.text, 1.4f);
    }
}

// ------------------------------------------------ lot 7 : le menu d'un onglet ---
gfx::Point TabControl::contextAnchor(std::size_t index) const {
    if (index >= tabs_.size()) return {viewport_.x, viewport_.bottom()};
    const auto& e = tabs_[index];
    // Sous l'en-tete, les croix du menu sous l'icone de l'onglet (le menu met
    // les siennes a 5 px de son bord). Un en-tete a moitie hors de la bande :
    // le menu reste dans la bande, sous ce qu'on en voit.
    const float x = e.headerRect.x + (e.compact ? kTabPadCompact : kTabPadX) - 5.f;
    const float right = std::max(viewport_.x, viewport_.right() - 40.f);
    return {std::clamp(x, viewport_.x, right), e.headerRect.y + barHeight()};
}

void TabControl::requestContextMenu(std::size_t index) {
    if (index < tabs_.size()) contextMenuRequested->emit(index, contextAnchor(index));
}

void TabControl::markTab(std::size_t index, const PopupMenu* menu) {
    markPage_ = menu ? page(index) : nullptr;
    markMenu_ = markPage_ ? menu : nullptr;
    invalidate();
}

// ------------------------------- lot 7 (disposition) : groupes et tuiles ---
std::size_t TabControl::insertTab(std::size_t at, Tab meta, WidgetPtr page) {
    if (!page) return tabs_.size();
    if (at >= tabs_.size()) return addTab(std::move(meta), std::move(page));
    Widget& ref = addChild(std::move(page));
    tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(at), Entry{std::move(meta), &ref, {}, false, false});
    // La bande n'etait pas vide : le nouveau se replie, l'onglet ouvert garde
    // SA page - son indice avance quand on insere a sa gauche.
    ref.setVisibility(Visibility::Collapsed);
    if (at <= current_) ++current_;
    revealCurrent_ = true;
    invalidateLayout();
    return at;
}

void TabControl::moveTab(std::size_t from, std::size_t to) {
    if (from >= tabs_.size() || to >= tabs_.size() || from == to) return;
    const Widget* shown = current_ < tabs_.size() ? tabs_[current_].page : nullptr;
    Entry moved = std::move(tabs_[from]);
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(from));
    tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    for (std::size_t i = 0; i < tabs_.size(); ++i)
        if (tabs_[i].page == shown) current_ = i;
    revealCurrent_ = true;
    invalidateLayout();
}

int TabControl::headerAt(gfx::Point p, bool* onButton) const noexcept {
    if (onButton) *onButton = false;
    if (!viewport_.contains(p)) return -1;      // hors de la bande, ou sur la liste, les fleches
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        const auto& hr = tabs_[i].headerRect;
        if (hr.w <= 0.f || !hr.contains(p)) continue;
        if (onButton) {
            if (tile_) {
                gfx::Rect closeR, maxR;
                tileButtons(hr, tabs_[i].meta.closable, closeR, maxR);
                *onButton = maxR.contains(p) || (closeR.w > 0.f && closeR.contains(p));
            } else {
                *onButton = tabs_[i].meta.closable && p.x > hr.right() - 20.f;
            }
        }
        return static_cast<int>(i);
    }
    return -1;
}

gfx::Rect TabControl::barRect() const noexcept {
    const auto area = contentRect();
    return {area.x, area.y, area.w, std::min(area.h, barHeight())};
}

float TabControl::barHeight() const noexcept {
    // Une tuile a une barre de titre plus basse qu'une bande d'onglets : la
    // place va a ce qu'elle montre, et elle ne se confond pas avec une bande.
    return tile_ ? std::max(22.f, tabHeight_ - 6.f) : tabHeight_;
}

void TabControl::setTileMode(bool tile) {
    if (tile_ == tile) return;
    tile_ = tile;
    underlineX_ = -1.f;
    scrollOffset_ = 0.f;
    revealCurrent_ = true;
    invalidateLayout();
}

void TabControl::setTileMaximized(bool maximized) {
    if (tileMaximized_ == maximized) return;
    tileMaximized_ = maximized;
    invalidate();
}

void TabControl::setGroupFocus(bool focus) {
    if (groupFocus_ == focus) return;
    groupFocus_ = focus;
    invalidate();
}

void TabControl::tileButtons(const gfx::Rect& header, bool closable, gfx::Rect& close,
                             gfx::Rect& maximize) const noexcept {
    constexpr float kButton = 24.f;
    close = closable ? gfx::Rect{header.right() - kButton - 2.f, header.y, kButton, header.h} : gfx::Rect{};
    const float right = closable ? close.x : header.right() - 2.f;
    maximize = {right - kButton, header.y, kButton, header.h};
}

void TabControl::paintTile(const PaintContext& ctx) {
    const auto& theme = ctx.theme;
    const auto& c = theme.color;
    const auto  r = bounds();
    const float bh = barHeight();
    ctx.r.fillRect(r, c.panelBg);
    ctx.r.fillRect({r.x, r.y, r.w, bh}, c.headerBg);
    ctx.r.line({r.x, r.y + bh - 0.5f}, {r.right(), r.y + bh - 0.5f}, c.border, 1.f);
    if (current_ >= tabs_.size()) {
        markMenu_ = nullptr;
        markPage_ = nullptr;
        return;
    }
    const auto& t = tabs_[current_];
    const auto& hr = t.headerRect;
    gfx::Rect closeR, maxR;
    tileButtons(hr, t.meta.closable, closeR, maxR);

    // L'infobulle : ce que fait le bouton sous la souris, sinon le nom entier
    // (la barre coupe les titres longs) et les gestes de la tuile.
    const gfx::Point mouse{r.x + lastPointer_.x, r.y + lastPointer_.y};
    const bool inBar = hovered() && lastPointer_.y >= 0.f && lastPointer_.y < bh;
    std::string tip;
    if (inBar && maxR.contains(mouse))
        tip = tileMaximized_ ? "Revenir \xC3\xA0 la mosa\xC3\xAFque (double clic sur la barre)"
                             : "Agrandir cette tuile (double clic sur la barre)";
    else if (inBar && closeR.w > 0.f && closeR.contains(mouse))
        tip = "Fermer";
    else if (inBar)
        tip = t.meta.title + (t.meta.badge.empty() ? std::string() : " \xE2\x80\x94 " + t.meta.badge)
            + "\nDouble clic : agrandir ou revenir \xC2\xB7 tire la barre pour d\xC3\xA9placer la tuile";
    if (tip != tooltip()) setTooltip(tip);

    // Le titre, coupe avant les boutons.
    const auto  fg = groupFocus_ ? c.text : c.textMuted;
    const float lh = ctx.r.lineHeight(theme.font.ui);
    float x = hr.x + 10.f;
    ctx.r.pushClip({hr.x, hr.y, std::max(0.f, maxR.x - 4.f - hr.x), bh});
    if (t.meta.live) {
        const float pulse = 0.55f + 0.45f * static_cast<float>(0.5 + 0.5 * std::sin(ctx.time * 6.2831853));
        const float d = 8.f;
        ctx.r.fillRoundedRect({x, hr.y + (bh - d) * 0.5f, d, d},
                              theme.brand.led.withAlpha(static_cast<std::uint8_t>(255.f * pulse)), d * 0.5f);
        x += kTabLive;
        invalidate();
    }
    if (t.meta.icon != Icon::None) {
        const float side = std::min(bh - 10.f, 16.f);
        const int code = codeIconIndex(t.meta.icon);
        drawIcon(ctx.r, t.meta.icon, {x, hr.y + (bh - side) * 0.5f, side, side}, code >= 0 ? codeIconColor(code) : groupFocus_ ? c.accent : fg);
        x += side + kTabIconGap;
    }
    ctx.r.drawText({x, hr.y + (bh - lh) * 0.5f}, t.meta.title, theme.font.ui, fg);
    if (groupFocus_) ctx.r.drawText({x + 0.6f, hr.y + (bh - lh) * 0.5f}, t.meta.title, theme.font.ui, fg);
    x += ctx.r.measure(t.meta.title, theme.font.ui).width;
    if (t.meta.modified) {
        const float d = 7.f;
        ctx.r.fillRoundedRect({x + 5.f, hr.y + (bh - d) * 0.5f, d, d}, c.accent, d * 0.5f);
        x += kTabDot;
    }
    if (!t.meta.badge.empty()) (void)paintTabBadge(ctx, t.meta, t.meta.badge, x, hr, bh);
    ctx.r.popClip();

    // Les boutons : un fond au survol ; agrandir est une fenetre (un cadre et
    // sa barre), restaurer en est deux qui se chevauchent.
    const auto hot = [&](const gfx::Rect& b) {
        if (inBar && b.contains(mouse))
            ctx.r.fillRoundedRect({b.x + 2.f, b.y + 3.f, b.w - 4.f, std::max(0.f, b.h - 6.f)}, theme.brand.hover, 4.f);
    };
    const auto glyph = c.textMuted;
    hot(maxR);
    const float s = 9.f;
    const float gx = maxR.x + (maxR.w - s) * 0.5f, gy = maxR.y + (maxR.h - s) * 0.5f;
    if (tileMaximized_) {
        ctx.r.strokeRect({gx + 2.f, gy, s - 2.f, s - 2.f}, glyph, 1.f);
        ctx.r.fillRect({gx, gy + 2.f, s - 2.f, s - 2.f}, c.headerBg);
        ctx.r.strokeRect({gx, gy + 2.f, s - 2.f, s - 2.f}, glyph, 1.f);
    } else {
        ctx.r.strokeRect({gx, gy, s, s}, glyph, 1.f);
        ctx.r.fillRect({gx, gy, s, 2.f}, glyph);
    }
    if (closeR.w > 0.f) {
        hot(closeR);
        const float side = std::min(bh - 12.f, 12.f);
        drawIcon(ctx.r, Icon::Close, {closeR.x + (closeR.w - side) * 0.5f, closeR.y + (closeR.h - side) * 0.5f, side, side},
                 glyph);
    }
    // La tuile ou l'on travaille (ou s'ouvrent les onglets) : l'accent sous sa barre.
    if (groupFocus_) ctx.r.fillRect({r.x, r.y + bh - 2.f, r.w, 2.f}, c.accent);
    // Lot 7 : la tuile que vise le menu ouvert (clic droit sur sa barre).
    if (markMenu_ && markMenu_->isOpen()) {
        if (markPage_ == t.page) dashedRect(ctx.r, {hr.x + 3.f, hr.y + 3.f, hr.w - 6.f, bh - 6.f}, c.accent);
    } else {
        markMenu_ = nullptr;
        markPage_ = nullptr;
    }
}

EventResult TabControl::onEvent(const InputEvent& ev) {
    // La molette sur les en-tetes : la barre defile quand les onglets debordent.
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        const auto area = contentRect();
        if (stripWidth_ > viewport_.w + 0.5f && w->pos.y >= area.y && w->pos.y <= area.y + tabHeight_) {
            scrollOffset_ += (w->dy != 0.f ? w->dy : w->dx) * 48.f;
            invalidateLayout();
            return EventResult::Consumed;
        }
        return EventResult::Ignored;
    }
    if (const auto* m = std::get_if<MouseMove>(&ev)) {
        int h = -1;
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            if (tabs_[i].headerRect.contains(m->pos) && viewport_.contains(m->pos)) h = static_cast<int>(i);
        if (h != hoverTab_) { hoverTab_ = h; invalidate(); }
        return EventResult::Ignored;
    }
    if (const auto* d = std::get_if<MouseDown>(&ev)) {
        if (listButton_.w > 0.f && listButton_.contains(d->pos)) {
            if (tabListOpen()) tabList_->close();
            else openTabList();
            return EventResult::Consumed;
        }
        if (arrowLeft_.w > 0.f && arrowLeft_.contains(d->pos)) {
            scrollOffset_ += 120.f;
            invalidateLayout();
            return EventResult::Consumed;
        }
        if (arrowRight_.w > 0.f && arrowRight_.contains(d->pos)) {
            scrollOffset_ -= 120.f;
            invalidateLayout();
            return EventResult::Consumed;
        }
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            if (tabs_[i].headerRect.contains(d->pos) && viewport_.contains(d->pos)) {
                // Lot 7 : le clic droit demande le menu de CET onglet sans le
                // rendre courant (on ferme les autres depuis celui qu'on lit) ;
                // le clic du milieu le ferme, comme sa croix.
                if (d->button == MouseButton::Right) {
                    contextMenuRequested->emit(i, contextAnchor(i));
                    return EventResult::Consumed;
                }
                if (d->button == MouseButton::Middle) {
                    if (tabs_[i].meta.closable) tabCloseRequested->emit(i);
                    return EventResult::Consumed;
                }
                // Lot 7 (disposition) : la barre d'une tuile - sa croix, son
                // bouton agrandir, et le double clic qui agrandit (ou revient).
                if (tile_) {
                    gfx::Rect closeR, maxR;
                    tileButtons(tabs_[i].headerRect, tabs_[i].meta.closable, closeR, maxR);
                    if (closeR.w > 0.f && closeR.contains(d->pos)) tabCloseRequested->emit(i);
                    else if (maxR.contains(d->pos) || d->clickCount >= 2) maximizeRequested->emit(i);
                    else setCurrentIndex(i);
                    return EventResult::Consumed;
                }
                if (tabs_[i].meta.closable && d->pos.x > tabs_[i].headerRect.right() - 20.f)
                    tabCloseRequested->emit(i);
                else
                    setCurrentIndex(i);
                return EventResult::Consumed;
            }
    }
    return EventResult::Ignored;
}

// ======================================================= ScrollablePanel ====
ScrollablePanel::ScrollablePanel(std::string id) : Widget(std::move(id)) {
    const std::string base = this->id().empty() ? std::string("scroll") : this->id();
    vBar_ = &static_cast<ScrollBar&>(addChild(std::make_unique<ScrollBar>(base + ".vbar", false)));
    hBar_ = &static_cast<ScrollBar&>(addChild(std::make_unique<ScrollBar>(base + ".hbar", true)));
    vBar_->onScroll = [this](float y) { scrollTo({offset_.x, y}); };
    hBar_->onScroll = [this](float x) { scrollTo({x, offset_.y}); };
}

void ScrollablePanel::keepBarsOnTop() {
    // Le dernier ajoute recoit les clics le premier : les barres passent apres le contenu.
    auto v = removeChild(*vBar_);
    auto h = removeChild(*hBar_);
    vBar_ = &static_cast<ScrollBar&>(addChild(std::move(v)));
    hBar_ = &static_cast<ScrollBar&>(addChild(std::move(h)));
}

void ScrollablePanel::setContent(WidgetPtr w) {
    if (content_) removeChild(*content_);
    content_ = &addChild(std::move(w));
    keepBarsOnTop();
    invalidateLayout();
}

void ScrollablePanel::setScrollPolicy(bool horizontal, bool vertical) {
    hScroll_ = horizontal;
    vScroll_ = vertical;
    invalidateLayout();
}

gfx::Size ScrollablePanel::viewportSize() const noexcept {
    const auto r = contentRect();
    return {std::max(0.f, r.w - (vScroll_ ? 12.f : 0.f)),
            std::max(0.f, r.h - (hScroll_ ? 12.f : 0.f))};
}

void ScrollablePanel::scrollTo(gfx::Point o) {
    if (!content_) return;
    const auto vp = viewportSize();
    const auto ch = content_->sizeHint().preferred;
    offset_.x = std::clamp(o.x, 0.f, std::max(0.f, ch.w - vp.w));
    offset_.y = std::clamp(o.y, 0.f, std::max(0.f, ch.h - vp.h));
    invalidateLayout();
    scrolled->emit(offset_);
}

void ScrollablePanel::ensureVisible(const gfx::Rect& local) {
    const auto vp = viewportSize();
    gfx::Point o = offset_;
    if (local.y < o.y)                 o.y = local.y;
    else if (local.bottom() > o.y + vp.h) o.y = local.bottom() - vp.h;
    if (local.x < o.x)                 o.x = local.x;
    else if (local.right() > o.x + vp.w)  o.x = local.right() - vp.w;
    scrollTo(o);
}

void ScrollablePanel::onLayout() {
    const auto area = contentRect();
    const auto vp   = viewportSize();
    if (!content_) {
        vBar_->setVisibility(Visibility::Collapsed);
        hBar_->setVisibility(Visibility::Collapsed);
        return;
    }
    const auto want = content_->sizeHint().preferred;
    content_->setBounds({area.x - offset_.x, area.y - offset_.y,
                         std::max(vp.w, want.w), std::max(vp.h, want.h)});
    // 1.11.4 : les barres, la ou elles se dessinaient (12 px au bord), et qu'on tire.
    const bool v = vScroll_ && want.h > vp.h, h = hScroll_ && want.w > vp.w;
    vBar_->setVisibility(v ? Visibility::Visible : Visibility::Collapsed);
    hBar_->setVisibility(h ? Visibility::Visible : Visibility::Collapsed);
    vBar_->setBounds({area.right() - 12.f, area.y, 12.f, vp.h});
    hBar_->setBounds({area.x, area.bottom() - 12.f, vp.w, 12.f});
    vBar_->setRange(want.h, vp.h, offset_.y);
    hBar_->setRange(want.w, vp.w, offset_.x);
}

void ScrollablePanel::onPaint(const PaintContext& ctx) {
    ctx.r.fillRect(bounds(), ctx.theme.color.panelBg);
}

EventResult ScrollablePanel::onEvent(const InputEvent& ev) {
    if (const auto* w = std::get_if<MouseWheel>(&ev)) {
        if (!bounds().contains(w->pos)) return EventResult::Ignored;
        scrollTo({offset_.x - w->dx * 40.f, offset_.y - w->dy * 40.f});
        return EventResult::Consumed;
    }
    return EventResult::Ignored;
}

// =============================================================== ToolBar ====
ToolBar::ToolBar(std::string id) : Widget(std::move(id)) {
    setPadding({4.f, 6.f, 4.f, 6.f});

    // The overflow control is created up front and simply collapsed when the
    // bar fits. Creating it on demand would mean re-parenting a widget in the
    // middle of a layout pass, which is exactly the kind of thing that goes
    // wrong once and is then hard to find.
    auto menu = std::make_unique<DropDown>(this->id() + ".overflow");
    menu->setTooltip("Commands that do not fit on the bar");
    overflowMenu_ = &static_cast<DropDown&>(addChild(std::move(menu)));
    overflowMenu_->setVisibility(Visibility::Collapsed);
    links_ += overflowMenu_->selectionChanged->connect([this](int index) {
        if (sink_ && index >= 0 && index < static_cast<int>(overflowActions_.size()))
            sink_(overflowActions_[static_cast<std::size_t>(index)]);
    });
}

void ToolBar::setActionSink(std::function<void(core::ActionId)> sink) { sink_ = std::move(sink); }

Button& ToolBar::addButton(std::string text, core::ActionId action, Icon icon) {
    auto b = std::make_unique<Button>(std::move(text));
    b->setStyle(Button::Style::Flat);
    b->setAction(action);
    b->setIcon(icon);
    auto& ref = static_cast<Button&>(addChild(std::move(b)));
    links_ += ref.clicked->connect([this, action] { if (sink_) sink_(action); });
    slots_.push_back(Slot{&ref, false, &ref});
    invalidateLayout();
    return ref;
}

void ToolBar::addSeparator() { slots_.push_back(Slot{nullptr, true, nullptr}); invalidateLayout(); }

Widget& ToolBar::addCustom(WidgetPtr w) {
    Widget& ref = addChild(std::move(w));
    slots_.push_back(Slot{&ref, false, nullptr});
    invalidateLayout();
    return ref;
}

// ---------------------------------------------------------------------------
//  A toolbar that does not fit has three honest answers, in this order:
//
//   1. show everything;
//   2. drop the labels and keep every icon - the tooltips still carry the
//      names, and an icon-only bar holds roughly three times as many commands;
//   3. keep what fits and put the rest behind a single overflow control.
//
//  Each step is only taken when the previous one has genuinely run out of room,
//  measured against the actual widths rather than a guess. Nothing is ever
//  silently unreachable: whatever comes off the bar goes into the overflow list,
//  which is why step 3 reserves room for that control before deciding what fits.
// ---------------------------------------------------------------------------
void ToolBar::setSlotHidden(const Widget& w, bool hidden) {
    for (auto& s : slots_)
        if (s.w == &w && s.hidden != hidden) {
            s.hidden = hidden;
            invalidateLayout();
        }
}

void ToolBar::onLayout() {
    const auto  area = contentRect();
    for (const auto& s : slots_)
        if (s.hidden) s.w->setVisibility(Visibility::Collapsed);
    const float gap = 4.f;
    const float separatorWidth = 11.f;

    auto naturalWidth = [&](bool compact) {
        float total = 0.f;
        for (const auto& s : slots_) {
            if (s.separator) { total += separatorWidth; continue; }
            if (s.hidden) continue;
            if (s.button) s.button->setCompact(compact);
            total += s.w->sizeHint().preferred.w + gap;
        }
        return total;
    };

    // --- step 1: everything, with labels ----------------------------------
    float needed = naturalWidth(false);
    fit_ = Fit::Full;

    // --- step 2: icons only -------------------------------------------------
    if (needed > area.w) {
        needed = naturalWidth(true);
        fit_ = Fit::IconsOnly;
    }

    hidden_ = 0;
    overflowActions_.clear();

    if (needed <= area.w || !overflow_) {
        overflowMenu_->setVisibility(Visibility::Collapsed);
        float x = area.x;
        for (const auto& s : slots_) {
            if (s.separator) { x += separatorWidth; continue; }
            if (s.hidden) continue;
            s.w->setVisibility(Visibility::Visible);
            const float w = s.w->sizeHint().preferred.w;
            s.w->setBounds({x, area.y, w, area.h});
            x += w + gap;
        }
        return;
    }

    // --- step 3: overflow ---------------------------------------------------
    fit_ = Fit::Overflowing;
    overflowMenu_->setVisibility(Visibility::Visible);
    const float reserved = std::max(64.f, overflowMenu_->sizeHint().preferred.w) + gap;

    float x = area.x;
    bool  overflowing = false;
    for (const auto& s : slots_) {
        if (s.separator) {
            if (!overflowing) x += separatorWidth;
            continue;
        }
        if (s.hidden) continue;
        const float w = s.w->sizeHint().preferred.w;

        // A custom widget has no action id, so it cannot be reached from the
        // overflow list; it stays on the bar rather than becoming unreachable.
        const bool canOverflow = s.button != nullptr;
        if (!overflowing && x + w <= area.right() - reserved) {
            s.w->setVisibility(Visibility::Visible);
            s.w->setBounds({x, area.y, w, area.h});
            x += w + gap;
            continue;
        }
        if (!canOverflow) {
            s.w->setVisibility(Visibility::Visible);
            s.w->setBounds({x, area.y, std::max(0.f, area.right() - reserved - x), area.h});
            x = area.right() - reserved;
            continue;
        }
        overflowing = true;
        s.w->setVisibility(Visibility::Collapsed);
        ++hidden_;
        overflowActions_.push_back(s.button->action());
    }

    const_cast<ToolBar*>(this)->rebuildOverflow();
    overflowMenu_->setBounds({area.right() - reserved + gap, area.y, reserved - gap, area.h});
}

void ToolBar::rebuildOverflow() {
    std::vector<DropDown::Item> items;
    items.reserve(slots_.size());
    for (const auto& s : slots_) {
        if (!s.button || s.hidden || s.w->visibility() == Visibility::Visible) continue;
        items.push_back(DropDown::Item{s.button->text(), std::string(s.button->action()),
                                       {}, s.button->enabled()});
    }
    // The label says how much is hidden, so the bar never pretends to be complete.
    overflowMenu_->setItems(std::move(items));
    overflowMenu_->setSelectedIndex(-1);
}

void ToolBar::onPaint(const PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    ctx.r.fillRect(bounds(), c.headerBg);
    ctx.r.line({bounds().x, bounds().bottom() - 1.f}, {bounds().right(), bounds().bottom() - 1.f},
               c.border, 1.f);

    float x = contentRect().x;
    for (const auto& s : slots_) {
        if (s.separator) {
            ctx.r.line({x + 5.f, bounds().y + 7.f}, {x + 5.f, bounds().bottom() - 7.f}, c.border, 1.f);
            x += 11.f;
        } else if (s.w->visible()) {
            x = s.w->bounds().right() + 4.f;
        }
    }

    // A small count on the overflow control: "3 more" is information, an
    // anonymous chevron is a puzzle.
    if (fit_ == Fit::Overflowing && hidden_ > 0 && overflowMenu_->visible()) {
        const auto label = std::to_string(hidden_) + " more";
        const auto m = ctx.r.measure(label, ctx.theme.font.smallUi);
        const auto r = overflowMenu_->bounds();
        ctx.r.drawText({r.x + 6.f, r.y + (r.h - m.height) * 0.5f}, label,
                       ctx.theme.font.smallUi, c.textMuted);
    }
}

// ============================================================= StatusBar ====
StatusBar::StatusBar(std::string id) : Widget(std::move(id)) {
    setPadding({2.f, 8.f, 2.f, 8.f});
}

void StatusBar::setMessage(std::string text, gfx::Color accent) {
    // UN TRANSITOIRE EN COURS RESTE A L'ECRAN. Le message durable est retenu
    // et reviendra quand le transitoire expirera (onPaint). Sans ce test, le
    // moindre message d'etat - la position de la souris sur une vue, rafraichie
    // a chaque mouvement - effacait un avertissement a l'image suivante.
    // LE MEME MESSAGE NE REPART PAS DU DEBUT. Le fondu d'entree (onPaint) se
    // relance a chaque changement ; un ecran qui redit la meme chose a chaque
    // image restait ainsi eternellement a sa premiere image de fondu :
    // invisible.
    if (text == steady_ && accent.r == steadyAccent_.r && accent.g == steadyAccent_.g
        && accent.b == steadyAccent_.b && accent.a == steadyAccent_.a && steadySeverity_ == Severity::None
        && (expiry_ > 0.0 || message_ == steady_))
        return;
    if (hook_) hook_(text, hookSeverity_, false);   // Lot API 8 : le journal des messages
    steady_ = text; steadyAccent_ = accent; steadySeverity_ = Severity::None;
    if (expiry_ > 0.0) { invalidate(); return; }
    message_  = std::move(text);
    accent_   = accent;
    severity_ = Severity::None;
    expiresAt_ = -1.0;
    shownAt_  = -1.0;
    invalidate();
}

void StatusBar::setMessage(std::string text, Severity severity) {
    if (text == steady_ && severity == steadySeverity_ && (expiry_ > 0.0 || message_ == steady_))
        return;                              // voir plus haut : pas de fondu relance pour rien
    steadySeverity_ = Severity::None;        // force la mise a jour ci-dessous
    hookSeverity_ = severity;                // Lot API 8 : le journal garde la gravite
    setMessage(std::move(text));
    hookSeverity_ = Severity::None;
    steadySeverity_ = severity;
    if (expiry_ <= 0.0) severity_ = severity;
}

void StatusBar::setTransientMessage(std::string text, double seconds, Severity severity) {
    // Le message durable n'est PAS remplace : il revient quand celui-ci
    // s'efface. C'etait promis par le nom et pas fait - un transitoire restait
    // affiche jusqu'au message suivant.
    if (hook_) hook_(text, severity, true);   // Lot API 8 : le journal des messages
    message_   = std::move(text);
    accent_    = gfx::Color{0, 0, 0, 0};
    severity_  = severity;
    expiry_    = seconds;
    expiresAt_ = -1.0;
    shownAt_   = -1.0;
    invalidate();
}

void StatusBar::dismissTransient() {
    if (expiry_ <= 0.0) return;
    message_ = steady_; accent_ = steadyAccent_; severity_ = steadySeverity_;
    expiry_ = 0.0; expiresAt_ = -1.0; shownAt_ = -1.0;
    invalidate();
}

// ---- Lot API 8 : le bandeau bas ----
// Un clic sur le texte du message (hors des indicateurs, qui prennent leurs
// clics avant) : le journal des messages s'ouvre.
EventResult StatusBar::onEvent(const InputEvent& ev) {
    if (!click_) return EventResult::Ignored;
    if (const auto* d = std::get_if<MouseDown>(&ev); d && d->button == MouseButton::Left && bounds().contains(d->pos)) {
        float right = bounds().right();
        for (auto* w : right_) right = std::min(right, w->bounds().x);
        if (d->pos.x < right) { click_(); return EventResult::Consumed; }
    }
    return EventResult::Ignored;
}
// ---- fin Lot API 8 : le bandeau bas ----

Widget& StatusBar::addIndicator(WidgetPtr w, Slot slot) {
    Widget& ref = addChild(std::move(w));
    (slot == Slot::Left ? left_ : right_).push_back(&ref);
    invalidateLayout();
    return ref;
}

void StatusBar::onLayout() {
    const auto area = contentRect();
    float x = area.right();
    for (auto it = right_.rbegin(); it != right_.rend(); ++it) {
        const float w = (*it)->sizeHint().preferred.w;
        x -= w + 12.f;
        (*it)->setBounds({x, area.y, w, area.h});
    }
    float lx = area.x;
    for (auto* w : left_) {
        const float ww = w->sizeHint().preferred.w;
        w->setBounds({lx, area.y, ww, area.h});
        lx += ww + 12.f;
    }
}

void StatusBar::onPaint(const PaintContext& ctx) {
    const auto& th = ctx.theme;
    const auto& c = th.color;
    ctx.r.fillRect(bounds(), c.headerBg);
    ctx.r.line({bounds().x, bounds().y}, {bounds().right(), bounds().y}, c.border, 1.f);

    // ---- le transitoire qui expire ---------------------------------------
    float fade = 1.f;
    if (expiry_ > 0.0) {
        if (expiresAt_ < 0.0) expiresAt_ = ctx.time + expiry_;
        const double left = expiresAt_ - ctx.time;
        if (left <= 0.0) {
            message_ = steady_; accent_ = steadyAccent_; severity_ = steadySeverity_;
            expiry_ = 0.0; expiresAt_ = -1.0; shownAt_ = -1.0;
        } else {
            if (left < 0.35) fade = static_cast<float>(left / 0.35);
            invalidate();
        }
    }
    if (message_.empty()) return;

    // ---- le fondu d'entree : un message qui change se voit changer --------
    if (shownAt_ < 0.0) shownAt_ = ctx.time;
    const float since = static_cast<float>((ctx.time - shownAt_) * 1000.0);
    if (since < th.motion.hoverMs) {
        fade *= std::clamp(since / th.motion.hoverMs, 0.f, 1.f);
        invalidate();
    }
    const auto alpha = static_cast<std::uint8_t>(255.f * std::clamp(fade, 0.f, 1.f));

    const auto inner = contentRect();
    Tone tone = Tone::None;
    Icon icon = Icon::None;
    switch (severity_) {
        case Severity::Info:    tone = Tone::Info;    icon = Icon::Info;    break;
        case Severity::Success: tone = Tone::Ok;      icon = Icon::Ok;      break;
        case Severity::Warning: tone = Tone::Warning; icon = Icon::Warning; break;
        case Severity::Error:   tone = Tone::Error;   icon = Icon::Error;   break;
        case Severity::None:    break;
    }
    // « Pas d'accent » se dit de deux facons dans le code existant : l'alpha nul,
    // et gfx::Color{} - qui est un NOIR OPAQUE. Un noir pur n'est jamais une
    // couleur de message voulue (il disparait en theme sombre) : les deux
    // veulent dire la couleur attenuee du theme.
    const bool none = accent_.a == 0 || (accent_.r == 0 && accent_.g == 0 && accent_.b == 0);
    gfx::Color fg = none ? c.textMuted : accent_;
    if (tone != Tone::None) fg = th.onSurface(th.tone(tone, fg));
    if (severity_ == Severity::Info) fg = c.textMuted;      // l'info ne crie pas

    float x = inner.x;
    if (icon != Icon::None) {
        const float side = std::min(inner.h - 4.f, 14.f);
        drawIcon(ctx.r, icon, {x, inner.y + (inner.h - side) * 0.5f, side, side},
                 th.tone(tone, fg).withAlpha(alpha));
        x += side + 6.f;
    }
    // Le texte s'arrete avant le premier indicateur de droite.
    float right = inner.right();
    for (auto* w : right_) right = std::min(right, w->bounds().x - 8.f);
    const auto font = th.font.smallUi;
    const float maxW = std::max(0.f, right - x);
    std::string shown = message_;
    const auto fits = ctx.r.fitCharacters(shown, font, maxW);
    if (fits < shown.size()) {
        // La place des points se mesure (une police proportionnelle ne leur
        // donne pas trois largeurs de lettre), et la coupe recule au debut d'un
        // caractere : couper un « · » ou un « é » en deux laisse un octet
        // orphelin, que la police dessine en carre.
        std::size_t keep = std::min(fits, ctx.r.fitCharacters(
            shown, font, std::max(0.f, maxW - ctx.r.measure("...", font).width)));
        while (keep > 0 && (static_cast<unsigned char>(shown[keep]) & 0xC0) == 0x80) --keep;
        shown = shown.substr(0, keep) + "...";
    }
    ctx.r.drawText({x, inner.y + (inner.h - ctx.r.lineHeight(font)) * 0.5f}, shown, font,
                   fg.withAlpha(static_cast<std::uint8_t>(fg.a * alpha / 255)));
}

} // namespace ui
