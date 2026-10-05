// =============================================================================
//  app/hmi/HmiScriptPanes.hpp — Programmation generale et scripts de vue
// -----------------------------------------------------------------------------
//  UN VOLET, DEUX USAGES.
//    - IHM > Programmation generale : les scripts generaux (ST executes ; C et
//      C++ edites et verifies) avec leur declencheur - au demarrage, cyclique,
//      sur changement d'une expression, ou seulement appele -, et les
//      variables IHM.
//    - IHM > Vues > une vue > Scripts : OnOpen, OnCycle, OnClose de cette vue.
//
//    +-------------------- outils ---------------------+
//    | Scripts (tableau)      | editeur (colore, lignes)|
//    | Proprietes du script   |                         |
//    | Variables IHM          | Diagnostics (ligne)     |
//    +-------------------- etat -----------------------+
//
//  LA SAISIE EST UNE COMMANDE, fusionnee tant qu'on tape dans le meme script :
//  Ctrl+Z reprend la saisie entiere. Les diagnostics suivent la frappe ; un
//  clic sur l'un d'eux (ou un double-clic dans Compiler) amene a sa ligne, qui
//  est marquee dans la marge.
// =============================================================================
#pragma once

#include "HmiAssist.hpp"
#include "HmiPanels.hpp"
#include "HmiVariablePanes.hpp"
#include "HmiFolderTable.hpp"          // lot 21 : les scripts generaux ranges en dossiers
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiCheck.hpp"         // 1.10 : hmi::Issue (les resultats de Compiler)
#include "../../hmi/HmiScript.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hmi::scriptcheck { struct Finding; }   // 1.10 (integration I2) : les corrections proposees

namespace app {

// 1.10 : les fautes d'un code ST a leur place (nom inconnu, membre, appel,
// ecriture interdite, type) - les memes que Compiler : un script (de vue :
// `view`, ses parametres) ou le corps d'une fonction IHM (`function`). `plc` :
// le programme de l'automate (nullptr : un nom inconnu de l'IHM peut etre a lui).
[[nodiscard]] std::vector<hmi::ScriptDiagnostic> placedScriptDiagnostics(const hmi::Project&, std::string_view code,
                                                                       const hmi::View* view, const hmi::HmiFunction* function,
                                                                       const domain::Project* plc,
                                                                       std::vector<hmi::scriptcheck::Finding>* fixes = nullptr);
// 1.10.4 (le plantage de la 1.10.3, pendant la frappe) : un diagnostic ne fait
// jamais tomber l'appli. Une exception, quelle qu'elle soit, est ecrite dans le
// journal interne (`where` : le script, la fonction) ; la ligne `line` (1, 2...)
// dit alors kDiagnosticUnavailable, et l'editeur continue.
inline constexpr const char* kDiagnosticUnavailable =
    "diagnostic indisponible pour cette ligne (le d\xC3\xA9tail est dans Aide > Journal interne)";
[[nodiscard]] std::vector<hmi::ScriptDiagnostic> guardedDiagnostics(const std::function<std::vector<hmi::ScriptDiagnostic>()>& compute,
                                                                  int line, std::string_view where) noexcept;
// ... et ce que l'editeur en souligne (les fautes qui ont une colonne).
[[nodiscard]] std::vector<ui::MultiLineText::Squiggle> squigglesOf(const std::vector<hmi::ScriptDiagnostic>&);
// ... et le titre du tableau des diagnostics qui les compte (`where` : "ce script").
[[nodiscard]] std::string diagnosticsTitle(const std::vector<hmi::ScriptDiagnostic>&, std::string_view where);

class HmiScriptsPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    // `view` = kNoId : la Programmation generale ; sinon les scripts de cette vue.
    HmiScriptsPane(std::string id, hmi::DocumentPtr doc, Apply apply, hmi::Id view = hmi::kNoId);

    void refresh();
    [[nodiscard]] hmi::Id viewId() const noexcept { return view_; }
    [[nodiscard]] bool    general() const noexcept { return view_ == hmi::kNoId; }

    // Le script montre : un script general, ou l'evenement d'une vue (qui n'a
    // peut-etre pas encore de script : la premiere frappe le cree).
    [[nodiscard]] hmi::Id     selectedScript() const;
    [[nodiscard]] std::string selectedEvent() const;
    void selectScript(hmi::Id);
    void selectEvent(std::string_view event);
    // Aller a la source : le script, sa ligne (1 = la premiere), marquee.
    void goTo(hmi::Id script, int line);
    // 1.10 : ... et la colonne (1 = le premier octet de la ligne) : les `length`
    // caracteres de la faute selectionnes (un constat de Compiler).
    void goTo(hmi::Id script, int line, int column, int length);
    // 1.10 (maquette, scene 4) : Compiler (F7) sans quitter l'editeur. Le
    // tableau du bas devient "Resultats de Compiler" : les fautes de TOUS les
    // scripts du projet (Script, Ligne, Col., Gravite, Message) ; celles du
    // script montre suivent la frappe. Un clic : le bon script, la faute
    // selectionnee - d'ailleurs (une autre vue, une fonction, une action) :
    // hosts.openIssue. Rend le nombre de fautes des scripts.
    std::size_t compileHere();
    [[nodiscard]] bool compiledHere() const noexcept { return compiled_; }
    // Le script d'une ligne du tableau du bas ("Calcul", "Vue_A . OnOpen"...).
    [[nodiscard]] std::string resultScript(std::size_t row) const { return row < results_.size() ? results_[row].where : std::string{}; }
    // 1.10 (decision 15 ; integration I2) : la correction proposee d'une ligne du
    // tableau du bas ("Ajouter les valeurs manquantes" d'un CASE sur une
    // enumeration, dans le script montre) ; vide : aucune. L'outil du meme nom
    // l'applique (la ligne choisie, sinon la premiere qui en a une).
    [[nodiscard]] std::string resultFix(std::size_t row) const { return row < results_.size() ? results_[row].fixLabel : std::string{}; }
    // ... l'appliquer : les branches inserees avant le END_CASE, une commande
    // (Ctrl+Z). Faux : rien a corriger sur cette ligne.
    bool applyResultFix(std::size_t row);

    // Les actions (sans hote : directes). Faux / kNoId, et `why`, si refuse.
    hmi::Id addScript(hmi::ScriptLang, std::string name, std::string event, std::string* why = nullptr);
    // Le modele complet (nom, langage, declencheur, periode, expression,
    // description ; corps vide : un commentaire d'en-tete) : une seule commande.
    hmi::Id addScript(hmi::Script spec, std::string* why = nullptr);
    bool    renameScript(hmi::Id, const std::string& name, std::string* why = nullptr);
    bool    deleteScript(hmi::Id);
    bool    setBody(hmi::Id, const std::string& body);
    bool    setTrigger(hmi::Id, const std::string& event, int periodMs, const std::string& watch);
    bool    setLanguage(hmi::Id, hmi::ScriptLang);
    hmi::Id addVariable(const std::string& name, const std::string& type, const std::string& initial,
                        const std::string& description, std::string* why = nullptr);
    bool    updateVariable(hmi::Id, const std::string& name, const std::string& type, const std::string& initial,
                           const std::string& description, std::string* why = nullptr);
    bool    deleteVariable(hmi::Id);
    [[nodiscard]] hmi::Id selectedVariable() const;
    void selectVariable(hmi::Id);

    [[nodiscard]] const std::vector<hmi::ScriptDiagnostic>& diagnostics() const noexcept { return diagnostics_; }

    // L'AIDE A LA SAISIE, comme dans les sections de l'automate : la liste en
    // tapant (et les membres apres un point), la signature d'un appel, une
    // infobulle au survol, et sous le code la barre du nom ou est le curseur.
    // `plc` : le programme de l'automate ; `live` : en simulation, la valeur.
    void setAssist(std::function<std::shared_ptr<const domain::Project>()> plc,
                   std::function<bool(std::string_view, std::string&)> live = {});
    [[nodiscard]] ui::StatusBar& symbolBar() noexcept { return *symbolBar_; }
    // La barre, recalculee pour le curseur (la frappe l'appelle toute seule).
    void updateSymbolLine();

    struct Hosts {
        std::function<void(hmi::ScriptLang)> newScript;
        std::function<void(hmi::Id)>         rename, remove;
        std::function<void()>                newVariable;
        std::function<void(hmi::Id)>         editVariable, removeVariable;
        std::function<void()>                compile;          // ouvrir IHM > Compiler
        std::function<void(const hmi::Issue&)> openIssue;      // 1.10 : un resultat d'ailleurs (autre vue, fonction, action)
        // 1.11.2 (decision 174) : Exporter (les elements coches, celui-ci d'avance) et Importer... (tout paquet).
        std::function<void(hmi::Id)>            exportItems;
        std::function<void()>                   importAny;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    // Lot 16 : la Programmation generale a trois onglets - les scripts, les
    // variables IHM (dossiers, structures, tableaux, liaison), les types IHM.
    enum Tab : std::size_t { TabScripts = 0, TabVariables = 1, TabTypes = 2 };
    void showTab(std::size_t tab);
    [[nodiscard]] std::size_t currentTab() const noexcept;
    [[nodiscard]] HmiVariablesPane* variablesPane() noexcept { return varsPane_; }
    [[nodiscard]] HmiTypesPane*     typesPane() noexcept { return typesPane_; }
    [[nodiscard]] ui::TabControl*   tabs() noexcept { return tabs_; }

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::MultiLineText& editor() noexcept { return *editor_; }
    [[nodiscard]] ui::TableView&     scriptTable() noexcept { return *scripts_; }
    [[nodiscard]] ui::TableView&     variableTable() noexcept { return *variables_; }
    [[nodiscard]] ui::TableView&     diagnosticTable() noexcept { return *diagTable_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *props_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;   // 1.10 : F7, Compiler ici

private:
    void showSelected();                 // l'editeur et les proprietes suivent la selection
    void rebuildProperties();
    void updateDiagnostics();
    void say(std::string text, bool warning = false);
    [[nodiscard]] const hmi::Script* current() const;
    [[nodiscard]] std::vector<const hmi::Script*> list() const;

    hmi::DocumentPtr  doc_;
    Apply             apply_;
    hmi::Id           view_;
    Hosts             hosts_;
    HmiToolStrip*     tools_{nullptr};
    ui::TabControl*   tabs_{nullptr};          // lot 16 (la Programmation generale seulement)
    HmiVariablesPane* varsPane_{nullptr};
    HmiTypesPane*     typesPane_{nullptr};
    ui::Splitter*     split_{nullptr};
    ui::TableView*    scripts_{nullptr};
    ui::PropertyGrid* props_{nullptr};
    ui::TableView*    variables_{nullptr};
    HmiTitledPanel*   editorPanel_{nullptr};
    ui::MultiLineText* editor_{nullptr};
    ui::StatusBar*    symbolBar_{nullptr};
    assist::Sources   assist_;
    ui::TableView*    diagTable_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> scriptModel_, variableModel_, diagModel_;
    std::vector<hmi::Id>     scriptOrder_;     // vue : un par evenement (kNoId si vide) ; general : les scripts montres
    std::unique_ptr<HmiFolderTable> folders_;  // lot 21 : general - la liste rangee en dossiers
    std::vector<std::string> eventOrder_;
    std::vector<hmi::Id>     variableOrder_;
    std::vector<hmi::ScriptDiagnostic> diagnostics_;
    // 1.10 : les lignes du tableau du bas - le script montre (suit la frappe) et,
    // apres Compiler (F7), les autres scripts du projet.
    struct ResultRow {
        hmi::ScriptDiagnostic d;                 // ligne, colonne, longueur, gravite, message
        std::string           where;             // le script, tel que la colonne Script le dit
        hmi::Id               script{hmi::kNoId};   // un script de ce volet : y aller ici
        bool                  here{false};          // le script montre
        hmi::Issue            issue;             // d'ailleurs : le constat (l'hote y mene)
        std::string           fixLabel{};        // 1.10 (I2) : une correction proposee (vide : aucune)
        int                   fixLine{0};
        std::string           fixText{};
    };
    [[nodiscard]] int fixRow() const;            // 1.10 (I2) : la ligne a corriger (-1 : aucune)
    std::vector<ResultRow>  results_;
    std::vector<hmi::Issue> compiledIssues_;     // le dernier Compiler de ce volet (les scripts)
    bool                    compiled_{false};
    int               selectedRow_{-1};
    std::string       message_;
    bool              syncing_{false};
    core::ConnectionScope links_;
};

} // namespace app
