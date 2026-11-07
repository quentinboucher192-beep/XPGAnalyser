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
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
extern char** environ;
#endif

namespace license_admin::gui {

namespace {

using gfx::Color;
using gfx::FontId;
using gfx::Rect;

constexpr float  kRailW = 232.f;     // le menu de gauche
constexpr float  kTopH = 48.f;       // la barre du haut de la page
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

// Gras simule (une seule face : le texte trace deux fois, a un demi-pixel).
void boldText(gfx::IRenderer& r, gfx::Point p, const std::string& s, FontId f, Color c) {
    r.drawText(p, s, f, c);
    r.drawText({p.x + 0.6f, p.y}, s, f, c);
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

// 1.34.0 : une page de GitHub dans le navigateur du systeme (l'adresse est verifiee avant : safeGithubUrl). Vide :
// ouverte ; sinon la phrase.
std::string openUrlWithSystem(const std::string& url) {
#if defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 1), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, w.data(), n);
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (r <= 32) return "Windows n'a pas pu ouvrir le navigateur (code " + std::to_string(static_cast<long long>(r)) + ").";
    return {};
#else
#  if defined(__APPLE__)
    const char* tool = "open";
#  else
    const char* tool = "xdg-open";
#  endif
    std::string a0 = tool, a1 = url;
    char* argv[] = {a0.data(), a1.data(), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, tool, nullptr, nullptr, argv, environ) != 0) return std::string("aucun navigateur n'a pu s'ouvrir (") + tool + " manque ?)";
    std::thread([pid] {
        int status = 0;
        (void)waitpid(pid, &status, 0);
    }).detach();
    return {};
#endif
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
class NavItem final : public ui::Button {
public:
    NavItem(const Shell::NavEntry& e, const Shell& shell) : ui::Button(e.label, std::string("nav.") + e.id), e_(e), shell_(shell) {
        if (e.online) setTooltip("Demande le service de licences en ligne, pas encore fait : la page explique ce qu'elle fera.");
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const Rect  r = bounds();
        const bool  sel = shell_.currentPageId() == e_.id;
        if (sel) {
            ctx.r.fillRoundedRect(r, c.panelBg, 6.f);
            ctx.r.fillRoundedRect({r.x, r.y + 8.f, 3.f, r.h - 16.f}, c.accent, 1.5f);
        } else if (hovered()) {
            ctx.r.fillRoundedRect(r, ctx.theme.brand.hover, 6.f);
        }
        const Color ink = sel ? c.accent : e_.online ? c.textDisabled : c.text;
        const Color iconInk = sel ? c.accent : e_.online ? c.textDisabled : c.textMuted;
        ui::drawIcon(ctx.r, e_.icon, {r.x + 12.f, r.y + (r.h - 18.f) * 0.5f, 18.f, 18.f}, iconInk);
        const std::string t = fit(ctx.r, e_.label, kF15, r.w - 46.f);
        const float h = ctx.r.lineHeight(kF15);
        const gfx::Point at{r.x + 38.f, r.y + (r.h - h) * 0.5f};
        if (sel) boldText(ctx.r, at, t, kF15, ink);
        else ctx.r.drawText(at, t, kF15, ink);
    }

private:
    const Shell::NavEntry& e_;
    const Shell&           shell_;
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
        auto folder = std::make_unique<ui::Button>("Dossier du poste", "shell.dossier");
        folder->setStyle(ui::Button::Style::Flat);
        folder->setIcon(ui::Icon::Folder);
        folder_ = &static_cast<ui::Button&>(addChild(std::move(folder)));
        links += folder_->clicked->connect([this] { shell_.openFolder(shell_.workspace().folder()); });
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        float y = b.y + 96.f;
        bool prevOnline = false;
        captionY_ = sepY_ = -1.f;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const bool online = Shell::entries()[i].online;
            if (online && !prevOnline) {
                captionY_ = y + 2.f;
                y += 24.f;
            }
            if (!online && prevOnline) {
                sepY_ = y + 3.f;
                y += 8.f;
            }
            prevOnline = online;
            items_[i]->setBounds({b.x + 8.f, y, b.w - 16.f, 34.f});
            y += 36.f;
        }
        folder_->setTooltip(shell_.workspace().folder());
        const float fy = b.bottom() - 42.f;
        folder_->setVisibility(fy > y + 4.f ? ui::Visibility::Visible : ui::Visibility::Hidden);
        folder_->setBounds({b.x + 8.f, fy, std::min(b.w - 70.f, 150.f), 30.f});
        footerY_ = fy;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const Rect  b = bounds();
        ctx.r.fillRect(b, c.headerBg);
        ctx.r.fillRect({b.right() - 1.f, b.y, 1.f, b.h}, c.border);
        // La marque : un carre de l'accent et le cadenas ; le nom sur deux lignes.
        ctx.r.fillRoundedRect({b.x + 16.f, b.y + 18.f, 32.f, 32.f}, c.accent, 8.f);
        ui::drawIcon(ctx.r, ui::Icon::Lock, {b.x + 23.f, b.y + 25.f, 18.f, 18.f}, c.textInverted);
        boldText(ctx.r, {b.x + 58.f, b.y + 16.f}, "XPG License Manager", kF15, c.text);
        boldText(ctx.r, {b.x + 58.f, b.y + 34.f}, "Admin", kF15, c.accent);
        // << Editeur seulement >> : une pastille, l'outil n'est jamais livre a un client.
        const std::string only = "\xC3\x89" "diteur seulement";
        const float ow = ctx.r.measure(only, kF12).width + 16.f;
        const Rect  pill{b.x + 16.f, b.y + 60.f, ow, 20.f};
        ctx.r.fillRoundedRect(pill, c.warning.withAlpha(28), 10.f);
        ctx.r.drawText({pill.x + 8.f, pill.y + (pill.h - ctx.r.lineHeight(kF12)) * 0.5f}, only, kF12, c.warning);
        ctx.r.fillRect({b.x + 16.f, b.y + 88.f, b.w - 32.f, 1.f}, c.border);
        if (captionY_ >= 0.f)
            ctx.r.drawText({b.x + 20.f, captionY_ + 4.f}, "SERVICE EN LIGNE \xC2\xB7 PAS ENCORE", kF12, c.textMuted);
        if (sepY_ >= 0.f) ctx.r.fillRect({b.x + 16.f, sepY_, b.w - 32.f, 1.f}, c.border);
        if (folder_->visible()) {
            const std::string v = std::string("v") + kAppVersion;
            const float vw = ctx.r.measure(v, kF12).width;
            ctx.r.drawText({b.right() - 16.f - vw, footerY_ + (30.f - ctx.r.lineHeight(kF12)) * 0.5f}, v, kF12, c.textMuted);
        }
    }

private:
    Shell&                 shell_;
    std::vector<NavItem*>  items_;
    ui::Button*            folder_{nullptr};
    float                  captionY_{-1.f}, sepY_{-1.f}, footerY_{0.f};
};

// ======================================================= la barre du haut ====
// Une pastille cliquable : l'etat de la cle, de la synchro (ou le travail en cours).
class Chip final : public ui::Button {
public:
    explicit Chip(std::string id) : ui::Button("", std::move(id)) {}
    void set(std::string text, Tone tone, ui::Icon icon, bool spin, bool neutral) {
        if (text == text_ && tone == tone_ && icon == icon_ && spin == spin_ && neutral == neutral_) return;
        text_ = std::move(text);
        tone_ = tone;
        icon_ = icon;
        spin_ = spin;
        neutral_ = neutral;
        setText(text_);
        invalidateLayout();
    }
    [[nodiscard]] float wanted() const { return ui::measureWidth(text_, kF13) + 18.f + 24.f; }
    [[nodiscard]] ui::SizeHint sizeHint() const override {
        ui::SizeHint h;
        h.preferred = {wanted(), 28.f};
        h.minimum = h.preferred;
        return h;
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const Rect  r = bounds();
        const Color ink = neutral_ ? c.textMuted : toneColor(ctx.theme, tone_);
        const Color text = neutral_ || tone_ == Tone::Info ? c.text : ink;
        ctx.r.fillRoundedRect(r, neutral_ ? c.headerBg : ink.withAlpha(hovered() ? 40 : 24), r.h * 0.5f);
        if (hovered()) ctx.r.strokeRect(r, ink.withAlpha(120), 1.f);
        const Rect ic{r.x + 10.f, r.y + (r.h - 14.f) * 0.5f, 14.f, 14.f};
        if (spin_) spinner(ctx.r, {ic.x + 7.f, ic.y + 7.f}, 5.5f, ink, ctx.time);
        else ui::drawIcon(ctx.r, icon_, ic, ink);
        const std::string t = fit(ctx.r, text_, kF13, r.w - 42.f);
        ctx.r.drawText({r.x + 30.f, r.y + (r.h - ctx.r.lineHeight(kF13)) * 0.5f}, t, kF13, text);
    }

private:
    std::string text_;
    Tone        tone_{Tone::Info};
    ui::Icon    icon_{ui::Icon::None};
    bool        spin_{false}, neutral_{false};
};

class TopBar final : public ui::Widget {
public:
    TopBar(Shell& shell, core::ConnectionScope& links) : ui::Widget("shell.haut"), shell_(shell) {
        key_ = &static_cast<Chip&>(addChild(std::make_unique<Chip>("shell.cle")));
        sync_ = &static_cast<Chip&>(addChild(std::make_unique<Chip>("shell.synchro")));
        links += key_->clicked->connect([this] { shell_.navigate("securite"); });
        links += sync_->clicked->connect([this] { shell_.navigate("github"); });
        update();
    }
    // L'etat a montrer (a chaque image : un travail lent commence ou finit).
    void update() {
        const auto& k = shell_.workspace().key();
        key_->set(shell_.keyStatusText(), shell_.keyStatusTone(), k.present ? ui::Icon::Lock : ui::Icon::Unlock, false, false);
        key_->setTooltip(k.present ? "La cl\xC3\xA9 de l'\xC3\xA9" "diteur : " + k.id + (k.name.empty() ? std::string() : " (" + k.name + ")") + ". Clic : Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9."
                                   : (k.problem.empty() ? std::string("Aucune cl\xC3\xA9 sur ce poste.") : k.problem) + " Clic : Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9.");
        if (shell_.busy()) {
            sync_->set(shell_.syncStatusText(), Tone::Info, ui::Icon::Refresh, true, false);
            sync_->setTooltip("Un travail lent tourne sur un autre fil : la fen\xC3\xAAtre reste utilisable. Les op\xC3\xA9rations qui \xC3\xA9" "crivent attendent sa fin.");
        } else {
            const auto& g = shell_.workspace().github();
            const Tone t = !g.problem.empty() ? Tone::Error : g.configured && !g.token ? Tone::Warning : Tone::Info;
            sync_->set(shell_.syncStatusText(), t, ui::Icon::Refresh, false, !g.configured && g.problem.empty());
            sync_->setTooltip(!g.problem.empty() ? g.problem
                              : g.configured   ? "Le d\xC3\xA9p\xC3\xB4t GitHub de l'archive (branche " + g.config.branch + ")" + (g.token ? std::string() : std::string(" ; pas de jeton enregistr\xC3\xA9")) + ". Clic : la page Synchronisation GitHub."
                                               : std::string("La synchro GitHub n'est pas r\xC3\xA9gl\xC3\xA9" "e. Clic : la page Synchronisation GitHub."));
        }
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        float x = b.right() - 16.f;
        for (Chip* chip : {key_, sync_}) {
            const float w = std::min(chip->wanted(), std::max(120.f, b.w * 0.34f));
            x -= w;
            chip->setBounds({x, b.y + (b.h - 28.f) * 0.5f, w, 28.f});
            x -= 8.f;
        }
        titleRight_ = x - 8.f;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const Rect  b = bounds();
        ctx.r.fillRect(b, c.panelBg);
        ctx.r.fillRect({b.x, b.bottom() - 1.f, b.w, 1.f}, c.border);
        const Shell::NavEntry* e = Shell::entry(shell_.currentPageId());
        float x = b.x + 20.f;
        if (e) {
            ui::drawIcon(ctx.r, e->icon, {x, b.y + (b.h - 18.f) * 0.5f, 18.f, 18.f}, c.textMuted);
            x += 28.f;
        }
        // Le fil : << Admin > la page >> (la page porte son grand titre ; ici, ou l'on est).
        const std::string crumb = "Admin  \xE2\x80\xBA  ";
        const float ty = b.y + (b.h - ctx.r.lineHeight(kF15)) * 0.5f;
        ctx.r.drawText({x, ty}, crumb, kF15, c.textMuted);
        x += ctx.r.measure(crumb, kF15).width;
        const std::string t = fit(ctx.r, shell_.pageTitle(), kF15, titleRight_ - x);
        boldText(ctx.r, {x, ty}, t, kF15, c.text);
    }

private:
    Shell& shell_;
    Chip*  key_{nullptr};
    Chip*  sync_{nullptr};
    float  titleRight_{0.f};
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
    }
    [[nodiscard]] ui::Widget& stack() { return *stack_; }
    void update() {
        top_->update();
        banner_->update();
        invalidateLayout();
    }
    void updateTop() { top_->update(); }

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
    }
    void onPaint(const ui::PaintContext& ctx) override { ctx.r.fillRect(bounds(), ctx.theme.color.windowBg); }

private:
    Shell&                shell_;
    core::ConnectionScope links_;
    Rail*                 rail_{nullptr};
    TopBar*               top_{nullptr};
    ui::Widget*           stack_{nullptr};
    Banner*               banner_{nullptr};
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
        {"tableau", "Tableau de bord", ui::Icon::Chart, false},
        {"clients", "Clients", ui::Icon::User, false},
        {"licences", "Licences", ui::Icon::Document, false},
        {"editions", "\xC3\x89" "ditions", ui::Icon::Layers, false},
        {"fonctionnalites", "Fonctionnalit\xC3\xA9s", ui::Icon::Module, false},
        {"activations", "Activations", ui::Icon::Station, false},
        {"demandes", "Demandes en ligne", ui::Icon::Import, false},   // 1.34.0 : la passerelle GitHub
        {"flottantes", "Licences flottantes", ui::Icon::Network, true},
        {"boutique", "Boutique", ui::Icon::Globe, true},
        {"commandes", "Commandes", ui::Icon::Mail, true},
        {"revocations", "R\xC3\xA9vocations", ui::Icon::Close, false},
        {"github", "Synchronisation GitHub", ui::Icon::Refresh, false},
        {"historique", "Historique", ui::Icon::History, false},
        {"securite", "Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9", ui::Icon::Lock, false},
    };
    return list;
}

const Shell::NavEntry* Shell::entry(const std::string& pageId) {
    for (const auto& e : entries())
        if (pageId == e.id) return &e;
    return nullptr;
}

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
    h.openUrl = [this](const std::string& u) {
        if (!safeGithubUrl(u)) {
            report(OpResult::fail("cette adresse ne s'ouvre pas d'ici : " + u));
            return;
        }
        if (const std::string why = openUrlWithSystem(u); !why.empty()) report(OpResult::fail(why));
        else flash("Ouvert dans ton navigateur : " + u, Tone::Info);
    };
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
    else if (id == "activations") p = makeActivationsPage(h);
    else if (id == "demandes") p = makeRequestsPage(h);
    else if (id == "flottantes" || id == "boutique" || id == "commandes") p = makeOnlinePage(h, id);
    else if (id == "revocations") p = makeRevocationsPage(h);
    else if (id == "github") p = makeGithubPage(h);
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
    if (!bg_.start(label, std::move(work))) return false;   // un travail tourne deja
    pendingDone_ = std::move(done);
    if (view_) static_cast<ShellView*>(view_)->updateTop();
    return true;
}

std::string Shell::busyLabel() const { return bg_.running() ? bg_.label() : std::string(); }

bool Shell::poll() {
    auto r = bg_.take();
    if (!r) return false;
    auto done = std::move(pendingDone_);
    pendingDone_ = nullptr;
    if (done) done(*r);
    report(*r);
    // 1.34.0 : une publication sur la passerelle qui attendait la fin de ce travail.
    if (!busy() && ws_->hasGatewayJob()) (void)publishToGateway(host());
    return true;
}

bool Shell::finishSlow() {
    bg_.wait();
    return poll();
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
    flash("Relu : la cl\xC3\xA9, l'archive, le registre des activations, la synchro.", Tone::Info);
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
        std::string l = bg_.label();
        if (l.empty()) l = "Travail en cours";
        const bool dots = l.size() >= 3 && (l.compare(l.size() - 3, 3, "...") == 0 || l.compare(l.size() - 3, 3, "\xE2\x80\xA6") == 0);
        return dots ? l : l + "\xE2\x80\xA6";
    }
    const auto& g = ws_->github();
    if (!g.problem.empty()) return "GitHub : r\xC3\xA9glage illisible";
    if (!g.configured) return "GitHub : non r\xC3\xA9gl\xC3\xA9";
    return "GitHub : " + g.config.repo + (g.token ? std::string() : std::string(" (sans jeton)"));
}

void Shell::Update(const menu::FrameContext& fc) {
    (void)poll();
    if (!busy() && ws_->hasGatewayJob()) (void)publishToGateway(host());   // 1.34.0 : une publication en attente
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
