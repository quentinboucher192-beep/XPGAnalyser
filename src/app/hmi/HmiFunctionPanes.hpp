// =============================================================================
//  app/hmi/HmiFunctionPanes.hpp - Programmation generale > Fonctions (lot 7)
// -----------------------------------------------------------------------------
//  LES FONCTIONS IHM DU PROJET. Une fonction a un nom, un type de retour (ou
//  aucun), des parametres (VAR_INPUT), des variables locales (VAR, VAR_TEMP)
//  et un corps ST. Elle s'appelle comme une fonction de l'automate :
//
//      Moyenne(Armoires[0].ana.PT1.mes, Armoires[1].ana.PT1.mes)
//      Tracer(Message := 'porte ouverte');
//
//  depuis un script (general, de vue, une action "Script") et, si elle rend
//  une valeur, depuis une expression de vue - en lecture seule.
//
//    +-------------------------- outils ---------------------------+
//    | Fonctions (tableau)        | editeur (colore, lignes)       |
//    | Proprietes de la fonction  |   barre du nom sous le curseur |
//    | Essai (le dernier appel)   | Diagnostics (ligne)            |
//    +--------------------------- etat ----------------------------+
//
//  RENOMMER une fonction renomme ses appels partout (scripts, fonctions,
//  actions, expressions, alarmes) : une seule commande, Ctrl+Z reprend tout.
//  ESSAYER l'appelle sur un banc : les variables IHM a leur valeur initiale,
//  l'automate de la simulation en lecture seule s'il tourne ; l'essai dit le
//  resultat, ce que la fonction a ecrit au journal et les variables IHM
//  qu'elle a changees - sans rien toucher du projet ni de la simulation.
// =============================================================================
#pragma once

#include "../../hmi/HmiPipeline.hpp"   // 1.11.13 : les commandes du build

#include "HmiAssist.hpp"
#include "HmiDeclGrid.hpp"             // 1.11.18 (refonte, lot 5) : les onglets Parametres, Locales, Constantes
#include "HmiFunctionTestsPage.hpp"    // 1.13.0 (lot J4b) : l'onglet Tests (les tests unitaires de la fonction)
#include "HmiPanels.hpp"
#include "HmiLive.hpp"                  // 1.11.21 : les diagnostics en direct (le panneau du bas)
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../sim/Interpreter.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app {

class HmiFunctionsPane final : public ui::Widget, public HmiLiveSource {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiFunctionsPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    // 1.11.10 : LES FONCTIONS D'UN SYMBOLE (le sous-onglet Fonctions de son editeur) : la
    // meme liste, le meme editeur, sur View::functions du symbole `symbol` ; en plus, la
    // case Virtuelle (une instance peut la redefinir) et les instances qui la redefinissent.
    // Le corps lit les parametres du symbole (les diagnostics les connaissent).
    // 1.12.16 : aussi une vue ou une popup (ses fonctions, View::functions) - pas de virtuelle, une portee.
    void setSymbol(hmi::Id symbol);
    [[nodiscard]] hmi::Id symbol() const noexcept { return symbol_; }
    bool setFunctionVisibility(hmi::Id function, bool pub);   // 1.12.16 : une fonction de vue, publique ou privee
    bool setVirtual(hmi::Id, bool on);
    bool setGenerics(hmi::Id, const std::string& text);   // 1.13.0 : TEMPLATE <T> ; ses emplois suivent
    // 1.13.0 (lot J8) : l'espace de noms d'une fonction IHM du projet ("" ou "(racine)" : la racine) - ses surcharges suivent.
    bool setNamespace(hmi::Id, const std::string& ns, std::string* why = nullptr);

    void refresh();
    [[nodiscard]] hmi::Id selectedFunction() const;
    void selectFunction(hmi::Id);
    // Aller a la source : la fonction, sa ligne (1 = la premiere), marquee.
    void goTo(hmi::Id function, int line);
    // 1.10 : ... et la colonne : les `length` caracteres de la faute selectionnes.
    void goTo(hmi::Id function, int line, int column, int length);

    // Les actions (sans hote : directes). kNoId / faux, et `why`, si refuse.
    // `returnType` vide ou "(aucun)" : sans retour.
    hmi::Id addFunction(std::string name, std::string returnType, std::string description, std::string* why = nullptr);
    // 1.11.22 : une surcharge de `of` (le meme nom, son retour ; l'onglet Parametres s'ouvre). Refusee
    // pour une fonction virtuelle.
    hmi::Id addOverload(hmi::Id of, std::string* why = nullptr);
    bool    renameFunction(hmi::Id, const std::string& name, std::string* why = nullptr);
    // 1.12.18 (planche 19) : la copie juste apres elle (Moyenne_copie) ; son corps suit (Moyenne := ... -> Moyenne_copie := ...).
    hmi::Id duplicateFunction(hmi::Id, std::string* why = nullptr);
    bool    setReturnType(hmi::Id, const std::string& type, std::string* why = nullptr);
    // 1.13.0 (lot J6b) : l'unite du retour (Rend : REAL [m3/h]) - un retour numerique seulement ; vide : sans
    // unite. Changer le retour pour un type qui n'en prend pas la retire. pickReturnUnit : le selecteur d'unites.
    bool    setReturnUnit(hmi::Id, const std::string& unit, std::string* why = nullptr);
    void    pickReturnUnit(hmi::Id);
    bool    setDescription(hmi::Id, const std::string& description);
    bool    setBody(hmi::Id, const std::string& body);
    bool    deleteFunction(hmi::Id);

    // ESSAYER : un argument par parametre, en ST ("2.5", "'texte'", "TRUE",
    // "T#5s", une expression) ; vide : la valeur initiale du parametre.
    struct Trial {
        hmi::Id                  function{hmi::kNoId};
        std::string              call;        // "Moyenne(a := 2, b := 4)"
        bool                     ok{false};
        std::string              result;      // "3.0" ; vide : sans retour
        std::string              type;
        std::string              error;
        std::vector<std::string> journal;     // ce que la fonction a ecrit au journal
        std::vector<std::string> changed;     // "Compteur_Clics : 0 -> 1"
        std::vector<std::string> outputs;     // 1.11.20 : "Graine (E/S) = 12345", "Tirage (sortie) = 0.42"
        bool                     livePlc{false};
        std::string              plcState;    // 1.11.21 : "en marche", "arretee : ses valeurs initiales"... (vide : non dit)
    };
    bool tryFunction(hmi::Id, const std::vector<std::string>& arguments);
    [[nodiscard]] const Trial& lastTrial() const noexcept { return trial_; }

    [[nodiscard]] const std::vector<hmi::ScriptDiagnostic>& diagnostics() const noexcept { return diagnostics_; }

    // L'aide a la saisie (comme les scripts) : la liste, les signatures, la
    // barre du nom sous le curseur. `plc` : le programme de l'automate.
    void setAssist(std::function<std::shared_ptr<const domain::Project>()> plc,
                   std::function<bool(std::string_view, std::string&)> live = {});
    [[nodiscard]] ui::StatusBar& symbolBar() noexcept { return *symbolBar_; }
    void updateSymbolLine();

    struct Hosts {
        std::function<void()>                   newFunction;
        std::function<void(hmi::Id)>            remove;
        std::function<void(hmi::Id)>            tryIt;       // les arguments (un dialogue), puis tryFunction
        std::function<void()>                   compile;     // ouvrir IHM > Compiler (1.11.17 : plus par le bouton Compiler)
        std::function<sim::Environment*()>      plc;         // l'automate de la simulation, s'il est charge (meme arrete)
        std::function<std::string()>            plcState;    // 1.11.21 : son etat, dit par l'essai ("en marche"...)
        // 1.11.2 (decision 174) : Exporter (les elements coches, celui-ci d'avance) et Importer... (tout paquet).
        std::function<void(hmi::Id)>            exportItems;
        std::function<void()>                   importAny;
        // 1.11.13 : la generation incrementale (comme les scripts) - la fonction choisie
        // ("fonction:7", ou "fonction-symbole:31" dans un symbole), son etat de build.
        std::function<void(hmi::pipeline::Mode, const std::string& key)>           build;
        std::function<std::pair<std::string, std::string>(const std::string& key)> buildState;
        std::function<void()>                                                      buildOutputs;
        std::function<void()>                                                      showDiagnostics;   // 1.11.21 : le panneau du bas, ses Diagnostics
        // 1.11.21 : le resultat d'Essayer, aux Sorties du panneau du bas (plus de bandeau ESSAI) :
        // ses lignes (gravite, texte), dans l'ordre. Sans hote : la barre du volet seule.
        std::function<void(const std::vector<std::pair<hmi::pipeline::Severity, std::string>>&)> trialOutput;
        // 1.13.0 (lot J4b) : LES TESTS UNITAIRES - leurs verdicts aux Sorties du panneau du bas (categorie Tests ;
        // l'element de chaque ligne, cas-test:N, y mene), la fenetre " Nouveau cas " (la vue kNoId : le projet),
        // et l'ecriture du rapport dans exports/.
        std::function<void(const std::vector<TestLine>&)>                  testOutput;
        std::function<void(hmi::Id view, hmi::Id function)>                newTestCase;
        std::function<bool(const hmi::ExportRequest&, std::string*)>       exportFile;
    };
    void setHosts(Hosts h);
    [[nodiscard]] const Hosts& hosts() const noexcept { return hosts_; }   // 1.11.13 : les completer (l'ecran)
    [[nodiscard]] std::string buildKey() const;   // 1.11.13
    void refreshBuildState();
    // 1.11.17 (refonte des scripts, lot 1, spec. 13) : COMPILER LA FONCTION ACTUELLE (le
    // bouton, F7) - ses fautes ici, puis le build de cet element (son etat, les Diagnostics
    // du panneau du bas filtres sur lui). Avant, le bouton ouvrait aussi IHM > Compiler
    // (le projet entier) ; il reste a part. Rend le nombre de fautes (0 : aucune fonction).
    std::size_t compileCurrent();
    [[nodiscard]] const std::string& buildStateText() const noexcept { return buildStateText_; }

    // ---- 1.11.18 (refonte des scripts, lot 5) : LES ONGLETS DE LA FONCTION ----
    //  Code, Parametres (l'ordre est la signature), Locales, Constantes : ses declarations
    //  du modele (HmiDeclGrid). Une nouvelle fonction y recoit son parametre d'exemple et
    //  sa locale Resultat (plus de bloc VAR dans son code). Le bandeau de l'ancien format.
    // 1.13.0 (lot J4b) : CodeTabTests - le 5e onglet, les tests unitaires de la fonction (HmiFunctionTestsPage).
    enum CodeTab : std::size_t { CodeTabCode = 0, CodeTabParameters = 1, CodeTabLocals = 2, CodeTabConstants = 3, CodeTabTests = 4 };
    void showCodeTab(std::size_t tab);
    [[nodiscard]] std::size_t currentCodeTab() const noexcept;
    [[nodiscard]] HmiCodeTabs&   codeTabs() noexcept { return *codeTabs_; }
    [[nodiscard]] HmiDeclGrid&   parametersGrid() noexcept { return *codeTabs_->grid(hmi::decledit::Tab::Parameters); }
    [[nodiscard]] HmiDeclGrid&   localsGrid() noexcept { return *codeTabs_->grid(hmi::decledit::Tab::Variables); }
    [[nodiscard]] HmiDeclGrid&   constantsGrid() noexcept { return *codeTabs_->grid(hmi::decledit::Tab::Constants); }
    [[nodiscard]] HmiDeclBanner& declBanner() noexcept { return *banner_; }
    // ---- 1.13.0 (lot J4b) : LES TESTS UNITAIRES ----
    //  L'onglet Tests de la fonction montree ; montrer un cas (sa fonction choisie, l'onglet ouvert, la ligne
    //  choisie) ; " Lancer les tests " de la barre : tous les cas de toutes les fonctions du volet (le projet, ou
    //  la vue), leurs verdicts au panneau du bas. Rend le nombre de cas reussis.
    [[nodiscard]] HmiFunctionTestsPage& testsPage() noexcept { return *testsPage_; }
    bool showTestCase(hmi::Id test);
    std::size_t runTests();
    [[nodiscard]] const TestCampaign& lastCampaign() const noexcept { return campaign_; }
    [[nodiscard]] std::optional<hmi::decledit::Place> currentPlace() const;
    bool migrateCurrent();
    bool goToNextUse(const std::string& name, int which = -1);   // 1.12.18 : which >= 0 - celle de ce rang
    bool showDeclaration(const std::string& name);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::MultiLineText& editor() noexcept { return *editor_; }
    [[nodiscard]] ui::TableView&     functionTable() noexcept { return *functions_; }
    // ---- 1.11.21 : HmiLiveSource (les diagnostics de la fonction montree, au panneau du bas) ----
    [[nodiscard]] std::uint64_t liveRevision() const noexcept override { return liveRev_; }
    [[nodiscard]] std::string liveElement() const override { return buildKey(); }
    [[nodiscard]] std::vector<hmi::pipeline::Diagnostic> liveDiagnostics() const override;
    void goToLive(const hmi::pipeline::Diagnostic& d) override;
    // 1.11.21 : les lignes que l'essai envoie aux Sorties (trialLines(lastTrial())).
    [[nodiscard]] std::vector<std::pair<hmi::pipeline::Severity, std::string>> trialLines() const;
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *props_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;   // 1.11.17 : F7, la fonction actuelle

private:
    void showSelected();
    void rebuildProperties();
    void showStatus();                       // 1.11.21 : la barre - le compte du volet, les fautes de la fonction montree
    void updateDiagnostics();
    void say(std::string text, bool warning = false);
    [[nodiscard]] const hmi::HmiFunction* current() const;
    // 1.11.10 : la liste editee (les fonctions IHM, ou celles du symbole) et l'une d'elles.
    [[nodiscard]] std::vector<hmi::HmiFunction>*       listOf(hmi::Project&) const;
    [[nodiscard]] const std::vector<hmi::HmiFunction>* listOf(const hmi::Project&) const;
    [[nodiscard]] hmi::HmiFunction*       fnOf(hmi::Project&, hmi::Id) const;
    [[nodiscard]] const hmi::HmiFunction* fnOf(const hmi::Project&, hmi::Id) const;
    [[nodiscard]] const hmi::View*        symbolView() const;
    // 1.11.20 : `overload` - un nom deja pris est permis (une surcharge : sa forme differera) ;
    // faux (renommer) : le nom doit etre libre (hors ses propres surcharges, renommees avec elle).
    [[nodiscard]] bool nameAllowed(const std::string& name, hmi::Id self, std::string* why, bool overload = false) const;
    // 1.11.20 : les fonctions de ce nom la ou vit le volet (le symbole, ou le projet).
    [[nodiscard]] std::vector<const hmi::HmiFunction*> named(std::string_view name) const;

    hmi::DocumentPtr   doc_;
    Apply              apply_;
    Hosts              hosts_;
    std::string        buildStateText_;   // 1.11.13
    hmi::Id            symbol_{hmi::kNoId};   // 1.11.10 : le symbole dont on edite les fonctions
    HmiToolStrip*      tools_{nullptr};
    ui::Splitter*      split_{nullptr};
    ui::TableView*     functions_{nullptr};
    ui::PropertyGrid*  props_{nullptr};
    HmiTitledPanel*    editorPanel_{nullptr};
    HmiCodeTabs*       codeTabs_{nullptr};         // 1.11.18 (lot 5) : Code | Parametres | Locales | Constantes
    HmiFunctionTestsPage* testsPage_{nullptr};     // 1.13.0 (lot J4b) : ... | Tests
    TestCampaign       campaign_;                  // 1.13.0 (lot J4b) : le dernier " Lancer les tests " du volet
    void refreshTestsTab();
    HmiDeclBanner*     banner_{nullptr};
    ui::MultiLineText* editor_{nullptr};
    ui::StatusBar*     symbolBar_{nullptr};
    assist::Sources    assist_;
    ui::StatusBar*     status_{nullptr};
    std::shared_ptr<ui::ITableModel> functionModel_;
    std::string        countsText_;             // 1.11.21 : "9 fonction(s) IHM . 7 avec retour..." (refresh)
    std::uint64_t      liveRev_{0};             // 1.11.21 : croit a chaque calcul des diagnostics
    std::vector<hmi::Id> order_;
    std::vector<hmi::ScriptDiagnostic> diagnostics_;
    Trial              trial_;
    int                selectedRow_{-1};
    std::string        message_;
    bool               syncing_{false};
    hmi::Id            shownId_{hmi::kNoId};   // 1.12.2 : le document montre (annuler : le meme, la vue gardee)
    std::shared_ptr<int>  alive_{std::make_shared<int>(0)};   // 1.11.19 : le selecteur de types repond apres coup
    core::ConnectionScope links_;
};

} // namespace app
