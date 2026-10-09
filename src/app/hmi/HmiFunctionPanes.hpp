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
#include "HmiPanels.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../sim/Interpreter.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiFunctionsPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiFunctionsPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    // 1.11.10 : LES FONCTIONS D'UN SYMBOLE (le sous-onglet Fonctions de son editeur) : la
    // meme liste, le meme editeur, sur View::functions du symbole `symbol` ; en plus, la
    // case Virtuelle (une instance peut la redefinir) et les instances qui la redefinissent.
    // Le corps lit les parametres du symbole (les diagnostics les connaissent).
    void setSymbol(hmi::Id symbol);
    [[nodiscard]] hmi::Id symbol() const noexcept { return symbol_; }
    bool setVirtual(hmi::Id, bool on);

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
    bool    renameFunction(hmi::Id, const std::string& name, std::string* why = nullptr);
    bool    setReturnType(hmi::Id, const std::string& type, std::string* why = nullptr);
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
        bool                     livePlc{false};
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
        std::function<sim::Environment*()>      plc;         // l'automate de la simulation, s'il tourne
        // 1.11.2 (decision 174) : Exporter (les elements coches, celui-ci d'avance) et Importer... (tout paquet).
        std::function<void(hmi::Id)>            exportItems;
        std::function<void()>                   importAny;
        // 1.11.13 : la generation incrementale (comme les scripts) - la fonction choisie
        // ("fonction:7", ou "fonction-symbole:31" dans un symbole), son etat de build.
        std::function<void(hmi::pipeline::Mode, const std::string& key)>           build;
        std::function<std::pair<std::string, std::string>(const std::string& key)> buildState;
        std::function<void()>                                                      buildOutputs;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    [[nodiscard]] const Hosts& hosts() const noexcept { return hosts_; }   // 1.11.13 : les completer (l'ecran)
    [[nodiscard]] std::string buildKey() const;   // 1.11.13
    void refreshBuildState();
    // 1.11.17 (refonte des scripts, lot 1, spec. 13) : COMPILER LA FONCTION ACTUELLE (le
    // bouton, F7) - ses fautes ici, puis le build de cet element (son etat, les Diagnostics
    // du panneau du bas filtres sur lui). Avant, le bouton ouvrait aussi IHM > Compiler
    // (le projet entier) ; il reste a part. Rend le nombre de fautes (0 : aucune fonction).
    std::size_t compileCurrent();
    [[nodiscard]] const std::string& buildStateText() const noexcept { return buildStateText_; }

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::MultiLineText& editor() noexcept { return *editor_; }
    [[nodiscard]] ui::TableView&     functionTable() noexcept { return *functions_; }
    [[nodiscard]] ui::TableView&     diagnosticTable() noexcept { return *diagTable_; }
    [[nodiscard]] ui::TableView&     trialTable() noexcept { return *trialTable_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *props_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;   // 1.11.17 : F7, la fonction actuelle

private:
    void showSelected();
    void rebuildProperties();
    void rebuildTrial();
    void updateDiagnostics();
    void say(std::string text, bool warning = false);
    [[nodiscard]] const hmi::HmiFunction* current() const;
    // 1.11.10 : la liste editee (les fonctions IHM, ou celles du symbole) et l'une d'elles.
    [[nodiscard]] std::vector<hmi::HmiFunction>*       listOf(hmi::Project&) const;
    [[nodiscard]] const std::vector<hmi::HmiFunction>* listOf(const hmi::Project&) const;
    [[nodiscard]] hmi::HmiFunction*       fnOf(hmi::Project&, hmi::Id) const;
    [[nodiscard]] const hmi::HmiFunction* fnOf(const hmi::Project&, hmi::Id) const;
    [[nodiscard]] const hmi::View*        symbolView() const;
    [[nodiscard]] bool nameAllowed(const std::string& name, hmi::Id self, std::string* why) const;

    hmi::DocumentPtr   doc_;
    Apply              apply_;
    Hosts              hosts_;
    std::string        buildStateText_;   // 1.11.13
    hmi::Id            symbol_{hmi::kNoId};   // 1.11.10 : le symbole dont on edite les fonctions
    HmiToolStrip*      tools_{nullptr};
    ui::Splitter*      split_{nullptr};
    ui::TableView*     functions_{nullptr};
    ui::PropertyGrid*  props_{nullptr};
    ui::TableView*     trialTable_{nullptr};
    HmiTitledPanel*    editorPanel_{nullptr};
    ui::MultiLineText* editor_{nullptr};
    ui::StatusBar*     symbolBar_{nullptr};
    assist::Sources    assist_;
    ui::TableView*     diagTable_{nullptr};
    ui::StatusBar*     status_{nullptr};
    std::shared_ptr<ui::ITableModel> functionModel_, diagModel_, trialModel_;
    std::vector<hmi::Id> order_;
    std::vector<hmi::ScriptDiagnostic> diagnostics_;
    Trial              trial_;
    int                selectedRow_{-1};
    std::string        message_;
    bool               syncing_{false};
    core::ConnectionScope links_;
};

} // namespace app
