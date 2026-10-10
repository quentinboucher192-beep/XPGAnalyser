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

#include "../../hmi/HmiPipeline.hpp"   // 1.11.13 : les commandes du build

#include "HmiAssist.hpp"
#include "HmiDeclGrid.hpp"             // 1.11.18 (refonte, lot 5) : les onglets Constantes, Variables
#include "HmiPanels.hpp"
#include "HmiVariablePanes.hpp"
#include "HmiFolderTable.hpp"          // lot 21 : les scripts generaux ranges en dossiers
#include "HmiLive.hpp"                  // 1.11.21 : les diagnostics en direct (le panneau du bas)
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiCheck.hpp"         // 1.10 : hmi::Issue (les resultats de Compiler)
#include "../../hmi/HmiScript.hpp"
#include "../../hmi/HmiScriptFile.hpp"   // 1.11.3 : exporter / importer les scripts d'une vue (.xpgst)
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hmi::scriptcheck { struct Finding; }   // 1.10 (integration I2) : les corrections proposees

namespace app {

// 1.10 : les fautes d'un code ST a leur place (nom inconnu, membre, appel,
// ecriture interdite, type) - les memes que Compiler : un script (de vue :
// `view`, ses parametres) ou le corps d'une fonction IHM (`function`). `plc` :
// le programme de l'automate (nullptr : un nom inconnu de l'IHM peut etre a lui).
// 1.11.18 (refonte, lot 3) : `decls` - les declarations du modele du code (sur sa ligne 1,
// comme le moteur les lit ; leurs fautes nomment la declaration, ligne 0).
[[nodiscard]] std::vector<hmi::ScriptDiagnostic> placedScriptDiagnostics(const hmi::Project&, std::string_view code,
                                                                       const hmi::View* view, const hmi::HmiFunction* function,
                                                                       const domain::Project* plc,
                                                                       std::vector<hmi::scriptcheck::Finding>* fixes = nullptr,
                                                                       const std::vector<hmi::Declaration>* decls = nullptr);
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

class HmiScriptsPane final : public ui::Widget, public HmiLiveSource {
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
    // 1.10 (maquette, scene 4) : Compiler (F7) sans quitter l'editeur ; le tableau du
    // bas devient "Resultats de Compiler" (Script, Ligne, Col., Gravite, Message) et
    // suit la frappe. 1.11.17 (refonte des scripts, lot 1, spec. 13) : COMPILER LE
    // SCRIPT ACTUEL - lui seul (hmi::CompileFocus), plus tous les scripts du projet :
    // le projet entier, c'est IHM > Compiler. Rend le nombre de fautes du script
    // (0 : aucun script choisi, rien n'est compile).
    std::size_t compileHere();
    [[nodiscard]] bool compiledHere() const noexcept { return compiled_; }
    // 1.11.17 : le bouton Compiler et F7 - compileHere, puis le build de cet element
    // (son etat, et les Diagnostics du panneau du bas, filtres sur lui).
    void compileCurrent();
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

    // ---- 1.11.3 : EXPORTER ET IMPORTER LES SCRIPTS D'UNE VUE (.xpgst) ----
    //  Une vue, une popup, un symbole, un ecran modele, un en-tete ou un pied
    //  de page : ses OnOpen, OnCycle, OnClose dans un fichier texte lisible
    //  (HmiScriptFile.hpp). Exporter... et Importer... dans la barre d'outils ;
    //  l'import compare, demande (les scripts a prendre ; remplacer, ou ajouter a
    //  la suite) et fait UNE commande (Ctrl+Z). Un .st sans bloc va dans
    //  l'evenement choisi.
    // Ecrire le fichier (sans rien demander). Faux et `why` : rien a exporter, ecriture impossible.
    bool exportViewScripts(const std::string& path, std::string* why = nullptr);
    // Lire le fichier et ouvrir la fenetre d'import. Faux et `why` : illisible, ou des operateurs.
    bool importViewScripts(const std::string& path, std::string* why = nullptr);
    // Appliquer un fichier lu (les entrees cochees ; vide : toutes), une commande. Rend le nombre de scripts changes.
    std::size_t applyImport(const hmi::scriptfile::File&, const std::vector<bool>& chosen, hmi::scriptfile::Mode mode);

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
        // 1.11.13 : la generation incrementale - Generer, Regenerer, Compiler, Generer et
        // compiler le script choisi (sa cle : "script:12", "script-vue:30") ; son etat de
        // build (le libelle de la barre, et l'infobulle : la raison). Sans hote : pas de boutons.
        std::function<void(hmi::pipeline::Mode, const std::string& key)>           build;
        std::function<std::pair<std::string, std::string>(const std::string& key)> buildState;
        std::function<void()>                                                      buildOutputs;   // l'onglet IHM . Sorties
        std::function<void()>                                                      showDiagnostics;   // 1.11.21 : le panneau du bas, ses Diagnostics
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    // 1.11.13 : la cle de build du script choisi ("" : aucun) ; l'etat de la barre relu (l'ecran,
    // a chaque image : rien ne change si l'etat est le meme).
    [[nodiscard]] std::string buildKey() const;
    void refreshBuildState();

    // Lot 16 : la Programmation generale a trois onglets - les scripts, les
    // variables IHM (dossiers, structures, tableaux, liaison), les types IHM.
    enum Tab : std::size_t { TabScripts = 0, TabVariables = 1, TabTypes = 2 };
    void showTab(std::size_t tab);
    [[nodiscard]] std::size_t currentTab() const noexcept;
    [[nodiscard]] HmiVariablesPane* variablesPane() noexcept { return varsPane_; }
    [[nodiscard]] HmiTypesPane*     typesPane() noexcept { return typesPane_; }
    [[nodiscard]] ui::TabControl*   tabs() noexcept { return tabs_; }

    // ---- 1.11.18 (refonte des scripts, lot 5) : LES ONGLETS DU CODE ----
    //  L'editeur a trois onglets : Code, Constantes, Variables - les declarations du
    //  modele du script montre (HmiDeclGrid : ajouter, renommer, collage d'Excel...), un
    //  titre qui les compte (rouge : une fautive). Au-dessus du code, un bandeau quand le
    //  script declare encore ses variables dans son texte : "Migrer ce code".
    enum CodeTab : std::size_t { CodeTabCode = 0, CodeTabConstants = 1, CodeTabVariables = 2 };
    void showCodeTab(std::size_t tab);
    [[nodiscard]] std::size_t currentCodeTab() const noexcept;
    [[nodiscard]] HmiCodeTabs&    codeTabs() noexcept { return *codeTabs_; }
    [[nodiscard]] HmiDeclGrid&    constantsGrid() noexcept { return *codeTabs_->grid(hmi::decledit::Tab::Constants); }
    [[nodiscard]] HmiDeclGrid&    variablesGrid() noexcept { return *codeTabs_->grid(hmi::decledit::Tab::Variables); }
    [[nodiscard]] HmiDeclBanner&  declBanner() noexcept { return *banner_; }
    // Le code montre, son adresse pour les grilles (vide : aucun script choisi).
    [[nodiscard]] std::optional<hmi::decledit::Place> currentPlace() const;
    // "Migrer ce code" : ses blocs VAR dans ses onglets (une commande). Faux : rien a migrer
    // ou impossible (lastMessage() dit pourquoi).
    bool migrateCurrent();
    // Les utilisations d'un nom : l'onglet Code, la suivante apres le curseur (puis la
    // premiere). Faux : le code ne l'emploie pas.
    bool goToNextUse(const std::string& name);
    // Une declaration du script montre : son onglet, sa ligne choisie. Faux : inconnue.
    bool showDeclaration(const std::string& name);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] const std::string& buildStateText() const noexcept { return buildStateText_; }   // 1.11.13
    [[nodiscard]] ui::MultiLineText& editor() noexcept { return *editor_; }
    [[nodiscard]] ui::TableView&     scriptTable() noexcept { return *scripts_; }
    [[nodiscard]] ui::TableView&     variableTable() noexcept { return *variables_; }
    // 1.11.21 : plus de bandeau Diagnostics sous l'editeur (HmiLive.hpp) - ce qu'il listait :
    // le nombre de lignes (le script montre, puis ce que Compiler a dit des autres).
    [[nodiscard]] std::size_t resultCount() const noexcept { return results_.size(); }
    // 1.11.21 : ce que dit la barre du volet (les lignes, le langage, les fautes comptees).
    [[nodiscard]] const std::string& statusText() const noexcept { return statusText_; }
    // ---- 1.11.21 : HmiLiveSource (les diagnostics du script montre, au panneau du bas) ----
    [[nodiscard]] std::uint64_t liveRevision() const noexcept override { return liveRev_; }
    [[nodiscard]] std::string liveElement() const override { return buildKey(); }
    [[nodiscard]] std::vector<hmi::pipeline::Diagnostic> liveDiagnostics() const override;
    void goToLive(const hmi::pipeline::Diagnostic& d) override;
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *props_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;   // 1.10 : F7, Compiler ici

private:
    void showSelected();                 // l'editeur et les proprietes suivent la selection
    void refreshDeclarations();          // 1.11.18 (lot 5) : les grilles, leurs titres, le bandeau
    void rebuildProperties();
    void updateDiagnostics();
    void say(std::string text, bool warning = false);
    [[nodiscard]] const hmi::Script* current() const;
    [[nodiscard]] std::vector<const hmi::Script*> list() const;

    hmi::DocumentPtr  doc_;
    Apply             apply_;
    hmi::Id           view_;
    Hosts             hosts_;
    std::string       buildStateText_;   // 1.11.13 : ce que montre la barre (pour ne la refaire qu'au changement)
    HmiToolStrip*     tools_{nullptr};
    ui::TabControl*   tabs_{nullptr};          // lot 16 (la Programmation generale seulement)
    HmiVariablesPane* varsPane_{nullptr};
    HmiTypesPane*     typesPane_{nullptr};
    ui::Splitter*     split_{nullptr};
    ui::TableView*    scripts_{nullptr};
    ui::PropertyGrid* props_{nullptr};
    ui::TableView*    variables_{nullptr};
    HmiTitledPanel*   editorPanel_{nullptr};
    HmiCodeTabs*      codeTabs_{nullptr};          // 1.11.18 (lot 5) : Code | Constantes | Variables
    HmiDeclBanner*    banner_{nullptr};
    ui::MultiLineText* editor_{nullptr};
    ui::StatusBar*    symbolBar_{nullptr};
    assist::Sources   assist_;
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> scriptModel_, variableModel_;
    std::uint64_t     liveRev_{0};                 // 1.11.21 : croit a chaque calcul des diagnostics
    std::string       statusText_;                 // 1.11.21 : le message durable de la barre
    std::vector<hmi::Id>     scriptOrder_;     // vue : un par evenement (kNoId si vide) ; general : les scripts montres
    std::unique_ptr<HmiFolderTable> folders_;  // lot 21 : general - la liste rangee en dossiers
    std::vector<std::string> eventOrder_;
    std::vector<hmi::Id>     variableOrder_;
    std::vector<hmi::ScriptDiagnostic> diagnostics_;
    // 1.10 : les lignes du tableau du bas - le script montre (suit la frappe). 1.11.17 :
    // Compiler ne compile plus que lui (avant, aussi les autres scripts du projet).
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
    hmi::Id            shownId_{hmi::kNoId};   // 1.12.2 : le document montre (annuler : le meme, la vue gardee)
    core::ConnectionScope links_;
    std::shared_ptr<char> alive_ = std::make_shared<char>(0);   // 1.11.3 : les reponses de l'explorateur et des dialogues
};

} // namespace app
