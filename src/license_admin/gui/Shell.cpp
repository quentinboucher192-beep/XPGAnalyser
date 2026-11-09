// =============================================================================
//  license_admin/gui/Shell.cpp - 1.33.0 : la coquille de XPG-License-Manager-Admin
//  (voir Shell.hpp). Le menu de gauche, la barre du haut (la page, la cle, la
//  synchro), la pile des pages, le bandeau du bas (le dernier resultat, ses
//  details, le journal de la seance).
// =============================================================================
#include "Shell.hpp"

#include "../../core/SystemOpen.hpp"
#include "../../menu/MenuManager.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/Shapes.hpp"   // 1.41.0 : les cadres arrondis
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace license_admin::gui {

namespace {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;

constexpr float  kRailW = 232.f;     // le menu de gauche
constexpr float  kTopH = 64.f;       // la barre du haut de la page (1.41.0 : 64, la maquette)
constexpr float  kBannerH = 40.f;    // le bandeau du bas (une ligne)
constexpr FontId kF15{15}, kF14{14}, kF13{13}, kF12{12};

Color toneColor(const ui::Theme& th, Tone t) {
    switch (t) {
        case Tone::Ok: return th.color.ok;
        case Tone::Warning: return th.color.warning;
        case Tone::Error: return th.color.error;
        case Tone::Info: break;
    }
    return th.color.info;
}

ui::Icon toneIcon(Tone t) {
    switch (t) {
        case Tone::Ok: return ui::Icon::Ok;
        case Tone::Warning: return ui::Icon::Warning;
        case Tone::Error: return ui::Icon::Error;
        case Tone::Info: break;
    }
    return ui::Icon::Info;
}

// Le texte tronque a la largeur (une ellipse a la fin).
std::string fit(const gfx::IRenderer& r, const std::string& s, FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

// Un texte coupe en lignes a la largeur (mesure par les services de plate-forme : la mise en page).
std::vector<std::string> wrap(const std::string& text, FontId f, float w) {
    std::vector<std::string> out;
    std::string line;
    std::size_t i = 0;
    while (i <= text.size()) {
        const std::size_t sp = text.find(' ', i);
        const std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(tryLine, f) > w) {
            out.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        if (sp == std::string::npos) break;
        i = sp + 1;
    }
    if (!line.empty() || out.empty()) out.push_back(line);
    return out;
}

// Un sablier : huit points en rond, la tete tourne.
void spinner(gfx::IRenderer& r, gfx::Point c, float radius, Color col, double t) {
    constexpr int n = 8;
    const int head = static_cast<int>(t * 10.0) % n;
    for (int i = 0; i < n; ++i) {
        const float a = static_cast<float>(i) * 6.2831853f / n;
        const int age = (head - i + n) % n;
        const auto alpha = static_cast<std::uint8_t>(std::max(40, 255 - age * 30));
        const float x = c.x + std::cos(a) * radius, y = c.y + std::sin(a) * radius;
        r.fillRoundedRect({x - 1.7f, y - 1.7f, 3.4f, 3.4f}, col.withAlpha(alpha), 1.7f);
    }
}

std::string clockOf(Seconds t) {
    if (t <= 0) return {};
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

std::string parentFolder(const std::string& path) {
    const std::filesystem::path p(std::u8string(path.begin(), path.end()));
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec)) return path;
    const auto u = p.parent_path().u8string();
    return std::string(u.begin(), u.end());
}

// ---- une table a lignes fixes (le journal de la seance) ----
class RowsModel final : public ui::ITableModel {
public:
    RowsModel(std::vector<std::string> headers, std::vector<std::vector<std::string>> rows, std::vector<ui::CellStyle> styles)
        : headers_(std::move(headers)), rows_(std::move(rows)), styles_(std::move(styles)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return headers_.size(); }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c < headers_.size() ? headers_[c] : std::string(); }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        if (r >= styles_.size()) return {};
        ui::CellStyle s = styles_[r];
        if (c != 1) s.icon = ui::Icon::None;
        return s;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }

private:
    std::vector<std::string>              headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<ui::CellStyle>            styles_;
};

// =============================================================== le menu ====
// 1.41.0 : LA BARRE DE LA NUIT (la maquette validee : --night). Le menu est sombre quel que soit le theme : c'est la
// marque de l'Admin, l'accent du logiciel (le violet) y porte la page ouverte.
constexpr Color kNightTop = Color::rgb(0x101833), kNightBottom = Color::rgb(0x0B1222);
constexpr Color kNightCard = Color::rgb(0x111A2E), kNightLine = Color::rgb(0x1B2640), kNightActive = Color::rgb(0x1E2A4A);
constexpr Color kNightInk = Color::rgb(0xC7D0E0), kNightMuted = Color::rgb(0x7C89A3), kNightFaint = Color::rgb(0x56627C);
constexpr Color kNightWhite = Color::rgb(0xFFFFFF), kVioletSoft = Color::rgb(0xA993FF), kLive = Color::rgb(0x22C55E);

// Un degrade vertical (deux triangles, la couleur a chaque sommet).
void fillVertical(gfx::IRenderer& r, const Rect& b, Color top, Color bottom) {
    const gfx::Vertex v[6] = {{{b.x, b.y}, top},           {{b.right(), b.y}, top},      {{b.right(), b.bottom()}, bottom},
                              {{b.x, b.y}, top},           {{b.right(), b.bottom()}, bottom}, {{b.x, b.bottom()}, bottom}};
    r.fillTriangles(v, 6);
}

// La cle de la marque, dessinee : l'anneau, la tige et deux dents.
void drawKey(gfx::IRenderer& r, const Rect& box, Color c) {
    const float s = box.w / 20.f;
    const gfx::Point o{box.x + 6.5f * s, box.y + 13.5f * s};
    for (int i = 0; i < 24; ++i) {
        const float a0 = static_cast<float>(i) * 6.2831853f / 24.f, a1 = static_cast<float>(i + 1) * 6.2831853f / 24.f;
        r.line({o.x + std::cos(a0) * 4.f * s, o.y + std::sin(a0) * 4.f * s}, {o.x + std::cos(a1) * 4.f * s, o.y + std::sin(a1) * 4.f * s}, c, 1.9f * s);
    }
    const gfx::Point a{o.x + 2.9f * s, o.y - 2.9f * s}, b{box.x + 17.f * s, box.y + 3.f * s};
    r.line(a, b, c, 1.9f * s);
    r.line({box.x + 13.6f * s, box.y + 6.4f * s}, {box.x + 15.8f * s, box.y + 8.6f * s}, c, 1.9f * s);
    r.line({box.x + 11.3f * s, box.y + 8.7f * s}, {box.x + 12.9f * s, box.y + 10.3f * s}, c, 1.9f * s);
}

// Les quatre groupes du menu (la maquette), par l'identifiant de leur premiere page.
const char* sectionBefore(const std::string& id) {
    if (id == "tableau") return "PILOTAGE";
    if (id == "editions") return "CATALOGUE";
    if (id == "boutique") return "VENTE EN LIGNE \xC2\xB7 PAS ENCORE";
    if (id == "revocations") return "S\xC3\x89" "CURIT\xC3\x89";
    return nullptr;
}

// 1.44.0 : le groupe d'une page, pour le fil du haut (<< Admin > Securite > Administrateurs >>) ; une sous-page : celui de
// sa page parente.
std::string sectionTitle(const std::string& pageId) {
    const std::string id = Shell::parentOf(pageId).empty() ? pageId : Shell::parentOf(pageId);
    std::string current;
    for (const auto& e : Shell::entries()) {
        const std::string eid = e.id;
        if (eid == "tableau") current = "Pilotage";
        else if (eid == "editions") current = "Catalogue";
        else if (eid == "boutique") current = "Vente en ligne";
        else if (eid == "revocations") current = "S\xC3\xA9" "curit\xC3\xA9";
        if (eid == id) return current;
    }
    return {};
}

class NavItem final : public ui::Button {
public:
    NavItem(const Shell::NavEntry& e, const Shell& shell) : ui::Button(e.label, std::string("nav.") + e.id), e_(e), shell_(shell) {
        if (e.online) setTooltip("Demande un paiement en ligne, pas encore fait : la page explique ce qu'elle fera.");
    }
    void setCount(int n) { count_ = n; }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const Rect  r = bounds();
        // 1.44.0 : une sous-page (Droits) garde sa page parente choisie (Administrateurs).
        const bool  sel = shell_.currentPageId() == e_.id || Shell::parentOf(shell_.currentPageId()) == e_.id;
        if (sel) {
            ctx.r.fillRoundedRect(r, kNightActive, 8.f);
            ctx.r.fillRoundedRect({r.x - 8.f, r.y + 7.f, 3.f, r.h - 14.f}, c.accent, 1.5f);
        } else if (hovered() && !e_.online) {
            ctx.r.fillRoundedRect(r, kNightWhite.withAlpha(12), 8.f);
        }
        const Color ink = sel ? kNightWhite : e_.online ? kNightFaint : kNightInk;
        const Color iconInk = sel ? kVioletSoft : e_.online ? kNightFaint : kNightMuted;
        ui::drawIcon(ctx.r, e_.icon, {r.x + 10.f, r.y + (r.h - 17.f) * 0.5f, 17.f, 17.f}, iconInk);
        // Le nombre (licences, clients, PC) ; les demandes qui attendent ta decision : une pastille ambre. 1.44.0 : les
        // demandes d'admin (Administrateurs) et les alertes pas encore vues (Journal des admins), ambre aussi.
        const std::string eid = e_.id;
        const bool demandes = eid == "demandes" || eid == "admins" || eid == "journal";
        const int  count = eid == "demandes" ? shell_.pendingRequests()
                           : eid == "admins" ? shell_.pendingAdminRequests()
                           : eid == "journal" ? shell_.unseenAlerts()
                                              : count_;
        float badgeW = 0.f;
        if (count > 0) {
            const std::string n = count > 999 ? std::string("999+") : std::to_string(count);
            const float nw = ctx.r.measure(n, kF12).width;
            badgeW = std::max(24.f, nw + 14.f);
            const Rect pill{r.right() - 8.f - badgeW, r.y + (r.h - 20.f) * 0.5f, badgeW, 20.f};
            ctx.r.fillRoundedRect(pill, demandes ? Color::rgb(0xF59E0B).withAlpha(46) : kNightWhite.withAlpha(sel ? 22 : 14), 10.f);
            ctx.r.drawText({pill.x + (pill.w - nw) * 0.5f, pill.y + (20.f - ctx.r.lineHeight(kF12)) * 0.5f}, n, kF12,
                           demandes ? Color::rgb(0xFBBF24) : sel ? kNightWhite : kNightMuted);
            badgeW += 8.f;
        }
        const FontId f = sel ? gfx::semibold(kF14) : kF14;
        const std::string t = fit(ctx.r, e_.label, f, r.w - 44.f - badgeW);
        ctx.r.drawText({r.x + 38.f, r.y + (r.h - ctx.r.lineHeight(f)) * 0.5f}, t, f, ink);
    }

private:
    const Shell::NavEntry& e_;
    const Shell&           shell_;
    int                    count_{0};
};

// Un bouton sur la nuit (le dossier du poste) : le texte clair, le survol a peine marque.
class NightButton final : public ui::Button {
public:
    NightButton(std::string text, std::string id) : ui::Button(std::move(text), std::move(id)) {}

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect r = bounds();
        if (hovered()) ctx.r.fillRoundedRect(r, kNightWhite.withAlpha(12), 8.f);
        ui::drawIcon(ctx.r, ui::Icon::Folder, {r.x + 8.f, r.y + (r.h - 15.f) * 0.5f, 15.f, 15.f}, kNightMuted);
        ctx.r.drawText({r.x + 30.f, r.y + (r.h - ctx.r.lineHeight(kF12)) * 0.5f}, fit(ctx.r, text(), kF12, r.w - 34.f), kF12, kNightMuted);
    }
};

// La carte du service, en bas du menu : en direct (le point vert), ou ce qui manque. Un clic : la page Service en ligne.
class ServiceCard final : public ui::Button {
public:
    explicit ServiceCard(const Shell& shell) : ui::Button("Service en ligne", "shell.service-carte"), shell_(shell) {
        setTooltip("Le service des licences. Clic : la page Service en ligne.");
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect r = bounds();
        ctx.r.fillRoundedRect(r, kNightLine, 12.f);
        ctx.r.fillRoundedRect(r.inset(1.f, 1.f), hovered() ? Color::rgb(0x15203A) : kNightCard, 11.f);
        const auto& s = shell_.workspace().service();
        const bool  live = shell_.live();
        const Color dot = live ? kLive : !s.problem.empty() ? Color::rgb(0xEF4444) : s.configured ? Color::rgb(0xF59E0B) : kNightFaint;
        const float x = r.x + 14.f;
        float       y = r.y + 12.f;
        if (live) ctx.r.fillRoundedRect({x - 3.f, y + 2.f, 14.f, 14.f}, kLive.withAlpha(46), 7.f);
        ctx.r.fillRoundedRect({x, y + 5.f, 8.f, 8.f}, dot, 4.f);
        const FontId ft = gfx::semibold(kF13);
        ctx.r.drawText({x + 16.f, y}, "Service en ligne", ft, kNightWhite);
        y += ctx.r.lineHeight(ft) + 3.f;
        std::string line;
        if (!s.problem.empty()) line = "R\xC3\xA9glage illisible";
        else if (!s.configured) line = "Pas encore r\xC3\xA9gl\xC3\xA9";
        else if (live) {
            const double ago = shell_.secondsSinceLive();
            line = "Connect\xC3\xA9 \xC2\xB7 relev\xC3\xA9 il y a " + std::to_string(static_cast<int>(std::max(0.0, ago))) + " s";
        } else line = "R\xC3\xA9gl\xC3\xA9 \xC2\xB7 pas en direct";
        ctx.r.drawText({x, y}, fit(ctx.r, line, kF12, r.w - 28.f), kF12, kNightMuted);
        y += ctx.r.lineHeight(kF12) + 1.f;
        ctx.r.drawText({x, y}, std::string("v") + kAppVersion, kF12, kNightFaint);
    }

private:
    const Shell& shell_;
};

class Rail final : public ui::Widget {
public:
    Rail(Shell& shell, core::ConnectionScope& links) : ui::Widget("shell.menu"), shell_(shell) {
        for (const auto& e : Shell::entries()) {
            auto item = std::make_unique<NavItem>(e, shell);
            const std::string id = e.id;
            links += item->clicked->connect([this, id] { shell_.navigate(id); });
            items_.push_back(&static_cast<NavItem&>(addChild(std::move(item))));
        }
        card_ = &static_cast<ServiceCard&>(addChild(std::make_unique<ServiceCard>(shell)));
        links += card_->clicked->connect([this] { shell_.navigate("service"); });
        folder_ = &static_cast<NightButton&>(addChild(std::make_unique<NightButton>("Dossier du poste", "shell.dossier")));
        links += folder_->clicked->connect([this] { shell_.openFolder(shell_.workspace().folder()); });
        update();
    }
    // Les nombres du menu (apres chaque relecture du Workspace). 1.44.0 : les entrees de l'admin+ seul (le journal des
    // admins) ne sont montrees qu'a lui.
    void update() {
        const Dashboard d = shell_.workspace().dashboard();
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const std::string id = Shell::entries()[i].id;
            items_[i]->setCount(id == "licences" ? d.licences : id == "clients" ? d.clients : id == "activations" ? d.devices + d.onlineDevices : 0);
            const auto want = shell_.pageAllowed(id) ? ui::Visibility::Visible : ui::Visibility::Collapsed;
            if (items_[i]->visibility() != want) {
                items_[i]->setVisibility(want);
                invalidateLayout();
            }
        }
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        // La hauteur d'une entree : 36 (la maquette), moins si la fenetre est basse (toutes restent visibles).
        int sections = 0, shown = 0;
        for (const auto& e : Shell::entries()) sections += sectionBefore(e.id) ? 1 : 0;
        for (auto* it : items_) shown += it->visibility() == ui::Visibility::Collapsed ? 0 : 1;   // 1.44.0 : selon le role
        const float top = b.y + 84.f, n = static_cast<float>(std::max(1, shown));
        const float needFull = n * 36.f + static_cast<float>(sections) * 30.f;
        const float room = b.bottom() - 12.f - top;
        const bool  roomForCard = room >= needFull + 120.f;
        const float avail = room - (roomForCard ? 120.f : 44.f);
        const float rowH = std::clamp((avail - static_cast<float>(sections) * 26.f) / n, 28.f, 36.f);
        const float capH = std::clamp(avail - rowH * n, 18.f * static_cast<float>(sections), 30.f * static_cast<float>(sections)) / std::max(1.f, static_cast<float>(sections));
        float y = top;
        captions_.clear();
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (const char* cap = sectionBefore(Shell::entries()[i].id)) {
                captions_.push_back({y + capH - 18.f, cap});
                y += capH;
            }
            if (items_[i]->visibility() == ui::Visibility::Collapsed) continue;
            items_[i]->setBounds({b.x + 14.f, y, b.w - 28.f, rowH - 2.f});
            y += rowH;
        }
        folder_->setTooltip(shell_.workspace().folder());
        const float fy = b.bottom() - 34.f;
        folder_->setVisibility(fy > y + 4.f ? ui::Visibility::Visible : ui::Visibility::Hidden);
        folder_->setBounds({b.x + 10.f, fy, std::min(b.w - 70.f, 160.f), 26.f});
        footerY_ = fy;
        const float cy = fy - 82.f;
        card_->setVisibility(cy > y + 6.f ? ui::Visibility::Visible : ui::Visibility::Hidden);
        card_->setBounds({b.x + 14.f, cy, b.w - 28.f, 74.f});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const Rect  b = bounds();
        fillVertical(ctx.r, b, kNightTop, kNightBottom);
        // La marque : la cle sur un carre de l'accent ; le nom, puis << Admin - Editeur seulement >> (l'outil n'est
        // jamais livre a un client).
        const Rect logo{b.x + 18.f, b.y + 20.f, 38.f, 38.f};
        ctx.r.fillRoundedRect({logo.x, logo.y + 4.f, logo.w, logo.h}, c.accent.withAlpha(60), 11.f);
        ctx.r.fillRoundedRect(logo, c.accent, 11.f);
        ctx.r.fillRoundedRect({logo.x + 3.f, logo.y + 2.f, logo.w - 6.f, logo.h * 0.45f}, kNightWhite.withAlpha(22), 9.f);
        drawKey(ctx.r, {logo.x + 9.f, logo.y + 9.f, 20.f, 20.f}, kNightWhite);
        const FontId fn = gfx::semibold(kF15);
        ctx.r.drawText({logo.right() + 12.f, logo.y + 1.f}, "XPG License Manager", fn, kNightWhite);
        // 1.44.0 : qui est sur ce poste (la maquette : << Camille - admin+ >>) ; sans cle : l'outil de l'editeur.
        const Workspace& w = shell_.workspace();
        std::string who = "Admin \xC2\xB7 \xC3\x89" "diteur seulement";
        switch (w.role()) {
            case Role::Plus: who = (w.key().name.empty() ? std::string("Toi") : w.key().name) + " \xC2\xB7 admin+"; break;
            case Role::Admin: who = (w.me() ? w.me()->name : w.adminKey().name) + " \xC2\xB7 admin"; break;
            case Role::Pending: who = (w.adminKey().name.empty() ? std::string("Toi") : w.adminKey().name) + " \xC2\xB7 en attente"; break;
            case Role::Retired: who = (w.me() ? w.me()->name : w.adminKey().name) + " \xC2\xB7 admin retir\xC3\xA9"; break;
            case Role::None: break;
        }
        ctx.r.drawText({logo.right() + 12.f, logo.y + 21.f}, fit(ctx.r, who, kF12, b.right() - 14.f - (logo.right() + 12.f)), kF12, kNightMuted);
        const FontId fc = gfx::semibold(FontId{11});
        for (const auto& [y, text] : captions_) ctx.r.drawText({b.x + 24.f, y}, text, fc, kNightFaint);
        if (folder_->visible()) {
            const std::string v = kAppVersion;
            const float vw = ctx.r.measure(v, kF12).width;
            ctx.r.drawText({b.right() - 18.f - vw, footerY_ + (26.f - ctx.r.lineHeight(kF12)) * 0.5f}, v, kF12, kNightFaint);
        }
    }

private:
    Shell&                 shell_;
    std::vector<NavItem*>  items_;
    ServiceCard*           card_{nullptr};
    NightButton*           folder_{nullptr};
    std::vector<std::pair<float, std::string>> captions_;
    float                  footerY_{0.f};
};

// ======================================================= la barre du haut ====
// Une pastille cliquable : l'etat de la cle, du service (ou le travail en cours). 1.41.0 : en direct, un point vert
// et le texte vert (la maquette) ; sinon une pastille claire, bordee.
class Chip final : public ui::Button {
public:
    explicit Chip(std::string id) : ui::Button("", std::move(id)) {}
    void set(std::string text, Tone tone, ui::Icon icon, bool spin, bool neutral, bool live = false) {
        if (text == text_ && tone == tone_ && icon == icon_ && spin == spin_ && neutral == neutral_ && live == live_) return;
        text_ = std::move(text);
        tone_ = tone;
        icon_ = icon;
        spin_ = spin;
        neutral_ = neutral;
        live_ = live;
        setText(text_);
        invalidateLayout();
    }
    [[nodiscard]] float wanted() const { return ui::measureWidth(text_, kF13) + 18.f + 26.f; }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {wanted(), 32.f};
        h.minimum = h.preferred;
        return h;
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const Rect  r = bounds();
        const Color ink = live_ ? c.ok : neutral_ ? c.textMuted : toneColor(ctx.theme, tone_);
        const Color text = live_ ? c.ok : neutral_ || tone_ == Tone::Info ? c.text : ink;
        const Color fill = live_ ? c.ok.withAlpha(hovered() ? 30 : 18) : neutral_ || tone_ == Tone::Info ? (hovered() ? c.rowAltBg : c.panelBg) : ink.withAlpha(hovered() ? 34 : 20);
        const Color line = live_ ? c.ok.withAlpha(60) : neutral_ || tone_ == Tone::Info ? c.border : ink.withAlpha(70);
        ui::shapes::fillFramedRect(ctx.r, r, fill, line, r.h * 0.5f);
        const Rect ic{r.x + 12.f, r.y + (r.h - 14.f) * 0.5f, 14.f, 14.f};
        if (spin_) spinner(ctx.r, {ic.x + 7.f, ic.y + 7.f}, 5.5f, ink, ctx.time);
        else if (live_) {
            const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(ctx.time * 3.0));
            ctx.r.fillRoundedRect({ic.x - 1.f, ic.y - 1.f, 16.f, 16.f}, kLive.withAlpha(static_cast<std::uint8_t>(30 + 40 * pulse)), 8.f);
            ctx.r.fillRoundedRect({ic.x + 3.f, ic.y + 3.f, 8.f, 8.f}, kLive, 4.f);
        } else ui::drawIcon(ctx.r, icon_, ic, ink);
        const FontId f = live_ ? gfx::semibold(kF13) : kF13;
        const std::string t = fit(ctx.r, text_, f, r.w - 44.f);
        ctx.r.drawText({r.x + 32.f, r.y + (r.h - ctx.r.lineHeight(f)) * 0.5f}, t, f, text);
    }

private:
    std::string text_;
    Tone        tone_{Tone::Info};
    ui::Icon    icon_{ui::Icon::None};
    bool        spin_{false}, neutral_{false}, live_{false};
};

class TopBar final : public ui::Widget {
public:
    TopBar(Shell& shell, core::ConnectionScope& links) : ui::Widget("shell.haut"), shell_(shell) {
        key_ = &static_cast<Chip&>(addChild(std::make_unique<Chip>("shell.cle")));
        sync_ = &static_cast<Chip&>(addChild(std::make_unique<Chip>("shell.synchro")));
        // 1.41.0 : l'action principale de l'Admin, toujours en haut a droite (la maquette).
        auto nl = std::make_unique<ui::Button>("Nouvelle licence", "shell.nouvelle-licence");
        nl->setStyle(ui::Button::Style::Primary);
        nl->setIcon(ui::Icon::Plus);
        newLicence_ = &static_cast<ui::Button&>(addChild(std::move(nl)));
        // 1.44.0 : un admin (sa cle d'admin) va a Administrateurs, l'admin+ et un poste sans cle a Parametres de securite.
        links += key_->clicked->connect([this] {
            const Role r = shell_.workspace().role();
            shell_.navigate(r == Role::Plus || r == Role::None ? "securite" : "admins");
        });
        links += sync_->clicked->connect([this] { shell_.navigate("service"); });
        links += newLicence_->clicked->connect([this] {
            if (shell_.workspace().can("licence_creer") && shell_.workspace().hasRegistry()) shell_.openDialog(makeLicenceWizard(shell_.host(), {}));
        });
        update();
    }
    // L'etat a montrer (a chaque image : un travail lent commence ou finit).
    void update() {
        const Workspace& w = shell_.workspace();
        const auto& k = w.key();
        const Role role = w.role();
        key_->set(shell_.keyStatusText(), shell_.keyStatusTone(), role == Role::Plus || role == Role::Admin ? ui::Icon::Lock : ui::Icon::Unlock, false, false);
        if (role == Role::Plus || role == Role::None)
            key_->setTooltip(k.present ? "La cl\xC3\xA9 de l'\xC3\xA9" "diteur : " + k.id + (k.name.empty() ? std::string() : " (" + k.name + ")") + ". Clic : Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9."
                                       : (k.problem.empty() ? std::string("Aucune cl\xC3\xA9 sur ce poste.") : k.problem) + " Clic : Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9.");
        else   // 1.44.0 : un admin (ou en attente, ou retire) : sa cle d'admin
            key_->setTooltip("Ta cl\xC3\xA9 d'admin : " + w.adminKey().id + (role == Role::Admin ? " \xC2\xB7 " + std::to_string(w.myRights().size()) + " droits sur 13" : std::string()) +
                             ". Clic : Administrateurs.");
        // 1.44.0 : le droit << licence_creer >> (un admin qui ne l'a pas : le bouton dit pourquoi).
        const bool canSign = w.can("licence_creer") && w.hasRegistry();
        newLicence_->setEnabled(canSign);
        newLicence_->setTooltip(canSign ? std::string("L'assistant : le client, l'\xC3\xA9" "dition, les PC (ou une licence en ligne), la dur\xC3\xA9" "e, puis signer.")
                                : role == Role::None ? std::string("Il faut d'abord la cl\xC3\xA9 de l'\xC3\xA9" "diteur (Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9), ou devenir admin (Administrateurs).")
                                : !w.can("licence_creer") ? w.whyNot("licence_creer")
                                             : std::string("Il faut d'abord le registre sign\xC3\xA9 (\xC3\x89" "ditions ; un admin le re\xC3\xA7oit avec l'archive)."));
        if (shell_.busy()) {
            sync_->set(shell_.syncStatusText(), Tone::Info, ui::Icon::Refresh, true, false);
            sync_->setTooltip("Un travail lent tourne sur un autre fil : la fen\xC3\xAAtre reste utilisable. Les op\xC3\xA9rations qui \xC3\xA9" "crivent attendent sa fin.");
        } else {
            const auto& s = shell_.workspace().service();
            const bool certWarn = s.hasState && (!s.state.certOk || s.state.needsRenewal(shell_.workspace().now()));
            const Tone t = !s.problem.empty() ? Tone::Error : s.configured && (!s.syncKey || certWarn) ? Tone::Warning : Tone::Info;
            sync_->set(shell_.syncStatusText(), t, ui::Icon::Globe, false, !s.configured && s.problem.empty(), shell_.live() && s.problem.empty() && !certWarn);
            sync_->setTooltip(!s.problem.empty() ? s.problem
                              : s.configured   ? "Le service des licences : " + s.config.url + (certWarn ? std::string(" ; sa cl\xC3\xA9 est \xC3\xA0 certifier ou \xC3\xA0 renouveler") : std::string()) +
                                                     ". Clic : la page Service en ligne."
                                               : std::string("Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9. Clic : la page Service en ligne."));
        }
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        float x = b.right() - 20.f;
        const float bw = std::max(150.f, newLicence_->sizeHint().preferred.w + 24.f);
        x -= bw;
        newLicence_->setBounds({x, b.y + (b.h - 36.f) * 0.5f, bw, 36.f});
        x -= 12.f;
        for (Chip* chip : {key_, sync_}) {
            const float w = std::min(chip->wanted(), std::max(120.f, b.w * 0.27f));
            x -= w;
            chip->setBounds({x, b.y + (b.h - 32.f) * 0.5f, w, 32.f});
            x -= 8.f;
        }
        titleRight_ = x - 8.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const Rect  b = bounds();
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, c.border);
        // Le fil : << Admin > la page >> (la page porte son grand titre ; ici, ou l'on est).
        float x = b.x + 28.f;
        const std::string crumb = "Admin";
        const float ty = b.y + (b.h - ctx.r.lineHeight(kF14)) * 0.5f;
        ctx.r.drawText({x, ty}, crumb, kF14, c.textMuted);
        x += ctx.r.measure(crumb, kF14).width + 10.f;
        // le chevron
        const float cy = b.y + b.h * 0.5f;
        const auto chevron = [&] {
            ctx.r.line({x, cy - 4.f}, {x + 4.f, cy}, c.textDisabled, 1.5f);
            ctx.r.line({x + 4.f, cy}, {x, cy + 4.f}, c.textDisabled, 1.5f);
            x += 14.f;
        };
        chevron();
        // 1.44.0 : le fil de la maquette - le groupe (<< Securite >>), la page parente d'une sous-page (<< Administrateurs >>).
        std::vector<std::string> trail;
        if (const std::string s = sectionTitle(shell_.currentPageId()); !s.empty()) trail.push_back(s);
        if (const std::string p = Shell::parentOf(shell_.currentPageId()); !p.empty())
            if (const auto* e = Shell::entry(p)) trail.push_back(e->label);
        for (const auto& t : trail) {
            if (x + ctx.r.measure(t, kF14).width > titleRight_ - 80.f) break;
            ctx.r.drawText({x, ty}, t, kF14, c.textMuted);
            x += ctx.r.measure(t, kF14).width + 10.f;
            chevron();
        }
        const FontId fb = gfx::semibold(kF14);
        ctx.r.drawText({x, ty}, fit(ctx.r, shell_.pageTitle(), fb, titleRight_ - x), fb, c.text);
    }

private:
    Shell&      shell_;
    Chip*       key_{nullptr};
    Chip*       sync_{nullptr};
    ui::Button* newLicence_{nullptr};
    float       titleRight_{0.f};
};

// ======================================================== le bandeau bas ====
class Banner final : public ui::Widget {
public:
    Banner(Shell& shell, core::ConnectionScope& links) : ui::Widget("shell.bandeau"), shell_(shell) {
        details_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("D\xC3\xA9tails", "shell.details")));
        folder_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ouvrir le dossier", "shell.ouvrir-dossier")));
        log_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Journal", "shell.journal")));
        details_->setStyle(ui::Button::Style::Flat);
        folder_->setStyle(ui::Button::Style::Flat);
        log_->setStyle(ui::Button::Style::Flat);
        folder_->setIcon(ui::Icon::Folder);
        log_->setIcon(ui::Icon::History);
        log_->setTooltip("Tous les r\xC3\xA9sultats de cette s\xC3\xA9" "ance (les plus r\xC3\xA9" "cents d'abord).");
        auto table = std::make_unique<ui::TableView>("shell.journal.table");
        table->setColumns({{"Heure", 90.f}, {"R\xC3\xA9sultat", 110.f}, {"Message", 900.f}});
        table->setAlternatingRowColors(true);
        table_ = &static_cast<ui::TableView&>(addChild(std::move(table)));
        table_->setVisibility(ui::Visibility::Collapsed);
        links += details_->clicked->connect([this] { shell_.setDetailsOpen(!shell_.detailsOpen()); });
        links += log_->clicked->connect([this] { shell_.setLogOpen(!shell_.logOpen()); });
        links += folder_->clicked->connect([this] {
            if (const auto* m = shell_.lastMessage(); m && !m->result.output.empty()) shell_.openFolder(parentFolder(m->result.output));
        });
        update();
    }

    // Le dernier message, les boutons, le journal.
    void update() {
        const auto* m = shell_.lastMessage();
        const bool hasDetails = m && !m->result.details.empty();
        details_->setVisibility(hasDetails ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        details_->setText(shell_.detailsOpen() ? std::string("Masquer les d\xC3\xA9tails")
                                               : "D\xC3\xA9tails (" + std::to_string(hasDetails ? m->result.details.size() : 0) + ")");
        folder_->setVisibility(m && !m->result.output.empty() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (m && !m->result.output.empty()) folder_->setTooltip(m->result.output);
        log_->setText(shell_.logOpen() ? std::string("Fermer le journal") : "Journal (" + std::to_string(shell_.sessionLog().size()) + ")");
        table_->setVisibility(shell_.logOpen() ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (shown_ != shell_.sessionLog().size()) {
            shown_ = shell_.sessionLog().size();
            std::vector<std::vector<std::string>> rows;
            std::vector<ui::CellStyle>            styles;
            const auto& log = shell_.sessionLog();
            for (auto it = log.rbegin(); it != log.rend(); ++it) {
                const char* what = it->tone == Tone::Ok ? "fait" : it->tone == Tone::Error ? "refus\xC3\xA9" : it->tone == Tone::Warning ? "attention" : "info";
                rows.push_back({clockOf(it->at), what, it->result.message});
                ui::CellStyle s;
                s.icon = toneIcon(it->tone);
                s.iconTone = it->tone == Tone::Ok ? ui::Tone::Ok : it->tone == Tone::Error ? ui::Tone::Error : it->tone == Tone::Warning ? ui::Tone::Warning : ui::Tone::Info;
                if (it->tone == Tone::Error) s.fgTone = ui::Tone::Error;
                styles.push_back(s);
                for (const auto& d : it->result.details) {
                    rows.push_back({"", "", "    " + d});
                    ui::CellStyle ds;
                    ds.fgTone = ui::Tone::Muted;
                    styles.push_back(ds);
                }
            }
            model_ = std::make_shared<RowsModel>(std::vector<std::string>{"Heure", "R\xC3\xA9sultat", "Message"}, std::move(rows), std::move(styles));
            table_->setModel(model_);
        }
        invalidateLayout();
    }

    // La hauteur voulue a cette largeur (le parent la donne ; `maxH` : la moitie de la fenetre).
    [[nodiscard]] float wanted(float width, float maxH) {
        float h = kBannerH;
        lines_.clear();
        const auto* m = shell_.lastMessage();
        if (shell_.detailsOpen() && m) {
            for (const auto& d : m->result.details)
                for (auto& l : wrap(d, kF14, std::max(200.f, width - 64.f))) lines_.push_back(std::move(l));
            h += static_cast<float>(lines_.size()) * 19.f + 10.f;
        }
        if (shell_.logOpen()) h = std::max(h, std::clamp(maxH * 0.7f, std::min(160.f, maxH), 320.f));
        return std::min(std::max(h, kBannerH), std::max(kBannerH, maxH));
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        float x = b.right() - 10.f;
        for (ui::Button* btn : {log_, folder_, details_}) {
            if (btn->visibility() == ui::Visibility::Collapsed) continue;
            const float w = btn->sizeHint().preferred.w;
            x -= w;
            btn->setBounds({x, b.y + (kBannerH - 28.f) * 0.5f, w, 28.f});
            x -= 4.f;
        }
        messageRight_ = x - 8.f;
        float y = b.y + kBannerH;
        if (!lines_.empty()) y += static_cast<float>(lines_.size()) * 19.f + 10.f;
        if (table_->visibility() != ui::Visibility::Collapsed) table_->setBounds({b.x + 12.f, y + 2.f, b.w - 24.f, std::max(40.f, b.bottom() - y - 10.f)});
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect  b = bounds();
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.fillRect({b.x, b.y, b.w, 1.f}, c.border);
        const auto* m = shell_.lastMessage();
        const Tone  tone = m ? m->tone : Tone::Info;
        const Color ink = m ? toneColor(th, tone) : c.textMuted;
        const Rect  row{b.x, b.y + 1.f, b.w, kBannerH - 1.f};
        if (m && tone != Tone::Info) ctx.r.fillRect(row, ink.withAlpha(18));
        if (m) ctx.r.fillRect({row.x, row.y, 4.f, row.h}, ink);
        ui::drawIcon(ctx.r, m ? toneIcon(tone) : ui::Icon::Info, {b.x + 16.f, row.y + (row.h - 16.f) * 0.5f, 16.f, 16.f}, ink);
        std::string text = m ? m->result.message
                             : std::string("Pr\xC3\xAAt. Glisse des fichiers sur la fen\xC3\xAAtre (licences, registre, r\xC3\xA9vocations, demandes) pour les ranger dans l'archive.");
        if (m && m->at > 0) {
            const std::string at = clockOf(m->at);
            const float aw = ctx.r.measure(at, kF13).width;
            ctx.r.drawText({messageRight_ - aw, row.y + (row.h - ctx.r.lineHeight(kF13)) * 0.5f}, at, kF13, c.textMuted);
            text = fit(ctx.r, text, kF15, messageRight_ - aw - 12.f - (b.x + 42.f));
        } else {
            text = fit(ctx.r, text, kF15, messageRight_ - (b.x + 42.f));
        }
        const Color textInk = !m ? c.textMuted : tone == Tone::Info ? c.text : ink;
        ctx.r.drawText({b.x + 42.f, row.y + (row.h - ctx.r.lineHeight(kF15)) * 0.5f}, text, kF15, textInk);
        float y = b.y + kBannerH + 4.f;
        for (const auto& l : lines_) {
            ctx.r.drawText({b.x + 42.f, y}, l, kF14, c.textMuted);
            y += 19.f;
        }
    }

    // Par-dessus la table (dessinee apres le fond) : le journal vide le dit.
    void onPaintOverlay(const ui::PaintContext& ctx) override {
        if (table_->visibility() == ui::Visibility::Collapsed || !shell_.sessionLog().empty()) return;
        const Rect t = table_->bounds();
        ctx.r.drawText({t.x + 12.f, t.y + 36.f}, "Rien encore dans cette s\xC3\xA9" "ance.", kF14, ctx.theme.color.textMuted);
    }

private:
    Shell&                      shell_;
    ui::Button*                 details_{nullptr};
    ui::Button*                 folder_{nullptr};
    ui::Button*                 log_{nullptr};
    ui::TableView*              table_{nullptr};
    std::shared_ptr<RowsModel>  model_;
    std::size_t                 shown_{static_cast<std::size_t>(-1)};
    std::vector<std::string>    lines_;
    float                       messageRight_{0.f};
};

// ============================================================ l'alerte ====
// 1.44.0 : L'ALERTE EN DIRECT (planche 04, en bas a gauche) - une ligne du journal des admins que tu as demande a voir
// tout de suite (<< Critique : Hugo a revoque XPG-2026-00377 >>), sur la nuit, avec << Voir >> (le journal, la ligne
// depliee) et << Fermer >>. Montree quelle que soit la page ; la plus recente, et combien d'autres.
class ToastView final : public ui::Widget {
public:
    ToastView(Shell& shell, core::ConnectionScope& links) : ui::Widget("shell.toast"), shell_(shell) {
        see_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Voir", "shell.toast.voir")));
        close_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Fermer", "shell.toast.fermer")));
        see_->setTooltip("Le journal des admins, cette ligne d\xC3\xA9pli\xC3\xA9" "e.");
        links += see_->clicked->connect([this] { shell_.openToast(); });
        links += close_->clicked->connect([this] { shell_.dismissToasts(); });
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float cw = std::max(76.f, close_->sizeHint().preferred.w + 8.f), sw = std::max(64.f, see_->sizeHint().preferred.w + 8.f);
        close_->setBounds({b.right() - 14.f - cw, b.y + (b.h - 32.f) * 0.5f, cw, 32.f});
        see_->setBounds({close_->bounds().x - 8.f - sw, b.y + (b.h - 32.f) * 0.5f, sw, 32.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& list = shell_.toasts();
        if (list.empty()) return;
        const Rect b = bounds();
        const auto& t = list.back();
        ctx.r.fillRoundedRect({b.x, b.y + 3.f, b.w, b.h + 2.f}, Color::rgb(0x000000).withAlpha(50), 14.f);
        ctx.r.fillRoundedRect(b, Color::rgb(0x111827), 14.f);
        const Color tone = t.level >= 3 ? Color::rgb(0xEF4444) : Color::rgb(0xF59E0B);
        const Rect ib{b.x + 16.f, b.y + (b.h - 36.f) * 0.5f, 36.f, 36.f};
        ctx.r.fillRoundedRect(ib, tone.withAlpha(40), 10.f);
        ui::drawIcon(ctx.r, ui::Icon::Warning, {ib.x + 9.f, ib.y + 9.f, 18.f, 18.f}, tone);
        const float x = ib.right() + 14.f, w = see_->bounds().x - 12.f - x;
        const FontId ft = gfx::semibold(kF14);
        ctx.r.drawText({x, b.y + 15.f}, fit(ctx.r, t.title, ft, w), ft, kNightWhite);
        std::string line = t.line;
        if (list.size() > 1) line += " \xC2\xB7 + " + std::to_string(list.size() - 1) + (list.size() > 2 ? " autres alertes" : " autre alerte");
        ctx.r.drawText({x, b.y + 38.f}, fit(ctx.r, line, kF12, w), kF12, kNightMuted);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && bounds().contains(d->pos)) return ui::EventResult::Consumed;
        return ui::EventResult::Ignored;
    }

private:
    Shell&      shell_;
    ui::Button* see_{nullptr};
    ui::Button* close_{nullptr};
};

// ============================================================ la pile ====
class PageStack final : public ui::Widget {
public:
    PageStack() : ui::Widget("shell.pages") {}

protected:
    void onLayout() override {
        for (const auto& c : children())
            if (c->visibility() != ui::Visibility::Collapsed) c->setBounds(bounds());
    }
    void onPaint(const ui::PaintContext& ctx) override { ctx.r.fillRect(bounds(), ctx.theme.color.windowBg); }
};

// =========================================================== la racine ====
class ShellView final : public ui::Widget {
public:
    explicit ShellView(Shell& shell) : ui::Widget("shell.racine"), shell_(shell) {
        rail_ = &static_cast<Rail&>(addChild(std::make_unique<Rail>(shell, links_)));
        top_ = &static_cast<TopBar&>(addChild(std::make_unique<TopBar>(shell, links_)));
        stack_ = &addChild(std::make_unique<PageStack>());
        banner_ = &static_cast<Banner&>(addChild(std::make_unique<Banner>(shell, links_)));
        toast_ = &static_cast<ToastView&>(addChild(std::make_unique<ToastView>(shell, links_)));   // 1.44.0 : par-dessus tout
        toast_->setVisibility(ui::Visibility::Collapsed);
    }
    [[nodiscard]] ui::Widget& stack() { return *stack_; }
    void update() {
        rail_->update();
        top_->update();
        banner_->update();
        updateToast();
        invalidateLayout();
    }
    void updateTop() { top_->update(); }
    void updateToast() {
        const auto want = shell_.toasts().empty() ? ui::Visibility::Collapsed : ui::Visibility::Visible;
        if (toast_->visibility() != want) {
            toast_->setVisibility(want);
            invalidateLayout();
        }
        toast_->invalidate();
        rail_->invalidate();
    }

protected:
    void onLayout() override {
        const Rect  b = bounds();
        const float rail = std::min(kRailW, b.w * 0.4f);
        rail_->setBounds({b.x, b.y, rail, b.h});
        const float x = b.x + rail, w = std::max(0.f, b.w - rail);
        top_->setBounds({x, b.y, w, kTopH});
        const float bh = banner_->wanted(w, std::max(kBannerH, (b.h - kTopH) * 0.5f));
        banner_->setBounds({x, b.bottom() - bh, w, bh});
        stack_->setBounds({x, b.y + kTopH, w, std::max(0.f, b.h - kTopH - bh)});
        // Le bandeau change de hauteur sans changer de cadre (les details) : il se refait quand meme.
        banner_->invalidateLayout();
        // 1.44.0 : l'alerte, en bas a gauche de la page (au-dessus du bandeau).
        const float tw = std::min(620.f, std::max(320.f, w - 48.f));
        toast_->setBounds({x + 24.f, b.bottom() - bh - 16.f - 68.f, tw, 68.f});
    }
    void onPaint(const ui::PaintContext& ctx) override { ctx.r.fillRect(bounds(), ctx.theme.color.windowBg); }

private:
    Shell&                shell_;
    core::ConnectionScope links_;
    Rail*                 rail_{nullptr};
    TopBar*               top_{nullptr};
    ui::Widget*           stack_{nullptr};
    Banner*               banner_{nullptr};
    ToastView*            toast_{nullptr};
};

// Une page qui manque (une fabrique qui n'a rien rendu) : dit-le, ne simule rien.
class MissingPage final : public Page {
public:
    MissingPage(ShellHost host, std::string title) : Page("page.manquante", std::move(host)), title_(std::move(title)) {}
    [[nodiscard]] std::string title() const override { return title_; }
    void refresh() override {}

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        ctx.r.drawText({b.x + 24.f, b.y + 24.f}, "Cette page ne s'est pas construite.", ctx.theme.font.ui, ctx.theme.color.error);
    }

private:
    std::string title_;
};

} // namespace

// ======================================================================= Shell ====
const std::vector<Shell::NavEntry>& Shell::entries() {
    static const std::vector<NavEntry> list = {
        // 1.41.0 : les groupes de la maquette - Pilotage (le tableau, les licences, les clients, les demandes, les PC),
        // Catalogue (les editions, les fonctionnalites, les flottantes), la vente en ligne, Securite.
        {"tableau", "Tableau de bord", ui::Icon::Chart, false},
        {"licences", "Licences", ui::Icon::Document, false},
        {"clients", "Clients", ui::Icon::User, false},
        {"demandes", "Demandes en ligne", ui::Icon::Import, false},       // 1.35.0 : le service des licences
        {"activations", "Activations", ui::Icon::Station, false},
        {"editions", "Offres", ui::Icon::Layers, false},                  // 1.42.0 : les editions s'appellent les offres
        {"options", "Options", ui::Icon::Star, false},                    // 1.42.0
        {"fonctionnalites", "Fonctionnalit\xC3\xA9s", ui::Icon::Module, false},
        {"flottantes", "Licences flottantes", ui::Icon::Network, false},  // 1.35.0 : une vraie page (le service)
        {"boutique", "Boutique", ui::Icon::Globe, true},                  // pas encore : un paiement en ligne
        {"commandes", "Commandes", ui::Icon::Mail, true},
        {"revocations", "R\xC3\xA9vocations", ui::Icon::Close, false},
        // 1.44.0 : plusieurs admins (la maquette 1.43.0) ; << Versions >> viendra avec sa page.
        {"admins", "Administrateurs", ui::Icon::User, false},
        {"journal", "Journal des admins", ui::Icon::Task, false},         // l'admin+ seulement
        {"service", "Service en ligne", ui::Icon::Refresh, false},        // 1.35.0 : a la place de Synchronisation GitHub
        {"historique", "Historique", ui::Icon::History, false},
        {"securite", "Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9", ui::Icon::Lock, false},
    };
    return list;
}

const Shell::NavEntry* Shell::entry(const std::string& pageId) {
    for (const auto& e : entries())
        if (pageId == e.id) return &e;
    for (const auto& e : subPages())
        if (pageId == e.id) return &e;
    return nullptr;
}

// 1.44.0 : les sous-pages (pas dans le menu) et leur page parente.
const std::vector<Shell::NavEntry>& Shell::subPages() {
    static const std::vector<NavEntry> list = {
        {"droits", "Droits", ui::Icon::Lock, false},   // Administrateurs > Droits (planche 08)
    };
    return list;
}

std::string Shell::parentOf(const std::string& pageId) { return pageId == "droits" ? std::string("admins") : std::string(); }

bool Shell::plusOnly(const std::string& pageId) { return pageId == "journal" || pageId == "droits"; }

bool Shell::pageAllowed(const std::string& pageId) const { return !plusOnly(pageId) || ws_->role() == Role::Plus; }

Shell::Shell(std::string folder, std::function<Seconds()> clock)
    : menu::WidgetMenu("admin.shell"), ws_(std::make_unique<Workspace>(std::move(folder))) {
    if (clock) {
        ws_->setClock(std::move(clock));
        ws_->reload();
    }
}

Shell::~Shell() {
    // Le travail lent d'abord (il lit le Workspace), puis les pages (elles tiennent des ShellHost vers ici).
    bg_.wait();
    pendingDone_ = nullptr;
    loose_.clear();
    pages_.clear();
    stack_ = view_ = nullptr;
    setRoot(std::unique_ptr<ui::Widget>{});
}

ShellHost Shell::host() {
    ShellHost h;
    h.workspace = [this]() -> Workspace& { return *ws_; };
    h.report = [this](const OpResult& r) { report(r); };
    h.flash = [this](const std::string& t, Tone tone) { flash(t, tone); };
    h.runSlow = [this](const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done) {
        return runSlow(label, std::move(work), std::move(done));
    };
    h.busy = [this] { return busy(); };
    h.navigate = [this](const std::string& id) { navigate(id); };
    h.openDialog = [this](menu::MenuPtr m) { openDialog(std::move(m)); };
    h.pickFile = [this](const ui::FilePick& p, std::function<void(std::string)> done) { pickFile(p, std::move(done)); };
    h.openFolder = [this](const std::string& f) { openFolder(f); };
    return h;
}

core::Status Shell::buildUi() {
    auto view = std::make_unique<ShellView>(*this);
    view_ = view.get();
    stack_ = &view->stack();
    setRoot(std::move(view));
    showCurrent();
    return core::ok();
}

Page* Shell::page(const std::string& pageId) const {
    const auto it = pages_.find(pageId);
    return it == pages_.end() ? nullptr : it->second;
}

Page* Shell::ensurePage(const std::string& id) {
    if (Page* p = page(id)) return p;
    if (!stack_ || !entry(id)) return nullptr;
    PagePtr p;
    const ShellHost h = host();
    if (id == "tableau") p = makeDashboardPage(h);
    else if (id == "clients") p = makeClientsPage(h);
    else if (id == "licences") p = makeLicencesPage(h);
    else if (id == "editions") p = makeEditionsPage(h);
    else if (id == "fonctionnalites") p = makeFeaturesPage(h);
    else if (id == "options") p = makeOptionsPage(h);
    else if (id == "activations") p = makeActivationsPage(h);
    else if (id == "demandes") p = makeRequestsPage(h);
    else if (id == "flottantes") p = makeFloatingPage(h);
    else if (id == "boutique" || id == "commandes") p = makeOnlinePage(h, id);
    else if (id == "revocations") p = makeRevocationsPage(h);
    else if (id == "service") p = makeServicePage(h);
    else if (id == "historique") p = makeHistoryPage(h);
    else if (id == "securite") p = makeSecurityPage(h);
    if (!p) p = std::make_unique<MissingPage>(h, entry(id)->label);
    p->setVisibility(ui::Visibility::Collapsed);
    Page* raw = static_cast<Page*>(&stack_->addChild(std::move(p)));
    pages_[id] = raw;
    return raw;
}

void Shell::showCurrent() {
    if (!stack_) return;
    Page* shown = ensurePage(current_);
    for (auto& [id, p] : pages_) p->setVisibility(id == current_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    if (shown) shown->refresh();
    stack_->invalidateLayout();
    if (view_) static_cast<ShellView*>(view_)->update();
}

void Shell::navigate(const std::string& pageId) {
    if (!entry(pageId)) {
        flash("Page inconnue : " + pageId, Tone::Warning);
        return;
    }
    current_ = pageId;
    showCurrent();
}

std::string Shell::pageTitle() const {
    if (const Page* p = page(current_)) return p->title();
    if (const NavEntry* e = entry(current_)) return e->label;
    return kAppName;
}

void Shell::refreshPages() {
    std::vector<Page*> all;
    for (auto& [id, p] : pages_) all.push_back(p);
    for (Page* p : all) p->refresh();
}

void Shell::pushLog(Tone tone, OpResult r) {
    log_.push_back({ws_->now(), tone, std::move(r)});
    detailsOpen_ = false;
    if (view_) static_cast<ShellView*>(view_)->update();
}

void Shell::report(const OpResult& r) {
    if (r.ok) {
        ws_->reload();
        refreshPages();
    }
    pushLog(r.ok ? Tone::Ok : Tone::Error, r);
}

void Shell::flash(const std::string& text, Tone tone) {
    OpResult r;
    r.ok = tone != Tone::Error;
    r.message = text;
    pushLog(tone, std::move(r));
}

bool Shell::runSlow(const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done) {
    if (!work) return false;
    if (bg_.running()) {
        // 1.40.0 : pendant le regard en direct (court, sans bruit), le travail attend sa fin ; sinon : refuse.
        if (!quietSlow_ || queued_) return false;
        queued_ = Queued{label, std::move(work), std::move(done)};
        if (view_) static_cast<ShellView*>(view_)->updateTop();
        return true;
    }
    if (!bg_.start(label, std::move(work))) return false;   // un travail tourne deja
    pendingDone_ = std::move(done);
    quietSlow_ = false;
    if (view_) static_cast<ShellView*>(view_)->updateTop();
    return true;
}

std::string Shell::busyLabel() const {
    if (queued_) return queued_->label;
    return busy() ? bg_.label() : std::string();
}

void Shell::startQueued() {
    if (!queued_ || bg_.running()) return;
    Queued q = std::move(*queued_);
    queued_.reset();
    if (!bg_.start(q.label, std::move(q.work))) {
        report(OpResult::fail("\xC2\xAB " + q.label + " \xC2\xBB n'a pas pu partir : recommence"));
        return;
    }
    pendingDone_ = std::move(q.done);
    quietSlow_ = false;
    if (view_) static_cast<ShellView*>(view_)->updateTop();
}

bool Shell::poll() {
    auto r = bg_.take();
    if (!r) return false;
    auto done = std::move(pendingDone_);
    pendingDone_ = nullptr;
    const bool quiet = quietSlow_;
    quietSlow_ = false;
    if (done) done(*r);
    if (!quiet) report(*r);   // 1.36.0 : la releve automatique ne parle que si elle a du nouveau (son `done`)
    startQueued();            // 1.40.0 : le travail qui attendait la fin du regard
    // 1.35.0 : une publication sur le service qui attendait la fin de ce travail.
    if (!busy() && ws_->hasServiceJob()) (void)publishToService(host());
    return true;
}

bool Shell::finishSlow() {
    bg_.wait();
    const bool r = poll();
    if (bg_.running()) {   // 1.40.0 : le travail qui attendait le regard est parti : sa fin aussi
        bg_.wait();
        (void)poll();
    }
    return r;
}

void Shell::openDialog(menu::MenuPtr dialog) {
    if (!dialog) return;
    if (attached_) {
        manager().PushMenu(std::move(dialog));
        return;
    }
    (void)dialog->Initialize();
    loose_.push_back(std::move(dialog));
}

void Shell::pickFile(const ui::FilePick& pick, std::function<void(std::string)> done) {
    if (!ui::pickFile(pick, std::move(done)))
        report(OpResult::fail("l'explorateur de fichiers ne s'ouvre pas ici : glisse le fichier sur la fen\xC3\xAAtre"));
}

void Shell::openFolder(const std::string& folder) {
    if (folder.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::path(std::u8string(folder.begin(), folder.end())), ec)) {
        report(OpResult::fail("le dossier n'existe pas (encore) : " + folder));
        return;
    }
    if (const std::string why = core::openWithSystem(folder); !why.empty()) report(OpResult::fail("le dossier ne s'ouvre pas : " + why));
}

void Shell::dropFiles(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    if (Page* p = currentPage(); p && p->dropFiles(paths)) return;
    if (busy()) {
        report(OpResult::fail("attends la fin de \xC2\xAB " + busyLabel() + " \xC2\xBB avant de ranger des fichiers"));
        return;
    }
    report(ws_->importFiles(paths));
}

void Shell::reloadAll() {
    ws_->reload();
    refreshPages();
    flash("Relu : la cl\xC3\xA9, l'archive, le registre des activations, le service.", Tone::Info);
}

void Shell::setDetailsOpen(bool open) {
    detailsOpen_ = open;
    if (view_) static_cast<ShellView*>(view_)->update();
}

void Shell::setLogOpen(bool open) {
    logOpen_ = open;
    if (view_) static_cast<ShellView*>(view_)->update();
}

std::string Shell::keyStatusText() const {
    const auto& k = ws_->key();
    if (!k.present) return "Pas de cl\xC3\xA9";
    if (k.test) return "CL\xC3\x89 D'ESSAI \xC2\xB7 " + k.id;
    return "Cl\xC3\xA9 : " + k.id;
}

Tone Shell::keyStatusTone() const {
    const auto& k = ws_->key();
    if (!k.present) return Tone::Error;
    if (k.test) return Tone::Warning;
    return Tone::Ok;
}

std::string Shell::syncStatusText() const {
    if (busy()) {
        std::string l = busyLabel();
        if (l.empty()) l = "Travail en cours";
        const bool dots = l.size() >= 3 && (l.compare(l.size() - 3, 3, "...") == 0 || l.compare(l.size() - 3, 3, "\xE2\x80\xA6") == 0);
        return dots ? l : l + "\xE2\x80\xA6";
    }
    const auto& s = ws_->service();
    if (!s.problem.empty()) return "Service : r\xC3\xA9glage illisible";
    if (!s.configured) return "Service : non r\xC3\xA9gl\xC3\xA9";
    std::string host = s.config.url;
    if (const auto p = host.find("://"); p != std::string::npos) host.erase(0, p + 3);
    if (s.hasState && !s.state.certOk) return "Service : " + host + " (cl\xC3\xA9 \xC3\xA0 certifier)";
    return "Service : " + host + (live() ? " \xC2\xB7 en direct" : "");
}

int Shell::pendingRequests() const { return ws_->service().ready() ? ws_->service().pending : 0; }

bool Shell::autoFetchRequests() {
    if (bg_.running() || !ws_->service().ready() || !ws_->key().present) return false;
    std::set<int> before;
    for (const auto& e : ws_->service().requests)
        if (e.waiting()) before.insert(e.number);
    const Workspace* w = ws_.get();
    if (!runSlow("Service : relev\xC3\xA9 des demandes", [w] { return w->serviceFetch(); },
                 [this, before](const OpResult& r) {
                     ws_->reload();
                     refreshPages();
                     if (!r.ok) {
                         if (!autoProblemSaid_) flash("Rel\xC3\xA8ve automatique des demandes : " + r.message, Tone::Warning);
                         autoProblemSaid_ = true;
                         return;
                     }
                     autoProblemSaid_ = false;
                     std::vector<const online::Entry*> fresh;
                     for (const auto& e : ws_->service().requests)
                         if (e.waiting() && !before.count(e.number)) fresh.push_back(&e);
                     if (fresh.empty()) return;
                     if (fresh.size() == 1) {
                         const auto& e = *fresh.front();
                         std::string who = e.customer.empty() ? std::string("un client") : e.customer;
                         if (!e.deviceName.empty()) who += " (PC \xC2\xAB " + e.deviceName + " \xC2\xBB)";
                         flash("Nouvelle demande de licence : " + who + " \xE2\x80\x94 Demandes en ligne \xE2\x80\xBA Accepter.", Tone::Warning);
                     } else {
                         flash(std::to_string(fresh.size()) + " nouvelles demandes de licence \xE2\x80\x94 Demandes en ligne \xE2\x80\xBA Accepter.", Tone::Warning);
                     }
                 }))
        return false;
    quietSlow_ = true;
    return true;
}

// ============================================================================ 1.40.0 : le regard en direct
namespace {
double steadyNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

bool Shell::live() const { return lastLive_ >= 0 && steadyNow() - lastLive_ < 3.0 * kWatchFallback; }

double Shell::secondsSinceLive() const { return lastLive_ >= 0 ? steadyNow() - lastLive_ : -1.0; }

bool Shell::autoWatch() {
    if (bg_.running() || !ws_->service().ready() || !ws_->key().present) return false;
    const Workspace* w = ws_.get();
    auto st = std::make_shared<online::AdminWatch>();
    if (!runSlow("Service : en direct", [w, st] { return w->serviceWatch(*st); },
                 [this, st](const OpResult& r) {
                     if (!r.ok) {
                         lastLive_ = -1;
                         if (!autoProblemSaid_) flash("En direct : " + r.message, Tone::Warning);
                         autoProblemSaid_ = true;
                         return;
                     }
                     autoProblemSaid_ = false;
                     lastLive_ = steadyNow();
                     if (!st->supported) {   // un service d'avant la 1.40.0 : relire les licences elles-memes
                         watchSupported_ = false;
                         wantLicences_ = true;
                         return;
                     }
                     watchSupported_ = true;
                     // Le premier regard : relire les licences si rien n'a jamais ete releve sur ce poste.
                     if (knownActivity_ < 0 ? ws_->service().snapshot.at == 0 : st->activity != knownActivity_) wantLicences_ = true;
                     if (knownRequests_ >= 0 && st->requests != knownRequests_) wantRequests_ = true;
                     knownActivity_ = st->activity;
                     knownRequests_ = st->requests;
                 }))
        return false;
    quietSlow_ = true;
    return true;
}

bool Shell::autoRefreshLicences() {
    if (bg_.running() || !ws_->service().ready() || !ws_->key().present) return false;
    const Workspace* w = ws_.get();
    auto snap = std::make_shared<online::Snapshot>();
    auto before = std::make_shared<online::Snapshot>(ws_->service().snapshot);
    if (!runSlow("Service : relev\xC3\xA9 des licences en ligne", [w, snap] { return w->serviceListLicences(*snap); },
                 [this, snap, before](const OpResult& r) {
                     if (!r.ok) {
                         if (!autoProblemSaid_) flash("Rel\xC3\xA8ve automatique des licences en ligne : " + r.message, Tone::Warning);
                         autoProblemSaid_ = true;
                         return;
                     }
                     autoProblemSaid_ = false;
                     lastLive_ = steadyNow();
                     ws_->adoptSnapshot(std::move(*snap));
                     refreshPages();
                     const auto customerOf = [this](const std::string& id) {
                         const LicenceEntry* e = ws_->licence(id);
                         return e ? e->licence.customer : std::string();
                     };
                     const std::vector<std::string> said = describeChanges(*before, ws_->service().snapshot, customerOf);
                     if (said.size() == 1) flash(said.front(), Tone::Info);
                     else if (!said.empty()) {
                         OpResult m;
                         m.ok = true;
                         m.message = std::to_string(said.size()) + " changements sur tes licences en ligne (D\xC3\xA9tails).";
                         m.details = said;
                         pushLog(Tone::Info, std::move(m));
                     }
                 }))
        return false;
    quietSlow_ = true;
    return true;
}

std::vector<std::string> Shell::describeChanges(const online::Snapshot& before, const online::Snapshot& after,
                                                const std::function<std::string(const std::string&)>& customerOf) {
    std::vector<std::string> out;
    if (before.at == 0) return out;   // pas de releve d'avant : rien a comparer
    for (const auto& lic : after.licences) {
        const online::OnlineLicence* old = before.licence(lic.licenseId);
        std::string who = customerOf ? customerOf(lic.licenseId) : std::string();
        const std::string where = " (licence " + lic.licenseId + ")";
        for (const auto& a : lic.activations) {
            const online::OnlineActivation* was = old ? old->activation(a.deviceKey) : nullptr;
            const std::string nom = "PC \xC2\xAB " + (a.name.empty() ? std::string("sans nom") : a.name) + " \xC2\xBB";
            const std::string pc = "le " + nom;
            const std::string sujet = who.empty() ? std::string("Le client") : who;
            if (a.state == "active" && (!was || was->state != "active"))
                out.push_back(sujet + " a activ\xC3\xA9 " + pc + where + ".");
            else if (a.state == "liberee" && was && was->state == "active")
                out.push_back(sujet + " a lib\xC3\xA9r\xC3\xA9 " + pc + where + " : sa place est libre.");
            else if (a.state == "retiree" && was && was->state == "active")
                out.push_back("Le " + nom + where + " n'est plus actif : sa place a \xC3\xA9t\xC3\xA9 reprise par un autre PC.");
        }
    }
    return out;
}

void Shell::Update(const menu::FrameContext& fc) {
    (void)poll();
    // 1.36.0 : la releve automatique des demandes (au lancement, puis toutes les 2 minutes ; pas sous un dialogue).
    // 1.40.0 : le regard en direct (toutes les 15 s) ; ce qu'il a vu bouger est relu des que rien ne tourne ; les
    // demandes ne sont plus relues que toutes les 10 minutes quand le regard les suit.
    {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (nextAutoFetch_ < 0) nextAutoFetch_ = now + kAutoFetchFirst;
        if (nextWatch_ < 0) nextWatch_ = now + kWatchFirst;
        const bool free = attached_ && manager().top() == this && !bg_.running() && !queued_;
        if (free && wantLicences_) {
            wantLicences_ = false;
            (void)autoRefreshLicences();
        } else if (free && wantRequests_) {
            wantRequests_ = false;
            nextAutoFetch_ = now + (watchSupported_ ? kFetchWhenWatched : kAutoFetchPeriod);
            (void)autoFetchRequests();
        } else if (free && now >= nextAutoFetch_) {
            nextAutoFetch_ = now + (watchSupported_ && live() ? kFetchWhenWatched : kAutoFetchPeriod);
            (void)autoFetchRequests();
        } else if (free && now >= nextWatch_) {
            nextWatch_ = now + (watchSupported_ ? kWatchPeriod : kWatchFallback);
            if (watchSupported_) (void)autoWatch();
            else (void)autoRefreshLicences();
        }
    }
    if (!busy() && ws_->hasServiceJob()) (void)publishToService(host());   // 1.35.0 : une publication en attente
    if (view_) static_cast<ShellView*>(view_)->updateTop();
    menu::WidgetMenu::Update(fc);
}

ui::EventResult Shell::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::F5 && k->mods.none()) {
        reloadAll();
        return ui::EventResult::Consumed;
    }
    if (const auto* d = std::get_if<ui::FileDropped>(&ev)) {
        dropFiles({d->path});
        return ui::EventResult::Consumed;
    }
    const ui::EventResult r = menu::WidgetMenu::HandleEvent(ev);
    // Echap (que rien d'autre n'a pris) referme le journal, puis les details.
    if (const auto* k = std::get_if<ui::KeyDown>(&ev); r == ui::EventResult::Ignored && k && k->key == ui::Key::Escape) {
        if (logOpen_) {
            setLogOpen(false);
            return ui::EventResult::Consumed;
        }
        if (detailsOpen_) {
            setDetailsOpen(false);
            return ui::EventResult::Consumed;
        }
    }
    return r;
}

} // namespace license_admin::gui
