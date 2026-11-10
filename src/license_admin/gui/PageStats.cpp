// =============================================================================
//  license_admin/gui/PageStats.cpp - 1.47.0 : LA PAGE << Statistiques >> (sous
//  Pilotage ; la maquette validee, planche 08 : la vue generale)
// -----------------------------------------------------------------------------
//  En tete : << Statistiques >>, les onglets (Vue generale, Ventes, Licences, PC,
//  Mises a jour, Demandes, Admins, Globe), la periode montree (<< 13/08 ->
//  10/11/2026 >>), << Exporter... >> (les chiffres en CSV) et, pour l'admin+,
//  << Relever maintenant >> (le releve du jour : POST /admin/stats/releve).
//  Dessous, la barre commune : la periode (7 j, 30 j, 90 j, 12 mois, tout, deux
//  dates), la comparaison (la periode d'avant, l'an dernier, rien), le pas
//  (jour, semaine, mois) ; << Un clic sur un point filtre la liste
//  correspondante >>.
//  LA VUE GENERALE : les cartes de la planche, dans son ordre, ses mots et ses
//  couleurs - les ventes (montant encaisse, cumule sur l'annee, commandes,
//  panier moyen, ventes par offre, options vendues, licences vendues,
//  remboursements), les licences et les demandes (licences actives, mouvements,
//  echeances, demandes, delai de reponse), les PC (actifs par offre, mouvements,
//  flottantes, Decouvertes). Chaque carte : sa valeur, l'ecart a la comparaison,
//  sa courbe ; la bulle au survol ; un clic sur un point ouvre la liste filtree
//  (Commandes, Licences, Demandes en ligne, Activations).
//  Les onglets Ventes, Licences, PC, Demandes : les memes cartes en plus grand et
//  ce qui se divise (par client, par admin, par offre) ; Admins : ce que chacun a
//  fait et ses heures (a l'heure du PC) ; Mises a jour : la planche 09
//  (PageStatsUpdates.cpp) ; Globe : la planche 11 (PageStatsGlobe.cpp, le globe).
//  LES CHIFFRES : GET /admin/stats/serie (docs/STATISTIQUES.md, par. 14), par
//  runQuiet (jamais sur le fil de l'interface). Un service d'avant la 1.47.0 :
//  un seul avertissement et << Deployer le service >>. Avant le premier releve de
//  la 1.47 (releves_depuis), des chiffres sont reconstitues : c'est dit.
// =============================================================================
#include "StatsKit.hpp"

#include "../../ui/widgets/DatePicker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace license_admin::gui {

namespace {

using namespace statskit;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;
using stats::Compare;
using stats::Step;

constexpr Color kInk = Color::rgb(0x0F172A), kMuted = Color::rgb(0x64748B), kFaint = Color::rgb(0x94A3B8);
constexpr Color kLine = Color::rgb(0xE3E8EF), kSurface2 = Color::rgb(0xF8FAFC), kViolet = Color::rgb(0x6D4AFF);
constexpr Color kPageBg = Color::rgb(0xF4F6FA);
const std::string kNone = "\xE2\x80\x94";

// ============================================================================ les chiffres d'une page
// Les groupes de mesures (une requete chacun) ; leur periode, leur pas, leur comparaison.
struct Group {
    std::string              key;
    std::vector<std::string> measures;
    std::string              split;
    enum class When : std::uint8_t { Period, Deadlines, YearToDate, Daily } when{When::Period};
    bool                     compare{true};
};

const std::vector<Group>& groups() {
    static const std::vector<Group> g = {
        {"principal",
         {"ventes_montant", "ventes_commandes", "ventes_panier", "ventes_remboursements", "ventes_remboursements_nombre", "licences_vendues_en_ligne",
          "licences_signees_main", "licences_actives", "licences_nouvelles", "licences_prolongees", "licences_expirees", "licences_revoquees", "licences_options",
          "demandes_recues", "demandes_traitees", "demandes_attente", "demandes_delai_median", "demandes_delai_moyen", "demandes_acceptees", "demandes_refusees"},
         "", Group::When::Period, true},
        {"pc",
         {"pc_actifs", "pc_actives", "pc_liberes", "pc_retires", "decouvertes_commencees", "decouvertes_converties", "decouvertes_en_cours", "demandes_plus_ancienne"},
         "", Group::When::Period, true},
        {"offre", {"ventes_montant", "pc_actifs", "licences_actives"}, "offre", Group::When::Period, false},
        {"option", {"ventes_montant", "ventes_commandes"}, "option", Group::When::Period, false},
        {"echeances", {"licences_expirees", "licences_echeances_montant"}, "", Group::When::Deadlines, false},
        {"cumul", {"ventes_cumul"}, "", Group::When::YearToDate, true},
        {"flottantes", {"jetons_pic", "jetons_plafond", "licences_au_plafond"}, "", Group::When::Daily, false},
        {"client", {"ventes_montant", "licences_nouvelles"}, "client", Group::When::Period, false},
        {"admin", {"ventes_montant", "licences_nouvelles", "licences_prolongees", "demandes_traitees", "admin_jours", "admin_appels"}, "admin", Group::When::Period, false},
    };
    return g;
}

struct Data {
    std::map<std::string, stats::SeriesReply> replies;
    std::map<std::string, std::string>        errors;
    stats::AdminsReply                        admins;
    bool                                      hasAdmins{false};
    stats::Period                             period;
    Step                                      step{Step::Week};
    Compare                                   compare{Compare::LastYear};
    std::string                               today;
    Seconds                                   now{0};
    [[nodiscard]] const stats::SeriesReply* reply(const std::string& g) const {
        const auto it = replies.find(g);
        return it == replies.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const stats::Series* series(const std::string& g, const std::string& m) const {
        const stats::SeriesReply* r = reply(g);
        return r ? r->find(m) : nullptr;
    }
    [[nodiscard]] std::string error(const std::string& g) const {
        const auto it = errors.find(g);
        return it == errors.end() ? std::string() : it->second;
    }
    // L'ordre et le nom des admins (la reponse de /admin/stats/admins : l'admin+ d'abord).
    [[nodiscard]] int adminOrder(const std::string& id) const {
        for (std::size_t i = 0; i < admins.admins.size(); ++i)
            if (admins.admins[i].id == id) return static_cast<int>(i);
        return -1;
    }
    [[nodiscard]] std::string adminName(const std::string& id) const {
        if (const stats::AdminUsage* a = admins.find(id); a && !a->name.empty()) return a->name;
        return id.empty() ? std::string("sans admin connu") : id;
    }
};

std::string signature(const Choice& c, Seconds now) {
    return std::string(rangeKey(c.range)) + "|" + stats::compareKey(c.compare) + "|" + stats::stepKey(c.step) + "|" + c.from + "|" + c.to + "|" + stats::dayOf(now);
}

stats::SeriesQuery queryFor(const Group& g, const Choice& c, const stats::Period& period, Seconds now) {
    stats::SeriesQuery q;
    q.measures = g.measures;
    q.split = g.split;
    q.step = fitStep(period, c.step);
    q.period = period;
    q.compare = g.compare ? c.compare : Compare::None;
    switch (g.when) {
        case Group::When::Period: break;
        case Group::When::Deadlines:
            q.period = stats::nextDays(now, 90);
            q.step = Step::Week;
            break;
        case Group::When::YearToDate: {
            const std::string today = stats::dayOf(now);
            q.period = {today.substr(0, 4) + "-01-01", today};
            q.step = Step::Week;
            q.compare = c.compare == Compare::None ? Compare::None : Compare::LastYear;
            break;
        }
        case Group::When::Daily:
            q.step = daysOf(period) <= 400 ? Step::Day : Step::Week;
            break;
    }
    return q;
}

// ============================================================================ la grille des cartes
// Des colonnes egales, des rangees (une hauteur au moins) ; a gauche, les bandes des sections (<< VENTES >>). Plus
// haute que la place : elle defile (la molette).
class CardGrid final : public ui::Widget {
public:
    CardGrid(std::string id, int columns, int rows, float minRow, float railW) : ui::Widget(std::move(id)), cols_(columns), rows_(rows), minRow_(minRow), railW_(railW) {}
    template <class T>
    T& put(std::unique_ptr<T> w, int col, int row, int cs = 1, int rs = 1) {
        T& ref = static_cast<T&>(addChild(std::move(w)));
        cells_.push_back({&ref, col, row, cs, rs});
        invalidateLayout();
        return ref;
    }
    void rail(std::string label, int row0, int row1) { rails_.push_back({std::move(label), row0, row1}); }
    [[nodiscard]] float scroll() const noexcept { return scroll_; }
    [[nodiscard]] float contentHeight() const noexcept { return content_; }
    [[nodiscard]] int flowColumns() const noexcept { return flow_; }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float gap = 12.f;
        float x0 = b.x + (railW_ > 0 ? railW_ + gap : 0.f);
        float cw = (b.right() - 10.f - x0 - gap * static_cast<float>(cols_ - 1)) / static_cast<float>(cols_);
        // UNE FENETRE ETROITE (1100 x 700) : des cartes trop etroites pour leurs textes - elles se rangent dans l'ordre de
        // lecture sur moins de colonnes (3, puis 2), sans les bandes des sections ; la grille defile.
        flow_ = 0;
        if (cw < kMinCard && cells_.size() > 1) {
            x0 = b.x;
            for (int n = std::min(cols_ - 1, 3); n >= 1; --n) {
                flow_ = n;
                cw = (b.right() - 10.f - x0 - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
                if (cw >= kMinCard) break;
            }
        }
        if (flow_ > 0) {
            int col = 0, row = 0;
            std::vector<std::pair<int, int>> at;   // (colonne, rangee) de chaque cellule, dans l'ordre de lecture
            std::vector<const Cell*> order;
            for (const auto& c : cells_) order.push_back(&c);
            std::stable_sort(order.begin(), order.end(), [](const Cell* a, const Cell* x) { return a->row != x->row ? a->row < x->row : a->col < x->col; });
            // dans l'ordre de lecture ; une carte large qui ne tient plus sur la rangee laisse sa place a la suivante qui y
            // tient (pas de trou)
            std::vector<const Cell*> todo = order;
            order.clear();
            while (!todo.empty()) {
                std::size_t pick = 0;
                if (col + std::min(todo[0]->cs, flow_) > flow_) {
                    pick = todo.size();
                    for (std::size_t k = 1; k < todo.size(); ++k)
                        if (col + std::min(todo[k]->cs, flow_) <= flow_) {
                            pick = k;
                            break;
                        }
                    if (pick == todo.size()) {
                        col = 0;
                        ++row;
                        pick = 0;
                    }
                }
                const Cell* c = todo[pick];
                todo.erase(todo.begin() + static_cast<std::ptrdiff_t>(pick));
                const int span = std::min(c->cs, flow_);
                order.push_back(c);
                at.push_back({col, row});
                col += span;
            }
            const int rows = row + 1;
            rowH_ = std::max(minRow_, (b.h - 4.f - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows));
            content_ = rowH_ * static_cast<float>(rows) + gap * static_cast<float>(rows - 1) + 4.f;
            scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - b.h));
            for (std::size_t i = 0; i < order.size(); ++i) {
                const int span = std::min(order[i]->cs, flow_);
                const float x = x0 + static_cast<float>(at[i].first) * (cw + gap);
                const float y = b.y + 2.f - scroll_ + static_cast<float>(at[i].second) * (rowH_ + gap);
                order[i]->w->setBounds({x, y, cw * static_cast<float>(span) + gap * static_cast<float>(span - 1), rowH_});
            }
            return;
        }
        const float fit = (b.h - 4.f - gap * static_cast<float>(rows_ - 1)) / static_cast<float>(rows_);
        rowH_ = std::max(minRow_, fit);
        content_ = rowH_ * static_cast<float>(rows_) + gap * static_cast<float>(rows_ - 1) + 4.f;
        scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, content_ - b.h));
        for (const auto& c : cells_) {
            const float x = x0 + static_cast<float>(c.col) * (cw + gap);
            const float y = b.y + 2.f - scroll_ + static_cast<float>(c.row) * (rowH_ + gap);
            const float w = cw * static_cast<float>(c.cs) + gap * static_cast<float>(c.cs - 1);
            const float h = rowH_ * static_cast<float>(c.rs) + gap * static_cast<float>(c.rs - 1);
            c.w->setBounds({x, y, w, h});
        }
    }
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        const float gap = 12.f;
        for (const auto& r : flow_ > 0 ? std::vector<Rail>{} : rails_) {
            const float y0 = b.y + 2.f - scroll_ + static_cast<float>(r.row0) * (rowH_ + gap);
            const float y1 = b.y + 2.f - scroll_ + static_cast<float>(r.row1 + 1) * (rowH_ + gap) - gap;
            const Rect rr{b.x, y0, railW_, y1 - y0};
            ctx.r.fillRoundedRect(rr, kLine, 8.f);
            ctx.r.fillRoundedRect(rr.inset(1.f, 1.f), kSurface2, 7.f);
            const FontId f = gfx::semibold(FontId{10});
            std::string up;
            for (const char ch : r.label) up += (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 32) : ch;
            const float tw = ctx.r.measure(up, f).width + 0.12f * 10.f * static_cast<float>(up.size());
            const gfx::Point c{rr.x + rr.w * 0.5f, rr.y + rr.h * 0.5f};
            ctx.r.drawTextRotated({c.x - tw * 0.5f, c.y - ctx.r.lineHeight(f) * 0.5f}, up, f, kFaint, -90.f, c);
        }
        if (content_ > b.h + 1.f) {   // la barre de defilement, discrete
            const float k = b.h / content_;
            const float th = std::max(30.f, b.h * k);
            const float ty = b.y + (b.h - th) * (scroll_ / std::max(1.f, content_ - b.h));
            ctx.r.fillRoundedRect({b.right() - 6.f, ty, 4.f, th}, kFaint.withAlpha(110), 2.f);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* w = std::get_if<ui::MouseWheel>(&ev)) {
            if (!bounds().contains(w->pos) || content_ <= bounds().h) return ui::EventResult::Ignored;
            scroll_ = std::clamp(scroll_ - w->dy * 48.f, 0.f, content_ - bounds().h);
            invalidateLayout();
            invalidate();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    struct Cell {
        ui::Widget* w;
        int         col, row, cs, rs;
    };
    struct Rail {
        std::string label;
        int         row0, row1;
    };
    static constexpr float kMinCard = 172.f;   // une carte plus etroite ne tient pas sa ligne de valeur
    int               cols_, rows_;
    int               flow_{0};   // 0 : la grille de la planche ; n : rangees dans l'ordre de lecture sur n colonnes
    float             minRow_, railW_;
    float             rowH_{160.f}, content_{0.f}, scroll_{0.f};
    std::vector<Cell> cells_;
    std::vector<Rail> rails_;
};

// ============================================================================ les cartes
// Une carte : son id, sa carte et son graphique (un TimeChart ou un BarList), et ce qui la remplit.
struct Slot {
    std::string   id;
    FitCard*       card{nullptr};
    ch::TimeChart* time{nullptr};
    FitBarList*    bars{nullptr};
    std::string   group;   // le groupe dont viennent ses periodes (le clic)
    std::function<void(Slot&, const Data&)> fill;
};

ch::Series serie(std::string key, std::string name, std::vector<double> v) {
    ch::Series s;
    s.key = std::move(key);
    s.name = std::move(name);
    s.values = std::move(v);
    return s;
}
ch::Series compareSerie(std::string name, std::vector<double> v) {
    ch::Series s = serie("cmp", std::move(name), std::move(v));
    s.role = ch::Role::Compare;
    return s;
}
ch::Format eurosF() {
    return [](double v) { return ch::fr::euros(v); };
}
ch::Format intF() {
    return [](double v) { return ch::fr::integer(v); };
}
ch::Format hoursF() {
    return [](double v) { return ch::fr::hours(v); };
}

// Les categories et les libelles d'une reponse.
ch::TimeChartData baseOf(const stats::SeriesReply& r, const Data& d, bool level = false) {
    ch::TimeChartData t;
    t.x = axisOf(r.buckets, r.step, level);
    t.xTicks = ticksOf(t.x.size());
    t.partialFrom = level ? -1 : partialFrom(r.buckets, r.step, d.today);
    return t;
}

// Une borne basse ronde pour une courbe d'un niveau (la maquette : 80 sous 109..128).
double niceFloor(double lo) {
    if (!(lo > 0)) return 0;
    const double step = ch::niceStep(lo / 4.0);
    return std::max(0.0, std::floor(lo * 0.8 / step) * step);
}

// La carte sans chiffres : la raison (un droit qui manque, une erreur), ou rien encore.
bool missing(Slot& s, const Data& d, const std::string& group, const stats::Series* ser, const std::string& waiting = {}) {
    std::string why;
    if (const std::string e = d.error(group); !e.empty()) why = e;
    else if (!d.reply(group)) why = waiting.empty() ? std::string("Lecture des chiffres\xE2\x80\xA6") : waiting;
    else if (!ser) why = "Le service n'a pas rendu cette mesure.";
    else if (ser->refused) {
        if (!ser->rights.empty()) {
            why = "Il te manque le droit \xC2\xAB " + ser->rights.front() + " \xC2\xBB pour ces chiffres.";
        } else {
            why = "Cette mesure ne se filtre pas ainsi.";
        }
    }
    if (why.empty()) return false;
    s.card->setText(kNone);
    if (s.time) {
        ch::TimeChartData t;
        t.emptyText = why;
        s.time->setData(std::move(t));
    }
    if (s.bars) {
        ch::BarListData b;
        b.emptyText = why;
        s.bars->setData(std::move(b));
    }
    return true;
}

std::string euroText(double cents) { return stats::euros(cents); }
// Les valeurs a cote en plus court (une carte etroite) : sans les parts en %, << Decouverte >> sans << (l'essai) >>.
std::vector<ch::SideRow> narrowSide(std::vector<ch::SideRow> rows) {
    for (auto& r : rows) {
        r.faint.clear();
        if (const auto p = r.label.find(" ("); p != std::string::npos) r.label = r.label.substr(0, p);
    }
    return rows;
}
// << contre 23 000 \xE2\x82\xAC l'an dernier >>, sinon << contre 23 000 \xE2\x82\xAC >> (la place manque).
std::vector<std::string> against(Compare c, const std::string& value) { return {againstText(c, value), "contre " + value}; }
std::string intText(double v) { return stats::isNumber(v) ? ch::fr::integer(v) : kNone; }

// ---- les cartes de la planche 08 ----
void fillMontant(Slot& s, const Data& d) {
    const auto* m = d.series("principal", "ventes_montant");
    if (missing(s, d, "principal", m)) return;
    const auto& r = *d.reply("principal");
    ch::TimeChartData t = baseOf(r, d);
    t.kind = ch::Kind::Bars;
    t.series.push_back(serie("un", mainName(d.compare, r.period), valuesOf(m->points, 0.01)));
    if (m->hasComparison()) t.series.push_back(compareSerie(compareName(d.compare, true, r.period), valuesOf(m->before, 0.01)));
    t.valueFormat = eurosF();
    const auto* cmd = d.series("principal", "ventes_commandes");
    const auto* pan = d.series("principal", "ventes_panier");
    if (cmd && pan && !cmd->refused) {
        const std::vector<double> c = cmd->points, p = pan->points;
        t.tipExtra = [c, p](int i) {
            const auto k = static_cast<std::size_t>(i);
            if (k >= c.size()) return std::string();
            std::string out = ch::fr::integer(c[k]) + (c[k] > 1 ? " commandes" : " commande");
            if (k < p.size() && stats::isNumber(p[k])) out += " \xC2\xB7 panier " + stats::euros(p[k]);
            return out;
        };
    }
    t.clickHint = "Clic : ces commandes";
    if (const char* e = std::getenv("XPG_STATS_ESSAI"); e && std::string(e) == "1" && hooked() && t.x.size() > 3) t.exampleTip = static_cast<int>(t.x.size()) - 3;
    s.time->setData(std::move(t));
    s.card->setSmalls("Montant encaiss\xC3\xA9", stepSmall(r.step));
    s.card->setText(euroText(m->total), m->hasComparison() ? ch::deltaOf(m->total, m->beforeTotal) : ch::Delta{},
                     m->hasComparison() ? against(d.compare, euroText(m->beforeTotal)) : std::vector<std::string>{});
}

void fillCumul(Slot& s, const Data& d) {
    const auto* m = d.series("cumul", "ventes_cumul");
    if (missing(s, d, "cumul", m)) return;
    const auto& r = *d.reply("cumul");
    ch::TimeChartData t;
    t.x = axisOf(r.buckets, r.step, true);
    // les mois en bas (janv., avr., juil., oct.)
    int lastMonth = 0;
    for (std::size_t i = 0; i < r.buckets.size(); ++i) {
        const ch::fr::Day dd = dayNumber(r.buckets[i].from);
        int y = 0, mo = 0, dm = 0;
        ch::fr::civil(dd, y, mo, dm);
        t.x[i].label = ch::fr::monthShort(mo);
        t.x[i].tip = "Au " + ddmm(r.buckets[i].to);
        if (mo != lastMonth && (mo == 1 || mo == 4 || mo == 7 || mo == 10)) t.xTicks.push_back({static_cast<int>(i), 0});
        lastMonth = mo;
    }
    ch::Series a = serie("un", r.period.to.substr(0, 4), valuesOf(m->points, 0.01));
    a.area = true;
    a.endDot = true;
    t.series.push_back(a);
    if (m->hasComparison()) {
        const int y = std::atoi(r.period.to.substr(0, 4).c_str()) - 1;
        t.series.push_back(compareSerie(std::to_string(y), valuesOf(m->before, 0.01)));
    }
    t.yFormat = [](double v) { return v ? ch::fr::compact(v) : std::string("0"); };
    t.valueFormat = eurosF();
    t.yTickCount = 2;
    if (!a.values.empty() && stats::isNumber(a.values.back()))
        t.notes.push_back({static_cast<int>(a.values.size()) - 1, a.values.back(), ch::fr::eurosCompact(a.values.back()), ch::Note::Anchor::End, -10.f});
    t.clickHint = "Clic : les commandes jusqu'\xC3\xA0 ce jour";
    s.time->setData(std::move(t));
    s.card->setText(euroText(m->total), m->hasComparison() ? ch::deltaOf(m->total, m->beforeTotal) : ch::Delta{}, "au " + ddmm(r.period.to));
}

void fillCommandes(Slot& s, const Data& d) {
    const auto* m = d.series("principal", "ventes_commandes");
    if (missing(s, d, "principal", m)) return;
    const auto& r = *d.reply("principal");
    ch::TimeChartData t = baseOf(r, d);
    t.kind = ch::Kind::Bars;
    t.series.push_back(serie("un", mainName(d.compare, r.period), valuesOf(m->points)));
    if (m->hasComparison()) t.series.push_back(compareSerie(compareName(d.compare), valuesOf(m->before)));
    t.yTickCount = 2;
    t.tipForward = true;
    t.clickHint = "Clic : ces commandes";
    s.time->setData(std::move(t));
    s.card->setText(intText(m->total), m->hasComparison() ? ch::deltaOf(m->total, m->beforeTotal) : ch::Delta{},
                     m->hasComparison() ? "contre " + intText(m->beforeTotal) : std::string());
}

void fillPanier(Slot& s, const Data& d) {
    const auto* m = d.series("principal", "ventes_panier");
    if (missing(s, d, "principal", m)) return;
    const auto& r = *d.reply("principal");
    ch::TimeChartData t = baseOf(r, d);
    t.lineInBands = true;
    std::vector<double> v = valuesOf(m->points, 0.01);
    const int partial = partialFrom(r.buckets, r.step, d.today);
    if (partial >= 0 && r.step != Step::Day && static_cast<std::size_t>(partial) < v.size()) v[static_cast<std::size_t>(partial)] = ch::kNone;   // pas de panier qui tienne
    ch::Series a = serie("un", mainName(d.compare, r.period), v);
    a.endDot = true;
    t.series.push_back(a);
    if (m->hasComparison()) t.series.push_back(compareSerie(compareName(d.compare), valuesOf(m->before, 0.01)));
    t.partialFrom = -1;
    t.valueFormat = eurosF();
    t.yTickCount = 2;
    t.tipForward = true;
    t.clickHint = "Clic : les commandes de la p\xC3\xA9riode";
    s.time->setData(std::move(t));
    s.card->setText(euroText(m->total), m->hasComparison() ? ch::deltaOf(m->total, m->beforeTotal) : ch::Delta{},
                     m->hasComparison() ? "contre " + euroText(m->beforeTotal) : std::string());
}

const std::vector<std::string>& offerOrder() {
    static const std::vector<std::string> o = {"PERSONAL", "PROFESSIONAL", "ENTERPRISE", "options"};
    return o;
}
// Les parts dans l'ordre fixe des offres, puis les autres (la plus grande d'abord).
std::vector<const stats::Part*> orderedParts(const stats::Series& s) {
    std::vector<const stats::Part*> out;
    for (const auto& k : offerOrder())
        if (const stats::Part* p = s.part(k)) out.push_back(p);
    for (const auto& p : s.parts)
        if (std::find(out.begin(), out.end(), &p) == out.end()) out.push_back(&p);
    return out;
}

void fillParOffre(Slot& s, const Data& d) {
    const auto* m = d.series("offre", "ventes_montant");
    if (missing(s, d, "offre", m)) return;
    const auto& r = *d.reply("offre");
    ch::TimeChartData t = baseOf(r, d);
    t.kind = ch::Kind::Stacked;
    std::vector<ch::SideRow> side;
    const double tot = m->total;
    int offers = 0;
    for (const stats::Part* p : orderedParts(*m)) {
        if (p->total <= 0) continue;
        const std::string label = partLabel("offre", p->key);
        t.series.push_back(serie(offerColorKey(p->key), label, valuesOf(p->points, 0.01)));
        side.push_back({label, stats::euros(p->total), tot > 0 ? ch::fr::percent(p->total * 100.0 / tot) : std::string(),
                        ch::ColorKeys::standard().colorOf(offerColorKey(p->key)), false, false});
        ++offers;
    }
    side.push_back({"D\xC3\xA9" "couverte (l'essai)", "gratuite", "", {0, 0, 0, 0}, false, true});
    t.legend = ch::LegendMode::None;
    t.valueFormat = eurosF();
    t.clickHint = "Clic : les commandes de cette offre";
    s.time->setData(std::move(t));
    s.card->setSmalls("Ventes par offre", "montant " + stepSmall(r.step));
    s.card->setSideRows(side, narrowSide(side));
    const bool withOptions = m->part("options") != nullptr;
    s.card->setText(euroText(tot), {},
                     std::to_string(offers - (withOptions ? 1 : 0)) + (offers - (withOptions ? 1 : 0) > 1 ? " offres" : " offre") + (withOptions ? " et options" : ""));
}

void fillOptions(Slot& s, const Data& d) {
    const auto* m = d.series("option", "ventes_montant");
    if (missing(s, d, "option", m)) return;
    const auto* c = d.series("option", "ventes_commandes");
    ch::BarListData b;
    b.mode = ch::BarMode::Track;
    b.labelsAbove = true;   // la planche : le libelle et le montant au-dessus de la barre (jamais coupes)
    b.color = ch::colors::kOptions;
    b.format = eurosF();
    double sum = 0, orders = 0, max = 0;
    for (const auto& p : m->parts) {
        if (p.key.empty() || p.key == "\xE2\x80\xA6" || p.total <= 0) continue;
        const stats::Part* pc = c ? c->part(p.key) : nullptr;
        ch::BarRow row;
        row.label = partLabel("option", p.key);
        row.value = p.total / 100.0;
        row.extra = pc ? ch::fr::integer(pc->total) + " cmd" : std::string();
        b.rows.push_back(row);
        sum += p.total;
        orders += pc ? pc->total : 0;
        max = std::max(max, p.total / 100.0);
    }
    std::sort(b.rows.begin(), b.rows.end(), [](const auto& x, const auto& y) { return x.value > y.value; });
    b.maxValue = max;
    b.clickHint = "Clic : les commandes de cette option";
    b.emptyText = "Aucune option vendue sur la p\xC3\xA9riode";
    s.bars->setData(std::move(b));
    s.card->setText(euroText(sum), {}, ch::fr::integer(orders) + (orders > 1 ? " commandes" : " commande"));
}

void fillVendues(Slot& s, const Data& d) {
    const auto* on = d.series("principal", "licences_vendues_en_ligne");
    const auto* hand = d.series("principal", "licences_signees_main");
    if (missing(s, d, "principal", on) || missing(s, d, "principal", hand)) return;
    const auto& r = *d.reply("principal");
    ch::TimeChartData t = baseOf(r, d);
    t.kind = ch::Kind::Stacked;
    ch::Series a = serie("un", "en ligne", valuesOf(on->points)), m = serie("deux", "sign\xC3\xA9" "es \xC3\xA0 la main", valuesOf(hand->points));
    a.legendValue = ch::fr::integer(on->total);
    m.legendValue = ch::fr::integer(hand->total);
    t.series = {a, m};
    t.yTickCount = 2;
    t.tipForward = true;
    t.clickHint = "Clic : ces licences";
    s.time->setData(std::move(t));
    const double tot = on->total + hand->total, before = on->beforeTotal + hand->beforeTotal;
    s.card->setText(intText(tot), on->hasComparison() ? ch::deltaOf(tot, before) : ch::Delta{},
                     tot > 0 ? ch::fr::percent(on->total * 100.0 / tot) + " en ligne" : std::string());
}

void fillRemboursements(Slot& s, const Data& d) {
    const auto* m = d.series("principal", "ventes_remboursements");
    if (missing(s, d, "principal", m)) return;
    const auto* n = d.series("principal", "ventes_remboursements_nombre");
    const auto& r = *d.reply("principal");
    ch::TimeChartData t = baseOf(r, d);
    t.kind = ch::Kind::Bars;
    ch::Series a = serie("un", "rembours\xC3\xA9", valuesOf(m->points, 0.01));
    a.zeroInTip = true;
    t.series = {a};
    t.valueFormat = eurosF();
    t.yTickCount = 2;
    t.clickHint = "Clic : ces remboursements";
    t.emptyText = "Aucun remboursement sur la p\xC3\xA9riode";
    s.time->setData(std::move(t));
    const double nb = n && !n->refused ? n->total : 0;
    s.card->setText(euroText(m->total), m->hasComparison() ? ch::deltaOf(m->total, m->beforeTotal, false) : ch::Delta{},
                     ch::fr::integer(nb) + (nb > 1 ? " commandes" : " commande"));
}

void fillActives(Slot& s, const Data& d) {
    const auto* m = d.series("principal", "licences_actives");
    if (missing(s, d, "principal", m)) return;
    const auto& r = *d.reply("principal");
    ch::TimeChartData t = baseOf(r, d, true);
    ch::Series a = serie("un", mainName(d.compare, r.period), valuesOf(m->points));
    a.endDot = true;
    t.series.push_back(a);
    double lo = 1e18;
    for (const double v : m->points)
        if (stats::isNumber(v)) lo = std::min(lo, v);
    if (m->hasComparison()) {
        t.series.push_back(compareSerie(compareName(d.compare), valuesOf(m->before)));
        for (const double v : m->before)
            if (stats::isNumber(v)) lo = std::min(lo, v);
    }
    if (lo < 1e17) t.yMin = niceFloor(lo);
    t.clickHint = "Clic : les licences actives";
    s.time->setData(std::move(t));
    s.card->setText(intText(m->total), m->hasComparison() ? ch::deltaOf(m->total, m->beforeTotal) : ch::Delta{},
                     m->hasComparison() ? "contre " + intText(m->beforeTotal) : std::string());
}

void fillMouvements(Slot& s, const Data& d) {
    const auto* nw = d.series("principal", "licences_nouvelles");
    const auto* pr = d.series("principal", "licences_prolongees");
    const auto* ex = d.series("principal", "licences_expirees");
    const auto* rv = d.series("principal", "licences_revoquees");
    if (missing(s, d, "principal", nw) || missing(s, d, "principal", pr) || missing(s, d, "principal", ex) || missing(s, d, "principal", rv)) return;
    const auto& r = *d.reply("principal");
    ch::TimeChartData t = baseOf(r, d);
    t.kind = ch::Kind::Diverging;
    ch::Series n = serie("up:0", "nouvelles", valuesOf(nw->points)), p = serie("up:1", "prolong\xC3\xA9" "es", valuesOf(pr->points));
    ch::Series e = serie("down:1", "expir\xC3\xA9" "es", valuesOf(ex->points)), v = serie("down:0", "r\xC3\xA9voqu\xC3\xA9" "es", valuesOf(rv->points));
    e.below = v.below = true;
    n.legendValue = ch::fr::integer(nw->total);
    p.legendValue = ch::fr::integer(pr->total);
    e.legendValue = ch::fr::integer(ex->total);
    v.legendValue = ch::fr::integer(rv->total);
    t.series = {n, p, e, v};
    t.tipForward = true;
    t.partialFrom = -1;
    t.clickHint = "Clic : ces licences";
    s.time->setData(std::move(t));
    const double net = nw->total - ex->total - rv->total;
    s.card->setText(ch::fr::signedInteger(net) + " net", {}, "entr\xC3\xA9" "es \xE2\x88\x92 sorties");
}

void fillEcheances(Slot& s, const Data& d) {
    const auto* m = d.series("echeances", "licences_expirees");
    if (missing(s, d, "echeances", m)) return;
    const auto* eur = d.series("echeances", "licences_echeances_montant");
    const auto& r = *d.reply("echeances");
    ch::TimeChartData t;
    t.x = axisOf(r.buckets, r.step);
    t.xTicks = ticksOf(t.x.size());
    t.kind = ch::Kind::Stacked;
    std::vector<double> soon(m->points.size(), ch::kNone), later(m->points.size(), ch::kNone);
    double nSoon = 0, nLater = 0;
    const ch::fr::Day limit = dayNumber(d.today) + 30;
    for (std::size_t i = 0; i < m->points.size() && i < r.buckets.size(); ++i) {
        const double v = stats::isNumber(m->points[i]) ? m->points[i] : 0;
        if (dayNumber(r.buckets[i].from) < limit) {
            soon[i] = v;
            nSoon += v;
        } else {
            later[i] = v;
            nLater += v;
        }
    }
    ch::Series a = serie("un", "dans les 30 jours", soon), l = serie("autres", "ensuite", later);
    a.legendValue = ch::fr::integer(nSoon);
    l.legendValue = ch::fr::integer(nLater);
    a.zeroInTip = true;
    t.series = {a, l};
    t.yTickCount = 2;
    t.clickHint = "Clic : ces licences, \xC3\xA0 renouveler";
    t.emptyText = "Aucune licence ne finit dans les 90 jours";
    s.time->setData(std::move(t));
    s.card->setText(intText(m->total), {}, eur && !eur->refused ? stats::euros(eur->total) + " \xC3\xA0 renouveler" : std::string());
}

void fillDemandes(Slot& s, const Data& d) {
    const auto* in = d.series("principal", "demandes_recues");
    const auto* done = d.series("principal", "demandes_traitees");
    if (missing(s, d, "principal", in) || missing(s, d, "principal", done)) return;
    const auto* wait = d.series("principal", "demandes_attente");
    const auto& r = *d.reply("principal");
    ch::TimeChartData t = baseOf(r, d);
    std::vector<double> a = valuesOf(in->points), b = valuesOf(done->points);
    const int partial = partialFrom(r.buckets, r.step, d.today);
    if (partial >= 0 && r.step != Step::Day && static_cast<std::size_t>(partial) < a.size()) {   // la courbe s'arrete a la derniere periode entiere
        a[static_cast<std::size_t>(partial)] = ch::kNone;
        b[static_cast<std::size_t>(partial)] = ch::kNone;
    }
    ch::Series x = serie("un", "re\xC3\xA7ues", a), y = serie("deux", "trait\xC3\xA9" "es", b);
    y.legendValue = ch::fr::integer(done->total);
    y.endDot = true;
    t.series = {x, y};
    t.partialFrom = -1;
    t.yTickCount = 2;
    t.tipForward = true;
    t.clickHint = "Clic : ces demandes";
    s.time->setData(std::move(t));
    s.card->setText(intText(in->total) + " re\xC3\xA7ues", in->hasComparison() ? ch::deltaOf(in->total, in->beforeTotal) : ch::Delta{},
                     wait && !wait->refused && stats::isNumber(wait->total) ? ch::fr::integer(wait->total) + " en attente" : std::string());
}

void fillDelai(Slot& s, const Data& d) {
    const auto* m = d.series("principal", "demandes_delai_median");
    if (missing(s, d, "principal", m)) return;
    const auto* mean = d.series("principal", "demandes_delai_moyen");
    const auto& r = *d.reply("principal");
    ch::TimeChartData t = baseOf(r, d);
    ch::Series a = serie("un", mainName(d.compare, r.period), valuesOf(m->points, 1.0 / 3600.0));
    a.endDot = true;
    t.series.push_back(a);
    if (m->hasComparison()) t.series.push_back(compareSerie(compareName(d.compare), valuesOf(m->before, 1.0 / 3600.0)));
    t.yFormat = [](double v) { return ch::fr::integer(v) + ch::fr::kNbsp + "h"; };
    t.valueFormat = hoursF();
    t.yTickCount = 2;
    t.partialFrom = -1;
    t.clickHint = "Clic : les demandes de la p\xC3\xA9riode";
    s.time->setData(std::move(t));
    s.card->setText(stats::duration(m->total), m->hasComparison() ? ch::deltaOf(m->total, m->beforeTotal, false) : ch::Delta{},
                     mean && !mean->refused && stats::isNumber(mean->total) ? "moyen " + stats::duration(mean->total) : std::string());
}

void fillPcActifs(Slot& s, const Data& d) {
    const auto* m = d.series("offre", "pc_actifs");
    if (missing(s, d, "offre", m)) return;
    const auto* total = d.series("pc", "pc_actifs");
    const auto* disc = d.series("pc", "decouvertes_en_cours");
    const auto& r = *d.reply("offre");
    ch::TimeChartData t = baseOf(r, d, true);
    t.kind = ch::Kind::Stacked;
    std::vector<ch::SideRow> side;
    double all = 0;
    for (const stats::Part* p : orderedParts(*m)) {
        const std::string label = partLabel("offre", p->key);
        t.series.push_back(serie(offerColorKey(p->key), label, valuesOf(p->points)));
        const double last = lastOf(p->points);
        side.push_back({label, intText(last), "", ch::ColorKeys::standard().colorOf(offerColorKey(p->key)), false, false});
        all += stats::isNumber(last) ? last : 0;
    }
    if (disc && !disc->refused && disc->points.size() == r.buckets.size()) {
        t.series.push_back(serie("offre:DECOUVERTE", "D\xC3\xA9" "couverte", valuesOf(disc->points)));
        const double last = lastOf(disc->points);
        side.push_back({"D\xC3\xA9" "couverte", intText(last), "", ch::colors::kDiscovery, false, false});
        all += stats::isNumber(last) ? last : 0;
    }
    side.push_back({"Tous", intText(all), "", {0, 0, 0, 0}, false, true});
    t.legend = ch::LegendMode::None;
    t.clickHint = "Clic : ces PC (Activations)";
    s.time->setData(std::move(t));
    s.card->setSmalls("PC actifs", {"par offre, \xC3\xA0 la fin de chaque " + std::string(stepLabel(r.step)), "par offre"});
    s.card->setSideRows(side, narrowSide(side));
    if (total && !total->refused && total->hasComparison()) {
        const double before = total->beforeTotal + (disc && disc->hasComparison() ? disc->beforeTotal : 0);
        s.card->setText(intText(all), ch::deltaOf(all, before), against(d.compare, intText(before)));
    } else {
        s.card->setText(intText(all));
    }
}

void fillMouvementsPc(Slot& s, const Data& d) {
    const auto* ac = d.series("pc", "pc_actives");
    const auto* li = d.series("pc", "pc_liberes");
    const auto* re = d.series("pc", "pc_retires");
    if (missing(s, d, "pc", ac) || missing(s, d, "pc", li) || missing(s, d, "pc", re)) return;
    const auto* lvl = d.series("pc", "pc_actifs");
    const auto& r = *d.reply("pc");
    ch::TimeChartData t = baseOf(r, d);
    t.kind = ch::Kind::Diverging;
    ch::Series a = serie("up:0", "activ\xC3\xA9s", valuesOf(ac->points)), l = serie("down:1", "lib\xC3\xA9r\xC3\xA9s", valuesOf(li->points)),
               x = serie("down:0", "retir\xC3\xA9s", valuesOf(re->points));
    l.below = x.below = true;
    a.legendValue = ch::fr::integer(ac->total);
    l.legendValue = ch::fr::integer(li->total);
    x.legendValue = ch::fr::integer(re->total);
    t.series = {a, l, x};
    t.tipForward = true;
    t.partialFrom = -1;
    t.clickHint = "Clic : ces PC";
    s.time->setData(std::move(t));
    const double net = ac->total - li->total - re->total;
    std::string cmp;
    if (lvl && !lvl->refused && stats::isNumber(lvl->total)) cmp = ch::fr::integer(lvl->total - net) + " " + ch::fr::kArrow + " " + ch::fr::integer(lvl->total);
    s.card->setText(ch::fr::signedInteger(net) + " net", {}, cmp);
}

void fillFlottantes(Slot& s, const Data& d) {
    const auto* m = d.series("flottantes", "jetons_pic");
    if (missing(s, d, "flottantes", m)) return;
    const auto* cap = d.series("flottantes", "jetons_plafond");
    const auto* full = d.series("flottantes", "licences_au_plafond");
    const auto& r = *d.reply("flottantes");
    ch::TimeChartData t = baseOf(r, d);
    t.partialFrom = -1;
    ch::Series a = serie("un", "jetons pris en m\xC3\xAAme temps", valuesOf(m->points));
    a.area = true;
    a.width = 1.5f;
    t.series = {a};
    const double ceiling = cap && !cap->refused ? lastOf(cap->points) : ch::kNone;
    if (stats::isNumber(ceiling) && ceiling > 0) {
        t.refs.push_back({ceiling, "plafond", true, {0, 0, 0, 0}});
        t.yMax = ceiling;
    }
    t.yTickCount = 2;
    t.clickHint = "Clic : les sessions de ce jour";
    t.emptyText = "Aucune licence flottante sur la p\xC3\xA9riode";
    s.time->setData(std::move(t));
    double peak = 0;
    for (const double v : m->points)
        if (stats::isNumber(v)) peak = std::max(peak, v);
    const double days = full && !full->refused ? full->total : 0;
    const std::string n = ch::fr::integer(days);
    s.card->setText(ch::fr::integer(peak) + (stats::isNumber(ceiling) ? " / " + ch::fr::integer(ceiling) + " jetons" : std::string()), {},
                    days > 0 ? std::vector<std::string>{"\xE2\x9A\xA0 " + n + (days > 1 ? " jours au plafond" : " jour au plafond"), "\xE2\x9A\xA0 " + n + "\xC2\xA0j au plafond",
                                                        "\xE2\x9A\xA0 " + n + "\xC2\xA0j"}
                             : std::vector<std::string>{"jamais au plafond"});
}

void fillDecouvertes(Slot& s, const Data& d) {
    const auto* st = d.series("pc", "decouvertes_commencees");
    const auto* cv = d.series("pc", "decouvertes_converties");
    if (missing(s, d, "pc", st) || missing(s, d, "pc", cv)) return;
    const auto& r = *d.reply("pc");
    ch::TimeChartData t = baseOf(r, d);
    t.kind = ch::Kind::Grouped;
    ch::Series a = serie("un", "commenc\xC3\xA9" "es", valuesOf(st->points)), c = serie("deux", "converties", valuesOf(cv->points));
    a.legendValue = ch::fr::integer(st->total);
    c.legendValue = ch::fr::integer(cv->total);
    a.zeroInTip = c.zeroInTip = true;
    t.series = {a, c};
    t.tipForward = true;
    t.yTickCount = 2;
    t.barMax = 7.f;
    t.clickHint = "Clic : ces D\xC3\xA9" "couvertes";
    s.time->setData(std::move(t));
    const double pct = st->total > 0 ? cv->total * 100.0 / st->total : ch::kNone;
    ch::Delta delta;
    if (st->hasComparison() && st->beforeTotal > 0 && stats::isNumber(pct)) {
        const double before = cv->beforeTotal * 100.0 / st->beforeTotal;
        const double diff = std::round(pct) - std::round(before);
        delta = {ch::fr::signedPoints(diff), diff > 0 ? 1 : diff < 0 ? -1 : 0, diff > 0 ? 1 : diff < 0 ? -1 : 0};
    }
    s.card->setText(stats::isNumber(pct) ? ch::fr::percent(pct) + " converties" : kNone, delta);
}

// ---- les cartes des onglets ----
// Une mesure divisee par admin : empilee, chacun sa couleur (l'ordre de la reponse des admins : l'admin+ d'abord).
void fillByAdmin(Slot& s, const Data& d, const std::string& measure, double scale, bool euros, const std::string& clickHint) {
    const auto* m = d.series("admin", measure);
    if (missing(s, d, "admin", m)) return;
    const auto& r = *d.reply("admin");
    ch::TimeChartData t = baseOf(r, d);
    t.kind = ch::Kind::Stacked;
    std::vector<const stats::Part*> parts;
    for (const auto& p : m->parts) parts.push_back(&p);
    std::sort(parts.begin(), parts.end(), [&d](const stats::Part* a, const stats::Part* b) {
        const int x = d.adminOrder(a->key), y = d.adminOrder(b->key);
        return (x < 0 ? 99 : x) < (y < 0 ? 99 : y);
    });
    for (const stats::Part* p : parts) {
        if (p->total <= 0) continue;
        const int order = d.adminOrder(p->key);
        ch::Series x = serie(order >= 0 ? "admin:" + std::to_string(std::min(order, 3)) : "cmp", d.adminName(p->key), valuesOf(p->points, scale));
        x.legendValue = euros ? stats::euros(p->total) : ch::fr::integer(p->total);
        t.series.push_back(x);
    }
    t.legend = ch::LegendMode::Top;
    if (euros) t.valueFormat = eurosF();
    t.tipForward = true;
    t.clickHint = clickHint;
    const std::size_t admins = t.series.size();
    s.time->setData(std::move(t));
    s.card->setText(euros ? stats::euros(m->total) : intText(m->total), {}, std::to_string(admins) + (admins > 1 ? " admins" : " admin"));
}

void fillTopClients(Slot& s, const Data& d) {
    const auto* m = d.series("client", "ventes_montant");
    if (missing(s, d, "client", m)) return;
    ch::BarListData b;
    b.mode = ch::BarMode::Ranking;
    b.format = eurosF();
    double sum = 0;
    for (const auto& p : m->parts) {
        if (p.total <= 0 || p.key.empty() || p.key == "\xE2\x80\xA6") continue;
        if (b.rows.size() >= 8) break;
        ch::BarRow row;
        row.label = p.key;
        row.value = p.total / 100.0;
        row.extra = m->total > 0 ? ch::fr::percent(p.total * 100.0 / m->total) : std::string();
        b.rows.push_back(row);
        sum += p.total;
    }
    b.color = ch::colors::kOne;
    b.labelsAbove = true;   // le nom entier du client, au-dessus de sa barre
    b.clickHint = "Clic : ses commandes";
    b.emptyText = "Aucune vente sur la p\xC3\xA9riode";
    s.bars->setData(std::move(b));
    s.card->setText(euroText(m->total), {}, "les " + std::to_string(std::min<std::size_t>(8, m->parts.size())) + " premiers clients");
}

void fillSimpleLine(Slot& s, const Data& d, const std::string& group, const std::string& measure, double scale, const ch::Format& valueF, bool level,
                    const std::string& clickHint, std::function<std::string(double)> valueText) {
    const auto* m = d.series(group, measure);
    if (missing(s, d, group, m)) return;
    const auto& r = *d.reply(group);
    ch::TimeChartData t = baseOf(r, d, level);
    ch::Series a = serie("un", mainName(d.compare, r.period), valuesOf(m->points, scale));
    a.endDot = true;
    t.series.push_back(a);
    if (m->hasComparison()) t.series.push_back(compareSerie(compareName(d.compare), valuesOf(m->before, scale)));
    if (valueF) t.valueFormat = valueF;
    t.yTickCount = 2;
    t.partialFrom = -1;
    t.clickHint = clickHint;
    s.time->setData(std::move(t));
    s.card->setText(valueText(m->total), m->hasComparison() ? ch::deltaOf(m->total, m->beforeTotal) : ch::Delta{},
                     m->hasComparison() ? "contre " + valueText(m->beforeTotal) : std::string());
}

void fillAcceptRefuse(Slot& s, const Data& d) {
    const auto* a = d.series("principal", "demandes_acceptees");
    const auto* r = d.series("principal", "demandes_refusees");
    if (missing(s, d, "principal", a) || missing(s, d, "principal", r)) return;
    const auto& rep = *d.reply("principal");
    ch::TimeChartData t = baseOf(rep, d);
    t.kind = ch::Kind::Grouped;
    ch::Series x = serie("un", "accept\xC3\xA9" "es", valuesOf(a->points)), y = serie("deux", "refus\xC3\xA9" "es", valuesOf(r->points));
    x.legendValue = ch::fr::integer(a->total);
    y.legendValue = ch::fr::integer(r->total);
    t.series = {x, y};
    t.tipForward = true;
    t.yTickCount = 2;
    t.clickHint = "Clic : ces demandes";
    s.time->setData(std::move(t));
    const double tot = a->total + r->total;
    s.card->setText(tot > 0 ? ch::fr::percent(a->total * 100.0 / tot) + " accept\xC3\xA9" "es" : kNone, {}, "sur " + ch::fr::integer(tot) + " d\xC3\xA9" "cid\xC3\xA9" "es");
}

void fillLicencesParOffre(Slot& s, const Data& d) {
    const auto* m = d.series("offre", "licences_actives");
    if (missing(s, d, "offre", m)) return;
    const auto& r = *d.reply("offre");
    ch::TimeChartData t = baseOf(r, d, true);
    t.kind = ch::Kind::Stacked;
    std::vector<ch::SideRow> side;
    for (const stats::Part* p : orderedParts(*m)) {
        const std::string label = partLabel("offre", p->key);
        t.series.push_back(serie(offerColorKey(p->key), label, valuesOf(p->points)));
        side.push_back({label, intText(lastOf(p->points)), "", ch::ColorKeys::standard().colorOf(offerColorKey(p->key)), false, false});
    }
    side.push_back({"Toutes", intText(m->total), "", {0, 0, 0, 0}, false, true});
    t.legend = ch::LegendMode::None;
    t.clickHint = "Clic : les licences de cette offre";
    s.time->setData(std::move(t));
    s.card->setSideRows(side, narrowSide(side));
    s.card->setText(intText(m->total), {}, "\xC3\xA0 la fin de la p\xC3\xA9riode");
}

// Les admins : leurs jours, leurs heures habituelles (a l'heure du PC), leur dernier passage, leur version.
class AdminsTable final : public ui::Widget {
public:
    explicit AdminsTable(std::string id) : ui::Widget(std::move(id)) {}
    void set(const Data& d) {
        rows_.clear();
        const Seconds now = d.now;
        for (std::size_t i = 0; i < d.admins.admins.size(); ++i) {
            const auto& a = d.admins.admins[i];
            Row r;
            r.order = static_cast<int>(i);
            r.name = a.name.empty() ? a.id : a.name;
            r.role = a.role == "admin+" ? "admin+" : a.retired ? "retir\xC3\xA9(e)" : "admin";
            r.days = std::to_string(a.days.size()) + " j";
            if (a.usualStart >= 0 && a.usualEnd >= 0)
                r.hours = std::to_string(localHour(a.usualStart, now)) + " h " + ch::fr::kArrow + " " + std::to_string(localHour(a.usualEnd, now)) + " h";
            else
                r.hours = kNone;
            r.seen = seenText(now, a.lastSeen);
            r.version = a.softwareVersion.empty() ? kNone : a.softwareVersion;
            r.city = a.city.empty() ? kNone : a.city + (a.country.empty() ? "" : ", " + a.country);
            rows_.push_back(r);
        }
        empty_ = d.hasAdmins ? "Aucun admin sur la p\xC3\xA9riode" : (d.error("admins").empty() ? "Lecture des admins\xE2\x80\xA6" : d.error("admins"));
        invalidate();
    }
    [[nodiscard]] std::size_t rows() const { return rows_.size(); }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const Rect b = bounds();
        if (rows_.empty()) {
            ctx.r.drawText({b.x + 4.f, b.y + 8.f}, empty_, FontId{12}, kMuted);
            return;
        }
        // les colonnes a la mesure de leur contenu ; la place manque : la ville, puis la version s'effacent (jamais coupees) ;
        // le role sous le nom
        const char* heads[] = {"ADMIN", "JOURS", "SES HEURES (HEURE DU PC)", "VU", "VERSION", "VILLE"};
        const FontId hf = gfx::semibold(FontId{10}), f = FontId{12};
        float cw[6];
        for (int c = 0; c < 6; ++c) cw[c] = ctx.r.measure(heads[c], hf).width;
        for (const auto& r : rows_) {
            cw[0] = std::max({cw[0], ctx.r.measure(r.name, f).width + 26.f, ctx.r.measure(r.role, FontId{10}).width + 26.f});
            cw[1] = std::max(cw[1], ctx.r.measure(r.days, f).width);
            cw[2] = std::max(cw[2], ctx.r.measure(r.hours, f).width);
            cw[3] = std::max(cw[3], ctx.r.measure(r.seen, f).width);
            cw[4] = std::max(cw[4], ctx.r.measure(r.version, f).width);
            cw[5] = std::max(cw[5], ctx.r.measure(r.city, f).width);
        }
        const float gap = 18.f;
        int cols = 6;
        const auto total = [&](int n) {
            float t = 0.f;
            for (int c = 0; c < n; ++c) t += cw[c] + (c ? gap : 0.f);
            return t;
        };
        while (cols > 3 && total(cols) > b.w) --cols;
        float xs[6];
        float x = b.x;
        const float extra = cols > 1 ? std::max(0.f, (b.w - total(cols)) / static_cast<float>(cols - 1)) : 0.f;
        for (int c = 0; c < cols; ++c) {
            xs[c] = x;
            x += cw[c] + gap + extra;
        }
        for (int c = 0; c < cols; ++c) ctx.r.drawText({xs[c], b.y}, heads[c], hf, kFaint);
        float y = b.y + 22.f;
        for (const auto& r : rows_) {
            if (y + 34.f > b.bottom()) break;
            const Color col = ch::colors::admin(std::min(r.order, 3));
            ctx.r.fillRoundedRect({b.x, y + 7.f, 18.f, 18.f}, col, 9.f);
            ctx.r.drawText({b.x + 5.f, y + 9.f}, r.name.substr(0, 1), gfx::semibold(FontId{11}), Color::rgb(0xFFFFFF));
            ctx.r.drawText({b.x + 26.f, y + 2.f}, r.name, f, kInk);
            ctx.r.drawText({b.x + 26.f, y + 18.f}, r.role, FontId{10}, kFaint);
            const std::string* cells[6] = {nullptr, &r.days, &r.hours, &r.seen, &r.version, &r.city};
            for (int c = 1; c < cols; ++c) ctx.r.drawText({xs[c], y + 9.f}, *cells[c], f, c == 3 || c == 5 ? kMuted : kInk);
            ctx.r.fillRect({b.x, y + 33.f, b.w, 1.f}, Color::rgb(0xF1F4F8));
            y += 35.f;
        }
    }

private:
    struct Row {
        int         order{0};
        std::string name, role, days, hours, seen, version, city;
    };
    std::vector<Row> rows_;
    std::string      empty_;
};

// ============================================================================ la page
enum class Tab : std::uint8_t { Overview, Sales, Licences, Pcs, Updates, Requests, Admins, Globe };
constexpr int kTabCount = 8;
const struct {
    const char* key;
    const char* label;
    ui::Icon    icon;
} kTabs[kTabCount] = {
    {"vue", "Vue g\xC3\xA9n\xC3\xA9rale", ui::Icon::Chart},   {"ventes", "Ventes", ui::Icon::Mail},       {"licences", "Licences", ui::Icon::Document},
    {"pc", "PC", ui::Icon::Station},                          {"mises-a-jour", "Mises \xC3\xA0 jour", ui::Icon::Refresh},
    {"demandes", "Demandes", ui::Icon::Import},               {"admins", "Admins", ui::Icon::User},       {"globe", "Globe", ui::Icon::Globe},
};

// Les groupes qu'un onglet lit.
std::vector<std::string> groupsFor(Tab t) {
    switch (t) {
        case Tab::Overview: return {"principal", "pc", "offre", "option", "echeances", "cumul", "flottantes"};
        case Tab::Sales: return {"principal", "offre", "option", "cumul", "client", "admin"};
        case Tab::Licences: return {"principal", "offre", "echeances", "admin"};
        case Tab::Pcs: return {"pc", "offre", "flottantes"};
        case Tab::Requests: return {"principal", "pc", "admin"};
        case Tab::Admins: return {"admin"};
        default: return {};
    }
}

class StatsPage final : public Page {
public:
    explicit StatsPage(ShellHost host) : Page("page.statistiques", std::move(host)) {
        installSamplesFromEnvironment([this] { return ws().now(); });
        for (int i = 0; i < kTabCount; ++i) {
            auto b = std::make_unique<TabButton>(kTabs[i].label, std::string("stats.onglet.") + kTabs[i].key, kTabs[i].icon);
            TabButton* raw = &static_cast<TabButton&>(addChild(std::move(b)));
            links_ += raw->clicked->connect([this, i] { showTab(static_cast<Tab>(i)); });
            tabs_.push_back(raw);
        }
        export_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Exporter\xE2\x80\xA6", "stats.exporter")));
        export_->setIcon(ui::Icon::Export);
        export_->setTooltip("Les chiffres de l'onglet, une ligne par p\xC3\xA9riode du pas (un fichier CSV pour Excel)");
        links_ += export_->clicked->connect([this] { exportCsv(); });
        snapshot_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Relever maintenant", "stats.relever")));
        snapshot_->setIcon(ui::Icon::Refresh);
        snapshot_->setTooltip("Le relev\xC3\xA9 du jour (les niveaux : licences, PC, versions), s'il n'est pas encore pris ; puis les chiffres relus");
        links_ += snapshot_->clicked->connect([this] { takeSnapshot(); });
        deploy_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("D\xC3\xA9ployer le service", "stats.deployer")));
        deploy_->setStyle(ui::Button::Style::Primary);
        deploy_->setIcon(ui::Icon::ArrowUp);
        deploy_->setTooltip("Lance le kit install\xC3\xA9 avec l'Admin : ton service part \xC3\xA0 jour sur ton compte Cloudflare (la sortie s'affiche en direct).");
        links_ += deploy_->clicked->connect([this] {
            if (host_.busy && host_.busy()) {
                host_.report(OpResult::fail("un travail est en cours : attends qu'il finisse, puis D\xC3\xA9ployer le service"));
                return;
            }
            if (deployer::running()) return;
            std::weak_ptr<bool> alive = alive_;
            if (host_.openDialog) host_.openDialog(makeDeployDialog(host_, [this, alive] {
                if (!alive.expired()) refresh();
            }));
        });
        retry_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("R\xC3\xA9" "essayer", "stats.reessayer")));
        links_ += retry_->clicked->connect([this] {
            sig_.clear();
            data_ = {};
            load();
        });

        // la barre commune
        range_ = &static_cast<SegGroup&>(addChild(std::make_unique<SegGroup>(
            "stats.periode", std::vector<SegGroup::Item>{{rangeKey(Range::Days7), rangeLabel(Range::Days7)},
                                                         {rangeKey(Range::Days30), rangeLabel(Range::Days30)},
                                                         {rangeKey(Range::Days90), rangeLabel(Range::Days90)},
                                                         {rangeKey(Range::Months12), rangeLabel(Range::Months12)},
                                                         {rangeKey(Range::All), rangeLabel(Range::All)},
                                                         {rangeKey(Range::Dates), rangeLabel(Range::Dates), ui::Icon::History}})));
        range_->onPick = [this](const std::string& k) { pickRange(k); };
        compare_ = &static_cast<SegGroup&>(addChild(std::make_unique<SegGroup>(
            "stats.comparer", std::vector<SegGroup::Item>{{"avant", compareLabel(Compare::Previous)}, {"an", compareLabel(Compare::LastYear)}, {"rien", "rien"}})));
        compare_->onPick = [this](const std::string& k) {
            Compare c = Compare::LastYear;
            (void)stats::parseCompare(k, c);
            choice_.compare = c;
            load();
        };
        step_ = &static_cast<SegGroup&>(
            addChild(std::make_unique<SegGroup>("stats.pas", std::vector<SegGroup::Item>{{"jour", "jour"}, {"semaine", "semaine"}, {"mois", "mois"}})));
        step_->onPick = [this](const std::string& k) {
            Step s = Step::Week;
            (void)stats::parseStep(k, s);
            choice_.step = s;
            load();
        };
        software_ = &static_cast<SegGroup&>(addChild(std::make_unique<SegGroup>(
            "stats.logiciel", std::vector<SegGroup::Item>{{"tous", "tous"}, {"studios", "les Studios"}, {"admins", "les Admins"}})));
        software_->onPick = [this](const std::string&) {
            if (updates_) reloadView(*updates_);
        };
        from_ = &static_cast<ui::DatePicker&>(addChild(std::make_unique<ui::DatePicker>("stats.du")));
        to_ = &static_cast<ui::DatePicker&>(addChild(std::make_unique<ui::DatePicker>("stats.au")));
        links_ += from_->changed->connect([this](ui::DatePicker::Seconds) { datesChanged(); });
        links_ += to_->changed->connect([this](ui::DatePicker::Seconds) { datesChanged(); });

        buildOverview();
        buildSales();
        buildLicences();
        buildPcs();
        buildRequests();
        buildAdmins();
        updates_ = &addChild(makeUpdatesView(
            host_, [this] { return choice_; }, [this] { return software_->current(); }));
        globe_ = &addChild(makeStatsGlobeView(host_));
        syncBar();
        showTab(Tab::Overview);
    }

    [[nodiscard]] std::string title() const override { return "Statistiques"; }

    void refresh() override {
        const Workspace& w = ws();
        snapshot_->setVisibility(w.role() == Role::Plus ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        bool old = false;
        note_ = blocked(w, &old);
        tooOld_ = old;
        // Une fois le service a jour (deploye), les chiffres se relisent.
        if (note_.empty() && (lastNote_ != note_)) sig_.clear();
        lastNote_ = note_;
        if (tab_ == Tab::Updates && updates_) reloadView(*updates_);
        load();
        invalidateLayout();
        invalidate();
    }

    // ---- les essais ----
    [[nodiscard]] Tab tab() const noexcept { return tab_; }
    [[nodiscard]] const Choice& choice() const noexcept { return choice_; }
    [[nodiscard]] const Data& data() const noexcept { return data_; }
    [[nodiscard]] const std::string& note() const noexcept { return note_; }

protected:
    void onLayout() override {
        const Rect b = bounds();
        const float pad = b.w < 1000.f ? 20.f : 28.f;
        const float x0 = b.x + pad, w = std::max(300.f, b.w - 2.f * pad);
        float y = b.y + 18.f;
        titleY_ = y;
        // les onglets apres le titre ; a droite, la periode et Exporter
        const float titleW = ui::measureWidth("Statistiques", gfx::semibold(FontId{26})) + 26.f;
        float right = x0 + w;
        const float ew = export_->sizeHint().preferred.w + 10.f;
        export_->setBounds({right - ew, y + 2.f, ew, 32.f});
        right -= ew + 8.f;
        float natural = 0.f;
        for (auto* t : tabs_) natural += t->preferredWidth() + 12.f;
        const float periodW = ui::measureWidth(periodText(periodNow()), FontId{13}) + 12.f;
        if (snapshot_->visible()) {
            // << Relever maintenant >> : son icone seule (l'infobulle) si la place manque
            snapshot_->setCompact(false);
            float sw = snapshot_->sizeHint().preferred.w + 10.f;
            if (x0 + titleW + natural + sw + 8.f > right) {
                snapshot_->setCompact(true);
                sw = 34.f;
            }
            snapshot_->setBounds({right - sw, y + 2.f, sw, 32.f});
            right -= sw + 8.f;
        }
        periodRight_ = right - 6.f;
        float tx = x0 + titleW, ty = y + 6.f;
        float tabsRoom = periodRight_ - tx;
        // les onglets a cote du titre (la planche), l'espace entre eux resserre s'il le faut ; une fenetre etroite : sur leur
        // propre rangee, sous le titre (jamais l'un sur l'autre, jamais coupes)
        float labels = 0.f;
        for (auto* t : tabs_) labels += t->preferredWidth();
        float tabGap = 20.f;
        while (tabGap > 10.f && labels + tabGap * static_cast<float>(tabs_.size()) > tabsRoom) tabGap -= 2.f;
        const bool ownRow = labels + tabGap * static_cast<float>(tabs_.size()) > tabsRoom + 1.f;
        if (ownRow) {
            tx = x0;
            ty = y + 44.f;
            tabsRoom = x0 + w - tx;
            tabGap = 20.f;
            while (tabGap > 10.f && labels + tabGap * static_cast<float>(tabs_.size()) > tabsRoom) tabGap -= 2.f;
        }
        // la periode en tete si elle tient a cote des onglets ; sinon au bout de la barre des choix
        periodInTabs_ = !ownRow && labels + tabGap * static_cast<float>(tabs_.size()) + periodW <= tabsRoom + 1.f;
        for (auto* t : tabs_) {
            const float tw = t->preferredWidth();
            t->setBounds({tx, ty, tw, 30.f});
            tx += tw + tabGap;
        }
        y += ownRow ? 82.f : 44.f;
        tabsBottom_ = y;
        // la barre commune
        y += 10.f;
        barY_ = y;
        const float segH = 34.f;
        float bx = x0;
        const bool globe = tab_ == Tab::Globe;
        const bool upd = tab_ == Tab::Updates;
        range_->setVisibility(globe ? ui::Visibility::Collapsed : ui::Visibility::Visible);
        // une fenetre etroite : ce qui ne tient plus sur la rangee passe a la suivante (jamais l'un sur l'autre)
        float rowY = y;
        const auto wrapIf = [&](float need) {
            if (bx > x0 && bx + need > x0 + w) {
                bx = x0;
                rowY += segH + 8.f;
            }
        };
        const float rw = std::min(range_->preferredWidth(), w);
        range_->setBounds({bx, rowY, rw, segH});
        bx += rw + 10.f;
        const bool dates = choice_.range == Range::Dates && !globe;
        from_->setVisibility(dates ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        to_->setVisibility(dates ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (dates) {
            const float dw = 150.f;
            wrapIf(2.f * dw + 30.f);
            from_->setBounds({bx, rowY + 1.f, dw, segH - 2.f});
            arrow_ = {bx + dw + 4.f, rowY + 9.f};
            to_->setBounds({bx + dw + 20.f, rowY + 1.f, dw, segH - 2.f});
            bx += 2.f * dw + 30.f;
        }
        labels_.clear();
        seps_.clear();
        compare_->setVisibility(!globe && !upd ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        step_->setVisibility(!globe && !upd ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        software_->setVisibility(upd ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        // un groupe : un trait, son libelle discret, ses choix
        const auto group = [&](const char* label, SegGroup* g) {
            const float lw = ui::measureWidth(label, gfx::semibold(FontId{11}));
            const float gw = g->preferredWidth();
            wrapIf(14.f + lw + 8.f + gw);
            if (bx > x0) seps_.push_back({bx, rowY});
            labels_.push_back({{bx > x0 ? bx + 10.f : bx, rowY}, label});
            bx += (bx > x0 ? 14.f : 0.f) + lw + 8.f;
            g->setBounds({bx, rowY, gw, segH});
            bx += gw + 10.f;
        };
        if (upd) {
            group("les PC", software_);
        } else if (!globe) {
            group("compar\xC3\xA9 \xC3\xA0", compare_);
            group("par", step_);
        }
        y = rowY;
        hintX_ = bx;
        {
            float room = x0 + w - bx;
            periodInBar_ = !globe && !periodInTabs_ && room >= periodW + 16.f;
            if (periodInBar_) room -= periodW + 24.f;
            hint_.clear();
            if (!globe) {
                for (const char* h : {"Un clic sur un point filtre la liste correspondante", "Un clic sur un point filtre la liste"})
                    if (room >= ui::measureWidth(h, FontId{12}) + 40.f) {
                        hint_ = h;
                        break;
                    }
            }
        }
        barLastY_ = y;
        y += segH + 12.f;
        barBottom_ = y;
        // le bandeau : le service trop ancien (Deployer), une panne (Reessayer), les chiffres reconstitues
        bannerRect_ = {};
        deploy_->setVisibility(ui::Visibility::Collapsed);
        retry_->setVisibility(ui::Visibility::Collapsed);
        const std::string banner = bannerText();
        if (!banner.empty() && !globe && !upd) {
            ui::Button* btn = tooOld_ ? deploy_ : (!note_.empty() || failed_.empty()) ? nullptr : retry_;
            const float bw = btn ? btn->sizeHint().preferred.w + 12.f : 0.f;
            const float bh = bannerHeight(banner, w, bw + 16.f);
            bannerRect_ = {x0, y, w, bh};
            if (btn) {
                btn->setVisibility(ui::Visibility::Visible);
                btn->setBounds({x0 + w - bw - 12.f, y + (bh - 32.f) * 0.5f, bw, 32.f});
            }
            y += bh + 10.f;
        }
        // la note des chiffres reconstitues
        rebuiltY_ = -1.f;
        if (banner.empty() && !globe && !upd && !rebuiltText().empty()) {
            rebuiltY_ = y - 4.f;
            // la note sur une ou deux lignes (jamais coupee)
            rebuiltLines_.clear();
            std::string line, word;
            const std::string text = rebuiltText() + " ";
            for (const char c : text) {
                if (c != ' ') {
                    word += c;
                    continue;
                }
                const std::string next = line.empty() ? word : line + " " + word;
                if (!line.empty() && ui::measureWidth(next, FontId{12}) > w - 20.f) {
                    rebuiltLines_.push_back(line);
                    line = word;
                } else {
                    line = next;
                }
                word.clear();
            }
            if (!line.empty()) rebuiltLines_.push_back(line);
            y += 18.f * static_cast<float>(std::max<std::size_t>(1, rebuiltLines_.size()));
        }
        const Rect area{x0, y, w, std::max(120.f, b.bottom() - 10.f - y)};
        for (auto* g : grids_) g->setBounds(area);
        updates_->setBounds(area);
        globe_->setBounds(globe ? Rect{x0, tabsBottom_ + 8.f, w, std::max(120.f, b.bottom() - 10.f - tabsBottom_ - 8.f)} : area);
    }

    void onPaint(const ui::PaintContext& ctx) override {
        (void)job_.poll();   // le releve fini : rendu ici, sur le fil de l'interface
        const Rect b = bounds();
        ctx.r.fillRect(b, kPageBg);
        const float pad = b.w < 1000.f ? 20.f : 28.f;
        const float x0 = b.x + pad, w = std::max(300.f, b.w - 2.f * pad);
        ctx.r.drawText({x0, titleY_}, "Statistiques", gfx::semibold(FontId{26}), kInk);
        ctx.r.fillRect({x0, tabsBottom_ - 1.f, w, 1.f}, kLine);
        const std::string period = periodText(periodNow());
        if (periodInTabs_ && tab_ != Tab::Globe) ctx.r.drawText({periodRight_ - ctx.r.measure(period, FontId{13}).width, titleY_ + 10.f}, period, FontId{13}, kMuted);
        float hintRight = x0 + w;
        if (periodInBar_ && tab_ != Tab::Globe) {
            const float pw = ctx.r.measure(period, FontId{13}).width;
            ctx.r.drawText({hintRight - pw, barLastY_ + 9.f}, period, FontId{13}, kMuted);
            hintRight -= pw + 24.f;
        }
        if (tab_ != Tab::Globe) {
            for (const auto& p : seps_) ctx.r.fillRect({p.x, p.y + 6.f, 1.f, 22.f}, kLine);
            for (const auto& [p, t] : labels_) ctx.r.drawText({p.x, p.y + 10.f}, t, gfx::semibold(FontId{11}), kFaint);
            if (choice_.range == Range::Dates) ctx.r.drawText(arrow_, ch::fr::kArrow, FontId{13}, kMuted);
            if (!hint_.empty()) {
                const std::string a = "Un clic sur un point ", c = hint_.substr(a.size());
                const float wa = ctx.r.measure(a, FontId{12}).width, wc = ctx.r.measure(c, gfx::semibold(FontId{12})).width;
                float x = hintRight - wa - wc;
                ui::drawIcon(ctx.r, ui::Icon::GoTo, {x - 22.f, barLastY_ + 9.f, 15.f, 15.f}, kViolet);
                ctx.r.drawText({x, barLastY_ + 10.f}, a, FontId{12}, kMuted);
                x += wa;
                ctx.r.drawText({x, barLastY_ + 10.f}, c, gfx::semibold(FontId{12}), kInk);
            }
            ctx.r.fillRect({x0, barBottom_ - 1.f, w, 1.f}, kLine);
        }
        if (!bannerRect_.empty()) {
            const ui::Button* btn = deploy_->visible() ? deploy_ : retry_->visible() ? retry_ : nullptr;
            paintBanner(ctx.r, ctx.theme, bannerRect_, bannerText(), tooOld_ || !failed_.empty(), btn ? btn->bounds().w + 28.f : 0.f);
        }
        if (rebuiltY_ >= 0.f) {
            ui::drawIcon(ctx.r, ui::Icon::Info, {x0, rebuiltY_ + 1.f, 13.f, 13.f}, kFaint);
            for (std::size_t i = 0; i < rebuiltLines_.size(); ++i) ctx.r.drawText({x0 + 18.f, rebuiltY_ + 17.f * static_cast<float>(i)}, rebuiltLines_[i], FontId{12}, kMuted);
        }
        // un travail qui n'a pas pu partir (un autre tournait) : il part des que possible
        if (wantLoad_ && !loading_) load();
    }

private:
    // ---- la periode ----
    [[nodiscard]] stats::Period periodNow() const { return periodOf(choice_, ws().now(), allFromOf(ws())); }

    void pickRange(const std::string& k) {
        for (Range r : {Range::Days7, Range::Days30, Range::Days90, Range::Months12, Range::All, Range::Dates})
            if (k == rangeKey(r)) choice_.range = r;
        if (choice_.range == Range::Dates) {
            const stats::Period p = stats::lastDays(ws().now(), 90);
            if (choice_.from.empty()) choice_.from = p.from;
            if (choice_.to.empty()) choice_.to = p.to;
            from_->setDate(stats::dayStart(choice_.from));
            to_->setDate(stats::dayStart(choice_.to));
            to_->setRangeStart(stats::dayStart(choice_.from));
        }
        // le pas par defaut d'une periode (7 et 30 jours : chaque jour ; 90 jours : chaque semaine ; 12 mois, tout : chaque mois)
        if (choice_.range == Range::Days7 || choice_.range == Range::Days30) choice_.step = Step::Day;
        else if (choice_.range == Range::Days90) choice_.step = Step::Week;
        else if (choice_.range == Range::Months12 || choice_.range == Range::All) choice_.step = Step::Month;
        syncBar();
        invalidateLayout();
        load();
        if (tab_ == Tab::Updates && updates_) reloadView(*updates_);
    }
    void datesChanged() {
        if (from_->date() > 0) choice_.from = stats::dayOf(from_->date());
        if (to_->date() > 0) choice_.to = stats::dayOf(to_->date());
        to_->setRangeStart(from_->date());
        choice_.range = Range::Dates;
        syncBar();
        load();
        if (tab_ == Tab::Updates && updates_) reloadView(*updates_);
    }
    void syncBar() {
        range_->setCurrent(rangeKey(choice_.range));
        compare_->setCurrent(stats::compareKey(choice_.compare));
        step_->setCurrent(stats::stepKey(fitStep(periodNow(), choice_.step)));
    }

    // ---- les onglets ----
    void showTab(Tab t) {
        tab_ = t;
        for (int i = 0; i < kTabCount; ++i) tabs_[static_cast<std::size_t>(i)]->setSelected(i == static_cast<int>(t));
        for (std::size_t i = 0; i < grids_.size(); ++i)
            grids_[i]->setVisibility(gridTabs_[i] == t ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        updates_->setVisibility(t == Tab::Updates ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        globe_->setVisibility(t == Tab::Globe ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        if (t == Tab::Updates) reloadView(*updates_);
        load();
        invalidateLayout();
        invalidate();
    }

    // ---- les cartes ----
    CardGrid& grid(Tab t, int cols, int rows, float minRow, float railW = 0.f) {
        auto g = std::make_unique<CardGrid>(std::string("stats.grille.") + kTabs[static_cast<int>(t)].key, cols, rows, minRow, railW);
        CardGrid& ref = static_cast<CardGrid&>(addChild(std::move(g)));
        grids_.push_back(&ref);
        gridTabs_.push_back(t);
        return ref;
    }
    // `bars` : 0 un TimeChart, 1 un BarList (les options vendues : libelles au-dessus des barres).
    Slot& slot(CardGrid& g, Tab t, const std::string& id, const std::string& title, const std::string& small, int bars, const std::string& group,
               std::function<void(Slot&, const Data&)> fill, int col, int row, int cs = 1, int rs = 1) {
        const std::string full = std::string("stats.") + kTabs[static_cast<int>(t)].key + "." + id;
        auto card = std::make_unique<FitCard>(full + ".carte", title, small);
        Slot s;
        s.id = id;
        s.group = group;
        s.fill = std::move(fill);
        if (bars) s.bars = &card->setChartAs(std::make_unique<FitBarList>(full));
        else s.time = &card->setChartAs(std::make_unique<ch::TimeChart>(full));
        s.card = &g.put(std::move(card), col, row, cs, rs);
        slots_.push_back(std::make_unique<Slot>(std::move(s)));
        Slot* raw = slots_.back().get();
        slotTabs_.push_back(t);
        const auto pick = [this, raw](const ch::Pick& p) { picked(*raw, p); };
        if (raw->time) raw->time->setOnPick(pick);
        if (raw->bars) raw->bars->setOnPick(pick);
        return *raw;
    }

    void buildOverview() {
        const Tab t = Tab::Overview;
        CardGrid& g = grid(t, 5, 4, 150.f, 26.f);
        g.rail("Ventes", 0, 1);
        g.rail("Licences \xC2\xB7 demandes", 2, 2);
        g.rail("PC", 3, 3);
        slot(g, t, "montant", "Montant encaiss\xC3\xA9", "par semaine", 0, "principal", fillMontant, 0, 0, 2);
        slot(g, t, "cumul", "Cumul\xC3\xA9 sur l'ann\xC3\xA9" "e", "", 0, "cumul", fillCumul, 2, 0);
        slot(g, t, "commandes", "Commandes", "", 0, "principal", fillCommandes, 3, 0);
        slot(g, t, "panier", "Panier moyen", "", 0, "principal", fillPanier, 4, 0);
        slot(g, t, "offres", "Ventes par offre", "montant par semaine", 0, "offre", fillParOffre, 0, 1, 2);
        slot(g, t, "options", "Options vendues", "", 1, "option", fillOptions, 2, 1);
        slot(g, t, "vendues", "Licences vendues", "", 0, "principal", fillVendues, 3, 1);
        slot(g, t, "remboursements", "Remboursements", "", 0, "principal", fillRemboursements, 4, 1);
        slot(g, t, "actives", "Licences actives", "", 0, "principal", fillActives, 0, 2);
        slot(g, t, "mouvements", "Mouvements des licences", "", 0, "principal", fillMouvements, 1, 2);
        slot(g, t, "echeances", "\xC3\x89" "ch\xC3\xA9" "ances", "les 90 jours \xC3\xA0 venir", 0, "echeances", fillEcheances, 2, 2);
        slot(g, t, "demandes", "Demandes", "", 0, "principal", fillDemandes, 3, 2);
        slot(g, t, "delai", "D\xC3\xA9lai de r\xC3\xA9ponse", "m\xC3\xA9" "dian", 0, "principal", fillDelai, 4, 2);
        slot(g, t, "pc", "PC actifs", "par offre, \xC3\xA0 la fin de chaque semaine", 0, "offre", fillPcActifs, 0, 3, 2);
        slot(g, t, "mouvements-pc", "Mouvements des PC", "", 0, "pc", fillMouvementsPc, 2, 3);
        slot(g, t, "flottantes", "Flottantes", "le pic du jour", 0, "flottantes", fillFlottantes, 3, 3);
        slot(g, t, "decouvertes", "D\xC3\xA9" "couvertes", "l'essai", 0, "pc", fillDecouvertes, 4, 3);
    }
    void buildSales() {
        const Tab t = Tab::Sales;
        CardGrid& g = grid(t, 4, 3, 170.f);
        slot(g, t, "montant", "Montant encaiss\xC3\xA9", "par semaine", 0, "principal", fillMontant, 0, 0, 2);
        slot(g, t, "cumul", "Cumul\xC3\xA9 sur l'ann\xC3\xA9" "e", "", 0, "cumul", fillCumul, 2, 0);
        slot(g, t, "commandes", "Commandes", "", 0, "principal", fillCommandes, 3, 0);
        slot(g, t, "offres", "Ventes par offre", "montant par semaine", 0, "offre", fillParOffre, 0, 1, 2);
        slot(g, t, "panier", "Panier moyen", "", 0, "principal", fillPanier, 2, 1);
        slot(g, t, "options", "Options vendues", "", 1, "option", fillOptions, 3, 1);
        slot(g, t, "clients", "Les meilleurs clients", "montant encaiss\xC3\xA9", 1, "client", fillTopClients, 0, 2);
        slot(g, t, "livrees", "Livr\xC3\xA9" "es par", "le montant que chacun a livr\xC3\xA9", 0, "admin",
             [](Slot& s, const Data& d) { fillByAdmin(s, d, "ventes_montant", 0.01, true, "Clic : ses commandes"); }, 1, 2);
        slot(g, t, "vendues", "Licences vendues", "", 0, "principal", fillVendues, 2, 2);
        slot(g, t, "remboursements", "Remboursements", "", 0, "principal", fillRemboursements, 3, 2);
    }
    void buildLicences() {
        const Tab t = Tab::Licences;
        CardGrid& g = grid(t, 4, 2, 200.f);
        slot(g, t, "actives", "Licences actives", "", 0, "principal", fillActives, 0, 0, 2);
        slot(g, t, "mouvements", "Mouvements des licences", "", 0, "principal", fillMouvements, 2, 0);
        slot(g, t, "echeances", "\xC3\x89" "ch\xC3\xA9" "ances", "les 90 jours \xC3\xA0 venir", 0, "echeances", fillEcheances, 3, 0);
        slot(g, t, "par-offre", "Licences actives par offre", "", 0, "offre", fillLicencesParOffre, 0, 1, 2);
        slot(g, t, "options-lic", "Options ajout\xC3\xA9" "es", "", 0, "principal",
             [](Slot& s, const Data& d) {
                 fillSimpleLine(s, d, "principal", "licences_options", 1.0, intF(), false, "Clic : ces licences", [](double v) { return intText(v); });
             },
             2, 1);
        slot(g, t, "signees", "Sign\xC3\xA9" "es par", "les licences neuves", 0, "admin",
             [](Slot& s, const Data& d) { fillByAdmin(s, d, "licences_nouvelles", 1.0, false, "Clic : ses licences"); }, 3, 1);
    }
    void buildPcs() {
        const Tab t = Tab::Pcs;
        CardGrid& g = grid(t, 5, 2, 200.f);
        slot(g, t, "pc", "PC actifs", "par offre, \xC3\xA0 la fin de chaque semaine", 0, "offre", fillPcActifs, 0, 0, 3);
        slot(g, t, "mouvements-pc", "Mouvements des PC", "", 0, "pc", fillMouvementsPc, 3, 0, 2);
        slot(g, t, "flottantes", "Flottantes", "le pic du jour", 0, "flottantes", fillFlottantes, 0, 1, 3);
        slot(g, t, "decouvertes", "D\xC3\xA9" "couvertes", "l'essai", 0, "pc", fillDecouvertes, 3, 1, 2);
    }
    void buildRequests() {
        const Tab t = Tab::Requests;
        CardGrid& g = grid(t, 5, 2, 200.f);
        slot(g, t, "demandes", "Demandes", "", 0, "principal", fillDemandes, 0, 0, 2);
        slot(g, t, "delai", "D\xC3\xA9lai de r\xC3\xA9ponse", "m\xC3\xA9" "dian", 0, "principal", fillDelai, 2, 0, 2);
        slot(g, t, "attente", "En attente", "\xC3\xA0 la fin de chaque p\xC3\xA9riode", 0, "principal",
             [](Slot& s, const Data& d) {
                 fillSimpleLine(s, d, "principal", "demandes_attente", 1.0, intF(), true, "Clic : les demandes en attente", [](double v) { return intText(v); });
             },
             4, 0);
        slot(g, t, "traitees", "Trait\xC3\xA9" "es par", "", 0, "admin",
             [](Slot& s, const Data& d) { fillByAdmin(s, d, "demandes_traitees", 1.0, false, "Clic : ses demandes"); }, 0, 1, 2);
        slot(g, t, "issue", "Accept\xC3\xA9" "es, refus\xC3\xA9" "es", "", 0, "principal", fillAcceptRefuse, 2, 1, 2);
        slot(g, t, "ancienne", "Attente la plus longue", "", 0, "pc",
             [](Slot& s, const Data& d) {
                 fillSimpleLine(s, d, "pc", "demandes_plus_ancienne", 1.0 / 3600.0, hoursF(), true, "Clic : les demandes en attente",
                                [](double v) { return stats::duration(v); });
             },
             4, 1);
    }
    void buildAdmins() {
        const Tab t = Tab::Admins;
        CardGrid& g = grid(t, 3, 2, 210.f);
        slot(g, t, "signees", "Licences sign\xC3\xA9" "es", "neuves, par admin", 0, "admin",
             [](Slot& s, const Data& d) { fillByAdmin(s, d, "licences_nouvelles", 1.0, false, "Clic : ses licences"); }, 0, 0);
        slot(g, t, "traitees", "Demandes trait\xC3\xA9" "es", "par admin", 0, "admin",
             [](Slot& s, const Data& d) { fillByAdmin(s, d, "demandes_traitees", 1.0, false, "Clic : ses demandes"); }, 1, 0);
        slot(g, t, "livrees", "Ventes livr\xC3\xA9" "es", "par admin", 0, "admin",
             [](Slot& s, const Data& d) { fillByAdmin(s, d, "ventes_montant", 0.01, true, "Clic : ses commandes"); }, 2, 0);
        slot(g, t, "jours", "Jours de connexion", "par admin", 0, "admin",
             [](Slot& s, const Data& d) { fillByAdmin(s, d, "admin_jours", 1.0, false, "Clic : son journal"); }, 0, 1);
        // le tableau des admins (ses heures a l'heure du PC)
        auto card = std::make_unique<AdminsCard>("stats.admins.qui");
        adminsCard_ = &g.put(std::move(card), 1, 1, 2);
    }

    // Une carte qui porte le tableau des admins.
    class AdminsCard final : public ui::Widget {
    public:
        explicit AdminsCard(std::string id) : ui::Widget(id) { table_ = &static_cast<AdminsTable&>(addChild(std::make_unique<AdminsTable>(id + ".tableau"))); }
        void set(const Data& d) { table_->set(d); }

    protected:
        void onLayout() override {
            const Rect b = bounds();
            table_->setBounds({b.x + 16.f, b.y + 44.f, b.w - 32.f, b.h - 52.f});
        }
        void onPaint(const ui::PaintContext& ctx) override {
            paintCard(ctx.r, ctx.theme, bounds());
            ctx.r.drawText({bounds().x + 16.f, bounds().y + 14.f}, "Qui et quand", gfx::semibold(FontId{14}), kInk);
            ctx.r.drawText({bounds().x + 16.f + ctx.r.measure("Qui et quand", gfx::semibold(FontId{14})).width + 8.f, bounds().y + 16.f},
                           "leurs jours et leurs heures habituelles, \xC3\xA0 l'heure de ce PC", FontId{11}, kFaint);
        }

    private:
        AdminsTable* table_{nullptr};
    };

    // ---- les chiffres ----
    void load() {
        wantLoad_ = false;
        if (tab_ == Tab::Updates || tab_ == Tab::Globe) return;
        if (!note_.empty()) {
            fillAll();
            return;
        }
        const Seconds now = ws().now();
        const std::string sig = signature(choice_, now);
        if (sig != sig_) {
            data_ = {};
            sig_ = sig;
        }
        std::vector<std::string> want;
        for (const auto& g : groupsFor(tab_))
            if (!data_.reply(g) && data_.error(g).empty()) want.push_back(g);
        const bool wantAdmins = (tab_ == Tab::Admins || tab_ == Tab::Sales || tab_ == Tab::Licences || tab_ == Tab::Requests) && !data_.hasAdmins &&
                                data_.error("admins").empty();
        if (want.empty() && !wantAdmins) {
            fillAll();
            return;
        }
        if (loading_) {
            wantLoad_ = true;
            return;
        }
        const stats::Period period = periodNow();
        data_.period = period;
        data_.step = fitStep(period, choice_.step);
        data_.compare = choice_.compare;
        data_.today = stats::dayOf(now);
        data_.now = now;
        failed_.clear();
        auto out = std::make_shared<Data>();
        const Workspace* w = &ws();
        const Choice c = choice_;
        std::vector<Group> todo;
        for (const auto& g : groups())
            if (std::find(want.begin(), want.end(), g.key) != want.end()) todo.push_back(g);
        loading_ = true;
        std::weak_ptr<bool> alive = alive_;
        const bool started = job_.start("Statistiques : relev\xC3\xA9",
            [w, out, todo, c, period, now, wantAdmins] {
                OpResult last = OpResult::done({});
                for (const auto& g : todo) {
                    stats::SeriesReply r;
                    const OpResult x = fetchSeries(*w, queryFor(g, c, period, now), r);
                    if (x.ok) out->replies[g.key] = std::move(r);
                    else {
                        out->errors[g.key] = x.message;
                        last = x;
                        if (saysTooOld(x.message)) break;
                    }
                }
                if (wantAdmins) {
                    const OpResult x = fetchAdmins(*w, period, out->admins);
                    if (x.ok) out->hasAdmins = true;
                    else out->errors["admins"] = x.message;
                }
                return last.ok ? OpResult::done({}) : last;
            },
            [this, alive, out, sig](const OpResult& r) {
                if (alive.expired()) return;
                loading_ = false;
                if (sig == sig_) {
                    for (auto& [k, v] : out->replies) data_.replies[k] = std::move(v);
                    for (auto& [k, v] : out->errors) data_.errors[k] = v;
                    if (out->hasAdmins) {
                        data_.admins = std::move(out->admins);
                        data_.hasAdmins = true;
                    }
                    if (!r.ok) {
                        failed_ = r.message;
                        if (saysTooOld(r.message)) {
                            note_ = r.message;
                            tooOld_ = true;
                        }
                    }
                }
                fillAll();
                invalidateLayout();
                if (wantLoad_) load();
            });
        if (!started) {
            loading_ = false;
            wantLoad_ = true;   // un travail tourne : des qu'il finit
        }
        fillAll();
    }

    void fillAll() {
        Data shown = data_;
        if (!note_.empty()) {
            for (const auto& g : groups()) shown.errors[g.key] = tooOld_ ? std::string("D\xC3\xA9ploie la 1.47.0 du service pour ces chiffres.") : note_;
            shown.errors["admins"] = shown.errors["principal"];
        }
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            Slot& s = *slots_[i];
            if (s.fill) s.fill(s, shown);
        }
        if (adminsCard_) adminsCard_->set(shown);
        invalidate();
    }

    [[nodiscard]] std::string bannerText() const {
        if (!note_.empty()) return note_;
        if (!failed_.empty() && tab_ != Tab::Updates) return "Les chiffres ne se lisent pas : " + failed_;
        return {};
    }
    [[nodiscard]] std::string rebuiltText() const {
        const stats::SeriesReply* r = data_.reply("principal");
        if (!r) r = data_.reply("pc");
        if (!r || r->recordedSince.empty() || data_.period.from.empty() || data_.period.from >= r->recordedSince) return {};
        return "Avant le " + ddmmyyyy(r->recordedSince) +
               ", des chiffres sont reconstitu\xC3\xA9s des tables du service (le relev\xC3\xA9 de chaque jour commence avec sa 1.47) : ils peuvent \xC3\xAAtre un peu bas.";
    }

    // ---- un clic sur un point : la liste filtree ----
    void picked(const Slot& s, const ch::Pick& p) {
        const stats::SeriesReply* r = data_.reply(s.group);
        ListFilter f;
        std::string from, to;
        if (r && p.index >= 0 && static_cast<std::size_t>(p.index) < r->buckets.size()) {
            from = r->buckets[static_cast<std::size_t>(p.index)].from;
            to = r->buckets[static_cast<std::size_t>(p.index)].to;
        }
        const std::string when = from.empty() ? std::string() : from == to ? " du " + ddmm(from) : " du " + ddmm(from) + " au " + ddmm(to);
        const std::string id = s.id;
        const std::string seriesName = s.time && p.series >= 0 && static_cast<std::size_t>(p.series) < s.time->data().series.size()
                                           ? s.time->data().series[static_cast<std::size_t>(p.series)].name
                                           : std::string();
        if (id == "montant" || id == "commandes" || id == "panier" || id == "remboursements" || id == "offres" || id == "livrees") {
            f.page = "commandes";
            f.periodField = "Date";
            f.from = from;
            f.to = to;
            f.what = "Commandes" + when;
            if (id == "remboursements") f.values.push_back({"\xC3\x89tat", "rembours\xC3\xA9" "e"});
            if (id == "offres" && !seriesName.empty() && seriesName != "Options") {
                f.search = seriesName;
                f.what += " \xC2\xB7 " + seriesName;
            }
            if (id == "livrees" && !seriesName.empty()) {
                f.values.push_back({"Livr\xC3\xA9" "e par", seriesName});
                f.what += " \xC2\xB7 livr\xC3\xA9" "es par " + seriesName;
            }
        } else if (id == "cumul") {
            f.page = "commandes";
            f.periodField = "Date";
            f.from = r ? r->period.from : std::string();
            f.to = to;
            f.what = "Commandes de l'ann\xC3\xA9" "e jusqu'au " + ddmm(to);
        } else if (id == "options" || id == "clients") {
            f.page = "commandes";
            if (s.bars && p.index >= 0 && static_cast<std::size_t>(p.index) < s.bars->data().rows.size()) f.search = s.bars->data().rows[static_cast<std::size_t>(p.index)].label;
            f.periodField = "Date";
            f.from = data_.period.from;
            f.to = data_.period.to;
            f.what = "Commandes \xC2\xB7 " + f.search;
        } else if (id == "vendues" || id == "mouvements" || id == "signees" || id == "options-lic") {
            f.page = "licences";
            f.periodField = "\xC3\x89mise le";
            f.from = from;
            f.to = to;
            f.what = "Licences \xC3\xA9mises" + when;
            if (id == "signees" && !seriesName.empty()) {
                f.values.push_back({"Sign\xC3\xA9" "e par", seriesName});
                f.what += " \xC2\xB7 sign\xC3\xA9" "es par " + seriesName;
            }
        } else if (id == "actives" || id == "par-offre" || id == "echeances") {
            f.page = "licences";
            f.values.push_back({"\xC3\x89tat", "Active"});
            if (id == "par-offre" && !seriesName.empty()) f.values.push_back({"\xC3\x89" "dition", seriesName});
            f.what = id == "echeances" ? "Licences actives (celles qui finissent" + when + " : la colonne Fin)" : "Licences actives";
        } else if (id == "demandes" || id == "delai" || id == "traitees" || id == "issue") {
            f.page = "demandes";
            f.periodField = "Date";
            f.from = from;
            f.to = to;
            f.what = "Demandes en ligne" + when;
            if (id == "traitees" && !seriesName.empty()) {
                f.values.push_back({"R\xC3\xA9pondue par", seriesName});
                f.what += " \xC2\xB7 r\xC3\xA9pondues par " + seriesName;
            }
        } else if (id == "attente" || id == "ancienne") {
            f.page = "demandes";
            f.what = "Demandes en ligne en attente";
        } else if (id == "pc" || id == "mouvements-pc" || id == "decouvertes") {
            f.page = "activations";
            if (id == "mouvements-pc") {
                f.periodField = "Activ\xC3\xA9" "e le";
                f.from = from;
                f.to = to;
                f.what = "PC activ\xC3\xA9s" + when;
            } else {
                f.values.push_back({"\xC3\x89tat", "active"});
                if (id == "pc" && !seriesName.empty() && seriesName != "D\xC3\xA9" "couverte") f.values.push_back({"\xC3\x89" "dition", seriesName});
                f.what = "PC actifs" + std::string(seriesName.empty() ? "" : " \xC2\xB7 " + seriesName);
            }
        } else if (id == "flottantes") {
            f.page = "flottantes";
            f.what = "Licences flottantes" + when;
        } else if (id == "jours") {
            f.page = "journal";
            f.what = "Journal des admins";
        } else {
            return;
        }
        const std::string said = openList(host_, *this, f, ws().now());
        if (host_.flash && !said.empty()) host_.flash(said, Tone::Info);
    }

    // ---- Relever maintenant (l'admin+) ----
    void takeSnapshot() {
        const Workspace* w = &ws();
        auto out = std::make_shared<stats::SnapshotReply>();
        std::weak_ptr<bool> alive = alive_;
        if (!host_.runSlow) return;
        (void)host_.runSlow(
            "Statistiques : relev\xC3\xA9 du jour",
            [w, out] {
                OpResult r = fetchSnapshot(*w, *out);
                if (r.ok && r.message.empty())
                    r.message = out->taken ? "Relev\xC3\xA9 du " + ddmmyyyy(out->day) + " pris." : "Le relev\xC3\xA9 du " + ddmmyyyy(out->day) + " \xC3\xA9tait d\xC3\xA9j\xC3\xA0 pris.";
                return r;
            },
            [this, alive](const OpResult& r) {
                if (alive.expired() || !r.ok) return;
                sig_.clear();
                data_ = {};
                load();
            });
    }

    // ---- Exporter : les chiffres de l'onglet en CSV ----
    void exportCsv() {
        std::vector<std::string> keys = groupsFor(tab_);
        if (keys.empty()) keys = {"principal", "pc"};
        const Data d = data_;
        if (!host_.pickFile) return;
        ui::FilePick pick;
        pick.title = "Exporter les statistiques";
        pick.filters = {{"CSV (Excel)", "csv"}};
        pick.save = true;
        pick.start = "statistiques-" + stats::dayOf(ws().now()) + ".csv";
        ShellHost h = host_;
        host_.pickFile(pick, [h, d, keys](std::string path) {
            if (path.empty()) return;
            if (path.size() < 4 || path.substr(path.size() - 4) != ".csv") path += ".csv";
            std::string csv = "\xEF\xBB\xBF" "Groupe;Du;Au;Mesure;D\xC3\xA9tail;Valeur;Comparaison\r\n";
            int lines = 0;
            for (const auto& g : keys) {
                const stats::SeriesReply* r = d.reply(g);
                if (!r) continue;
                for (const auto& s : r->series) {
                    if (s.refused) continue;
                    const auto row = [&](std::size_t i, const std::string& detail, const std::vector<double>& pts, const std::vector<double>* before) {
                        if (i >= r->buckets.size() || i >= pts.size()) return;
                        const auto num = [](double v) {
                            if (!stats::isNumber(v)) return std::string();
                            char b[40];
                            std::snprintf(b, sizeof b, "%.2f", v);
                            std::string t = b;
                            for (char& c : t)
                                if (c == '.') c = ',';
                            return t;
                        };
                        csv += g + ";" + r->buckets[i].from + ";" + r->buckets[i].to + ";" + stats::measureLabel(s.name) + " (" + s.unit + ");" + detail + ";" +
                               num(pts[i]) + ";" + (before && i < before->size() ? num((*before)[i]) : std::string()) + "\r\n";
                        ++lines;
                    };
                    for (std::size_t i = 0; i < s.points.size(); ++i) row(i, "", s.points, s.hasComparison() ? &s.before : nullptr);
                    for (const auto& p : s.parts)
                        for (std::size_t i = 0; i < p.points.size(); ++i) row(i, p.key.empty() ? std::string("(sans)") : p.key, p.points, nullptr);
                }
            }
            std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
            f << csv;
            if (!f) {
                h.report(OpResult::fail("le fichier ne s'\xC3\xA9" "crit pas : " + path));
                return;
            }
            OpResult r = OpResult::done("Statistiques export\xC3\xA9" "es : " + std::to_string(lines) + " lignes.");
            r.output = path;
            h.report(r);
        });
    }

    std::vector<TabButton*>          tabs_;
    ui::Button*                      export_{nullptr};
    ui::Button*                      snapshot_{nullptr};
    ui::Button*                      deploy_{nullptr};
    ui::Button*                      retry_{nullptr};
    SegGroup*                        range_{nullptr};
    SegGroup*                        compare_{nullptr};
    SegGroup*                        step_{nullptr};
    SegGroup*                        software_{nullptr};
    ui::DatePicker*                  from_{nullptr};
    ui::DatePicker*                  to_{nullptr};
    std::vector<CardGrid*>           grids_;
    std::vector<Tab>                 gridTabs_;
    std::vector<std::unique_ptr<Slot>> slots_;
    std::vector<Tab>                 slotTabs_;
    AdminsCard*                      adminsCard_{nullptr};
    ui::Widget*                      updates_{nullptr};
    ui::Widget*                      globe_{nullptr};
    Tab                              tab_{Tab::Overview};
    Choice                           choice_;
    Data                             data_;
    std::string                      sig_, note_, lastNote_, failed_;
    bool                             tooOld_{false}, loading_{false}, wantLoad_{false};
    float                            titleY_{0}, tabsBottom_{0}, barY_{0}, barBottom_{0}, periodRight_{0}, hintX_{0}, rebuiltY_{-1};
    std::vector<std::string>         rebuiltLines_;
    bool                             periodInTabs_{true}, periodInBar_{false};
    std::string                      hint_;
    std::vector<gfx::Point>          seps_;
    std::vector<std::pair<gfx::Point, std::string>> labels_;
    gfx::Point                       arrow_{};
    float                            barLastY_{0};
    Rect                             bannerRect_{};
    std::shared_ptr<bool>            alive_{std::make_shared<bool>(true)};
    core::ConnectionScope            links_;
    StatsJob                    job_;   // le releve a part (StatsKit.hpp)
};

} // namespace

PagePtr makeStatsPage(ShellHost host) { return std::make_unique<StatsPage>(std::move(host)); }

} // namespace license_admin::gui
