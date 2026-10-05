// =============================================================================
//  app/hmi/HmiMemoryMap.hpp - la carte memoire d'un equipement (lot 17)
// -----------------------------------------------------------------------------
//  CE QUE MONTRE L'ONGLET "Carte memoire" : une grille par table (registres de
//  maintien, registres d'entree, bobines, entrees TOR), dix cases par ligne
//  (seize au choix). Chaque variable IHM liee est une barre sur ses cases :
//  bleue lue, verte ecrite (une action, un champ, un script :=, une recette) ;
//  des lectures qui se chevauchent sont rayees (normal), une ecriture sur une
//  lecture est encadree d'ambre (attention), deux ecritures de rouge (erreur) ;
//  une case hors des zones declarees est hachuree de rouge. Un mot de BOOL
//  ranges montre ses seize bits.
//
//  LE SCANNER (ou le jumeau) : une case grise a une valeur, plus foncee avec
//  une vague quand elle change ; une variable montre sa valeur, sa vague, et L
//  / E quand l'IHM en marche la lit ou l'ecrit ; sur une case toujours a 0 elle
//  est en pointille (la bonne adresse ?).
//
//  Lot 18 : une case animee (un mouvement du jumeau) porte une vague, une case
//  forcee un cadre orange et un F.
//
//  Les lignes vides se replient ; la legende reste en bas. Un clic choisit une
//  case (sa fiche est a droite), un double-clic ouvre sa variable, une barre
//  tiree sur une autre case y deplace la variable.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiZones.hpp"
#include "../../ui/Widget.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace app {

// Ce que la carte sait d'une case en marche (le scanner, ou le jumeau).
using MemCellFn = std::function<std::optional<hmi::zones::CellStat>(hmi::MemTable, std::uint32_t)>;

// Une variable en marche : sa valeur (texte), L / E (l'IHM vient de la lire,
// de l'ecrire).
struct MemVarLive {
    std::string text;
    bool        read{false}, written{false};
};

class HmiMemoryMap final : public ui::Widget {
public:
    struct Options {
        int         perRow{10};            // 10 ou 16
        bool        fold{true};            // replier ce qui est vide
        int         show{0};               // 0 tout, 1 mes variables, 2 les problemes, 3 l'activite sans variable
        int         radix{0};              // 0 decimal (UINT), 1 signe (INT), 2 hexadecimal
        std::string search;
    };
    struct Cell {
        hmi::MemTable table{hmi::MemTable::Holding};
        std::uint32_t offset{0};
    };

    explicit HmiMemoryMap(std::string id = {});

    // La carte (les variables, les zones) ; `shown` : les plages montrees quand
    // les zones ne sont pas declarees (celles des variables).
    void setMap(hmi::zones::MemoryMap map, hmi::MemZones shown);
    [[nodiscard]] const hmi::zones::MemoryMap& map() const noexcept { return map_; }
    // En marche : les cases (nul : aucune), les variables (par indice de map().vars).
    void setLive(MemCellFn cells, std::vector<MemVarLive> vars, int passes);
    [[nodiscard]] bool live() const noexcept { return static_cast<bool>(cells_); }
    void setOptions(Options o);
    [[nodiscard]] const Options& options() const noexcept { return options_; }
    // La ligne du haut : "Scan en cours : passe 24..." (tone 1 en marche, 2 attention, 3 erreur, 4 info).
    void setBanner(std::string text, int tone);
    [[nodiscard]] const std::string& banner() const noexcept { return banner_; }
    // Les comportements du jumeau (les cases qui bougent toutes seules) : un petit rond.
    void setBehaviorCells(std::set<std::pair<int, std::uint32_t>> cells);
    // Lot 18 : les cases forcees du jumeau - un cadre orange et un F.
    void setForcedCells(std::set<std::pair<int, std::uint32_t>> cells);

    void select(hmi::MemTable, std::uint32_t offset, bool reveal = true);
    void clearSelection();
    [[nodiscard]] std::optional<Cell> selected() const noexcept { return selected_; }
    void setTableOpen(hmi::MemTable, bool open);
    [[nodiscard]] bool tableOpen(hmi::MemTable t) const noexcept { return !closed_.count(static_cast<int>(t)); }
    // Deplier une plage repliee (celle qui contient la case).
    void unfold(hmi::MemTable, std::uint32_t offset);

    // Pour les scripts et les tests : ou est dessinee une case (faux : pas montree).
    [[nodiscard]] bool cellRect(hmi::MemTable, std::uint32_t offset, gfx::Rect& out) const;
    // Les lignes montrees : (table, premiere case) ; -1 : un titre, un repli.
    [[nodiscard]] std::size_t rowCount() const noexcept { return rows_.size(); }
    [[nodiscard]] std::string describe(hmi::MemTable, std::uint32_t offset) const;   // l'infobulle d'une case
    // Lot 7 : la case survolee, decrite avec sa valeur du moment - relue tant
    // que l'infobulle est ouverte (elle restait a la valeur de l'arrivee).
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;

    const core::SignalPtr<int, std::uint32_t>                 cellChosen = core::Signal<int, std::uint32_t>::create();
    const core::SignalPtr<const std::string&>                 variableOpened = core::Signal<const std::string&>::create();
    // Une barre tiree sur une autre case : la variable (sa racine), la table, le decalage (en cases).
    const core::SignalPtr<const std::string&, int, int> variableMoved = core::Signal<const std::string&, int, int>::create();

    [[nodiscard]] ui::SizeHint sizeHint() const override;

protected:
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    enum class RowKind : std::uint8_t { Header, Columns, Line, Fold, Gap };
    struct Row {
        RowKind       kind{RowKind::Line};
        hmi::MemTable table{hmi::MemTable::Holding};
        std::uint32_t first{0}, last{0};   // Line : la premiere case ; Fold, Gap : la plage
        std::string   text;                 // Header, Fold, Gap
        std::string   alert;                // Header : "1 erreur - 1 attention"
        int           tone{0};
        float         y{0}, h{0};           // dans le contenu
    };
    void rebuild();                         // les lignes (le repli suit l'activite)
    [[nodiscard]] float contentHeight() const;
    [[nodiscard]] gfx::Rect mapArea() const;             // sous la banniere, au-dessus de la legende
    [[nodiscard]] float legendHeight() const;
    [[nodiscard]] bool cellActive(hmi::MemTable, std::uint32_t) const;
    [[nodiscard]] bool lineHasVars(hmi::MemTable, std::uint32_t first, std::uint32_t count) const;
    [[nodiscard]] bool lineHasProblem(hmi::MemTable, std::uint32_t first, std::uint32_t count) const;
    [[nodiscard]] bool lineMatches(hmi::MemTable, std::uint32_t first, std::uint32_t count) const;
    [[nodiscard]] bool lineActive(hmi::MemTable, std::uint32_t first, std::uint32_t count) const;
    [[nodiscard]] bool inZone(hmi::MemTable, std::uint32_t offset) const;
    [[nodiscard]] std::optional<Cell> cellAt(gfx::Point p) const;
    [[nodiscard]] int rowAt(gfx::Point p) const;
    [[nodiscard]] bool changing(hmi::MemTable, std::uint32_t offset, const hmi::zones::CellStat& st, double now) const;
    [[nodiscard]] std::string valueText(std::uint16_t v, bool bits) const;
    void reveal(hmi::MemTable, std::uint32_t offset);

    hmi::zones::MemoryMap                      map_;
    hmi::MemZones                              shown_;
    MemCellFn                                  cells_;
    std::vector<MemVarLive>                    varLive_;
    int                                        passes_{0};
    Options                                    options_;
    std::string                                banner_;
    int                                        bannerTone_{0};
    std::set<std::pair<int, std::uint32_t>>    behaviorCells_;
    std::set<std::pair<int, std::uint32_t>>    forcedCells_;
    std::optional<Cell>                        selected_;
    std::set<int>                              closed_;          // les tables repliees
    std::set<std::pair<int, std::uint32_t>>    unfolded_;        // les replis ouverts (table, debut)
    // Par case (table << 32 | case) : les variables qui l'occupent (indices de map_.vars).
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> at_;
    std::unordered_map<std::uint64_t, int>                        worst_;   // le pire chevauchement de la case
    std::vector<Row>                           rows_;
    float                                      scroll_{0};
    // La vague : le nombre de changements vu, et quand il a bouge (par case).
    mutable std::unordered_map<std::uint64_t, std::pair<std::uint32_t, double>> seen_;
    mutable double                             now_{0};
    // La souris.
    std::optional<Cell>                        hover_;
    std::optional<Cell>                        pressCell_;
    std::string                                dragVar_;
    bool                                       dragging_{false};
    gfx::Point                                 pressAt_{};
};

} // namespace app
