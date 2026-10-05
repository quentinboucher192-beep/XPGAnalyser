// =============================================================================
//  app/AnimationTablesPane.hpp - lot API 3 : les tables d'animation, avec l'IHM
// -----------------------------------------------------------------------------
//  Un onglet, fait comme un volet de l'IHM (la barre a libelles du cadre ApiFrame,
//  la ligne d'aide en bas) :
//
//   * a gauche, les tables du projet et leur nombre de lignes ; « + Nouvelle
//     table » en bas ; renommer sur place (double-clic ou « Renommer ») ;
//   * au centre, les lignes de la table choisie : leur SOURCE (API, bleu ; IHM,
//     violet), le type, la valeur en direct pendant la simulation (un
//     changement s'eclaire), le forcage, la nouvelle valeur a ecrire, et ce que
//     la ligne a a dire (liee a l'automate, locale a l'IHM, lue par l'IHM) ;
//     une table vide dit ou glisser des variables ;
//   * a droite, la ligne choisie (son type, ou elle est declaree, qui l'ecrit,
//     ce qu'en fait l'IHM) - ou la table (son nom, son unite) ;
//   * en bas, la ligne choisie en courbe, sur les derniers cycles.
//
//  Toute modification de la table est une commande (project/ApiCommands) : la
//  pile l'annule. Ecrire et forcer ne touchent que la simulation (sim::Runtime
//  pour l'automate, hmi::Runtime pour l'IHM), pas le projet.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../core/Signal.hpp"
#include "../domain/ProjectModel.hpp"
#include "../project/ApiCommands.hpp"
#include "../project/MemberTree.hpp"
#include "../ui/Widget.hpp"
#include "TablePaste.hpp"

#include <deque>
#include <set>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sim { class Runtime; }
namespace hmi { class Runtime; class Document; }
namespace ui { class TableView; class PropertyGrid; }

namespace app {

class ApiFrame;
class HmiSeriesChart;

class AnimationTablesPane final : public ui::Widget {
public:
    enum Action : int {
        ATable = 1, ARename, ADuplicate, ADelete,
        AAddApi, AAddHmi, ARemove,
        AWrite, AForce, AUnforce, AUnforceAll,
    };
    enum Column : std::size_t { CSource = 0, CName, CType, CValue, CForced, CNew, CNote, CCount };

    struct Hosts {
        std::function<std::shared_ptr<domain::Project>()> project;      // modifiable ; nul : rien a modifier
        std::function<void(core::CommandPtr)>             apply;
        std::function<sim::Runtime*()>                    plc;          // nul : pas de simulation preparee
        std::function<bool()>                             running;      // la simulation tourne
        std::function<std::shared_ptr<hmi::Document>()>   hmi;
        std::function<hmi::Runtime*()>                    hmiRuntime;   // nul : l'IHM ne tourne pas
        std::function<void(bool hmi)>                     pick;         // le dialogue de choix (l'ecran le pose)
        std::function<void(const std::string&)>           status;
        // Lot API 6 : apres un collage (les commandes du groupe n'ont rien
        // rafraichi) - l'arbre et les autres volets suivent.
        std::function<void()>                             refreshViews;
    };

    explicit AnimationTablesPane(std::string id);
    ~AnimationTablesPane() override;
    void setHosts(Hosts h);
    // La barre et la ligne d'aide du cadre : les boutons, leurs etats.
    void attach(ApiFrame& frame);

    void refresh();                 // apres une commande, un autre projet
    void tick();                    // chaque image : les valeurs, la courbe
    void runAction(int action);

    bool selectTable(std::size_t index);
    bool selectTable(std::string_view name);
    [[nodiscard]] std::size_t currentTable() const noexcept { return table_; }
    [[nodiscard]] std::string currentTableName() const;
    [[nodiscard]] std::vector<std::size_t> selectedLines() const;
    bool selectLine(std::string_view name);

    // Des variables pour la table choisie : une commande (les doublons sont
    // ignores) ; `table` npos : la table choisie.
    bool addLines(std::vector<project::AnimationLine> lines, std::size_t table = static_cast<std::size_t>(-1));

    // Le glisser depuis l'arbre : ce point est-il sur les lignes ? (l'ecran suit
    // la souris et depose) ; le cadre de depot, allume ou eteint.
    [[nodiscard]] bool dropZone(gfx::Point p) const;
    void setDropHint(bool on, std::string label = {}, gfx::Point at = {});

    // Pour les scripts et les tests.
    [[nodiscard]] ui::TableView& tables() noexcept { return *tables_; }
    [[nodiscard]] ui::TableView& lines() noexcept { return *lines_; }
    [[nodiscard]] std::string valueText(std::size_t row) const;
    [[nodiscard]] std::size_t lineCount() const;
    // Lot API 6 : une structure (DDT, tableau, instance de bloc) se deplie sur
    // ses membres, en lignes filles (le chemin complet, leur valeur, leur
    // nouvelle valeur). Deplier par le chemin de la ligne ; faux : pas une
    // structure montree.
    bool setExpanded(std::string_view path, bool open);
    [[nodiscard]] bool isExpanded(std::string_view path) const;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    // Une table vide n'a pas de lignes ou coller : Ctrl+V passe par le volet.
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    class TablesModel;
    class LinesModel;
    class DropOverlay;
    friend class TablesModel;
    friend class LinesModel;

    struct Row {
        std::string name, type, declared, note, address;
        bool        hmi{false}, known{true}, linked{false}, structure{false};
        // Lot API 6 : une ligne fille - le membre d'une structure depliee.
        // `entry` : la ligne de la table qui la porte (la sienne pour une racine).
        std::size_t entry{static_cast<std::size_t>(-1)};
        int         depth{0};
        bool        child{false};
        // Lot API 7 : ce que la ligne deplie (project/MemberTree) ; un GROUPE
        // (un paquet de 100, une ligne d'un tableau a deux dimensions) range des
        // elements et n'a pas de valeur a lui.
        project::members::Node node;
        bool        group{false};
        // Une variable locale a une unite : la simulation la connait sous
        // "Unite.nom" ("Gestion_armoires.Gc_SurpressionA.Enable").
        std::string scope;
    };
    struct Live {
        std::string text;
        int         flash{0};        // images restantes de l'eclairage (la valeur vient de changer)
        bool        truthy{false}, isBool{false}, ok{false};
    };

    [[nodiscard]] std::shared_ptr<domain::Project> project() const;
    [[nodiscard]] const domain::AnimationTable* current() const;
    void rebuildRows();
    void refreshProperties();
    void refreshChart();
    void updateHint();
    [[nodiscard]] std::string liveOf(const Row& r, bool& truthy, bool& isBool, bool& ok) const;
    // Le nom sous lequel la simulation connait la ligne (avec son unite au besoin).
    [[nodiscard]] std::string simName(const Row& r) const;
    [[nodiscard]] std::string keyOf(const Row& r) const { return (r.hmi ? "ihm:" : "api:") + r.name; }
    void writeSelected(bool force);
    void onLinesSelected();
    void addChildren(const domain::Project& p, const Row& parent, int depth);
    [[nodiscard]] std::vector<std::size_t> selectedEntries() const;
    [[nodiscard]] paste::Target pasteTarget(const ui::TableView::PasteRequest& rq);

    Hosts                          hosts_;
    ApiFrame*                      frame_{nullptr};
    ui::TableView*                 tables_{nullptr};
    ui::TableView*                 lines_{nullptr};
    ui::PropertyGrid*              props_{nullptr};
    HmiSeriesChart*                chart_{nullptr};
    DropOverlay*                   overlay_{nullptr};
    std::shared_ptr<TablesModel>   tablesModel_;
    std::shared_ptr<LinesModel>    linesModel_;
    std::size_t                    table_{static_cast<std::size_t>(-1)};
    std::vector<Row>               rows_;
    std::vector<Live>              live_;
    std::map<std::string, std::string> newValues_;     // cle de la ligne -> texte a ecrire
    std::string                    pendingSelect_;     // une table creee : choisie au prochain refresh
    bool                           pendingRename_{false};
    std::string                    chartKey_;
    std::deque<std::pair<double, double>> samples_;    // la ligne IHM choisie (l'automate garde les siennes)
    std::uint64_t                  lastScan_{static_cast<std::uint64_t>(-1)};
    std::string                    hintShown_;
    gfx::Rect                      linesArea_{}, chartArea_{};
    bool                           dropHint_{false};
    std::string                    dropLabel_;          // ce qu'on traine (le nom), dessine sous la souris
    gfx::Point                     dropAt_{};
    double                         lastHmiSample_{-1.0};
    bool                           syncing_{false};
    std::set<std::string>          expanded_;           // les chemins deplies, en minuscules
    paste::Binding                 paste_;
    bool                          pasteFresh_{false};   // le bandeau du collage survit au rafraichissement qui le suit
    core::ConnectionScope          links_;
};

} // namespace app
