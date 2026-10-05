// =============================================================================
//  app/TopBarLot8.cpp - lot API 8 : le bandeau haut, refondu
// -----------------------------------------------------------------------------
//  Trois zones sur 44 px (la conception : DESIGN-BANDEAUX.md, la maquette
//  validee : demo 13 "Les bandeaux").
//    - a gauche, l'identite et l'etat du projet : la puce projet et son bouton
//      Enregistrer integre ("Enregistre" quand rien n'attend), la puce version,
//      Annuler / Retablir et la liste des 10 dernieres actions ;
//    - au centre, la palette Aller a / Faire... (Ctrl+K), large ;
//    - a droite, la simulation (etat en couleur, cycle, mini-courbe du temps de
//      cycle), la cloche des notifications, les taches de fond, la sortie
//      (Vers Control Expert + Release), Deposer un fichier, Affichage et Aide.
//  Les parties d'avant gardent leurs cles et leurs actions ; Historique quitte
//  le bandeau (la liste d'Annuler le donne) et son action reste joignable.
// =============================================================================
#include "TopBar.hpp"

#include "../ui/Theme.hpp"
#include "../ui/widgets/Controls.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace app {

namespace {

constexpr float       kItemH8 = 34.f;
constexpr std::size_t kSpark = 40;          // les echantillons de la mini-courbe
const gfx::FontId     kLabel8{14};
const gfx::FontId     kSmall8{12};
const gfx::FontId     kTiny8{11};

std::string percent(float f) {
    const int p = static_cast<int>(std::lround(std::clamp(f, 0.f, 1.f) * 100.f));
    return std::to_string(p) + "\xC2\xA0%";
}

void bellGlyph(gfx::IRenderer& r, const gfx::Rect& b, gfx::Color c) {
    const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
    r.fillRoundedRect({cx - 5.5f, cy - 7.f, 11.f, 12.f}, c, 5.f);
    r.fillRect({cx - 5.5f, cy - 1.f, 11.f, 5.f}, c);
    r.fillRoundedRect({cx - 7.5f, cy + 3.f, 15.f, 2.5f}, c, 1.f);
    r.fillRoundedRect({cx - 1.8f, cy + 6.f, 3.6f, 3.2f}, c, 1.6f);
}

} // namespace

// ------------------------------------------------------------- les donnees ----
void TopBar::setRecentProjects(std::vector<std::string> paths) { recents_ = std::move(paths); }

void TopBar::setUndoList(std::vector<UndoItem> items) {
    if (items.size() > 10) items.resize(10);
    undoList_ = std::move(items);
}

void TopBar::setNotices(std::vector<Notice> notices) {
    if (notices == notices_) return;
    notices_ = std::move(notices);
    invalidate();
}

int TopBar::unreadNotices() const {
    int n = 0;
    for (const auto& x : notices_) {
        const std::string& k = x.key.empty() ? x.title : x.key;
        if (std::find(readNotices_.begin(), readNotices_.end(), k) == readNotices_.end()) ++n;
    }
    return n;
}

void TopBar::markNoticesRead() {
    for (const auto& x : notices_) {
        const std::string& k = x.key.empty() ? x.title : x.key;
        if (std::find(readNotices_.begin(), readNotices_.end(), k) == readNotices_.end()) readNotices_.push_back(k);
    }
    invalidate();
}

void TopBar::setTasks(std::vector<Task> tasks) {
    if (tasks == tasks_) return;
    const bool relayout = tasks.empty() != tasks_.empty();
    tasks_ = std::move(tasks);
    if (relayout) invalidateLayout();
    invalidate();
}

void TopBar::setCycleTime(float ms, float periodMs) {
    periodMs_ = periodMs;
    cycleMs_.push_back(std::max(0.f, ms));
    if (cycleMs_.size() > kSpark) cycleMs_.erase(cycleMs_.begin(), cycleMs_.begin() + static_cast<std::ptrdiff_t>(cycleMs_.size() - kSpark));
    invalidate();
}

std::string TopBar::hiddenPartAction(std::string_view part) const {
    layoutParts();
    for (const auto& p : parts_)
        if (p.key == part && p.rect.w <= 0.f) return p.action;
    return {};
}

// ------------------------------------------------------------ mise en place ----
void TopBar::layoutLot8() const {
    const auto b = bounds();
    const float barH = b.h > 0.f ? b.h : kBarHeight;
    const float y = b.y + std::floor(std::max(0.f, barH - kItemH8) * 0.5f);
    const auto find = [this](std::string_view key) -> const Part* {
        for (const auto& p : parts_)
            if (p.key == key) return &p;
        return nullptr;
    };
    for (const auto& p : parts_) p.rect = {};
    // Trois passes : a l'aise ; les libelles tombent (Enregistrer, Vers Control
    // Expert) ; serre (la puce version et la mini-courbe se resserrent).
    for (int pass = 0; pass < 3; ++pass) {
        const bool compact = pass >= 1, tight = pass >= 2;
        for (const auto& p : parts_) p.rect = {};
        float x = b.x + 8.f;
        const auto put = [&](std::string_view key, float w) {
            if (const Part* p = find(key)) {
                p->rect = {x, y, w, kItemH8};
                x += w;
            }
        };
        // ---- a gauche : le projet, la version, Annuler / Retablir ----
        const float nameW = ui::measureWidth(name_, gfx::FontId{15});
        const float line2 = ui::measureWidth(state_.empty() ? std::string("export") : state_, kTiny8) + 12.f + (modified_ ? 14.f : 0.f);
        put("projet", std::min(tight ? 130.f : 230.f, 5.f + 32.f + 9.f + std::max(nameW, line2) + 24.f));
        const std::string saveLabel = modified_ ? "Enregistrer" : "Enregistr\xC3\xA9";
        put("enregistrer", compact ? 34.f : 10.f + 18.f + 7.f + ui::measureWidth(saveLabel, kLabel8) + 12.f);
        x += 8.f;
        float vw = 0.f;
        if (!version_.title.empty()) {
            const float w = std::max(ui::measureWidth(version_.title, gfx::FontId{14}) + 1.f, ui::measureWidth(version_.subtitle, kSmall8));
            vw = std::min(tight ? 124.f : compact ? 200.f : 260.f, 8.f + 24.f + 8.f + w + 24.f);
        }
        put("version", vw);
        if (vw > 0.f) x += 8.f;
        const float ux = x;
        put("annuler", 32.f);
        put("annuler-liste", 18.f);
        put("retablir", 32.f);
        undoBox_ = {ux, y, x - ux, kItemH8};
        x += 6.f;
        put("nouveau", 44.f);
        const float leftEnd = x + 12.f;

        // ---- a droite, de droite a gauche ----
        float rx = b.right() - 8.f;
        const auto putR = [&](std::string_view key, float w) {
            if (const Part* p = find(key)) {
                rx -= w;
                p->rect = {rx, y, w, kItemH8};
            }
        };
        putR("aide", 44.f);
        putR("affichage", 44.f);
        rx -= 2.f;
        // serre : Deposer un fichier tombe (le glisser-deposer et la palette ">deposer" restent)
        if (!tight) putR("deposer", 34.f);
        rx -= 8.f;
        const float exportEnd = rx;
        const float configW = 104.f;
        rx -= configW;
        configRect_ = {rx, y + 1.f, configW - 1.f, kItemH8 - 2.f};
        rx -= 1.f;
        if (const Part* ce = find("control-expert"))
            putR("control-expert", compact ? 36.f : 10.f + 24.f + ui::measureWidth(ce->label, kLabel8) + 12.f);
        exportBox_ = {rx, y, exportEnd - rx, kItemH8};
        rx -= 8.f;
        // serre et rien ne tourne : l'horloge tombe
        if (!(tight && tasks_.empty())) putR("taches", tasks_.empty() || tight ? 34.f : 168.f);
        putR("cloche", 40.f);
        rx -= 8.f;
        // 1.10 (maquette, scene 2) : a droite du bloc de l'API, l'IHM - sa pastille, Demarrer
        // (F8), Arreter (Maj+F8) - puis le menu "Les deux" ; pas d'IHM : rien de tout cela.
        for (const char* k : {"ihm", "ihm-demarrer", "ihm-arreter", "les-deux"})
            if (const Part* p = find(k)) p->rect = {};
        if (hmi_ != Hmi::None) {
            if (const Part* both = find("les-deux")) putR("les-deux", 9.f + ui::measureWidth(both->label, kLabel8) + 26.f);
            rx -= 4.f;
            putR("ihm-arreter", 32.f);
            putR("ihm-demarrer", 32.f);
            putR("ihm", 9.f + 8.f + 7.f + std::max(ui::measureWidth("IHM arr\xC3\xAAt\xC3\xA9" "e", kSmall8),
                                                    ui::measureWidth("Maj+F8 l'arr\xC3\xAAte", kTiny8)) + 10.f);
            rx -= 8.f;
        }
        const float simEnd = rx;
        const float textW = std::max({ui::measureWidth(hmi_ == Hmi::None ? "en d\xC3\xA9" "faut" : "API en d\xC3\xA9" "faut", kSmall8) + 1.f,
                                      ui::measureWidth("cycle 9\xE2\x80\xAF" "999\xE2\x80\xAF" "999", kTiny8),
                                      ui::measureWidth("F5 la lance", kTiny8)});
        const float stateW = 9.f + 8.f + 7.f + textW + 8.f + (tight ? 0.f : 64.f);
        rx -= stateW;
        stateRect_ = {rx, y, stateW, kItemH8};
        putR("cycle", 32.f);
        putR("arreter", 32.f);
        putR("simuler", 34.f);
        simRect_ = {rx, y, simEnd - rx, kItemH8};
        const float rightStart = rx - 12.f;

        // ---- au centre : la palette, aussi large que la place le permet ----
        const float room = rightStart - leftEnd;
        if (room >= (compact ? 150.f : 190.f) || tight) {
            // trop serre pour 120 px : la loupe seule (Ctrl+K l'ouvre en grand)
            const float w = room >= 120.f ? std::min(room, 560.f) : 34.f;
            goToRect_ = {leftEnd + std::max(0.f, (room - w) * 0.5f), y, w, kItemH8};
            break;
        }
    }
    // Historique n'est plus dans le bandeau : la liste d'Annuler le donne (Ctrl+H).
    if (const Part* h = find("historique")) h->rect = {};
}

// ---------------------------------------------------------------- les menus ----
void TopBar::openLot8Menu(Menu m) {
    if (!popup_) return;
    std::vector<ui::PopupMenu::Item> items;
    popupActions_.clear();
    const auto heading = [&](std::string label, std::string right = {}) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.shortcut = std::move(right);
        it.heading = true;
        it.enabled = false;
        items.push_back(std::move(it));
    };
    const auto entry = [&](std::string label, std::string right, ui::Icon icon, std::string action) {
        ui::PopupMenu::Item it;
        it.label = std::move(label);
        it.shortcut = std::move(right);
        it.icon = icon;
        it.id = static_cast<int>(popupActions_.size());
        popupActions_.push_back(std::move(action));
        items.push_back(std::move(it));
    };
    const auto sep = [&] {
        ui::PopupMenu::Item it;
        it.separator = true;
        items.push_back(std::move(it));
    };
    if (m == Menu::UndoList) {
        // Les 10 dernieres actions, la plus recente d'abord : un clic y revient
        // (annule celle-ci et toutes celles d'apres).
        if (undoList_.empty()) heading("Rien \xC3\xA0 annuler");
        else heading("REVENIR AVANT\xE2\x80\xA6");
        for (std::size_t i = 0; i < undoList_.size(); ++i) {
            const auto& u = undoList_[i];
            std::string right = u.when;
            if (i > 0) right += std::string(right.empty() ? "" : " \xC2\xB7 ") + "annule " + std::to_string(i + 1) + " actions";
            entry(u.label, std::move(right), i == 0 ? ui::Icon::Undo : ui::Icon::None, "edit.undoTo:" + std::to_string(i + 1));
        }
        sep();
        entry("Historique du projet\xE2\x80\xA6", "Ctrl+H", ui::Icon::History, "edit.history");
    } else if (m == Menu::Notices) {
        const int unread = unreadNotices();
        heading("Notifications", unread > 0 ? std::to_string(unread) + " non lue" + (unread > 1 ? "s" : "") : std::string("tout est lu"));
        if (notices_.empty()) {
            heading("Rien ne demande ton attention.");
        } else {
            entry("Tout marquer comme lu", "", ui::Icon::Ok, "bandeau.notices.read");
            std::string group;
            for (const auto& n : notices_) {
                if (n.group != group) {
                    group = n.group;
                    std::size_t count = 0;
                    for (const auto& o : notices_) count += o.group == group ? 1u : 0u;
                    sep();
                    std::string up;
                    for (const char ch : group) up += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                    heading(up + " \xC2\xB7 " + std::to_string(count));
                }
                const auto icon = n.tone == "error" ? ui::Icon::Error : n.tone == "warning" ? ui::Icon::Warning
                                : n.tone == "ok" ? ui::Icon::Ok : ui::Icon::Info;
                entry(n.title, n.button.empty() ? std::string{} : n.button + " \xE2\x80\xBA", icon, n.action);
                if (!n.detail.empty()) heading("      " + n.detail);
            }
        }
    } else if (m == Menu::Tasks) {
        heading("T\xC3\xA2" "ches de fond");
        if (tasks_.empty()) heading("Aucune t\xC3\xA2" "che en cours (g\xC3\xA9n\xC3\xA9ration, import, export)");
        for (const auto& t : tasks_) {
            const std::string pct = t.progress >= 0.f ? " \xC2\xB7 " + percent(t.progress) : std::string(" \xC2\xB7 en cours");
            if (t.cancelAction.empty()) heading(t.label + pct);
            else entry(t.label + pct, "Annuler", ui::Icon::Close, t.cancelAction);
        }
    } else {
        return;
    }
    popup_->setItems(std::move(items));
    gfx::Rect anchor{};
    for (const auto& p : parts_)
        if (p.menu == m) anchor = p.rect;
    opened_ = m;
    popup_->openAt({anchor.x, anchor.y + anchor.h + 4.f}, surface_);
    invalidate();
}

// ---------------------------------------------------------------- le dessin ----
void TopBar::paintLot8Groups(const ui::PaintContext& ctx) const {
    const auto& c = ctx.theme.color;
    const auto frame = [&](const gfx::Rect& r) {
        if (r.w <= 0.f) return;
        ctx.r.fillRoundedRect(r, c.border, 7.f);
        ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, c.panelBg, 6.f);
    };
    const auto rectOf = [this](std::string_view key) {
        for (const auto& p : parts_)
            if (p.key == key) return p.rect;
        return gfx::Rect{};
    };
    // la puce projet et son Enregistrer, d'un seul tenant
    const auto pr = rectOf("projet"), sr = rectOf("enregistrer");
    if (pr.w > 0.f) frame({pr.x, pr.y, sr.w > 0.f ? sr.right() - pr.x : pr.w, pr.h});
    frame(undoBox_);
    frame(exportBox_);
    // les separations dans les groupes
    const auto rule = [&](float x, const gfx::Rect& box) { ctx.r.fillRect({x, box.y + 6.f, 1.f, box.h - 12.f}, c.border); };
    const auto re = rectOf("retablir");
    if (re.w > 0.f) rule(re.x, undoBox_);
    if (configRect_.w > 0.f && exportBox_.w > 0.f) rule(configRect_.x - 1.f, exportBox_);
}

void TopBar::paintSave(const ui::PaintContext& ctx, const Part& p, bool hot) const {
    const auto& c = ctx.theme.color;
    const auto r = p.rect;
    const bool compact = r.w <= 36.f;
    const gfx::Rect ib{r.x + (compact ? (r.w - 17.f) * 0.5f : 10.f), r.y + (r.h - 17.f) * 0.5f, 17.f, 17.f};
    if (modified_) {
        // quelque chose attend : le bouton en couleur
        ctx.r.fillRoundedRect({r.x + 2.f, r.y + 2.f, r.w - 4.f, r.h - 4.f}, hot ? c.accentHover : c.accent, 5.f);
        const auto fg = c.selectionText;      // sur l'accent plein (comme le logo)
        ui::drawIcon(ctx.r, ui::Icon::Save, ib, fg);
        if (!compact) ctx.r.drawText({ib.right() + 7.f, r.y + (r.h - ctx.r.lineHeight(kLabel8)) * 0.5f}, "Enregistrer", kLabel8, fg);
    } else {
        if (hot) ctx.r.fillRoundedRect({r.x + 2.f, r.y + 2.f, r.w - 4.f, r.h - 4.f}, c.text.withAlpha(22), 5.f);
        ui::drawIcon(ctx.r, ui::Icon::Ok, ib, c.ok);
        if (!compact) ctx.r.drawText({ib.right() + 7.f, r.y + (r.h - ctx.r.lineHeight(kLabel8)) * 0.5f}, "Enregistr\xC3\xA9", kLabel8, c.textMuted);
    }
}

void TopBar::paintBell(const ui::PaintContext& ctx, const Part& p, bool hot) const {
    const auto& c = ctx.theme.color;
    const auto r = p.rect;
    const bool open = opened_ == Menu::Notices;
    if (hot || open) ctx.r.fillRoundedRect(r, c.text.withAlpha(open ? 30 : 22), 6.f);
    const int unread = unreadNotices();
    bellGlyph(ctx.r, {r.x + (r.w - 18.f) * 0.5f, r.y + (r.h - 18.f) * 0.5f, 18.f, 18.f}, unread > 0 ? c.text : c.textMuted);
    if (unread > 0) {
        const std::string n = unread > 99 ? std::string("99+") : std::to_string(unread);
        const float w = std::max(15.f, ctx.r.measure(n, kTiny8).width + 8.f);
        const gfx::Rect badge{r.right() - w - 2.f, r.y + 2.f, w, 15.f};
        ctx.r.fillRoundedRect(badge, c.error, 7.5f);
        ctx.r.drawText({badge.x + (badge.w - ctx.r.measure(n, kTiny8).width) * 0.5f, badge.y + (badge.h - ctx.r.lineHeight(kTiny8)) * 0.5f},
                       n, kTiny8, c.selectionText);
    }
}

void TopBar::paintTasks(const ui::PaintContext& ctx, const Part& p, bool hot) const {
    const auto& c = ctx.theme.color;
    const auto r = p.rect;
    const bool open = opened_ == Menu::Tasks;
    if (hot || open) ctx.r.fillRoundedRect(r, c.text.withAlpha(open ? 30 : 22), 6.f);
    const gfx::Rect ib{r.x + (r.w <= 36.f ? (r.w - 17.f) * 0.5f : 8.f), r.y + (r.h - 17.f) * 0.5f, 17.f, 17.f};
    if (tasks_.empty()) {
        ui::drawIcon(ctx.r, ui::Icon::History, ib, c.textMuted);
        return;
    }
    const auto& t = tasks_.front();
    ui::drawIcon(ctx.r, ui::Icon::History, ib, c.accent);
    if (r.w <= 36.f) return;
    const float tx = ib.right() + 7.f, maxW = r.right() - tx - 8.f;
    std::string text = t.label + (t.progress >= 0.f ? " " + percent(t.progress) : std::string{});
    if (tasks_.size() > 1) text += " (+" + std::to_string(tasks_.size() - 1) + ")";
    const auto n = ctx.r.fitCharacters(text, kSmall8, maxW);
    ctx.r.drawText({tx, r.y + 5.f}, n >= text.size() ? text : text.substr(0, n), kSmall8, c.text);
    const gfx::Rect track{tx, r.bottom() - 10.f, maxW, 4.f};
    ctx.r.fillRoundedRect(track, c.border, 2.f);
    const float f = t.progress >= 0.f ? std::clamp(t.progress, 0.f, 1.f) : 0.35f;
    if (f > 0.f) ctx.r.fillRoundedRect({track.x, track.y, std::max(4.f, track.w * f), track.h}, c.accent, 2.f);
}

void TopBar::paintSparkline(const ui::PaintContext& ctx, const gfx::Rect& r, gfx::Color col) const {
    const auto& c = ctx.theme.color;
    // la periode (la limite du cycle), en pointilles discrets
    for (float x = r.x; x < r.right(); x += 6.f) ctx.r.fillRect({x, r.y, 3.f, 1.f}, c.border);
    if (cycleMs_.size() < 2) {
        ctx.r.fillRect({r.x, r.bottom() - 1.f, r.w, 1.f}, c.border);
        return;
    }
    float top = periodMs_;
    for (const float v : cycleMs_) top = std::max(top, v);
    if (top <= 0.f) top = 1.f;
    const float step = r.w / static_cast<float>(kSpark - 1);
    const float x0 = r.right() - step * static_cast<float>(cycleMs_.size() - 1);
    gfx::Point prev{x0, r.bottom() - r.h * cycleMs_.front() / top};
    for (std::size_t i = 1; i < cycleMs_.size(); ++i) {
        const gfx::Point pt{x0 + step * static_cast<float>(i), r.bottom() - r.h * cycleMs_[i] / top};
        ctx.r.line(prev, pt, col, 1.4f);
        prev = pt;
    }
}

} // namespace app
