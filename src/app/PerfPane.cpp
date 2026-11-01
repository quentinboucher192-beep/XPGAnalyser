// =============================================================================
//  app/PerfPane.cpp - 1.22.2 : l'onglet Performances (voir PerfPane.hpp)
// =============================================================================
#include "PerfPane.hpp"

#include "../ui/Theme.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <variant>

namespace app {

namespace pf = core::perf;

namespace {

const gfx::FontId kTiny{11};
const gfx::FontId kSmall{12};
const gfx::FontId kBody{13};
constexpr float kPad = 8.f;
constexpr float kChipH = 22.f;      // une puce, un bouton du haut
constexpr float kLine = 17.f;       // une ligne du resume
constexpr float kJobLine = 16.f;    // une ligne des travaux de fond
constexpr float kCaption = 18.f;    // le titre d'un tableau
constexpr double kRefreshSeconds = 0.25;
const char* const kMid = " \xC2\xB7 ";
const char* const kDots = "\xE2\x80\xA6";

double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s(buf);
    for (auto& ch : s)
        if (ch == '.') ch = ',';
    return s;
}

std::string thousands(std::uint64_t v) {
    const std::string digits = std::to_string(v);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ' ';
        out += digits[i];
    }
    return out;
}

std::string span(double seconds) {
    const auto s = static_cast<long long>(std::max(0.0, seconds));
    if (s < 60) return std::to_string(s) + " s";
    if (s < 3600) return std::to_string(s / 60) + " min " + std::to_string(s % 60) + " s";
    return std::to_string(s / 3600) + " h " + std::to_string((s / 60) % 60) + " min";
}

std::string lowered(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// Le haut de l'echelle : 1, 2, 5 x 10^n.
double niceTop(double v) {
    if (v <= 0) return 1.0;
    const double p = std::pow(10.0, std::floor(std::log10(v)));
    for (const double m : {1.0, 2.0, 5.0, 10.0})
        if (m * p >= v) return m * p;
    return 10.0 * p;
}

// Le texte coupe a la largeur, termine par ... ; jamais au milieu d'un caractere.
std::string fit(const ui::PaintContext& ctx, const std::string& s, gfx::FontId f, float w) {
    if (w <= 0.f) return {};
    if (ctx.r.measure(s, f).width <= w) return s;
    const float dots = ctx.r.measure(kDots, f).width;
    auto n = std::min(ctx.r.fitCharacters(s, f, std::max(0.f, w - dots)), s.size());
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
    return s.substr(0, n) + kDots;
}

float textY(const ui::PaintContext& ctx, const gfx::Rect& r, gfx::FontId f) { return r.y + (r.h - ctx.r.lineHeight(f)) * 0.5f; }

// Le cadre de la courbe : en haut l'echelle, puis le temps d'une image (les barres), en bas la cadence
// (les images par seconde, un trait).
float fpsHeight(const gfx::Rect& chart) { return std::clamp(chart.h * 0.26f, 16.f, 44.f); }
gfx::Rect plotOf(const gfx::Rect& chart) {
    return {chart.x + 6.f, chart.y + 16.f, std::max(0.f, chart.w - 12.f), std::max(0.f, chart.h - 25.f - fpsHeight(chart))};
}
gfx::Rect fpsOf(const gfx::Rect& chart) {
    const gfx::Rect plot = plotOf(chart);
    return {plot.x, plot.bottom() + 4.f, plot.w, fpsHeight(chart)};
}

// Ce que mesure une partie (son infobulle). Les autres : leur nom seul.
const char* describe(std::string_view name) {
    struct D {
        const char* name;
        const char* text;
    };
    static const D kDesc[] = {
        {"(hors mesure)", "Le reste de l'image : ce qu'aucune partie mesur\xC3\xA9" "e ne couvre."},
        {"evenements", "Les clics, les touches, la souris, et ce qu'ils d\xC3\xA9" "clenchent. Sous Windows, d\xC3\xA9placer ou redimensionner "
                       "la fen\xC3\xAAtre bloque aussi ici."},
        {"app.instances", "Une autre instance lanc\xC3\xA9" "e, l'annonce du projet aux autres fen\xC3\xAAtres."},
        {"app.messages", "Les avis entre les parties de l'appli, livr\xC3\xA9s une fois par image."},
        {"app.fichiers", "Les fichiers d\xC3\xA9pos\xC3\xA9s, l'explorateur de fichiers, les archives finies."},
        {"app.fenetre", "Le titre et l'ic\xC3\xB4ne de la fen\xC3\xAAtre."},
        {"app.navigation", "Les \xC3\xA9" "crans et les dialogues qui s'ouvrent ou se ferment."},
        {"app.nouveautes", "La fen\xC3\xAAtre des nouveaut\xC3\xA9s et ses rep\xC3\xA8res."},
        {"app.memoires", "Les filtres et le zoom retenus, \xC3\xA9" "crits sur le disque."},
        {"simulation.api", "La simulation de l'automate."},
        {"simulation.ihm", "Le moteur de l'IHM en simulation : les scripts, les expressions, les variables."},
        {"liaisons", "La liaison Modbus vers l'automate."},
        {"equipements", "Les \xC3\xA9quipements du r\xC3\xA9seau : liaisons, \xC3\xA9quipements simul\xC3\xA9s, pings."},
        {"enregistrement-auto", "La reprise : les modifications gard\xC3\xA9" "es \xC3\xA0 part toutes les 2 minutes."},
        {"notifications", "Les notifications de l'IHM."},
        {"incidents", "La surveillance des incidents."},
        {"acces-web", "L'acc\xC3\xA8s web : les navigateurs qui regardent la vue."},
        {"tutoriel", "Le tutoriel en cours."},
        {"mise-a-jour", "La mise \xC3\xA0 jour des \xC3\xA9" "crans avant le dessin. Ses parties sont mesur\xC3\xA9" "es \xC3\xA0 part."},
        {"mise-en-page", "La mise en page des panneaux, des listes et des onglets."},
        {"ecran.barres", "La barre du haut, Annuler et R\xC3\xA9tablir, la pastille de simulation."},
        {"ecran.travaux", "Les imports et exports suivis, les macros, le didacticiel."},
        {"ecran.api", "Les volets de l'automate : tables d'animation, simulation."},
        {"ecran.bande-etat", "La bande d'\xC3\xA9tat du bas."},
        {"ecran.build", "Le build de l'IHM, la Console, les diagnostics en direct."},
        {"ecran.recherche", "Chercher des fichiers."},
        {"peinture", "Le dessin de la fen\xC3\xAAtre. Ses parties sont mesur\xC3\xA9" "es \xC3\xA0 part."},
        {"peinture.tableaux", "Le dessin des tableaux (toutes les listes de l'appli)."},
        {"peinture.arbres", "Le dessin des arbres (l'explorateur du projet...)."},
        {"peinture.code", "Le dessin des \xC3\xA9" "diteurs de code et des champs de texte longs."},
        {"peinture.captures", "Les captures : F12, l'image pour l'acc\xC3\xA8s web et pour les incidents."},
        {"peinture.presenter", "L'envoi de l'image \xC3\xA0 l'\xC3\xA9" "cran. Il attend la synchronisation de l'\xC3\xA9" "cran : "
                               "qu'il prenne le reste de l'image est normal."},
        {"fenetres-detachees", "Les onglets d\xC3\xA9tach\xC3\xA9s : leur mise \xC3\xA0 jour et leur dessin."},
        {"ia", "L'Assistant IA \xC3\xA0 chaque image. Ses parties sont mesur\xC3\xA9" "es \xC3\xA0 part."},
        {"ia.tick", "L'Assistant IA : ce qui revient des fils de fond, la r\xC3\xA9ponse en direct, les missions."},
        {"ia.tick.analyses", "Les analyses de pi\xC3\xA8" "ces jointes et les classeurs relus, finis."},
        {"ia.tick.pont", "Les demandes de l'IA (un scan du r\xC3\xA9seau) et leur compte rendu."},
        {"ia.tick.application", "Une application de fond finie : sa commande, pos\xC3\xA9" "e sur ce fil."},
        {"ia.tick.verifications", "Les v\xC3\xA9rifications de fond finies : les comptes, la mission."},
        {"ia.tick.missions", "Les missions : la reprise, le projet qui change, les audits finis."},
        {"ia.tick.direct", "La r\xC3\xA9ponse de l'IA qui arrive, en direct."},
        {"ia.fin-reponse", "La r\xC3\xA9ponse finie : la proposition, son empreinte, son enregistrement."},
        {"ia.empreinte", "L'empreinte de ce qu'une proposition appliqu\xC3\xA9" "e touche (dire si Ctrl+Z l'a reprise)."},
        {"ia.copie-projet", "Une copie du projet pour un travail de fond (v\xC3\xA9rifier, appliquer, auditer)."},
        {"ia.panneau", "Le panneau de l'Assistant : sa mise \xC3\xA0 jour."},
        {"ia.panneau.sync", "Le panneau suit l'Assistant : la liste, le contexte, les boutons."},
        {"ia.panneau.vignettes", "Les v\xC3\xA9rifications des cartes finies : la discussion refaite."},
        {"ia.panneau.reponse", "Une r\xC3\xA9ponse qui vient de finir : l'avis, si tu ne regardes pas."},
        {"ia.chargement", "Le premier chargement de l'Assistant (ses conversations, ses missions)."},
        {"ia.panneau.notifications", "L'onglet Notifications de l'Assistant."},
        {"ia.panneau.missions", "Les missions : la liste, la bande, la pastille."},
        {"ia.panneau.etat-applique", "L'\xC3\xA9tat r\xC3\xA9" "el des propositions appliqu\xC3\xA9" "es, relu 4 fois par seconde."},
        {"ia.panneau.mise-en-page", "La mise en page du panneau de l'Assistant."},
        {"ia.missions.tableaux", "Les tableaux de bord des missions ouverts au centre."},
        {"ia.peinture.panneau", "Le dessin du panneau de l'Assistant (hors discussion)."},
        {"ia.peinture.discussion", "Le dessin de la discussion."},
        {"ia.peinture.carte", "Le dessin d'une carte de proposition."},
        {"ia.carte", "La vignette d'une proposition : son empreinte, sa v\xC3\xA9rification, son plan."},
        {"ia.carte.empreinte", "L'empreinte du projet pour une carte (relue chaque seconde ou quand le projet change)."},
        {"ia.carte.verification", "La v\xC3\xA9rification d'une petite proposition, sur ce fil."},
        {"ia.plan-en-cours", "Le plan d'action en cours (au-dessus de la discussion)."},
        {"ia.discussion.markdown", "La mise en forme du texte des messages (markdown)."},
        {"ia.discussion.mesure", "La mise en page de la discussion."},
        {"ia.discussion.refaire", "La discussion refaite (une r\xC3\xA9ponse, une carte qui change)."},
        {"ia.peinture.plan", "Le dessin du plan d'action en cours."},
        {"ia.peinture.bande-mission", "Le dessin de la bande Mission IA."},
        {"ia.peinture.tableau-mission", "Le dessin du tableau de bord d'une mission."},
        {"ia.peinture.missions", "Le dessin de l'onglet Missions IA."},
        {"ia.peinture.notifications", "Le dessin de l'onglet Notifications de l'Assistant."},
        {"ia.mission.bande", "La bande Mission IA refaite."},
        {"ia.mission.tableau", "Le tableau de bord d'une mission refait."},
        {"ia.missions.onglet", "L'onglet Missions IA refait."},
    };
    for (const auto& d : kDesc)
        if (name == d.name) return d.text;
    return nullptr;
}

const char* jobLabel(std::string_view name) {
    struct D {
        const char* name;
        const char* text;
    };
    static const D kJobs[] = {
        {"ia.appel", "l'appel \xC3\xA0 l'IA"},
        {"ia.liste-modeles", "la liste des mod\xC3\xA8les de l'IA"},
        {"ia.verification", "une v\xC3\xA9rification de proposition"},
        {"ia.verification-carte", "la v\xC3\xA9rification d'une carte"},
        {"ia.application", "une application de proposition"},
        {"ia.audit-mission", "l'audit final d'une mission"},
        {"ia.lecture-classeur", "la lecture d'un classeur"},
        {"build-ihm", "le build de l'IHM"},
        {"import", "un import"},
        {"recherche-fichiers", "une recherche de fichiers"},
        {"archive", "une archive du projet"},
    };
    for (const auto& d : kJobs)
        if (name == d.name) return d.text;
    return nullptr;
}

std::string jobText(const pf::JobInfo& j) {
    std::string s = j.name;
    if (const char* l = jobLabel(j.name)) s += std::string(" (") + l + ")";
    return s;
}

ui::TableView::Column column(std::string title, float width, bool right) {
    ui::TableView::Column c;
    c.title = std::move(title);
    c.width = width;
    c.minWidth = 36.f;
    c.align = right ? ui::Align::End : ui::Align::Start;
    return c;
}

} // namespace

// ============================================================== les deux tableaux ====
class PerfPane::PartsModel final : public ui::ITableModel {
public:
    struct Row {
        std::string   name;
        std::string   cells[6];
        std::int64_t  self{0};
        std::int64_t  worst{0};
        double        perFrame{0.0};
        std::uint64_t calls{0};
        bool          quiet{false};     // le temps qui n'est pas du travail : le reste, l'attente de l'ecran
        bool          slow{false};      // sa pire image passe le seuil des saccades
        std::string   tip;
    };
    std::vector<Row> rows;

    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 6; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Partie", "Part", "Moy./image", "Pire image", "Appels", "Cumul"};
        return c < 6 ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows.size() && c < 6 ? rows[r].cells[c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle st;
        if (r >= rows.size()) return st;
        if (rows[r].quiet) st.fgTone = ui::Tone::Muted;
        if (c == 3 && rows[r].slow) st.fgTone = ui::Tone::Warning;
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        if (a >= rows.size() || b >= rows.size()) return false;
        const Row& x = rows[a];
        const Row& y = rows[b];
        switch (c) {
            case 0:  return x.name < y.name;
            case 2:  return x.perFrame < y.perFrame;
            case 3:  return x.worst < y.worst;
            case 4:  return x.calls < y.calls;
            default: return x.self < y.self;      // la part, le cumul
        }
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < rows.size() ? rows[r].tip : std::string{}; }
};

class PerfPane::StuttersModel final : public ui::ITableModel {
public:
    struct Row {
        std::uint64_t serial{0};
        std::int64_t  frameNs{0};
        std::string   cells[3];
        std::string   tip;
    };
    std::vector<Row> rows;      // de la plus ancienne a la plus recente (le tableau trie par l'heure, la plus recente en haut)

    [[nodiscard]] std::size_t rowCount() const override { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const override { return 3; }
    [[nodiscard]] std::string headerText(std::size_t c) const override {
        static const char* const kTitles[] = {"Heure", "Dur\xC3\xA9" "e", "Ce qui l'a prise"};
        return c < 3 ? kTitles[c] : "";
    }
    [[nodiscard]] std::string cellText(ui::RowIndex r, std::size_t c) const override {
        return r < rows.size() && c < 3 ? rows[r].cells[c] : std::string{};
    }
    [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex r, std::size_t c) const override {
        ui::CellStyle st;
        if (r < rows.size() && c == 1)
            st.fgTone = rows[r].frameNs >= static_cast<std::int64_t>(pf::kTrailStutterMs) * 1'000'000 ? ui::Tone::Error : ui::Tone::Warning;
        return st;
    }
    [[nodiscard]] bool less(ui::RowIndex a, ui::RowIndex b, std::size_t c) const override {
        if (a >= rows.size() || b >= rows.size()) return false;
        if (c == 1) return rows[a].frameNs < rows[b].frameNs;
        if (c == 2) return rows[a].cells[2] < rows[b].cells[2];
        return rows[a].serial < rows[b].serial;
    }
    [[nodiscard]] std::string rowTooltip(ui::RowIndex r) const override { return r < rows.size() ? rows[r].tip : std::string{}; }
};

// ============================================================== le volet ====
PerfPane::PerfPane(std::string id) : ui::Widget(std::move(id)) {
    partsModel_ = std::make_shared<PartsModel>();
    stuttersModel_ = std::make_shared<StuttersModel>();
    auto parts = std::make_unique<ui::TableView>(this->id() + ".parties");
    parts->setColumns({column("Partie", 190.f, false), column("Part", 58.f, true), column("Moy./image", 82.f, true),
                       column("Pire image", 80.f, true), column("Appels", 62.f, true), column("Cumul", 70.f, true)});
    parts->setSelectionMode(ui::SelectionMode::Extended);
    parts->setExportName("performances");
    parts->setModel(partsModel_);
    parts_ = &static_cast<ui::TableView&>(addChild(std::move(parts)));
    auto list = std::make_unique<ui::TableView>(this->id() + ".saccades");
    list->setColumns({column("Heure", 104.f, false), column("Dur\xC3\xA9" "e", 64.f, true), column("Ce qui l'a prise", 360.f, false)});
    list->setSelectionMode(ui::SelectionMode::Extended);
    list->setExportName("saccades");
    list->setModel(stuttersModel_);
    list->sortBy(0, ui::SortOrder::Descending);      // la plus recente en haut
    stutters_ = &static_cast<ui::TableView&>(addChild(std::move(list)));
    refresh();
}

PerfPane::~PerfPane() = default;

bool PerfPane::tick(bool shown) {
    if (!shown) {
        wasShown_ = false;
        return false;
    }
    const double now = steadySeconds();
    if (wasShown_ && lastRefresh_ >= 0.0 && now >= lastRefresh_ && now - lastRefresh_ < kRefreshSeconds) return false;
    wasShown_ = true;
    refresh();
    return true;
}

void PerfPane::refresh() {
    lastRefresh_ = steadySeconds();
    win_ = pf::window(window_);
    timeline_ = pf::timeline(60);
    proc_ = pf::processStats();
    running_ = pf::runningJobs();
    finished_ = pf::finishedJobs(5);
    stutterTotal_ = pf::stutterCount();
    const std::string label = pf::windowLabel(window_);
    if (win_.frames == 0) {
        line1_ = "Aucune image mesur\xC3\xA9" "e (" + label + ")";
    } else {
        line1_ = fixed(win_.fps(), 1) + " images/s" + kMid + "image " + pf::formatMs(win_.averageFrameMs()) + kMid + "pire "
                 + pf::formatMs(static_cast<double>(win_.worstFrameNs) / 1e6) + " (" + label + ")";
    }
    line2_ = proc_.memoryKnown ? "M\xC3\xA9moire " + pf::formatBytes(proc_.workingSet) + " (priv\xC3\xA9" "e " + pf::formatBytes(proc_.privateBytes) + ")"
                               : std::string("M\xC3\xA9moire inconnue");
    if (proc_.cpuKnown && proc_.cpuPercent >= 0.0) line2_ += kMid + std::string("processeur ") + fixed(proc_.cpuPercent, 0) + " %";
    partsCaption_ = "Ce qui consomme \xE2\x80\x94 " + label + ", temps propre (sans les parties mesur\xC3\xA9" "es dedans)";
    refreshParts();
    refreshStutters();
    invalidate();
}

void PerfPane::refreshParts() {
    // Ce qui etait choisi (par nom) et la place du defilement : la relecture les garde.
    std::vector<std::string> chosen;
    for (const auto r : parts_->selectedModelRows())
        if (r < partsModel_->rows.size()) chosen.push_back(partsModel_->rows[r].name);
    const float scroll = parts_->scrollOffset();
    auto& rows = partsModel_->rows;
    rows.clear();
    const double total = static_cast<double>(std::max<std::int64_t>(1, win_.frameNs));
    const double frames = static_cast<double>(std::max<std::uint64_t>(1, win_.frames));
    const std::int64_t threshold = static_cast<std::int64_t>(pf::stutterThresholdMs()) * 1'000'000;
    const std::string needle = lowered(search_);
    const std::string label = pf::windowLabel(window_);
    for (const auto& p : win_.parts) {
        PartsModel::Row row;
        row.name = p.name;
        if (!needle.empty() && lowered(row.name).find(needle) == std::string::npos) continue;
        row.self = p.selfNs;
        row.worst = p.worstSelfNs;
        row.calls = p.calls;
        row.perFrame = static_cast<double>(p.selfNs) / frames;
        row.quiet = p.part == pf::kUnmeasured || row.name == "peinture.presenter";
        row.slow = p.worstSelfNs >= threshold;
        const std::string share = pf::formatPercent(static_cast<double>(p.selfNs) / total);
        const std::string avg = pf::formatMs(row.perFrame / 1e6);
        const std::string worst = pf::formatMs(static_cast<double>(p.worstSelfNs) / 1e6);
        const std::string cumul = pf::formatMs(static_cast<double>(p.selfNs) / 1e6);
        row.cells[0] = row.name;
        row.cells[1] = share;
        row.cells[2] = avg;
        row.cells[3] = worst;
        row.cells[4] = thousands(p.calls);
        row.cells[5] = cumul;
        row.tip = row.name;
        if (const char* d = describe(row.name)) row.tip += std::string("\n") + d;
        row.tip += "\nTemps propre (" + label + ") : " + cumul + ", " + share + " du temps des images ; " + avg + " par image ; pire image " + worst + ".";
        row.tip += "\nAvec ce qu'elle appelle : " + pf::formatMs(static_cast<double>(p.inclNs) / 1e6 / frames) + " par image ; pire image "
                   + pf::formatMs(static_cast<double>(p.worstInclNs) / 1e6) + ".";
        row.tip += "\nAppels : " + thousands(p.calls) + " (" + fixed(static_cast<double>(p.calls) / frames, 1) + " par image).";
        rows.push_back(std::move(row));
    }
    partsModel_->modelReset->emit();
    std::vector<ui::RowIndex> again;
    for (std::size_t r = 0; r < rows.size(); ++r)
        if (std::find(chosen.begin(), chosen.end(), rows[r].name) != chosen.end()) again.push_back(static_cast<ui::RowIndex>(r));
    if (!again.empty() || !parts_->selection().empty()) parts_->selectModelRows(std::move(again), false);
    parts_->setScrollOffset(scroll);
}

void PerfPane::refreshStutters() {
    const int threshold = pf::stutterThresholdMs();
    // Le journal ne change qu'avec une nouvelle saccade, le seuil, la remise a zero.
    if (stutterTotal_ == stutterSeen_ && threshold == thresholdSeen_) return;
    stutterSeen_ = stutterTotal_;
    thresholdSeen_ = threshold;
    std::vector<std::uint64_t> chosen;
    for (const auto r : stutters_->selectedModelRows())
        if (r < stuttersModel_->rows.size()) chosen.push_back(stuttersModel_->rows[r].serial);
    const float scroll = stutters_->scrollOffset();
    auto& rows = stuttersModel_->rows;
    rows.clear();
    const std::int64_t floorNs = static_cast<std::int64_t>(threshold) * 1'000'000;
    for (const auto& s : pf::stutters()) {
        if (s.frameNs < floorNs) continue;
        StuttersModel::Row row;
        row.serial = s.serial;
        row.frameNs = s.frameNs;
        row.cells[0] = pf::stutterClock(s);
        row.cells[1] = pf::formatMs(static_cast<double>(s.frameNs) / 1e6);
        row.cells[2] = pf::stutterParts(s);
        row.tip = "Image de " + row.cells[1] + ", finie \xC3\xA0 " + row.cells[0] + ".";
        if (s.count > 0) row.tip += "\nCe qui l'a prise (temps propre) :";
        for (int k = 0; k < s.count; ++k) {
            const char* name = pf::partName(s.top[k]);
            row.tip += std::string("\n  ") + name + " : " + pf::formatMs(static_cast<double>(s.topNs[k]) / 1e6);
            if (const char* d = describe(name)) row.tip += std::string(" \xE2\x80\x94 ") + d;
        }
        rows.push_back(std::move(row));
    }
    stuttersCaption_ = "Saccades d'au moins " + std::to_string(threshold) + " ms \xE2\x80\x94 " + std::to_string(rows.size())
                       + (rows.size() > 1 ? " gard\xC3\xA9" "es" : " gard\xC3\xA9" "e") + " (" + std::to_string(pf::kStutterCapacity) + " au plus)";
    stuttersModel_->modelReset->emit();
    std::vector<ui::RowIndex> again;
    for (std::size_t r = 0; r < rows.size(); ++r)
        if (std::find(chosen.begin(), chosen.end(), rows[r].serial) != chosen.end()) again.push_back(static_cast<ui::RowIndex>(r));
    if (!again.empty() || !stutters_->selection().empty()) stutters_->selectModelRows(std::move(again), false);
    stutters_->setScrollOffset(scroll);
}

void PerfPane::setWindowSeconds(int seconds) {
    window_ = seconds == 10 || seconds == 60 ? seconds : 0;
    refresh();
}

void PerfPane::setThresholdMs(int ms) {
    pf::setStutterThresholdMs(ms);
    refresh();
}

void PerfPane::resetAll() {
    pf::reset();
    stutterSeen_ = ~std::uint64_t{0};
    refresh();
    said->emit("Mesures remises \xC3\xA0 z\xC3\xA9ro.");
}

std::string PerfPane::reportText() const { return pf::report(window_); }

void PerfPane::copyReport() {
    ui::setClipboardText(reportText());
    said->emit("Rapport copi\xC3\xA9 dans le presse-papiers : colle-le dans ton message.");
}

void PerfPane::setSearch(std::string text) {
    if (text == search_) return;
    search_ = std::move(text);
    refreshParts();
    invalidate();
}

std::string PerfPane::statusText() const {
    return "Mesur\xC3\xA9 sur le fil de l'interface depuis " + span(pf::measuredSeconds()) + kMid + thousands(stutterTotal_) + " saccade"
           + (stutterTotal_ > 1 ? "s" : "") + " d'au moins " + std::to_string(pf::stutterThresholdMs()) + " ms" + kMid
           + "Copier le rapport : pour l'envoyer";
}

bool PerfPane::active(int action) const noexcept {
    switch (action) {
        case kWindow10:     return window_ == 10;
        case kWindow60:     return window_ == 60;
        case kWindowAll:    return window_ == 0;
        case kThreshold33:  return pf::stutterThresholdMs() == 33;
        case kThreshold50:  return pf::stutterThresholdMs() == 50;
        case kThreshold100: return pf::stutterThresholdMs() == 100;
        case kThreshold250: return pf::stutterThresholdMs() == 250;
        default:            return false;
    }
}

void PerfPane::trigger(int action) {
    switch (action) {
        case kWindow10:     setWindowSeconds(10); break;
        case kWindow60:     setWindowSeconds(60); break;
        case kWindowAll:    setWindowSeconds(0); break;
        case kThreshold33:  setThresholdMs(33); break;
        case kThreshold50:  setThresholdMs(50); break;
        case kThreshold100: setThresholdMs(100); break;
        case kThreshold250: setThresholdMs(250); break;
        case kReset:        resetAll(); break;
        case kCopy:         copyReport(); break;
        default:            break;
    }
}

bool PerfPane::actionRect(int action, gfx::Rect& out) const {
    for (const auto& h : hits_)
        if (h.action == action && action != 0) {
            out = h.r;
            return true;
        }
    return false;
}

// ============================================================== la mise en page ====
void PerfPane::layoutHeader(float x, float y, float w) {
    hits_.clear();
    float cx = x, cy = y;
    const float right = x + w;
    struct Item {
        int         action;
        std::string label, tip;
        bool        button;
    };
    // Un groupe passe a la rangee suivante s'il ne tient plus sur celle-ci.
    const auto group = [&](const std::vector<Item>& items, bool alignRight) {
        std::vector<float> widths;
        float total = 0.f;
        for (const auto& it : items) {
            const float wd = ui::measureWidth(it.label, kSmall) + (it.action == 0 ? 0.f : it.button ? 24.f : 18.f);
            widths.push_back(wd);
            total += wd + (it.action == 0 ? 6.f : 4.f);
        }
        if (alignRight && cx + 12.f + total <= right) cx = right - total + 4.f;
        else if (cx > x && cx + total > right) {
            cx = x;
            cy += kChipH + 6.f;
        }
        for (std::size_t k = 0; k < items.size(); ++k) {
            hits_.push_back({items[k].action, items[k].label, items[k].tip, {cx, cy, widths[k], kChipH}, items[k].button});
            cx += widths[k] + (items[k].action == 0 ? 6.f : 4.f);
        }
        cx += 14.f;
    };
    group({{0, "Fen\xC3\xAAtre", {}, false},
           {kWindow10, "10 s", "Les 10 derni\xC3\xA8res secondes (ce qui se passe maintenant).", false},
           {kWindow60, "60 s", "La derni\xC3\xA8re minute.", false},
           {kWindowAll, "Depuis le lancement", "Depuis le lancement de l'appli (ou depuis Remettre \xC3\xA0 z\xC3\xA9ro).", false}},
          false);
    group({{0, "Saccades d\xC3\xA8s", {}, false},
           {kThreshold33, "33 ms", "Une image de 33 ms et plus : moins de 30 images par seconde.", false},
           {kThreshold50, "50 ms", "Une image de 50 ms et plus : on la sent (le r\xC3\xA9glage d'origine).", false},
           {kThreshold100, "100 ms", "Une image de 100 ms et plus : l'appli accroche.", false},
           {kThreshold250, "250 ms", "Une image de 250 ms et plus : l'appli se fige (elle va aussi dans l'historique interne).", false}},
          false);
    group({{kReset, "Remettre \xC3\xA0 z\xC3\xA9ro", "Vider les mesures, la courbe et le journal des saccades, et repartir de maintenant.", true},
           {kCopy, "Copier le rapport",
            "Copier un rapport texte : la configuration, les 20 parties qui consomment le plus, les 20 derni\xC3\xA8res saccades. "
            "Colle-le dans ton message.",
            true}},
          true);
    headerH_ = (cy - y) + kChipH + 5.f + 7.f;
}

void PerfPane::layoutLeft(const gfx::Rect& r) {
    summary_ = {r.x, r.y, r.w, 2.f * kLine};
    const float jobsH = std::min(3.f * kJobLine, std::max(0.f, r.h - 2.f * kLine - 50.f));
    chart_ = {r.x, r.y + 2.f * kLine + 2.f, r.w, std::max(0.f, r.h - 2.f * kLine - 2.f - jobsH - 4.f)};
    jobs_ = {r.x, chart_.bottom() + 4.f, r.w, jobsH};
}

void PerfPane::onLayout() {
    const auto b = bounds();
    const float x0 = b.x + kPad;
    const float w = std::max(0.f, b.w - 2.f * kPad);
    layoutHeader(x0, b.y + 5.f, w);
    const float top = b.y + headerH_;
    const float h = std::max(0.f, b.bottom() - top - 4.f);
    if (w >= 860.f) {
        // Large : la courbe, ce qui consomme, les saccades - cote a cote.
        const float gap = 10.f;
        const float wa = std::floor(w * 0.27f), wb = std::floor(w * 0.44f), wc = std::max(0.f, w - wa - wb - 2.f * gap);
        layoutLeft({x0, top, wa, h});
        const float xb = x0 + wa + gap, xc = xb + wb + gap;
        partsHead_ = {xb, top, wb, kCaption};
        parts_->setBounds({xb, top + kCaption, wb, std::max(0.f, h - kCaption)});
        stuttersHead_ = {xc, top, wc, kCaption};
        stutters_->setBounds({xc, top + kCaption, wc, std::max(0.f, h - kCaption)});
    } else {
        // Etroit (le panneau a droite de l'editeur) : l'un sous l'autre.
        const float leftH = std::min(h, 2.f * kLine + 80.f + 3.f * kJobLine + 6.f);
        layoutLeft({x0, top, w, leftH});
        float y = top + leftH + 6.f;
        const float rest = std::max(0.f, b.bottom() - y - 4.f);
        const float ph = std::floor(rest * 0.6f);
        partsHead_ = {x0, y, w, kCaption};
        parts_->setBounds({x0, y + kCaption, w, std::max(0.f, ph - kCaption)});
        y += ph + 4.f;
        stuttersHead_ = {x0, y, w, kCaption};
        stutters_->setBounds({x0, y + kCaption, w, std::max(0.f, b.bottom() - y - kCaption - 4.f)});
    }
}

// ============================================================== le dessin ====
void PerfPane::onPaint(const ui::PaintContext& ctx) {
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    ctx.r.fillRect(b, c.panelBg);
    if (!hovered()) hot_ = 0;
    // ---- le haut : les puces, les boutons
    for (const auto& h : hits_) {
        if (h.action == 0) {
            ctx.r.drawText({h.r.x, textY(ctx, h.r, kSmall)}, h.label, kSmall, c.textMuted);
            continue;
        }
        const bool on = !h.button && active(h.action);
        const bool hot = hot_ == h.action;
        const float radius = h.button ? 5.f : kChipH * 0.5f;
        if (on) {
            ctx.r.fillRoundedRect(h.r, c.accent, radius);
            ctx.r.fillRoundedRect({h.r.x + 1.f, h.r.y + 1.f, h.r.w - 2.f, h.r.h - 2.f}, c.accent.withAlpha(hot ? 120 : 80), radius - 1.f);
        } else {
            ctx.r.fillRoundedRect(h.r, hot ? c.borderStrong : c.border, radius);
            ctx.r.fillRoundedRect({h.r.x + 1.f, h.r.y + 1.f, h.r.w - 2.f, h.r.h - 2.f}, hot ? c.rowAltBg : c.panelBg, radius - 1.f);
        }
        const float tw = ctx.r.measure(h.label, kSmall).width;
        const gfx::Point at{h.r.x + (h.r.w - tw) * 0.5f, textY(ctx, h.r, kSmall)};
        ctx.r.drawText(at, h.label, kSmall, c.text);
        if (on) ctx.r.drawText({at.x + 0.6f, at.y}, h.label, kSmall, c.text);   // le gras du logiciel
    }
    // ---- le resume
    if (summary_.w > 0.f) {
        const gfx::Rect l1{summary_.x, summary_.y, summary_.w, kLine};
        const gfx::Rect l2{summary_.x, summary_.y + kLine, summary_.w, kLine};
        const std::string a = fit(ctx, line1_, kBody, summary_.w);
        ctx.r.drawText({l1.x, textY(ctx, l1, kBody)}, a, kBody, c.text);
        ctx.r.drawText({l1.x + 0.6f, textY(ctx, l1, kBody)}, a, kBody, c.text);
        ctx.r.drawText({l2.x, textY(ctx, l2, kSmall)}, fit(ctx, line2_, kSmall, summary_.w), kSmall, c.textMuted);
    }
    paintChart(ctx);
    paintJobs(ctx);
    // ---- les titres des tableaux
    if (partsHead_.w > 0.f)
        ctx.r.drawText({partsHead_.x, textY(ctx, partsHead_, kSmall)}, fit(ctx, partsCaption_, kSmall, partsHead_.w), kSmall, c.textMuted);
    if (stuttersHead_.w > 0.f)
        ctx.r.drawText({stuttersHead_.x, textY(ctx, stuttersHead_, kSmall)}, fit(ctx, stuttersCaption_, kSmall, stuttersHead_.w), kSmall,
                       c.textMuted);
}

void PerfPane::paintChart(const ui::PaintContext& ctx) const {
    const gfx::Rect r = chart_;
    if (r.w < 60.f || r.h < 40.f) return;
    const auto& c = ctx.theme.color;
    ctx.r.fillRoundedRect(r, c.border, 6.f);
    ctx.r.fillRoundedRect({r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f}, c.inputBg, 5.f);
    const gfx::Rect plot = plotOf(r);
    const double threshold = static_cast<double>(pf::stutterThresholdMs());
    double worst = 0.0;
    for (const auto& s : timeline_) worst = std::max(worst, s.worstMs);
    // L'echelle : jusqu'a la pire image, sans ecraser le reste (au-dela de 4 fois le seuil : la barre depasse, un trait rouge le dit).
    const double top = niceTop(std::max({std::min(worst, 4.0 * threshold), threshold * 1.2, 20.0}));
    const auto yOf = [&](double ms) { return plot.bottom() - plot.h * static_cast<float>(std::min(ms, top) / top); };
    ctx.r.line({plot.x, yOf(1000.0 / 60.0)}, {plot.right(), yOf(1000.0 / 60.0)}, c.gridLine, 1.f);   // 60 images/s
    ctx.r.line({plot.x, yOf(threshold)}, {plot.right(), yOf(threshold)}, c.warning.withAlpha(160), 1.f);
    const std::size_t n = timeline_.size();
    if (n > 0) {
        const float step = plot.w / static_cast<float>(n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto& s = timeline_[i];
            if (!s.frames) continue;
            const float x = plot.x + step * static_cast<float>(i);
            const float y = yOf(s.worstMs);
            const gfx::Color col = s.worstMs >= static_cast<double>(pf::kTrailStutterMs) ? c.error
                                 : s.worstMs >= threshold                                ? c.warning
                                                                                         : c.accent.withAlpha(120);
            ctx.r.fillRect({x + 0.5f, y, std::max(1.f, step - 1.f), plot.bottom() - y}, col);
            if (s.worstMs > top) ctx.r.fillRect({x + 0.5f, plot.y - 3.f, std::max(1.f, step - 1.f), 2.f}, c.error);
        }
        // La moyenne de chaque seconde : un trait.
        bool have = false;
        gfx::Point prev{};
        for (std::size_t i = 0; i < n; ++i) {
            const auto& s = timeline_[i];
            if (!s.frames) {
                have = false;
                continue;
            }
            const gfx::Point p{plot.x + step * (static_cast<float>(i) + 0.5f), yOf(s.averageMs)};
            if (have) ctx.r.line(prev, p, c.text.withAlpha(210), 1.5f);
            prev = p;
            have = true;
        }
    }
    // La cadence : les images de chaque seconde finie (la seconde en cours n'est pas finie : sans elle).
    const gfx::Rect fps = fpsOf(r);
    if (n > 1 && fps.h >= 12.f) {
        ctx.r.line({fps.x, fps.y - 2.f}, {fps.right(), fps.y - 2.f}, c.gridLine, 1.f);
        std::uint32_t most = 60;
        for (const auto& s : timeline_) most = std::max(most, s.frames);
        const float step = fps.w / static_cast<float>(n);
        bool have = false;
        gfx::Point prev{};
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const auto& s = timeline_[i];
            const gfx::Point p{fps.x + step * (static_cast<float>(i) + 0.5f),
                               fps.bottom() - fps.h * static_cast<float>(s.frames) / static_cast<float>(most)};
            if (have) ctx.r.line(prev, p, c.ok, 1.5f);
            prev = p;
            have = true;
        }
        ctx.r.drawText({fps.x, fps.y}, "images/s (" + std::to_string(most) + " en haut)", kTiny, c.textMuted);
    }
    ctx.r.drawText({r.x + 6.f, r.y + 2.f}, pf::formatMs(top), kTiny, c.textMuted);
    const std::string legend = "60 derni\xC3\xA8res secondes \xC2\xB7 barres : pire image \xC2\xB7 trait : moyenne";
    const std::string shown = fit(ctx, legend, kTiny, std::max(0.f, r.w - 70.f));
    const float lw = ctx.r.measure(shown, kTiny).width;
    ctx.r.drawText({r.right() - lw - 6.f, r.y + 2.f}, shown, kTiny, c.textMuted);
}

void PerfPane::paintJobs(const ui::PaintContext& ctx) const {
    if (jobs_.h < kJobLine || jobs_.w <= 0.f) return;
    const auto& c = ctx.theme.color;
    gfx::Rect line{jobs_.x, jobs_.y, jobs_.w, kJobLine};
    const std::string head = running_.empty() ? std::string("Travaux de fond : aucun en cours")
                                              : "Travaux de fond en cours : " + std::to_string(running_.size());
    ctx.r.drawText({line.x, textY(ctx, line, kSmall)}, fit(ctx, head, kSmall, line.w), kSmall, c.textMuted);
    const auto room = static_cast<std::size_t>(std::max(0.f, (jobs_.bottom() - line.bottom()) / kJobLine));
    for (std::size_t k = 0; k < running_.size() && k < room; ++k) {
        line.y += kJobLine;
        std::string text;
        if (k + 1 == room && running_.size() > room) text = "+ " + std::to_string(running_.size() - k) + " autres";
        else text = "\xE2\x96\xB8 " + jobText(running_[k]) + kMid + "depuis " + pf::formatMs(running_[k].seconds * 1000.0);
        const bool longJob = running_[k].seconds >= 10.0;
        ctx.r.drawText({line.x + 4.f, textY(ctx, line, kSmall)}, fit(ctx, text, kSmall, line.w - 4.f), kSmall, longJob ? c.warning : c.text);
    }
}

std::string PerfPane::liveTooltip(gfx::Point m) const {
    for (const auto& h : hits_)
        if (h.action != 0 && h.r.contains(m)) return h.tip;
    if (chart_.contains(m) && !timeline_.empty()) {
        const gfx::Rect plot = plotOf(chart_);
        const float step = plot.w / static_cast<float>(timeline_.size());
        const auto i = static_cast<std::size_t>(std::clamp((m.x - plot.x) / std::max(1.f, step), 0.f, static_cast<float>(timeline_.size() - 1)));
        const auto& s = timeline_[i];
        const std::string when = s.ago == 0 ? std::string("Cette seconde") : "Il y a " + std::to_string(s.ago) + " s";
        if (!s.frames) return when + " : aucune image finie (l'appli \xC3\xA9tait fig\xC3\xA9" "e, ou pas encore mesur\xC3\xA9" "e).";
        return when + " : " + std::to_string(s.frames) + " image" + (s.frames > 1 ? "s" : "") + ", moyenne " + pf::formatMs(s.averageMs) + ", pire "
               + pf::formatMs(s.worstMs) + ".\nLes barres : la pire image de chaque seconde ; le trait : la moyenne. La ligne grise : 60 images/s "
                 "(16,7 ms) ; la ligne orange : le seuil des saccades. En bas, en vert : les images par seconde.";
    }
    if (summary_.contains(m)) {
        std::string tip = "Les images par seconde et le temps d'une image, sur la fen\xC3\xAAtre choisie.";
        tip += "\nM\xC3\xA9moire : celle que le processus occupe (Windows : l'ensemble de travail) ; priv\xC3\xA9" "e : la sienne seule.";
        tip += "\nProcesseur : sur la derni\xC3\xA8re seconde ; 100 % = un processeur logique entier (" + std::to_string(proc_.cores) + " sur ce PC).";
        if (proc_.cpuKnown) tip += "\nTemps processeur depuis le lancement : " + span(proc_.cpuSeconds) + ".";
        return tip;
    }
    if (jobs_.contains(m)) {
        std::string tip = "Les travaux sur un fil de fond : ils ne bloquent pas l'interface, mais prennent du processeur.";
        for (const auto& j : running_) tip += "\n\xE2\x96\xB8 " + jobText(j) + " : depuis " + pf::formatMs(j.seconds * 1000.0);
        if (!finished_.empty()) {
            tip += "\nFinis r\xC3\xA9" "cemment :";
            for (const auto& j : finished_)
                tip += "\n  " + jobText(j) + " : " + pf::formatMs(j.seconds * 1000.0) + " (il y a " + span(j.endedAgo) + ")";
        }
        return tip;
    }
    return {};
}

ui::EventResult PerfPane::onEvent(const ui::InputEvent& ev) {
    if (const auto* mv = std::get_if<ui::MouseMove>(&ev)) {
        int over = 0;
        for (const auto& h : hits_)
            if (h.action != 0 && h.r.contains(mv->pos)) over = h.action;
        if (over != hot_) {
            hot_ = over;
            invalidate();
        }
        return ui::EventResult::Ignored;
    }
    if (const auto* md = std::get_if<ui::MouseDown>(&ev); md && md->button == ui::MouseButton::Left) {
        for (const auto& h : hits_)
            if (h.action != 0 && h.r.contains(md->pos)) {
                trigger(h.action);
                return ui::EventResult::Consumed;
            }
    }
    return ui::EventResult::Ignored;
}

} // namespace app
