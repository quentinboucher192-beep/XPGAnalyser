// =============================================================================
//  license_admin/gui/PageJournal.cpp - 1.44.0 : << JOURNAL DES ADMINS >>
//  (maquette 1.43.0, planches 03 et 04 ; docs/ADMINS.md, par. 6) - l'admin+.
// -----------------------------------------------------------------------------
//  Chaque action de chaque admin (et de l'admin+, et ce que le service ecrit
//  lui-meme : une signature refusee, un autre PC, une demande d'admin), signee
//  par sa cle et chainee a la precedente ; ce poste la releve et la verifie
//  (Workspace::fetchJournal, journalLines). La page :
//    - un historique PAR ADMIN (ses pastilles : son nom, son nombre ; un admin
//      retire grise) et le GLOBAL (<< Tous >>) ; la CRITICITE (Info, Moyenne,
//      Haute, Critique : des pastilles qui se cochent) ; la periode (7, 30,
//      90 jours, tout) ; une recherche ;
//    - le tableau, TRIE PAR N'IMPORTE QUELLE COLONNE (un clic sur l'en-tete ; un
//      second inverse ; une fleche dit le tri) ; une ligne SE DEPLIE : l'avant et
//      l'apres, la signature de l'admin, le chainage, << Voir la licence >>,
//      << Tout le journal de Lea >> ; le CLIC DROIT : la criticite de cette ligne
//      (une note a cote du journal : la ligne signee ne change jamais), Voir le
//      detail, << Seulement ce type >>, Copier la ligne ;
//    - le pied : << Chaine verifiee : N lignes, aucune manquante, aucune
//      modifiee >> (ou les ruptures, en rouge), la pagination, Exporter (CSV),
//      << Criticite des actions... >> (le dialogue de la planche 04 : une
//      criticite par type d'action, << Me prevenir tout de suite >> pour Haute et
//      Critique, << Aussi par courriel >>, << Valeurs de depart >>).
//  Les alertes en direct (le toast) sont dans la coquille : elle les montre
//  meme quand cette page n'est pas ouverte.
// =============================================================================
#include "AdminsKit.hpp"
#include "ListKit.hpp"   // 1.47.0 : la barre de filtres, le clic droit, le depliage
#include "Page.hpp"

#include "../../core/Json.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace license_admin::gui {

namespace {

// 1.48.1 : pourquoi une ligne du journal ne se supprime pas (le bouton grise, le clic droit).
constexpr const char* kNoDelete = "le journal des admins ne s'efface pas : chaque ligne est sign\xC3\xA9" "e et cha\xC3\xAEn\xC3\xA9" "e \xC3\xA0 la pr\xC3\xA9" "c\xC3\xA9" "dente (une ligne \xC3\xB4t\xC3\xA9" "e casserait la cha\xC3\xAEne) ; c'est la trace de qui a fait quoi";

using namespace kit;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;
namespace json = core::json;

constexpr FontId kF28{28}, kF16{16}, kF14{14}, kF13{13}, kF12{12}, kF11{11};
constexpr Seconds kDay = 86400;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Une valeur de l'avant / l'apres, lisible (un droit : son libelle).
std::string valueText(const json::Value& v) {
    if (v.isString()) return adm::isRight(v.asString()) ? adm::rightLabel(v.asString()) : v.asString();
    if (v.isBool()) return v.asBool() ? "oui" : "non";
    if (v.isNumber()) {
        const double d = v.asNumber();
        if (std::floor(d) == d && std::fabs(d) < 1e15) return std::to_string(static_cast<long long>(d));
        char b[32];
        std::snprintf(b, sizeof b, "%g", d);
        return b;
    }
    if (v.isArray()) {
        if (v.items().empty()) return "aucun";
        std::string s;
        for (const auto& x : v.items()) s += (s.empty() ? "" : ", ") + valueText(x);
        return s;
    }
    if (v.isNull()) return "\xE2\x80\x94";
    return json::write(v, false);
}
// 1.44.0 : les cles du detail qui disent une date (en secondes) ; leur nom en francais.
bool isTimeKey(std::string_view k) {
    static const char* const kTimes[] = {"retire", "emis", "emise", "expire", "depuis", "recu", "quand", "jusqu_a", "activee", "vue", "le", "publiee", "cree", "creee"};
    for (const char* x : kTimes)
        if (k == x) return true;
    return false;
}
std::string keyText(std::string k) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"cle", "Cl\xC3\xA9"}, {"retire", "Retir\xC3\xA9 le"}, {"emis", "\xC3\x89mis le"}, {"emise", "\xC3\x89mise le"},
        {"expire", "Expire le"}, {"depuis", "Depuis le"}, {"recu", "Re\xC3\xA7u le"}, {"jusqu_a", "Jusqu'au"},
        {"poste", "PC"}, {"numero", "N\xC2\xB0"}, {"activee", "Activ\xC3\xA9" "e le"}, {"publiee", "Publi\xC3\xA9" "e le"},
        {"cree", "Cr\xC3\xA9\xC3\xA9 le"}, {"creee", "Cr\xC3\xA9\xC3\xA9" "e le"}, {"precedent", "Pr\xC3\xA9" "c\xC3\xA9" "dent"}};
    for (const auto& [from, to] : kNames)
        if (k == from) return to;
    for (auto& c : k)
        if (c == '_') c = ' ';
    if (!k.empty() && k[0] >= 'a' && k[0] <= 'z') k[0] = static_cast<char>(k[0] - 'a' + 'A');
    return k;
}
json::Value objectOf(const std::string& text) {
    json::Value v;
    if (text.empty() || !json::parse(text, v, nullptr) || !v.isObject()) return json::Value::object();
    return v;
}

// Une ligne du journal, prete a montrer.
struct Line {
    std::size_t row{0};   // dans view_.rows
    std::uint64_t number{0};
    Seconds     when{0};
    std::string key, who, pc, summary, object, objectText, action, actionLabel;
    adm::Level  level{adm::Level::Info};
    bool        otherPc{false};
};

enum Col : int { kQuand = 0, kAdmin = 1, kPc = 2, kAction = 3, kObjet = 4, kCrit = 5 };

// Un en-tete de colonne qui trie (un bouton : les scripts le cliquent) ; la fleche dit le tri.
class SortHeader final : public ui::Button {
public:
    SortHeader(std::string text, std::string id) : ui::Button(std::move(text), std::move(id)) {}
    void setSort(bool active, bool descending) {
        active_ = active;
        desc_ = descending;
        invalidate();
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& c = ctx.theme.color;
        const Rect r = bounds();
        if (hovered()) ctx.r.fillRoundedRect(r, c.rowAltBg, 6.f);
        const FontId f = gfx::semibold(kF11);
        const Color ink = active_ ? c.accent : c.textMuted;
        ctx.r.drawText({r.x + 4.f, r.y + (r.h - ctx.r.lineHeight(f)) * 0.5f}, text(), f, ink);
        const float ax = r.x + 10.f + ctx.r.measure(text(), f).width, ay = r.y + r.h * 0.5f;
        if (active_) {
            const float d = desc_ ? 1.f : -1.f;
            ctx.r.line({ax + 3.f, ay - 4.f * d}, {ax + 3.f, ay + 4.f * d}, ink, 1.5f);
            ctx.r.line({ax, ay + 1.f * d}, {ax + 3.f, ay + 4.f * d}, ink, 1.5f);
            ctx.r.line({ax + 6.f, ay + 1.f * d}, {ax + 3.f, ay + 4.f * d}, ink, 1.5f);
        } else {
            ctx.r.line({ax + 1.f, ay - 4.f}, {ax + 1.f, ay + 3.f}, c.textDisabled, 1.f);
            ctx.r.line({ax + 5.f, ay - 3.f}, {ax + 5.f, ay + 4.f}, c.textDisabled, 1.f);
        }
    }

private:
    bool active_{false}, desc_{true};
};

class Rows1 final : public ui::ITableModel {
public:
    explicit Rows1(std::vector<std::vector<std::string>> rows) : rows_(std::move(rows)) {}
    [[nodiscard]] std::size_t rowCount() const override { return rows_.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override { return c == 0 ? "Ligne" : c == 1 ? "Admin" : "Action"; }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows_.size() && c < rows_[r].size() ? rows_[r][c] : std::string();
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override { return cellText(a, c) < cellText(b, c); }

private:
    std::vector<std::vector<std::string>> rows_;
};

// ============================================================ la criticite ====
class CriticalityBody final : public ui::Widget {
public:
    CriticalityBody(Dialog& owner, ShellHost host, std::function<void()> done)
        : ui::Widget("dialog.criticite.corps"), owner_(owner), host_(std::move(host)), done_(std::move(done)) {
        saved_ = host_.workspace().criticality();
        draft_ = saved_;
        for (const auto& t : adm::actionTypes()) {
            const std::string id = t.id;
            auto dd = std::make_unique<ui::DropDown>("criticite." + id);
            std::vector<ui::DropDown::Item> items;
            for (adm::Level l : kLevels) {
                ui::DropDown::Item it;
                it.label = adm::levelLabel(l);
                it.value = adm::levelKey(l);
                it.note = levelCaption(l);
                items.push_back(std::move(it));
            }
            dd->setItems(std::move(items));
            ui::DropDown& d = adopt(*this, std::move(dd));
            Switch& s = adopt(*this, std::make_unique<Switch>("Me pr\xC3\xA9venir tout de suite", "criticite." + id + ".prevenir"));
            links_ += d.selectionChanged->connect([this, id](int i) {
                if (syncing_ || i < 0 || i > 3) return;
                auto& a = draft_.actions[id];
                a.level = kLevels[i];
                if (a.level == adm::Level::Info || a.level == adm::Level::Medium) a.notify = false;
                sync();
            });
            links_ += s.toggled->connect([this, id](bool on) {
                if (syncing_) return;
                draft_.actions[id].notify = on;
                sync();
            });
            rows_.push_back({id, &d, &s});
        }
        email_ = &adopt(*this, std::make_unique<Switch>("Aussi par courriel", "criticite.courriel"));
        links_ += email_->toggled->connect([this](bool on) {
            if (syncing_) return;
            draft_.emailCritical = on;
            sync();
        });
        defaults_ = &button(*this, "Valeurs de d\xC3\xA9part", "criticite.depart", ui::Button::Style::Flat, ui::Icon::Undo);
        close_ = &button(*this, "Fermer", "criticite.fermer");
        save_ = &button(*this, "Enregistrer", "criticite.enregistrer", ui::Button::Style::Primary, ui::Icon::Ok);
        links_ += defaults_->clicked->connect([this] {
            for (const auto& t : adm::actionTypes()) draft_.actions[t.id] = ActionSetting{t.level, t.notify};
            draft_.emailCritical = false;
            sync();
        });
        links_ += close_->clicked->connect([this] { owner_.close(); });
        links_ += save_->clicked->connect([this] { save(); });
        sync();
    }

protected:
    void onLayout() override {
        frame_.place(*this, 1080.f, 800.f);
        const auto& p = frame_.panel;
        const float top = p.y + 76.f + 40.f, foot = p.bottom() - 124.f;
        const float colGap = 28.f, colW = (p.w - 48.f - colGap) * 0.5f;
        // A gauche : licences, offres, clients ; a droite : admins et securite.
        int units[2] = {0, 0};
        std::string group;
        for (const auto& t : adm::actionTypes()) {
            const int side = std::string(t.group) == "admins" ? 1 : 0;
            if (t.group != group) {
                group = t.group;
                ++units[side];
            }
            ++units[side];
        }
        rowH_ = std::clamp((foot - top) / static_cast<float>(std::max(units[0], units[1])), 22.f, 34.f);
        float y[2] = {top, top};
        group.clear();
        heads_.clear();
        const auto& types = adm::actionTypes();
        for (std::size_t i = 0; i < types.size(); ++i) {
            const int side = std::string(types[i].group) == "admins" ? 1 : 0;
            const float x = p.x + 24.f + static_cast<float>(side) * (colW + colGap);
            if (types[i].group != group) {
                group = types[i].group;
                heads_.push_back({x, y[side], adm::groupLabel(group)});
                y[side] += rowH_;
            }
            const float ddW = std::min(150.f, colW * 0.32f);
            rows_[i].dd->setBounds({x + colW - 56.f - ddW, y[side] + (rowH_ - 26.f) * 0.5f, ddW, 26.f});
            rows_[i].sw->setBounds({x + colW - 44.f, y[side] + (rowH_ - 22.f) * 0.5f, 40.f, 22.f});
            rows_[i].y = y[side];
            rows_[i].x = x;
            rows_[i].w = colW;
            y[side] += rowH_;
        }
        colW_ = colW;
        email_->setBounds({p.x + 24.f, p.bottom() - 112.f, 40.f, 22.f});
        const float by = p.bottom() - 56.f;
        defaults_->setBounds({p.x + 20.f, by, buttonWidth(*defaults_, 150.f), 36.f});
        const float sw = buttonWidth(*save_, 140.f), cw = buttonWidth(*close_, 90.f);
        save_->setBounds({p.right() - 24.f - sw, by, sw, 36.f});
        close_->setBounds({save_->bounds().x - 8.f - cw, by, cw, 36.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto& p = frame_.panel;
        const float hy = frame_.paint(r, ctx.theme, "Criticit\xC3\xA9 des actions",
                                      "Par type d'action. Chaque nouvelle ligne du journal prend la criticit\xC3\xA9 de son type.", ui::Icon::Settings);
        const float colGap = 28.f;
        for (int side = 0; side < 2; ++side) {
            const float x = p.x + 24.f + static_cast<float>(side) * (colW_ + colGap);
            const FontId fh = gfx::semibold(kF11);
            r.drawText({x, hy + 14.f}, "ACTION", fh, c.textMuted);
            const float ddW = std::min(150.f, colW_ * 0.32f);
            r.drawText({x + colW_ - 56.f - ddW, hy + 14.f}, "CRITICIT\xC3\x89", fh, c.textMuted);
            r.drawText({x + colW_ - 58.f, hy + 14.f}, "ME PR\xC3\x89VENIR", fh, c.textMuted);
        }
        for (const auto& h : heads_) r.drawText({h.x, h.y + (rowH_ - r.lineHeight(gfx::semibold(kF13))) * 0.5f}, h.text, gfx::semibold(kF13), c.text);
        const auto& types = adm::actionTypes();
        for (std::size_t i = 0; i < types.size() && i < rows_.size(); ++i) {
            const auto& row = rows_[i];
            const float ddW = std::min(150.f, row.w * 0.32f);
            const auto& a = draft_.actions[types[i].id];
            const bool changed = a.level != saved_.actions[types[i].id].level || a.notify != saved_.actions[types[i].id].notify;
            if (changed) r.fillRoundedRect({row.x - 6.f, row.y + 1.f, row.w + 6.f, rowH_ - 2.f}, c.accent.withAlpha(12), 6.f);
            std::string label = types[i].label;
            if (types[i].service) label += " (le service)";
            r.drawText({row.x, row.y + (rowH_ - r.lineHeight(kF13)) * 0.5f}, fit(r, label, kF13, row.w - 70.f - ddW), kF13, c.text);
            const Tint t = levelTint(a.level);
            r.fillRoundedRect({row.dd->bounds().x - 12.f, row.y + rowH_ * 0.5f - 3.f, 6.f, 6.f}, t.fg, 3.f);
            if (!row.sw->visible()) r.drawText({row.x + row.w - 30.f, row.y + (rowH_ - r.lineHeight(kF13)) * 0.5f}, "\xE2\x80\x94", kF13, c.textDisabled);
        }
        const Rect eb = email_->bounds();
        r.fillRect({p.x + 1.f, eb.y - 14.f, p.w - 2.f, 1.f}, c.border);
        r.drawText({eb.right() + 12.f, eb.y - 3.f}, "Aussi par courriel", gfx::semibold(kF13), c.text);
        r.drawText({eb.right() + 12.f, eb.y + 15.f}, "l'adresse de l'\xC3\xA9" "diteur (Service en ligne \xE2\x80\xBA Courriel) \xC2\xB7 seulement Critique", kF12, c.textMuted);
        // La legende des niveaux, a droite du courriel.
        float lx = std::max(p.x + p.w * 0.5f - 120.f, eb.right() + 330.f);
        for (adm::Level l : kLevels) {
            const Tint t = levelTint(l);
            const float w = pill(r, lx, eb.y - 2.f, adm::levelLabel(l), gfx::semibold(kF11), t.fg, t.bg, 20.f);
            r.drawText({lx, eb.y + 21.f}, fit(r, levelCaption(l), kF11, 150.f), kF11, c.textMuted);
            lx += std::max(w, 150.f) + 8.f;
        }
        r.fillRect({p.x + 1.f, p.bottom() - 72.f, p.w - 2.f, 1.f}, c.border);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (frame_.hitClose(ev)) {
            owner_.close();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Row {
        std::string   id;
        ui::DropDown* dd{nullptr};
        Switch*       sw{nullptr};
        float         x{0.f}, y{0.f}, w{0.f};
    };
    struct Head {
        float       x, y;
        std::string text;
    };

    void sync() {
        syncing_ = true;
        const auto& types = adm::actionTypes();
        for (std::size_t i = 0; i < types.size() && i < rows_.size(); ++i) {
            const auto& a = draft_.actions[types[i].id];
            rows_[i].dd->setSelectedIndex(static_cast<int>(a.level));
            const bool high = a.level == adm::Level::High || a.level == adm::Level::Critical;
            rows_[i].sw->setChecked(a.notify && high);
            show(rows_[i].sw, high);
        }
        email_->setChecked(draft_.emailCritical);
        syncing_ = false;
        save_->setEnabled(changes() > 0);
        invalidate();
    }
    [[nodiscard]] int changes() {
        int n = draft_.emailCritical != saved_.emailCritical ? 1 : 0;
        for (const auto& t : adm::actionTypes()) {
            const auto& a = draft_.actions[t.id];
            const auto& b = saved_.actions[t.id];
            if (a.level != b.level || a.notify != b.notify) ++n;
        }
        return n;
    }
    void save() {
        Workspace& w = host_.workspace();
        OpResult all = OpResult::done("");
        int n = 0;
        for (const auto& t : adm::actionTypes()) {
            const auto& a = draft_.actions[t.id];
            const auto& b = saved_.actions[t.id];
            if (a.level == b.level && a.notify == b.notify) continue;
            const OpResult r = w.setCriticality(t.id, a.level, a.notify);
            if (!r.ok) {
                host_.report(r);
                return;
            }
            all.details.push_back(r.message);
            ++n;
        }
        if (draft_.emailCritical != saved_.emailCritical) {
            const OpResult r = w.setEmailCritical(draft_.emailCritical);
            if (!r.ok) {
                host_.report(r);
                return;
            }
            all.details.push_back(r.message);
            ++n;
        }
        all.message = n ? "Criticit\xC3\xA9 des actions enregistr\xC3\xA9" "e : " + std::to_string(n) + (n > 1 ? " changements" : " changement") +
                              " (sur ce poste, partag\xC3\xA9" "e par l'archive)."
                        : std::string("Rien n'a chang\xC3\xA9.");
        auto done = done_;
        owner_.close();
        host_.report(all);
        if (done) done();
    }

    Dialog&               owner_;
    ShellHost             host_;
    std::function<void()> done_;
    Criticality           saved_, draft_;
    std::vector<Row>      rows_;
    std::vector<Head>     heads_;
    Switch*               email_{nullptr};
    ui::Button*           defaults_{nullptr};
    ui::Button*           close_{nullptr};
    ui::Button*           save_{nullptr};
    bool                  syncing_{false};
    float                 rowH_{30.f}, colW_{400.f};
    Frame                 frame_;
    core::ConnectionScope links_;
};

class CriticalityDialog final : public Dialog {
public:
    CriticalityDialog(ShellHost host, std::function<void()> done) : Dialog("dialog.criticite"), host_(std::move(host)), done_(std::move(done)) {}
    [[nodiscard]] std::string title() const override { return "Criticit\xC3\xA9 des actions"; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<CriticalityBody>(*this, host_, done_));
        return core::ok();
    }

private:
    ShellHost             host_;
    std::function<void()> done_;
};

// ====================================================================== la page ====
class JournalPage final : public Page {
public:
    static constexpr float kRowH = 45.f, kExpandH = 186.f;

    explicit JournalPage(ShellHost host) : Page("journal", std::move(host)) {
        crit_ = &button(*this, "Criticit\xC3\xA9 des actions\xE2\x80\xA6", "journal.criticite", ui::Button::Style::Default, ui::Icon::Settings);
        crit_->setTooltip("Une criticit\xC3\xA9 par type d'action ; \xC2\xAB Me pr\xC3\xA9venir tout de suite \xC2\xBB pour Haute et Critique (une alerte en direct, chez toi seulement).");
        export_ = &button(*this, "Exporter (CSV)", "journal.exporter", ui::Button::Style::Default, ui::Icon::Export);
        export_->setTooltip("Les lignes montr\xC3\xA9" "es (les filtres, le tri), dans un fichier CSV.");
        fetch_ = &button(*this, "Actualiser", "journal.relever", ui::Button::Style::Default, ui::Icon::Refresh);
        fetch_->setTooltip("Relever tout de suite les nouvelles lignes sur le service et les v\xC3\xA9rifier (l'Admin le fait seul toutes les 15 s).");
        // 1.48.1 : Supprimer est la, grise : rien ne s'efface du journal (la raison dans son infobulle).
        delete_ = &button(*this, "Supprimer\xE2\x80\xA6", "journal.supprimer", ui::Button::Style::Danger, ui::Icon::Delete);
        delete_->setEnabled(false);
        delete_->setTooltip(kNoDelete);
        // 1.47.0 (planche 12) : LA BARRE - chercher (journal.recherche), l'admin, le type, la criticite, la periode (30 j
        // d'office), le PC, + Filtre ; << Mes filtres >>. Elle remplace les pastilles des admins et de la criticite.
        bar_ = &adopt(*this, std::make_unique<ui::FilterBar>("journal.barre", barFields(), "journal.recherche"));
        bar_->setNoun("lignes du journal");
        bar_->setChipOrder({6, 1, 0, 5});   // 1.47.0 : les planches - Type, Qui (l'admin), Periode, puis la criticite
        bar_->setClock([this] { return listkit::seconds(ws().now()); });
        {
            ui::FilterBar::Source src;
            src.rows = [this] { return lines_.size(); };
            src.text = [this](std::size_t r, std::size_t f) { return r < lines_.size() ? fieldText(lines_[r], f) : std::string(); };
            src.time = [this](std::size_t r, std::size_t f) -> std::int64_t { return f == 0 && r < lines_.size() ? listkit::seconds(lines_[r].when) : 0; };
            src.extra = [this](std::size_t r) {
                return r < lines_.size() ? lines_[r].object + " " + lines_[r].actionLabel + " n\xC2\xB0 " + std::to_string(lines_[r].number) : std::string();
            };
            bar_->setSource(std::move(src));
        }
        bar_->setMemoryKey("journal");
        if (!bar_->state().active()) bar_->setPeriod(0, 30);   // d'office : les 30 derniers jours (comme avant)
        links_ += bar_->changed->connect([this] {
            first_ = 0;
            rebuild();
        });
        const char* heads[6] = {"QUAND", "ADMIN", "PC", "ACTION", "OBJET", "CRITICIT\xC3\x89"};
        const char* sortIds[6] = {"journal.tri.quand", "journal.tri.admin", "journal.tri.pc", "journal.tri.action", "journal.tri.objet", "journal.tri.criticite"};
        for (int i = 0; i < 6; ++i) {
            sort_[i] = &adopt(*this, std::make_unique<SortHeader>(heads[i], sortIds[i]));
            links_ += sort_[i]->clicked->connect([this, i] { sortBy(i); });
        }
        prev_ = &button(*this, "Pr\xC3\xA9" "c\xC3\xA9" "dentes", "journal.page.precedente");
        next_ = &button(*this, "Suivantes", "journal.page.suivante");
        table_ = &adopt(*this, std::make_unique<ui::TableView>("journal.table"));
        table_->setSelectionMode(ui::SelectionMode::Single);
        table_->setVisibility(ui::Visibility::Collapsed);   // les scripts : la page dessine le sien
        menu_ = &adopt(*this, std::make_unique<ui::PopupMenu>("journal.menu"));

        links_ += crit_->clicked->connect([this] {
            auto alive = alive_;
            host_.openDialog(makeCriticalityDialog(host_, [this, alive] {
                if (*alive) refresh();
            }));
        });
        links_ += export_->clicked->connect([this] { exportCsv(); });
        links_ += fetch_->clicked->connect([this] { fetchNow(); });
        links_ += prev_->clicked->connect([this] {
            first_ = std::max(0, first_ - std::max(1, pageSize_));
            invalidateLayout();
            invalidate();
            updatePager();
        });
        links_ += next_->clicked->connect([this] {
            if (first_ + shownCount_ < static_cast<int>(shown_.size())) first_ += std::max(1, shownCount_);
            invalidateLayout();
            invalidate();
            updatePager();
        });
        links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& sel) {
            if (syncing_ || sel.empty() || sel.front() >= shown_.size()) return;
            expandAt(static_cast<int>(sel.front()), true);
        });
        links_ += menu_->itemChosen->connect([this](int id) { menuChosen(id); });
        refresh();
    }
    ~JournalPage() override { *alive_ = false; }

    [[nodiscard]] std::string title() const override { return "Journal des admins"; }

    void refresh() override {
        view_ = ws().journalLines();
        const std::string f = takePageFocus("journal");
        std::string whoKey;
        if (f.rfind("admin:", 0) == 0) {
            whoKey = f.substr(6);
            first_ = 0;
            expanded_ = 0;
        } else if (f.rfind("ligne:", 0) == 0) {
            // Une alerte : la ligne, quels que soient les filtres (la barre videe : tout le journal).
            bar_->clearAll();
            expanded_ = std::strtoull(f.c_str() + 6, nullptr, 10);
            revealExpanded_ = true;
        }
        rebuildLines();
        bar_->refreshCounts();
        if (!whoKey.empty()) setWho(whoKey);
        rebuild();
    }

    // Les essais.
    [[nodiscard]] std::size_t shownCount() const noexcept { return shown_.size(); }
    [[nodiscard]] std::uint64_t expandedNumber() const noexcept { return expanded_; }
    [[nodiscard]] std::vector<std::uint64_t> shownNumbers() const {
        std::vector<std::uint64_t> out;
        for (auto i : shown_) out.push_back(lines_[i].number);
        return out;
    }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = b.w < 900.f ? 20.f : 28.f;
        x0_ = b.x + pad;
        w_ = std::max(600.f, b.w - 2.f * pad);
        titleY_ = b.y + (b.h < 680.f ? 16.f : 24.f);
        float bx = x0_ + w_;
        for (ui::Button* btn : {export_, crit_, delete_, fetch_}) {
            const float bw = buttonWidth(*btn, 110.f);
            bx -= bw;
            btn->setBounds({bx, titleY_ + 4.f, bw, 38.f});
            bx -= 10.f;
        }
        leadRight_ = bx;
        // 1.47.0 : la barre de filtres (chercher, l'admin, le type, la criticite, la periode...).
        chipsY_ = titleY_ + 76.f;
        const float bh = bar_->heightFor(w_);
        bar_->setBounds({x0_, chipsY_, w_, bh});
        levelsY_ = chipsY_ + bh - 34.f;
        // Le tableau.
        const float top = chipsY_ + bh + 14.f, bottom = b.bottom() - 16.f;
        card_ = {x0_, top, w_, std::max(200.f, bottom - top)};
        const float cw = card_.w - 36.f;
        const float frac[6] = {0.11f, 0.13f, 0.15f, 0.35f, 0.16f, 0.10f};
        float x = card_.x + 18.f;
        for (int i = 0; i < 6; ++i) {
            colX_[i] = x;
            colW_[i] = cw * frac[i];
            const float hw = std::min(colW_[i] - 4.f, ui::measureWidth(sort_[i]->text(), gfx::semibold(kF11)) + 26.f);
            sort_[i]->setBounds({x - 4.f, card_.y + 8.f, hw, 26.f});
            x += colW_[i];
        }
        rowsTop_ = card_.y + 42.f;
        footY_ = card_.bottom() - 46.f;
        // 1.47.0 : la hauteur du depliage, celle de sa carte (ui::UnfoldCard).
        expandH_ = unfold_ ? std::max(120.f, unfold_->heightFor(card_.w - 2.f)) : kExpandH;
        // Les lignes qui tiennent (une depliee prend plus).
        const float avail = footY_ - rowsTop_;
        pageSize_ = std::max(1, static_cast<int>(avail / kRowH));
        if (revealExpanded_) {
            revealExpanded_ = false;
            for (std::size_t i = 0; i < shown_.size(); ++i)
                if (lines_[shown_[i]].number == expanded_) {
                    const int need = std::max(1, static_cast<int>((avail - expandH_) / kRowH));
                    if (static_cast<int>(i) < first_ || static_cast<int>(i) >= first_ + need) first_ = std::max(0, static_cast<int>(i) - need / 2);
                }
        }
        first_ = std::clamp(first_, 0, std::max(0, static_cast<int>(shown_.size()) - 1));
        shownCount_ = 0;
        float y = rowsTop_;
        rowTops_.clear();
        expandRect_ = {};
        for (int i = first_; i < static_cast<int>(shown_.size()); ++i) {
            const bool open = lines_[shown_[static_cast<std::size_t>(i)]].number == expanded_;
            const float h = kRowH + (open ? expandH_ : 0.f);
            if (y + h > footY_ + 0.5f && shownCount_ > 0) break;
            rowTops_.push_back(y);
            if (open) expandRect_ = {card_.x + 1.f, y + kRowH, card_.w - 2.f, expandH_};
            y += h;
            ++shownCount_;
        }
        // 1.47.0 : la carte de la ligne depliee (ui::UnfoldCard) a sa place, ou cachee (la ligne hors de la page).
        if (unfold_) {
            unfold_->setVisibility(expandRect_.empty() ? ui::Visibility::Hidden : ui::Visibility::Visible);
            if (!expandRect_.empty()) unfold_->setBounds(expandRect_);
        }
        const float nw = buttonWidth(*next_, 100.f), pw2 = buttonWidth(*prev_, 110.f);
        next_->setBounds({card_.right() - 16.f - nw, footY_ + 8.f, nw, 30.f});
        prev_->setBounds({next_->bounds().x - 8.f - pw2, footY_ + 8.f, pw2, 30.f});
        menu_->setBounds(b);
        updatePager();
    }

    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        r.fillRect(bounds(), c.windowBg);
        grave_.clear();   // 1.47.0 : les cartes remplacees pendant un evenement
        r.drawText({x0_, titleY_}, "Journal des admins", gfx::semibold(kF28), c.text);
        const Seconds since = view_.rows.empty() ? 0 : view_.rows.front().line.parsed.when;
        std::string lead = "Chaque action de chaque admin, sign\xC3\xA9" "e par sa cl\xC3\xA9 et cha\xC3\xAEn\xC3\xA9" "e \xC3\xA0 la pr\xC3\xA9" "c\xC3\xA9" "dente : rien ne s'efface \xC2\xB7 " +
                           std::to_string(view_.rows.size()) + (view_.rows.size() > 1 ? " actions" : " action");
        if (since) lead += " depuis le " + dateFr(since);
        if (fetching_) lead += " \xC2\xB7 relev\xC3\xA9 en cours\xE2\x80\xA6";
        // 1.47.0 : trop long pour la place (les boutons a droite) - la phrase courte, plutot qu'un << ... >> sur le compte.
        if (r.measure(lead, kF14).width > leadRight_ - x0_ - 12.f) {
            const std::string head = "Chaque action de chaque admin, sign\xC3\xA9" "e par sa cl\xC3\xA9 et cha\xC3\xAEn\xC3\xA9" "e \xC3\xA0 la pr\xC3\xA9" "c\xC3\xA9" "dente : ";
            lead = "Sign\xC3\xA9" "es et cha\xC3\xAEn\xC3\xA9" "es : " + lead.substr(head.size());
            if (const auto d = lead.find(" depuis le "); d != std::string::npos && r.measure(lead, kF14).width > leadRight_ - x0_ - 12.f)
                lead.erase(d, lead.find(" \xC2\xB7 ", d) == std::string::npos ? std::string::npos : lead.find(" \xC2\xB7 ", d) - d);
        }
        r.drawText({x0_, titleY_ + 40.f}, fit(r, lead, kF14, leadRight_ - x0_ - 12.f), kF14, c.textMuted);
        cardBox(r, th, card_);
        r.fillRect({card_.x + 1.f, rowsTop_ - 1.f, card_.w - 2.f, 1.f}, c.gridLine);
        const Seconds now = ws().now();
        r.pushClip({card_.x, rowsTop_, card_.w, footY_ - rowsTop_});
        for (int k = 0; k < shownCount_; ++k) {
            const std::size_t si = static_cast<std::size_t>(first_ + k);
            if (si >= shown_.size() || static_cast<std::size_t>(k) >= rowTops_.size()) break;
            paintRow(ctx, lines_[shown_[si]], rowTops_[static_cast<std::size_t>(k)], now);
        }
        r.popClip();
        if (shown_.empty()) {
            const std::string empty = view_.rows.empty()
                                          ? std::string("Aucune ligne relev\xC3\xA9" "e pour l'instant. Le journal se rel\xC3\xA8ve seul toutes les 15 s quand le service est r\xC3\xA9gl\xC3\xA9 ; "
                                                        "\xC2\xAB Actualiser \xC2\xBB le fait tout de suite.")
                                          : std::string("Aucune ligne pour ces filtres : \xC2\xAB Effacer les filtres \xC2\xBB, ou une autre p\xC3\xA9riode.");
            (void)drawWrapped(r, card_.x + 24.f, rowsTop_ + 20.f, card_.w - 48.f, empty, kF14, c.textMuted, 20.f, 3);
        }
        // Le pied : la chaine, la pagination.
        r.fillRect({card_.x + 1.f, footY_, card_.w - 2.f, 1.f}, c.gridLine);
        const float fy = footY_ + 15.f;
        const float pagerX = prev_->bounds().x - 16.f;
        const std::string pager = pagerText();
        const float pgw = r.measure(pager, kF13).width;
        r.drawText({pagerX - pgw, fy}, pager, kF13, c.textMuted);
        const float chainW = pagerX - pgw - 24.f - (card_.x + 44.f);
        if (view_.problems.empty()) {
            ui::drawIcon(r, ui::Icon::Link, {card_.x + 18.f, fy + 1.f, 15.f, 15.f}, c.ok);
            r.drawText({card_.x + 44.f, fy},
                       fit(r, "Cha\xC3\xAEne v\xC3\xA9rifi\xC3\xA9" "e : " + std::to_string(view_.rows.size()) + (view_.rows.size() > 1 ? " lignes" : " ligne") +
                                  ", aucune manquante, aucune modifi\xC3\xA9" "e",
                           kF13, chainW),
                       kF13, c.textMuted);
        } else {
            ui::drawIcon(r, ui::Icon::Error, {card_.x + 18.f, fy + 1.f, 15.f, 15.f}, c.error);
            std::string p = "Cha\xC3\xAEne : " + std::to_string(view_.problems.size()) + (view_.problems.size() > 1 ? " probl\xC3\xA8mes \xE2\x80\x94 " : " probl\xC3\xA8me \xE2\x80\x94 ") +
                            view_.problems.front();
            r.drawText({card_.x + 44.f, fy}, fit(r, p, gfx::semibold(kF13), chainW), gfx::semibold(kF13), c.error);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            const int k = rowAt(d->pos);
            if (k >= 0) {
                const int si = first_ + k;
                cursor_ = si;
                if (d->button == ui::MouseButton::Left && d->clickCount >= 2) {   // 1.47.0 : le double-clic ouvre l'objet
                    if (si >= 0 && static_cast<std::size_t>(si) < shown_.size() && lines_[shown_[static_cast<std::size_t>(si)]].number != expanded_)
                        expandAt(si, true);
                    seeObject();
                    return ui::EventResult::Consumed;
                }
                if (d->button == ui::MouseButton::Left) {
                    expandAt(si, false);
                    return ui::EventResult::Consumed;
                }
                if (d->button == ui::MouseButton::Right) {
                    openMenu(si, d->pos);
                    return ui::EventResult::Consumed;
                }
            }
        }
        // 1.47.0 (planche 07) : -> deplie, <- replie, Entree ouvre l'objet (la ligne cliquee en dernier).
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->mods.none() && !bar_->searchField().focused()) {
            if (k->key == ui::Key::Right && cursor_ >= 0 && static_cast<std::size_t>(cursor_) < shown_.size()) {
                expandAt(cursor_, true);
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::Left && expanded_) {
                expanded_ = 0;
                rebuildCard();
                return ui::EventResult::Consumed;
            }
            if (k->key == ui::Key::Return && expanded_) {
                seeObject();
                return ui::EventResult::Consumed;
            }
        }
        if (const auto* wh = std::get_if<ui::MouseWheel>(&ev); wh && card_.contains(wh->pos)) {
            const int before = first_;
            first_ = std::clamp(first_ + (wh->dy > 0 ? -1 : 1), 0, std::max(0, static_cast<int>(shown_.size()) - 1));
            if (first_ != before) {
                invalidateLayout();
                invalidate();
            }
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    // ------------------------------------------------------------------ les donnees
    void rebuildLines() {
        lines_.clear();
        const Workspace& w = ws();
        const std::string plusName = w.key().name.empty() ? std::string("admin+") : w.key().name;
        for (std::size_t i = 0; i < view_.rows.size(); ++i) {
            const JournalRow& jr = view_.rows[i];
            const auto& e = jr.line.parsed;
            Line l;
            l.row = i;
            l.number = jr.line.number;
            l.when = e.when ? e.when : jr.line.received;
            l.key = jr.line.key;
            l.who = jr.who == "admin+" ? plusName : jr.who == "le service" ? std::string("Le service") : jr.who.empty() ? std::string("Cl\xC3\xA9 inconnue") : jr.who;
            l.pc = e.pc;
            l.action = e.action;
            const adm::ActionType* t = adm::actionType(e.action);
            l.actionLabel = t ? t->label : e.action;
            l.summary = e.summary.empty() ? l.actionLabel : e.summary;
            l.object = e.object;
            l.level = jr.level;
            l.otherPc = e.action == "autre_pc";
            // L'objet : ce que dit le detail (le client, l'offre...), sinon l'objet.
            const json::Value d = objectOf(e.detail);
            std::string o;
            int n = 0;
            for (const auto& [k, v] : d.members()) {
                if (!(v.isString() || v.isNumber()) || n >= 3) continue;
                const std::string s = valueText(v);
                if (s.empty() || s == l.object) continue;
                o += (o.empty() ? "" : " \xC2\xB7 ") + s;
                ++n;
            }
            l.objectText = o.empty() ? l.object : o;
            lines_.push_back(std::move(l));
        }
        // Les couleurs des avatars : l'admin+, puis les admins dans l'ordre de la liste (ListKit).
        colorOf_.clear();
        for (const auto& l : lines_)
            if (!colorOf_.count(l.key)) colorOf_[l.key] = listkit::adminColor(w, l.key);
    }

    // Les couleurs des avatars viennent du theme au dessin ; ici, un theme de reference suffit (le violet).
    static const ui::Theme& ws0Theme() {
        static const ui::Theme t = ui::Theme::byName("XPG");
        return t;
    }

    // 1.47.0 (planche 12) : les colonnes vues par la barre.
    static std::vector<ui::FilterBar::Field> barFields() {
        using K = ui::FilterBar::Kind;
        std::vector<ui::FilterBar::Field> f(7);
        f[0] = {.title = "Quand", .kind = K::Period, .chip = true, .chipLabel = "P\xC3\xA9riode"};
        f[1] = {.title = "Admin", .chip = true, .noun = "un admin"};
        f[2] = {.title = "PC", .noun = "un PC"};
        f[3] = {.title = "Action", .kind = K::Text};
        f[4] = {.title = "Objet", .kind = K::Text};
        f[5] = {.title = "Criticit\xC3\xA9", .chip = true, .noun = "une criticit\xC3\xA9"};
        for (adm::Level l : kLevels) f[5].order.push_back(adm::levelLabel(l));
        f[6] = {.title = "Type", .chip = true, .noun = "un type"};
        for (const auto& t : adm::actionTypes()) f[6].order.push_back(t.label);
        return f;
    }
    [[nodiscard]] static std::string fieldText(const Line& l, std::size_t f) {
        switch (f) {
            case 0: return dateTimeFr(l.when);
            case 1: return l.who;
            case 2: return l.pc;
            case 3: return l.summary;
            case 4: return l.objectText;
            case 5: return adm::levelLabel(l.level);
            case 6: return l.actionLabel;
            default: return {};
        }
    }

    void rebuild() {
        shown_.clear();
        for (std::size_t i = 0; i < lines_.size(); ++i)
            if (bar_->accepts(i)) shown_.push_back(i);   // 1.47.0 : la barre (la recherche, les puces, la periode)
        const int col = sortCol_;
        const bool desc = sortDesc_;
        std::stable_sort(shown_.begin(), shown_.end(), [this, col, desc](std::size_t a, std::size_t b) {
            const Line& x = lines_[a];
            const Line& y = lines_[b];
            int cmp = 0;
            switch (col) {
                case kQuand: cmp = x.when < y.when ? -1 : x.when > y.when ? 1 : 0; break;
                case kAdmin: cmp = lower(x.who).compare(lower(y.who)); break;
                case kPc: cmp = lower(x.pc).compare(lower(y.pc)); break;
                case kAction: cmp = lower(x.summary).compare(lower(y.summary)); break;
                case kObjet: cmp = lower(x.objectText).compare(lower(y.objectText)); break;
                case kCrit: cmp = static_cast<int>(x.level) - static_cast<int>(y.level); break;
                default: break;
            }
            if (cmp == 0) cmp = x.number < y.number ? -1 : x.number > y.number ? 1 : 0;
            return desc ? cmp > 0 : cmp < 0;
        });
        for (int i = 0; i < 6; ++i) sort_[i]->setSort(i == sortCol_, sortDesc_);
        // Le tableau des scripts : les lignes montrees, dans l'ordre.
        std::vector<std::vector<std::string>> rows;
        for (auto i : shown_) rows.push_back({"n\xC2\xB0 " + std::to_string(lines_[i].number), lines_[i].who, lines_[i].summary});
        syncing_ = true;
        table_->setModel(std::make_shared<Rows1>(std::move(rows)));
        syncing_ = false;
        export_->setEnabled(!shown_.empty());
        fetch_->setEnabled(ws().service().ready() && !fetching_);
        crit_->setEnabled(ws().role() == Role::Plus);
        rebuildCard();
        invalidateLayout();
        invalidate();
    }

    // 1.47.0 : << Tout le journal de Lea >>, l'arrivee depuis Administrateurs : la puce Admin sur lui (vide : levee).
    void setWho(const std::string& key) {
        first_ = 0;
        if (key.empty()) {
            bar_->clearField(1);
            return;
        }
        for (const auto& l : lines_)
            if (l.key == key) {
                bar_->filterOn(1, l.who);
                return;
            }
        rebuild();
    }

    void sortBy(int col) {
        if (sortCol_ == col) sortDesc_ = !sortDesc_;
        else {
            sortCol_ = col;
            sortDesc_ = col == kQuand || col == kCrit;   // la date et la criticite : la plus recente, la plus haute d'abord
        }
        first_ = 0;
        rebuild();
    }

    [[nodiscard]] const Line* expandedLine() const {
        if (!expanded_) return nullptr;
        for (const auto& l : lines_)
            if (l.number == expanded_) return &l;
        return nullptr;
    }

    // Deplier (ou replier) la ligne montree `si` ; `select` : le tableau des scripts l'a choisie (elle reste depliee).
    void expandAt(int si, bool select) {
        if (si < 0 || static_cast<std::size_t>(si) >= shown_.size()) return;
        const std::uint64_t n = lines_[shown_[static_cast<std::size_t>(si)]].number;
        expanded_ = expanded_ == n && !select ? 0 : n;
        if (expanded_) revealExpanded_ = true;
        rebuildCard();
    }

    // ---- 1.47.0 (planche 07) : LA LIGNE DEPLIEE - l'objet touche (ses liens), l'avant et l'apres, la signature ;
    // << Voir... >>, l'admin (sa fiche), << Tout le journal de ... >>. Une carte (ui::UnfoldCard), refaite a chaque
    // changement ; l'ancienne attend le dessin suivant (elle peut etre en train d'emettre son clic).
    void rebuildCard() {
        if (unfold_) {
            grave_.push_back(removeChild(*unfold_));
            unfold_ = nullptr;
        }
        const Line* l = expandedLine();
        if (l) {
            bool shownNow = false;
            for (auto i : shown_) shownNow = shownNow || lines_[i].number == l->number;
            if (!shownNow) l = nullptr;
        }
        if (l) {
            auto card = makeCard(*l, view_.rows[l->row]);
            unfold_ = &static_cast<ui::UnfoldCard&>(addChild(std::move(card)));
            unfold_->setVisibility(ui::Visibility::Hidden);   // posee a la mise en page
        }
        invalidateLayout();
        invalidate();
    }

    std::unique_ptr<ui::UnfoldCard> makeCard(const Line& l, const JournalRow& jr) {
        auto card = std::make_unique<ui::UnfoldCard>("journal.depliage");
        card->setOwnBackground(true);
        const json::Value before = objectOf(jr.line.parsed.before), after = objectOf(jr.line.parsed.after), detail = objectOf(jr.line.parsed.detail);
        // L'objet touche : l'objet (un lien vers sa page), puis ce que dit le detail (le client, l'offre...).
        ui::UnfoldCard::Box obj;
        obj.title = "L'OBJET TOUCH\xC3\x89";
        obj.weight = 0.9f;
        if (!l.object.empty()) {
            ui::UnfoldCard::Line line{.icon = ui::Icon::Document, .text = "**" + l.object + "**"};
            if (!seeLabel(l).empty()) line.link = [this] { seeObject(); };
            obj.lines.push_back(std::move(line));
        }
        int n = 0;
        for (const auto& [k, v] : detail.members()) {
            if (n >= 3 || !(v.isString() || v.isNumber() || v.isBool())) continue;
            const bool when = isTimeKey(k) && v.isNumber() && v.asNumber() > 1e9 && v.asNumber() < 1e10;
            obj.lines.push_back({.icon = ui::Icon::Info, .text = keyText(k) + " **" + (when ? dateTimeFr(static_cast<Seconds>(v.asNumber())) : valueText(v)) + "**"});
            ++n;
        }
        if (!l.pc.empty()) obj.lines.push_back({.icon = l.otherPc ? ui::Icon::Warning : ui::Icon::Station, .text = "PC **" + l.pc + "**", .tone = l.otherPc ? ui::Tone::Warning : ui::Tone::None});
        if (obj.lines.empty()) obj.lines.push_back({.icon = ui::Icon::Info, .text = l.summary});
        card->addBox(std::move(obj));
        // L'avant et l'apres (ce qui change) ; sans : le reste du detail.
        std::vector<std::string> keys;
        for (const auto* o : {&after, &before})
            for (const auto& [k, v] : o->members())
                if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
        if (!keys.empty()) {
            ui::UnfoldCard::Box ba;
            ba.title = "AVANT \xC2\xB7 APR\xC3\x88S";
            ba.weight = 1.2f;
            ba.head = {"CE QUI CHANGE", "AVANT", "APR\xC3\x88S"};
            for (const auto& k : keys) {
                if (ba.table.size() >= 4) break;
                const json::Value* b = before.find(k);
                const json::Value* a = after.find(k);
                ba.table.push_back({keyText(k), b ? valueText(*b) : std::string("\xE2\x80\x94"), a ? valueText(*a) : std::string("\xE2\x80\x94")});
            }
            card->addBox(std::move(ba));
        }
        // La signature, le chainage, la reception.
        ui::UnfoldCard::Box sig;
        sig.title = "LA SIGNATURE";
        const bool service = jr.who == "le service";
        if (jr.verified) {
            sig.lines.push_back({.icon = ui::Icon::Lock,
                                 .text = service ? std::string("\xC3\x89" "crite et sign\xC3\xA9" "e par le service")
                                                 : "Sign\xC3\xA9" "e par la cl\xC3\xA9 de " + l.who + " " + shortFingerprint(adm::fingerprint(l.key)) + "\xE2\x80\xA6",
                                 .check = true});
            sig.lines.push_back({.icon = ui::Icon::Ok,
                                 .text = service ? std::string("sa cl\xC3\xA9, certifi\xC3\xA9" "e par l'\xC3\xA9" "diteur")
                                                 : jr.who == "admin+" ? std::string("la cl\xC3\xA9 de l'\xC3\xA9" "diteur") : std::string("certificat valide"),
                                 .tone = ui::Tone::Ok});
        } else {
            sig.lines.push_back({.icon = ui::Icon::Error, .text = jr.problem.empty() ? std::string("signature non v\xC3\xA9rifi\xC3\xA9" "e") : jr.problem, .tone = ui::Tone::Error});
        }
        const std::string tag = "n\xC2\xB0 " + std::to_string(l.number);
        std::string chainProblem;
        for (const auto& p : view_.problems)
            if (p.find(tag + " ") != std::string::npos || (p.size() >= tag.size() && p.compare(p.size() - tag.size(), tag.size(), tag) == 0)) chainProblem = p;
        if (chainProblem.empty())
            sig.lines.push_back({.icon = ui::Icon::Link,
                                 .text = l.number <= 1 ? std::string("Ligne n\xC2\xB0 1, la premi\xC3\xA8re de la cha\xC3\xAEne")
                                                       : "Ligne n\xC2\xB0 " + std::to_string(l.number) + ", cha\xC3\xAEn\xC3\xA9" "e \xC3\xA0 la n\xC2\xB0 " + std::to_string(l.number - 1),
                                 .check = true});
        else
            sig.lines.push_back({.icon = ui::Icon::Error, .text = chainProblem, .tone = ui::Tone::Error});
        std::string got = "Re\xC3\xA7ue par le service le " + dateTimeFr(jr.line.received ? jr.line.received : l.when);
        if (!jr.line.parsed.software.empty()) got += " \xC2\xB7 " + jr.line.parsed.software;
        sig.lines.push_back({.icon = ui::Icon::History, .text = got});
        if (jr.overridden) {
            std::string o = "Criticit\xC3\xA9 chang\xC3\xA9" "e \xC3\xA0 la main";
            if (!jr.overrideBy.empty()) o += " par " + jr.overrideBy;
            if (jr.overrideAt) o += " le " + dateTimeFr(jr.overrideAt);
            sig.lines.push_back({.icon = ui::Icon::Settings, .text = o, .tone = ui::Tone::Accent});
        }
        sig.weight = 1.1f;
        card->addBox(std::move(sig));
        // La rangee : Voir..., l'admin (sa fiche), Tout le journal de ... ; a droite : la criticite.
        if (const std::string see = seeLabel(l); !see.empty()) card->addButton(see, "journal.voir", ui::Icon::GoTo, [this] { seeObject(); });
        if (!l.key.empty() && !service) {
            (void)card->addPerson(listkit::person(host_, l.key, l.who), "journal.admin");
            const std::string key = l.key;
            card->addButton("Tout le journal de " + l.who, "journal.tout-de", ui::Icon::History, [this, key] { setWho(key); });
        }
        card->addText("Criticit\xC3\xA9 : " + adm::levelLabel(l.level), true);
        return card;
    }

    [[nodiscard]] int rowAt(gfx::Point p) const {
        if (p.x < card_.x || p.x > card_.right()) return -1;
        for (std::size_t k = 0; k < rowTops_.size(); ++k)
            if (p.y >= rowTops_[k] && p.y < rowTops_[k] + kRowH) return static_cast<int>(k);
        return -1;
    }

    [[nodiscard]] std::string pagerText() const {
        const int total = static_cast<int>(shown_.size());
        std::string s;
        if (total == 0) s = "0 sur " + std::to_string(lines_.size());
        else if (first_ == 0) s = std::to_string(shownCount_) + " sur " + std::to_string(total);
        else s = std::to_string(first_ + 1) + " \xC3\xA0 " + std::to_string(first_ + shownCount_) + " sur " + std::to_string(total);
        const char* what[6] = {"par date", "par admin", "par PC", "par action", "par objet", "par criticit\xC3\xA9"};
        std::string order;
        if (sortCol_ == kQuand) order = sortDesc_ ? "la plus r\xC3\xA9" "cente d'abord" : "la plus ancienne d'abord";
        else if (sortCol_ == kCrit) order = sortDesc_ ? "la plus haute d'abord" : "la plus basse d'abord";
        else order = sortDesc_ ? "de Z \xC3\xA0 A" : "de A \xC3\xA0 Z";
        return s + " \xC2\xB7 tri\xC3\xA9" "es " + what[sortCol_] + ", " + order;
    }

    void updatePager() {
        prev_->setEnabled(first_ > 0);
        next_->setEnabled(first_ + shownCount_ < static_cast<int>(shown_.size()));
    }

    static std::string seeLabel(const Line& l) {
        const std::string& a = l.action;
        if (a.rfind("licence_", 0) == 0 || a == "code_revoir" || a == "poste_retirer") return "Voir la licence";
        if (a == "offres" || a == "annonces") return "Voir l'offre";
        if (a == "options") return "Voir les options";
        if (a.rfind("client", 0) == 0 || a == "courriel") return "Voir les clients";
        if (a.rfind("demande_", 0) == 0 && a != "demande_admin") return "Voir les demandes";
        if (a.rfind("admin_", 0) == 0 || a == "demande_admin" || a == "autre_pc" || a == "signature_refusee") return "Voir les admins";
        if (a == "version_publier") return {};
        return {};
    }

    void seeObject() {
        const Line* l = expandedLine();
        if (!l) return;
        const std::string& a = l->action;
        if (a.rfind("licence_", 0) == 0 || a == "code_revoir" || a == "poste_retirer") {
            requestPageFocus("licences", l->object);
            host_.navigate("licences");
        } else if (a == "offres" || a == "annonces") host_.navigate("editions");
        else if (a == "options") host_.navigate("options");
        else if (a.rfind("client", 0) == 0 || a == "courriel") host_.navigate("clients");
        else if (a.rfind("demande_", 0) == 0 && a != "demande_admin") host_.navigate("demandes");
        else {
            requestPageFocus("admins", "admin:" + l->key);
            host_.navigate("admins");
        }
    }

    // ---- 1.47.0 (planche 06) : le clic droit, dans l'ordre de la regle ----
    void openMenu(int si, gfx::Point at) {
        if (si < 0 || static_cast<std::size_t>(si) >= shown_.size()) return;
        menuLine_ = lines_[shown_[static_cast<std::size_t>(si)]].number;
        const Line& l = lines_[shown_[static_cast<std::size_t>(si)]];
        const JournalRow& jr = view_.rows[l.row];
        adm::Level typeLevel = adm::Level::Info;
        if (const adm::ActionType* t = adm::actionType(l.action)) typeLevel = ws().criticality().actions[t->id].level;
        ui::ListMenu m({1, "lignes", true});
        const std::uint64_t number = l.number;
        const int row = si;
        m.unfold(expanded_ == number, [this, row] { expandAt(row, false); });
        // La criticite de cette ligne (une note a cote du journal : la ligne signee ne change jamais) - l'admin+.
        ui::ListMenu::Entry crit{.label = "Criticit\xC3\xA9 de cette ligne", .icon = ui::Icon::Settings,
                                 .why = ws().role() == Role::Plus ? std::string() : std::string("r\xC3\xA9serv\xC3\xA9 \xC3\xA0 l'admin+")};
        for (int i = 0; i < 4; ++i) {
            const adm::Level level = kLevels[i];
            crit.children.push_back({.label = adm::levelLabel(level), .run = [this, number, level, typeLevel] {
                                         host_.report(ws().setLineLevel(number, level, level == typeLevel));
                                         refresh();
                                     },
                                     .note = level == typeLevel ? std::string("celle du type") : std::string(), .radio = true, .checked = jr.level == level});
        }
        if (ws().role() == Role::Plus) crit.why.clear();
        else crit.children.clear();
        m.action(std::move(crit));
        if (const std::string see = seeLabel(l); !see.empty())
            m.goTo({.label = see.substr(0, 5) == "Voir " ? "L" + see.substr(5) : see, .icon = ui::Icon::Document, .run = [this, number] {
                        expanded_ = number;
                        seeObject();
                    }});
        if (!l.key.empty() && jr.who != "le service") {
            const std::string key = l.key;
            m.goTo({.label = "L'admin : " + l.who, .icon = ui::Icon::User, .run = [this, key] {
                        requestPageFocus("admins", "admin:" + key);
                        host_.navigate("admins");
                    }});
        }
        const auto on = [&](std::size_t f, const std::string& label) {
            const std::string v = fieldText(l, f);
            if (v.empty()) return;
            m.filterOn({.label = label + " : " + v, .run = [this, f, v] { bar_->filterOn(f, v); }});
        };
        on(1, "Cet admin");
        on(6, "Ce type");
        on(5, "Cette criticit\xC3\xA9");
        {
            const std::int64_t when = listkit::seconds(l.when);
            m.filterOn({.label = "Ce jour : " + dateFr(l.when), .run = [this, when] { bar_->filterOnDay(0, when); }});
        }
        on(2, "Ce PC");
        m.copy("Le num\xC3\xA9ro", std::to_string(l.number), ui::Icon::Copy);
        if (!l.object.empty()) m.copy("L'objet", l.object, ui::Icon::Document);
        m.copy("La ligne",
               "Ligne n\xC2\xB0 " + std::to_string(l.number) + "\t" + dateTimeFr(l.when) + "\t" + l.who + "\t" + l.pc + "\t" + l.summary + "\t" + l.objectText + "\t" +
                   adm::levelLabel(l.level),
               ui::Icon::Copy);
        m.exportRows([this, number] { exportCsv({number}); });
        m.danger({.label = "Supprimer\xE2\x80\xA6", .icon = ui::Icon::Delete, .why = kNoDelete});   // 1.48.1 : la, grise, avec sa raison
        menuList_ = m;
        menu_->setItems(menuList_.items(1));
        menu_->openInWindow(at);
    }

    void menuChosen(int id) { (void)menuList_.run(id); }

    // ---- relever, exporter ----
    void fetchNow() {
        if (fetching_) return;
        const Workspace* w = &ws();
        auto alive = alive_;
        fetching_ = true;
        if (!host_.runSlow("Journal des admins : relev\xC3\xA9", [w] { return w->fetchJournal(nullptr); },
                           [this, alive](const OpResult&) {
                               if (!*alive) return;
                               fetching_ = false;
                               refresh();
                           })) {
            fetching_ = false;
            host_.flash("Un travail est en cours : attends qu'il finisse.", Tone::Warning);
        }
        rebuild();
    }

    static std::string csvCell(const std::string& s) {
        if (s.find_first_of(";\"\n") == std::string::npos) return s;
        std::string o = "\"";
        for (char c : s) o += c == '"' ? std::string("\"\"") : std::string(1, c);
        return o + "\"";
    }

    // Les lignes montrees ; `only` non vide : seulement ces lignes (le clic droit << Exporter la ligne >>).
    void exportCsv(std::vector<std::uint64_t> only = {}) {
        if (shown_.empty()) return;
        ui::FilePick pick;
        pick.title = "Exporter le journal des admins";
        pick.filters = {{"CSV (point-virgule)", "csv"}};
        pick.save = true;
        pick.start = "journal-admins.csv";
        std::string csv = "\xEF\xBB\xBFLigne;Quand;Admin;PC;Action;Objet;Criticit\xC3\xA9;Sign\xC3\xA9" "e\n";
        std::size_t count = 0;
        for (auto i : shown_) {
            const Line& l = lines_[i];
            if (!only.empty() && std::find(only.begin(), only.end(), l.number) == only.end()) continue;
            ++count;
            const JournalRow& jr = view_.rows[l.row];
            csv += std::to_string(l.number) + ";" + csvCell(dateTimeFr(l.when)) + ";" + csvCell(l.who) + ";" + csvCell(l.pc) + ";" + csvCell(l.summary) + ";" +
                   csvCell(l.objectText) + ";" + csvCell(adm::levelLabel(l.level)) + ";" + (jr.verified ? "oui" : csvCell("non : " + jr.problem)) + "\n";
        }
        const std::size_t n = count;
        auto alive = alive_;
        host_.pickFile(pick, [this, alive, csv, n](std::string path) {
            if (!*alive) return;
            if (path.empty()) {
                host_.flash("Annul\xC3\xA9 : rien n'a \xC3\xA9t\xC3\xA9 export\xC3\xA9.", Tone::Info);
                return;
            }
            if (path.size() < 4 || lower(path.substr(path.size() - 4)) != ".csv") path += ".csv";
            std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::trunc);
            f << csv;
            if (!f) {
                host_.report(OpResult::fail("impossible d'\xC3\xA9" "crire " + path));
                return;
            }
            OpResult r = OpResult::done("Journal des admins export\xC3\xA9 : " + std::to_string(n) + (n > 1 ? " lignes." : " ligne."));
            r.output = path;
            host_.report(r);
        });
    }

    // ------------------------------------------------------------------ dessiner
    void paintRow(const ui::PaintContext& ctx, const Line& l, float y, Seconds now) {
        auto& r = ctx.r;
        const auto& th = ctx.theme;
        const auto& c = th.color;
        const bool open = l.number == expanded_;
        const bool fresh = now - l.when < 120;
        const Rect row{card_.x + 1.f, y, card_.w - 2.f, kRowH};
        if (open) {   // 1.47.0 : depliee - le violet tres pale, une barre a gauche (la ligne ; la carte peint le reste)
            r.fillRect(row, c.accent.withAlpha(18));
            r.fillRect({row.x, row.y, 3.f, kRowH}, c.accent);
        } else if (fresh && l.level == adm::Level::Critical) {
            r.fillRect(row, c.error.withAlpha(14));
            r.fillRect({row.x, row.y, 3.f, row.h}, c.error);
        }
        if (!open && menu_ && menu_->isOpen() && l.number == menuLine_) r.fillRect(row, c.accent.withAlpha(12));   // la ligne du clic droit
        r.fillRect({row.x, (open ? row.y + kRowH + expandH_ : row.bottom()) - 1.f, row.w, 1.f}, c.gridLine);
        const float ty = y + (kRowH - r.lineHeight(kF13)) * 0.5f;
        // Quand (1.47.0 : le chevron du depliage devant, planche 07)
        {
            const float ax = colX_[kQuand] - 2.f, cy0 = y + kRowH * 0.5f;
            const Color chev = open ? c.accent : c.textMuted;
            if (open) {
                r.line({ax, cy0 - 2.f}, {ax + 4.f, cy0 + 2.f}, chev, 1.5f);
                r.line({ax + 4.f, cy0 + 2.f}, {ax + 8.f, cy0 - 2.f}, chev, 1.5f);
            } else {
                r.line({ax + 2.f, cy0 - 4.f}, {ax + 6.f, cy0}, chev, 1.5f);
                r.line({ax + 6.f, cy0}, {ax + 2.f, cy0 + 4.f}, chev, 1.5f);
            }
        }
        r.drawText({colX_[kQuand] + 14.f, ty}, agoOrWhen(now, l.when), fresh ? gfx::semibold(kF13) : kF13, fresh && l.level == adm::Level::Critical ? c.error : c.text);
        // Admin
        const JournalRow& jr = view_.rows[l.row];
        const bool service = jr.who == "le service";
        const int col = colorOf_.count(l.key) ? colorOf_.at(l.key) : service ? -1 : 1;
        avatar(r, {colX_[kAdmin], y + (kRowH - 24.f) * 0.5f, 24.f, 24.f}, l.who, avatarColor(th, col), 7.f);
        r.drawText({colX_[kAdmin] + 32.f, ty}, fit(r, l.who, gfx::semibold(kF13), colW_[kAdmin] - 36.f), gfx::semibold(kF13), c.text);
        // PC
        if (l.otherPc) {
            ui::drawIcon(r, ui::Icon::Warning, {colX_[kPc], ty, 14.f, 14.f}, rgb(0xC2410C));
            r.drawText({colX_[kPc] + 18.f, ty}, fit(r, l.pc.empty() ? std::string("un autre PC") : l.pc, th.font.mono, colW_[kPc] - 22.f), th.font.mono, rgb(0xC2410C));
        } else {
            r.drawText({colX_[kPc], ty}, fit(r, l.pc.empty() ? std::string("\xE2\x80\x94") : l.pc, th.font.mono, colW_[kPc] - 8.f), th.font.mono, c.text);
        }
        // Action : le resume, l'objet en gras ; le chevron (depliee : vers le bas)
        const float ax = colX_[kAction];
        const float cy = y + kRowH * 0.5f;
        const float sx = ax, sw = colW_[kAction] - 8.f;
        const auto pos = l.object.empty() ? std::string::npos : l.summary.find(l.object);
        if (pos == std::string::npos) {
            r.drawText({sx, ty}, fit(r, l.summary, kF13, sw), kF13, c.text);
        } else {
            const std::string a = l.summary.substr(0, pos), o = l.object, z = l.summary.substr(pos + o.size());
            float x = sx;
            const FontId fb = gfx::semibold(kF13);
            r.drawText({x, ty}, fit(r, a, kF13, sw), kF13, c.text);
            x += r.measure(a, kF13).width;
            const std::string of = fit(r, o, fb, sx + sw - x);
            r.drawText({x, ty}, of, fb, c.text);
            x += r.measure(of, fb).width;
            if (x < sx + sw - 10.f) r.drawText({x, ty}, fit(r, z, kF13, sx + sw - x), kF13, c.text);
        }
        // Objet
        r.drawText({colX_[kObjet], ty}, fit(r, l.objectText, kF12, colW_[kObjet] - 8.f), kF12, c.textMuted);
        // Criticite
        const Tint t = levelTint(l.level);
        const float pw = pill(r, colX_[kCrit], y + (kRowH - 22.f) * 0.5f, adm::levelLabel(l.level), gfx::semibold(kF12), t.fg, t.bg, 22.f);
        if (jr.overridden) r.fillRoundedRect({colX_[kCrit] + pw + 4.f, cy - 3.f, 6.f, 6.f}, c.accent, 3.f);
        if (!jr.verified) ui::drawIcon(r, ui::Icon::Error, {colX_[kCrit] + pw + (jr.overridden ? 14.f : 4.f), cy - 7.f, 14.f, 14.f}, c.error);
    }

    static std::string agoOrWhen(Seconds now, Seconds t) { return now - t < 120 && now >= t ? std::string("\xC3\xA0 l'instant") : whenFr(now, t); }

    // --------------------------------------------------------------------- l'etat
    JournalView                 view_;
    std::vector<Line>           lines_;
    std::vector<std::size_t>    shown_;
    std::map<std::string, int>  colorOf_;
    int                         sortCol_{kQuand};
    bool                        sortDesc_{true};
    int                         first_{0}, shownCount_{0}, pageSize_{10};
    std::uint64_t               expanded_{0}, menuLine_{0};
    bool                        revealExpanded_{false}, syncing_{false}, fetching_{false};
    // les widgets
    ui::Button*                 crit_{nullptr};
    ui::Button*                 export_{nullptr};
    ui::Button*                 fetch_{nullptr};
    ui::Button*                 delete_{nullptr};   // 1.48.1 : grise (kNoDelete)
    ui::FilterBar*              bar_{nullptr};                 // 1.47.0 : la barre (chercher, l'admin, le type, la criticite, la periode)
    ui::UnfoldCard*             unfold_{nullptr};              // 1.47.0 : la carte de la ligne depliee
    std::vector<ui::WidgetPtr>  grave_;                        // ... les cartes remplacees (detruites au dessin suivant)
    ui::ListMenu                menuList_;                     // 1.47.0 : le clic droit ouvert
    float                       expandH_{kExpandH};
    int                         cursor_{-1};                   // la ligne cliquee en dernier (->)
    SortHeader*                 sort_[6]{};
    ui::Button*                 prev_{nullptr};
    ui::Button*                 next_{nullptr};
    ui::TableView*              table_{nullptr};
    ui::PopupMenu*              menu_{nullptr};
    // la mise en page
    float                       x0_{0.f}, w_{0.f}, titleY_{0.f}, leadRight_{0.f}, chipsY_{0.f}, levelsY_{0.f}, rowsTop_{0.f}, footY_{0.f};
    float                       colX_[6]{}, colW_[6]{};
    std::vector<float>          rowTops_;
    Rect                        card_{}, expandRect_{};
    std::shared_ptr<bool>       alive_ = std::make_shared<bool>(true);
    core::ConnectionScope       links_;
};

} // namespace

PagePtr makeJournalPage(ShellHost host) { return std::make_unique<JournalPage>(std::move(host)); }

menu::MenuPtr makeCriticalityDialog(ShellHost host, std::function<void()> done) {
    return std::make_unique<CriticalityDialog>(std::move(host), std::move(done));
}

} // namespace license_admin::gui
