// =============================================================================
//  app/hmi/HmiMethodPanes.hpp - la section Methodes d'un type IHM (1.13.0, J1b)
// -----------------------------------------------------------------------------
//  Dans Types IHM, sous la grille des membres (au-dessus des Operateurs) : les
//  METHODES du type choisi - les siennes, puis celles qu'il herite de ses parents
//  (grisees, "(herite de T_Pompe)") - ses proprietes et l'editeur de son corps.
//
//    +------------------------------ outils ------------------------------+
//    | METHODES DE T_POMPE (n)          | CORPS - T_Pompe.Demarrer()      |
//    | PROPRIETES DE LA METHODE         | legende SELF / membres / retour |
//    |                                  | editeur (marge des points)      |
//    +------------------------------- etat -------------------------------+
//
//  1.50.0 (planche 01) : UNE METHODE EST UNE FONCTION PORTEE PAR LE TYPE. "Ajouter une methode..." ouvre LA fenetre
//  des fonctions (HmiNewFunctionDialog, son 5e proprietaire : le type ; newfunction::ask) - Nom, Generiques, Retour,
//  Unite, Portee, Virtuelle, Description, "Elle s'ecrira FUNCTION PUBLIC VIRTUAL Demarrer : BOOL" -, "Creer" la pose en
//  une commande et ouvre l'onglet Parametres. L'editeur a LES CINQ ONGLETS d'une fonction (makeFunctionTabs, le meme
//  composant que le volet Fonctions) : Code (la legende SELF, le bandeau Migrer, le corps), Parametres (SELF en tete,
//  grise et verrouille), Locales, Constantes, Tests (les cas partent d'une instance d'essai du type : HmiFunctionTests).
//  Redefinir (sur une virtuelle heritee) cree la redefinition preremplie avec SUPER ; Dupliquer, Supprimer, Monter,
//  Descendre, Lancer les tests ; le nom se change dans sa case. Chaque geste est une commande
//  (hmi::changeProject) : Ctrl+Z le reprend. Le corps s'edite dans l'editeur des scripts (colore,
//  numerote, l'aide a la saisie qui connait SELF, les membres, les parametres et les locales) avec
//  la marge des points d'arret du debogueur (la cible : la methode) ; ses fautes
//  (hmi::checkMethod) sont soulignees a la frappe, F7 les compte.
//  Une INTERFACE montre ses signatures (sans corps) : "Ajouter une signature...".
// =============================================================================
#pragma once

#include "HmiAssist.hpp"
#include "HmiDebugSession.hpp"
#include "HmiFunctionDialog.hpp"     // 1.50.0 : la fenetre des fonctions (Nouvelle methode du type)
#include "HmiFunctionPanes.hpp"      // 1.50.0 : makeFunctionTabs, les cinq onglets partages
#include "HmiPanels.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiMethods.hpp"
#include "../../hmi/HmiScript.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app {

// ---- LA SECTION METHODES -------------------------------------------------------
class HmiMethodsPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiMethodsPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    // Le type montre (une structure ou une interface ; kNoId, une enumeration, un rappel : rien).
    void setType(hmi::Id type);
    [[nodiscard]] hmi::Id type() const noexcept { return type_; }
    void refresh();
    // Une ligne de la liste : une methode du type (propre) ou d'un parent (heritee).
    struct Row {
        hmi::Id id{hmi::kNoId};
        hmi::Id owner{hmi::kNoId};   // le type qui la porte
        bool    own{true};
    };
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t count() const noexcept { return rows_.size(); }
    [[nodiscard]] std::size_t ownCount() const;
    // La methode de la ligne choisie (heritee comprise) ; selectedMethod : une methode PROPRE seulement.
    [[nodiscard]] hmi::Id selectedRowMethod() const;
    [[nodiscard]] hmi::Id selectedMethod() const;
    [[nodiscard]] bool    selectedInherited() const;
    void selectMethod(hmi::Id);
    // Aller a la source : la methode, sa ligne (1 = la premiere ; 0 : seulement la choisir).
    void goTo(hmi::Id method, int line);

    // ---- les gestes (une commande chacun : Ctrl+Z) ; kNoId / faux et `why` si refuse ----
    // `params` : une ligne "Consigne : REAL; VAR_IN_OUT Cible : T_Pompe" (les scripts, l'IA) ; vide : aucun.
    hmi::Id addMethod(const std::string& name, const std::string& returnType, const std::string& params, bool isPrivate,
                      bool isVirtual, std::string* why = nullptr);
    // 1.50.0 : la reponse de la fenetre des fonctions - le nom, les generiques, le retour et son unite, la portee, la
    // virtuelle, la description : UNE commande (un Ctrl+Z la retire), la methode choisie, l'onglet Parametres ouvert.
    hmi::Id createMethod(const HmiNewFunctionDialog::Answer& answer, std::string* why = nullptr);
    bool    renameMethod(hmi::Id, const std::string& name, std::string* why = nullptr);
    hmi::Id duplicateMethod(hmi::Id, std::string* why = nullptr);
    bool    deleteMethod(hmi::Id, std::string* why = nullptr);
    bool    moveMethod(hmi::Id, int delta);
    bool    setPrivate(hmi::Id, bool isPrivate, std::string* why = nullptr);
    bool    setVirtual(hmi::Id, bool isVirtual, std::string* why = nullptr);
    bool    setReturnType(hmi::Id, const std::string& type, std::string* why = nullptr);
    bool    setParams(hmi::Id, const std::string& params, std::string* why = nullptr);
    bool    setDescription(hmi::Id, const std::string& text);
    bool    setBody(hmi::Id, const std::string& body);
    // Redefinir une virtuelle heritee : une methode de meme signature, son corps prerempli avec SUPER.
    hmi::Id overrideMethod(hmi::Id inherited, std::string* why = nullptr);
    // La fenetre des fonctions, sur ce type (newfunction::ask) : faux - aucun type, ou aucun hote pour l'ouvrir. Sa
    // reponse passe par createMethod.
    bool    openAddDialog();
    // La fenetre telle qu'elle s'ouvrirait (le type, un nom libre) : les essais la posent eux-memes.
    [[nodiscard]] HmiNewFunctionDialog::Spec addDialogSpec() const;

    // ---- 1.50.0 : LES CINQ ONGLETS (ceux d'une fonction : makeFunctionTabs) ----
    enum CodeTab : std::size_t { CodeTabCode = 0, CodeTabParameters = 1, CodeTabLocals = 2, CodeTabConstants = 3, CodeTabTests = 4 };
    void showCodeTab(std::size_t tab);
    [[nodiscard]] std::size_t currentCodeTab() const noexcept;
    [[nodiscard]] HmiCodeTabs&          codeTabs() noexcept { return *codeTabs_; }
    [[nodiscard]] HmiDeclGrid&          parametersGrid() noexcept { return *codeTabs_->grid(hmi::decledit::Tab::Parameters); }
    [[nodiscard]] HmiDeclGrid&          localsGrid() noexcept { return *codeTabs_->grid(hmi::decledit::Tab::Variables); }
    [[nodiscard]] HmiDeclGrid&          constantsGrid() noexcept { return *codeTabs_->grid(hmi::decledit::Tab::Constants); }
    [[nodiscard]] HmiDeclBanner&        declBanner() noexcept { return *banner_; }
    [[nodiscard]] HmiFunctionTestsPage& testsPage() noexcept { return *testsPage_; }
    // La methode montree, comme un code (ses declarations) ; vide : aucune, ou heritee (elle se change dans son type).
    [[nodiscard]] std::optional<hmi::decledit::Place> currentPlace() const;
    bool migrateCurrent();                      // le bandeau " Migrer ce code " (une commande)
    // Les tests : un cas montre (sa methode choisie, l'onglet Tests) ; " Lancer les tests " - tous les cas des methodes
    // du type, leurs verdicts au panneau du bas ; rend le nombre de cas reussis.
    bool showTestCase(hmi::Id test);
    std::size_t runTests();
    [[nodiscard]] const TestCampaign& lastCampaign() const noexcept { return campaign_; }
    void setTestHosts(HmiFunctionTestsPage::Hosts h);
    // Vit encore ? (une fenetre repond apres coup.)
    [[nodiscard]] std::weak_ptr<int> aliveToken() const { return alive_; }

    // La legende au-dessus du corps : SELF, ses membres, le retour.
    [[nodiscard]] std::string legendText() const;
    // 1.13.0 (correctifs F2) : comment s'appelle la methode choisie (la ligne d'etat) - avec une vraie instance du type
    // (Pompe2.EnDefaut() pour T_PompeDoublee), sinon <instance> ; une privee, seulement d'une methode du type.
    [[nodiscard]] std::string callHint() const;
    [[nodiscard]] std::string statusText() const;
    [[nodiscard]] std::string editorTitle() const;
    // F7 : les fautes de la methode montree (Compiler du document) ; rend leur nombre.
    std::size_t compileCurrent();
    [[nodiscard]] const std::vector<hmi::ScriptDiagnostic>& diagnostics() const noexcept { return diagnostics_; }

    [[nodiscard]] HmiToolStrip&        tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&       methodTable() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid&    properties() noexcept { return *props_; }
    [[nodiscard]] ui::MultiLineText&   editor() noexcept { return *editor_; }
    [[nodiscard]] HmiBreakpointMargin& breakpointMargin() noexcept { return margin_; }
    [[nodiscard]] const std::string&   lastMessage() const noexcept { return message_; }

    // Les actions de la barre (les essais, les scripts).
    enum Action : int { AAdd = 1, AOverride, ADuplicate, ADelete, AUp, ADown, ACompile, ARunTests };

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void showSelected();
    void rebuildProperties();
    void updateDiagnostics();
    void say(std::string text, bool warning = false);
    [[nodiscard]] const hmi::HmiType* owner() const;
    [[nodiscard]] const hmi::HmiFunction* current() const;
    // Un geste sur une methode : essaye sur une copie (refuse s'il fait naitre une faute de forme), puis une commande.
    bool change(hmi::Id method, std::string label,
                const std::function<bool(hmi::Project&, hmi::HmiType&, hmi::HmiFunction&, std::string*)>& edit, std::string* why,
                std::string mergeKey = {});

    hmi::DocumentPtr   doc_;
    Apply              apply_;
    hmi::Id            type_{hmi::kNoId};
    HmiToolStrip*      tools_{nullptr};
    ui::Splitter*      split_{nullptr};
    HmiTitledPanel*    listPanel_{nullptr};
    ui::TableView*     table_{nullptr};
    HmiTitledPanel*    propsPanel_{nullptr};   // 1.13.0 (correctifs F2) : "de la signature" sur une interface
    ui::PropertyGrid*  props_{nullptr};
    HmiTitledPanel*    editorPanel_{nullptr};
    ui::StatusBar*     legend_{nullptr};
    ui::MultiLineText* editor_{nullptr};
    ui::StatusBar*     status_{nullptr};
    HmiCodeTabs*       codeTabs_{nullptr};         // 1.50.0 : Code | Parametres | Locales | Constantes | Tests
    HmiDeclBanner*     banner_{nullptr};
    HmiFunctionTestsPage* testsPage_{nullptr};
    TestCampaign       campaign_;
    std::shared_ptr<int> alive_{std::make_shared<int>(0)};
    void refreshTabs();
    HmiBreakpointMargin margin_;
    assist::Sources    assist_;
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<Row>   rows_;
    std::vector<hmi::ScriptDiagnostic> diagnostics_;
    int                selectedRow_{-1};
    hmi::Id            shownId_{hmi::kNoId};
    std::string        message_;
    bool               syncing_{false};
    // 1.13.0 (relecture F2) : la liste montre une interface (le cadenas, Genre et Origine caches) ; ce que
    // l'utilisateur avait laisse de ces trois colonnes sur une structure, rendu au retour.
    bool               ifaceColumns_{false};
    bool               keptColumns_[3]{true, true, true};
    core::ConnectionScope links_;
};

} // namespace app
