// =============================================================================
//  app/SimCenter.hpp - lot API 8 : le Centre de simulation (ses volets)
// -----------------------------------------------------------------------------
//  Le dossier Simulation de l'arbre, entre IHM et Versions, rassemble ce qui
//  etait eparpille (API > Simulation, IHM > Simulation, les jumeaux, les
//  tables d'animation, la barre du haut) et y ajoute ce qui manquait : dire EN
//  CLAIR ce qui se passe, et ou regarder.
//
//   * VUE D'ENSEMBLE (SimOverviewPane) : le bandeau d'etat en francais clair et
//     LE bouton qui repare ; trois cartes (automate, IHM, equipements) ; la
//     chaine equipements <-> automate <-> IHM, ses fleches vivantes ou coupees ;
//     ce qui merite ton attention ; les 30 dernieres secondes.
//   * FORCAGES (SimForcingPane) : tous les forcages au meme endroit - l'automate,
//     ceux poses depuis l'IHM, les cases des jumeaux - qui, quoi, depuis quand ;
//     Relacher, Tout relacher.
//   * COURBES (SimTrendsPane) : huit variables au plus (automate ou IHM), une
//     piste chacune, dans le temps ; figer, un curseur, exporter en CSV.
//   * JOURNAL (SimJournalPane) : tous les evenements de simulation, filtres par
//     source et gravite, chacun explique, avec son << Aller a >>.
//
//  Les volets DESSINENT un modele (SimCenterModel) que l'ecran remplit a chaque
//  image (SimulationWorkspace.cpp : la photo de l'automate, de l'IHM, des
//  equipements ; le calcul de SimStatus ; les echantillons des courbes). Un
//  clic demande une CLE a l'ecran (`go`) - les cles de SimJournal.hpp. Les
//  volets ne connaissent ni le simulateur ni l'IHM : ils se testent et se
//  dessinent seuls.
// =============================================================================
#pragma once

#include "SimJournal.hpp"
#include "SimStatus.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/ScrollBar.hpp"   // 1.11.4 : la barre de defilement qu'on tire

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui { class TableView; class InputText; }

namespace app {

// ------------------------------------------------------------- les donnees ----
// Un forcage, d'ou qu'il vienne.
struct SimForcing {
    enum class Source : std::uint8_t { Automate, Ihm, Equipement };
    Source      source{Source::Automate};
    std::string where;          // "Automate", "IHM", "Balance B (jumeau)"
    std::string what;           // la variable, ou la case d'un jumeau ("40001 . INT")
    std::string value;          // la valeur forcee
    std::string who;            // qui l'a posee : "l'onglet Automate", "l'IHM en simulation", "le jumeau"
    double      since{-1};      // l'heure du forcage (l'horloge du centre) ; < 0 : inconnue
    std::string sinceText;      // "12 min", "depuis 14:32:10"
    std::string key;            // pour relacher : "plc:<chemin>", "twin:<equipement>:<n>"
    // Lot API 8 : LE PROGRAMME DIRAIT - ce que le programme ecrit sous le
    // forcage (l'automate : SimulationHost::unforcedValue) ; vide : sans objet
    // (la case d'un jumeau) ou pas lue.
    std::string programSays;
};

// Une courbe : une variable de l'automate ou de l'IHM, echantillonnee aux
// instants du modele (times). NaN : pas de valeur a cet instant.
struct SimTrend {
    std::string         path;   // "Armoires[0].ana.PT1.mes" ; IHM : le nom de la variable IHM
    bool                hmi{false};
    bool                boolean{false};
    std::vector<double> values;
    std::string         last;   // la derniere valeur, telle que dite ("7.25", "TRUE")
};

class SimCenterModel {
public:
    static constexpr std::size_t kMaxTrends = 8;
    static constexpr double      kKeepSeconds = 600.0;     // dix minutes d'historique

    // Ce que l'ecran calcule (SimStatus) et ce qu'il a vu.
    simstatus::Report       report;
    std::vector<SimForcing> forcings;
    const SimJournal*       journal{nullptr};
    double                  now{0};           // l'heure de la derniere photo (secondes)
    std::uint64_t           revision{0};      // change a chaque nouveau rapport
    std::uint64_t           forcingRevision{0};   // change quand la liste des forcages change

    // ---- les courbes ---------------------------------------------------------------
    [[nodiscard]] const std::vector<SimTrend>& trends() const noexcept { return trends_; }
    [[nodiscard]] const std::vector<double>&   times() const noexcept { return times_; }
    [[nodiscard]] std::uint64_t trendRevision() const noexcept { return trendRevision_; }
    // Faux : deja la, ou deja huit (why le dit).
    bool addTrend(const std::string& path, bool hmi, bool boolean, std::string* why = nullptr);
    bool removeTrend(const std::string& path);
    void clearTrends();
    [[nodiscard]] int trendIndex(const std::string& path) const;
    // Un instant de plus : une valeur par courbe, dans l'ordre (nullopt : pas
    // lue) ; `shown` : ce qui s'affiche. Au-dela de dix minutes, le plus ancien part.
    void pushSample(double t, const std::vector<std::optional<double>>& values, const std::vector<std::string>& shown);
    // Figer : l'affichage s'arrete a `at` (les echantillons continuent).
    void setFrozen(bool on, double at);
    [[nodiscard]] bool   frozen() const noexcept { return frozen_; }
    [[nodiscard]] double frozenAt() const noexcept { return frozenAt_; }
    // La fenetre montree, en secondes (30, 120, 600).
    void setWindow(double seconds);
    [[nodiscard]] double window() const noexcept { return window_; }
    // Le CSV des courbes : l'instant (s, depuis le premier), puis une colonne par
    // courbe (separateur ;, decimales a virgule).
    [[nodiscard]] std::string trendsCsv() const;

private:
    std::vector<SimTrend> trends_;
    std::vector<double>   times_;
    std::uint64_t         trendRevision_{0};
    bool                  frozen_{false};
    double                frozenAt_{0};
    double                window_{30.0};
};

// ------------------------------------------------------------ le Centre ----
// Ce que les volets demandent a l'ecran.
struct SimCenterHosts {
    std::function<void(const std::string& key)> go;                       // une cle "aller a"
    std::function<void(const std::vector<std::string>& keys)> release;    // relacher des forcages (leurs cles)
    // Ajouter une courbe : l'ecran sait si le chemin est de l'automate ou de
    // l'IHM ("ihm:" devant force l'IHM). Faux : `why` dit pourquoi.
    std::function<bool(const std::string& path, std::string* why)> addTrend;
    std::function<void(const std::string& text)> status;                  // la barre d'etat
    // L'aide a la saisie d'un chemin (les variables de l'automate et de l'IHM).
    std::function<void(ui::InputText& field)> assist;
};

// ----------------------------------------------------- la vue d'ensemble ----
class SimOverviewPane final : public ui::Widget {
public:
    SimOverviewPane(std::string id, std::shared_ptr<SimCenterModel> model, SimCenterHosts hosts);
    // Pour les scripts et les tests : les zones cliquables (apres un dessin), par
    // leur cle - "bandeau:bouton", "bandeau:second", "carte:automate" (la carte),
    // "carte:automate:0" (son 1er bouton), "chaine:ihm" (une boite de la chaine),
    // "attention:<id>" (le bouton d'une ligne), "frise:<numero>" (un evenement),
    // "frise:journal". Vide : pas montree.
    [[nodiscard]] gfx::Rect partRect(std::string_view key) const;
    [[nodiscard]] std::vector<std::string> partKeys() const;
    // Faire ce que ferait un clic sur cette zone ; faux : pas de telle zone.
    bool activate(std::string_view key);
    [[nodiscard]] float scrollOffset() const noexcept { return scrollY_; }
    void scrollTo(float y);
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Hit {
        gfx::Rect   rect;
        std::string key;        // pour partRect
        std::string go;         // la cle demandee a l'ecran
        std::string tip;
        bool        button{false};
    };
    struct Plan {
        float     pad{16.f}, gap{12.f};
        gfx::Rect banner{}, cards[3]{}, chain{}, attention{}, timeline{};
        float     height{0.f};
        bool      wide{true};
    };
    [[nodiscard]] Plan plan(float width) const;
    [[nodiscard]] int  hitAt(gfx::Point p) const;
    [[nodiscard]] float maxScroll() const;
    void paintBanner(const ui::PaintContext&, const gfx::Rect&);
    void paintCard(const ui::PaintContext&, const gfx::Rect&, const simstatus::Card&);
    void paintChain(const ui::PaintContext&, const gfx::Rect&);
    void paintAttention(const ui::PaintContext&, const gfx::Rect&);
    void paintTimeline(const ui::PaintContext&, const gfx::Rect&);

    std::shared_ptr<SimCenterModel> model_;
    SimCenterHosts                  hosts_;
    mutable std::vector<Hit>        hits_;
    int                             hover_{-1};
    int                             pressed_{-1};
    float                           scrollY_{0.f};
    ui::PaintedScrollBar            sbar_;   // 1.11.4 : la barre se tire
    float                           contentH_{0.f};
};

// ------------------------------------------------------------- les forcages ----
class SimForcingPane final : public ui::Widget {
public:
    SimForcingPane(std::string id, std::shared_ptr<SimCenterModel> model, SimCenterHosts hosts);
    ~SimForcingPane() override;
    // Relire le modele (l'ecran l'appelle quand les forcages changent).
    void refresh();
    // Relacher les lignes choisies ; tout relacher.
    void releaseSelected();
    void releaseAll();
    // Pour les scripts : choisir une ligne par la variable (ou son debut, sans casse).
    bool select(std::string_view what);
    [[nodiscard]] std::size_t rowCount() const noexcept;
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    class Model;
    std::shared_ptr<SimCenterModel> model_;
    SimCenterHosts                  hosts_;
    ui::TableView*                  table_{nullptr};
    std::shared_ptr<Model>          tableModel_;
    std::uint64_t                   shown_{~std::uint64_t{0}};
    core::ConnectionScope           links_;
};

// --------------------------------------------------------------- les courbes ----
class SimTrendsPane final : public ui::Widget {
public:
    SimTrendsPane(std::string id, std::shared_ptr<SimCenterModel> model, SimCenterHosts hosts);
    // Pour les scripts : le curseur a `secondsAgo` (< 0 : pas de curseur), ce
    // qu'il lit ; la fenetre (30, 120, 600 s).
    void setCursor(double secondsAgo);
    [[nodiscard]] std::vector<std::string> cursorReadout() const;
    [[nodiscard]] gfx::Rect partRect(std::string_view key) const;   // "fenetre:30", "retirer:<chemin>", "piste:<chemin>"
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Hit { gfx::Rect rect; std::string key; std::string tip; };
    [[nodiscard]] double viewEnd() const;
    [[nodiscard]] double timeAt(float x) const;
    [[nodiscard]] float  xOf(double t) const;
    [[nodiscard]] int    hitAt(gfx::Point p) const;
    [[nodiscard]] std::vector<std::string> readoutAt(double t) const;
    void act(const std::string& key);
    void addTyped();
    std::shared_ptr<SimCenterModel> model_;
    SimCenterHosts                  hosts_;
    ui::InputText*                  field_{nullptr};
    mutable std::vector<Hit>        hits_;
    mutable gfx::Rect               plot_{};       // la zone des traces (sans les etiquettes)
    double                          cursor_{-1};   // l'instant du curseur (horloge du centre) ; < 0 : aucun
    bool                            cursorPinned_{false};
    int                             hover_{-1};
    core::ConnectionScope           links_;

protected:
    void onLayout() override;
};

// --------------------------------------------------------------- le journal ----
class SimJournalPane final : public ui::Widget {
public:
    SimJournalPane(std::string id, std::shared_ptr<SimCenterModel> model, SimCenterHosts hosts);
    ~SimJournalPane() override;
    void refresh();                                   // le journal a change
    // Les filtres : une source ("automate", "ihm", "equipements", "debogage",
    // "simulation", "tout") ; une gravite ("tout", "surveiller", "erreurs").
    bool setSource(std::string_view source);
    bool setSeverity(std::string_view severity);
    void setSearch(const std::string& text);
    [[nodiscard]] std::size_t rowCount() const noexcept;
    [[nodiscard]] std::vector<std::string> lines(std::size_t max) const;   // les lignes montrees (les plus recentes d'abord)
    // Aller a ce que dit la ligne choisie (ou la ligne `row`).
    bool goSelected();
    bool selectRow(std::size_t row);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] gfx::Rect partRect(std::string_view key) const;   // "gravite:erreurs", "aller"

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    class Model;
    void rebuild();
    [[nodiscard]] const SimEvent* selected() const;
    [[nodiscard]] int hitAt(gfx::Point p) const;
    void act(const std::string& key);
    std::shared_ptr<SimCenterModel> model_;
    SimCenterHosts                  hosts_;
    ui::InputText*                  search_{nullptr};
    ui::TableView*                  table_{nullptr};
    std::shared_ptr<Model>          tableModel_;
    int                             source_{-1};      // -1 : toutes ; sinon SimSource
    int                             severity_{0};     // 0 tout, 1 a surveiller, 2 erreurs
    std::uint64_t                   shownRevision_{~std::uint64_t{0}};
    std::uint64_t                   selectedId_{0};
    struct Hit { gfx::Rect rect; std::string key; std::string tip; };
    mutable std::vector<Hit>        hits_;
    int                             hover_{-1};
    int                             pressed_{-1};
    core::ConnectionScope           links_;
};

} // namespace app
