// =============================================================================
//  app/hmi/HmiOperatorPanes.hpp - les operateurs d'un symbole ou d'un type IHM (1.10)
// -----------------------------------------------------------------------------
//  LE SOUS-ONGLET "OPERATEURS" D'UN SYMBOLE ET LA SECTION "OPERATEURS" D'UN
//  TYPE IHM : la liste de ses operateurs (conversions TO_xxx, + - * / += -= *= /=,
//  comparaisons), chacun avec son script, dans l'editeur de scripts habituel
//  (colore, numerote, l'aide a la saisie, la barre du nom sous le curseur, les
//  diagnostics a chaque frappe).
//
//    +---------------------------- outils -----------------------------+
//    | OPERATEURS (signature, fonction, etat) | EDITEUR - signature     |
//    | PROPRIETES (genre, operandes, cible)   | DIAGNOSTICS             |
//    +----------------------------- etat ------------------------------+
//
//  AJOUTER une conversion ou un operateur cree son script prerempli (la
//  signature, a et b, un corps d'exemple tire des membres du type) ; changer le
//  genre, un operande ou la cible dans les proprietes est refuse s'il ne va pas
//  (hmi::operatorProblem) et met a jour l'en-tete du script. Tout passe par
//  hmi::changeProject : Ctrl+Z.
//  1.10.1 (U2) : "Ajouter un operateur" ouvre la fenetre de la maquette ; au-dessus
//  du script, la legende dit ce que sont a, b et Resultat (avec leurs types) ;
//  l'aide a la saisie les connait (a. : les membres du type de gauche).
// =============================================================================
#pragma once

#include "HmiAssist.hpp"
#include "HmiDeclGrid.hpp"             // 1.11.18 (refonte, lot 5) : les onglets Locales, Constantes
#include "HmiPanels.hpp"
#include "HmiLive.hpp"                  // 1.11.21 : les diagnostics en direct (le panneau du bas)
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiOperators.hpp"
#include "../../hmi/HmiPipeline.hpp"   // 1.11.17 : Compiler les operateurs (le build de leur porteur)
#include "../../hmi/HmiScript.hpp"
#include "../../hmi/HmiScriptFile.hpp"   // 1.11.3 : exporter / importer les operateurs (.xpgst)
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app {

// ---- 1.10.1 (U2) : la fenetre "Ajouter un operateur" (maquette 1.10, scene 13) ----
//  Posee par-dessus toute la fenetre (la passe du dessus, modale) : le genre en
//  pastilles ; l'operande de droite, ou le type cible d'une conversion ; la
//  signature (nouveau, ou "existe deja . l'ouvrir" et Creer grise) ; la legende
//  (a, b, Resultat et leurs types, un exemple) ; le script prerempli. Annuler /
//  "Creer et ouvrir le script" (Entree) ; Echap ferme. L'hote fait la commande.
class HmiOperatorDialog final : public ui::Widget {
public:
    explicit HmiOperatorDialog(std::string id);
    // Ouvrir pour ce porteur : le genre ("TO", "+", "+="...) et l'operande (ou la
    // cible) preregles ; `operands` : les operandes de droite proposes ; `targets` :
    // les cibles d'une conversion ; `plcType` : un DDT de l'automate.
    void open(hmi::DocumentPtr doc, hmi::OperatorOwner owner, std::string kind, std::string operand,
              std::vector<std::string> operands, std::vector<std::string> targets,
              std::function<bool(std::string_view)> plcType = {});
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    void setKind(const std::string& op);            // une pastille du genre
    void setOperand(const std::string& type);       // une pastille de l'operande (ou de la cible)
    [[nodiscard]] const std::string& kind() const noexcept { return kind_; }
    [[nodiscard]] const std::string& operand() const noexcept { return kind_ == "TO" ? target_ : operand_; }
    [[nodiscard]] const hmi::OperatorOwner& owner() const noexcept { return owner_; }
    [[nodiscard]] const hmi::HmiOperator& draft() const noexcept { return draft_; }   // le brouillon : signature, script
    [[nodiscard]] std::string signature() const;
    [[nodiscard]] hmi::Id existing() const noexcept { return existing_; }       // la meme signature (kNoId : nouveau)
    [[nodiscard]] const std::string& problem() const noexcept { return problem_; }   // vide : Creer est permis
    [[nodiscard]] std::string legend() const;
    [[nodiscard]] std::string example() const;
    [[nodiscard]] std::vector<std::string> kinds() const;
    [[nodiscard]] const std::vector<std::string>& choices() const noexcept { return kind_ == "TO" ? targets_ : operands_; }
    // Les endroits (essais, sessions) ; faux / vide : pas montre.
    [[nodiscard]] bool kindRect(std::string_view op, gfx::Rect& out) const;
    [[nodiscard]] bool choiceRect(std::string_view type, gfx::Rect& out) const;
    [[nodiscard]] gfx::Rect createRect() const;
    [[nodiscard]] gfx::Rect cancelRect() const;
    [[nodiscard]] gfx::Rect openLinkRect() const;  // "l'ouvrir" (existe deja)

    std::function<void()>        onCreate;          // "Creer et ouvrir le script"
    std::function<void(hmi::Id)> onOpen;            // existe deja : l'ouvrir

protected:
    void onPaintOverlay(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
    [[nodiscard]] bool requestsOverlayPass() const override { return open_; }
    [[nodiscard]] gfx::Rect eventBounds() const override;

private:
    struct Chip { std::string value, label; gfx::Rect rect; };
    void rebuild();                                  // le brouillon, le refus, la place de chaque chose
    [[nodiscard]] gfx::Rect surface() const;

    hmi::DocumentPtr          doc_;
    hmi::OperatorOwner        owner_;
    std::string               kind_{"+"}, operand_, target_;
    std::vector<std::string>  operands_, targets_;
    std::function<bool(std::string_view)> plcType_;
    hmi::HmiOperator          draft_;
    hmi::Id                   existing_{hmi::kNoId};
    std::string               problem_;
    bool                      open_{false};
    // la place de chaque chose (calculee par rebuild)
    gfx::Rect                 box_{}, close_{}, create_{}, cancel_{}, link_{}, sig_{}, legend_{}, code_{};
    float                     kindsY_{0.f}, choicesY_{0.f}, hintY_{0.f};
    std::vector<Chip>         kindChips_, choiceChips_;
};

class HmiOperatorsPane final : public ui::Widget, public HmiLiveSource {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiOperatorsPane(std::string id, hmi::DocumentPtr doc, Apply apply, hmi::OperatorOwner owner = {});

    // Le porteur montre : un symbole (sa vue) ou un type IHM.
    void setOwner(hmi::OperatorOwner owner);
    [[nodiscard]] const hmi::OperatorOwner& owner() const noexcept { return owner_; }
    void refresh();
    [[nodiscard]] hmi::Id selectedOperator() const;
    void selectOperator(hmi::Id);
    // Aller a la source : l'operateur, sa ligne (1 = la premiere).
    void goTo(hmi::Id op, int line);
    [[nodiscard]] std::size_t count() const noexcept { return order_.size(); }

    // ---- les actions (directes, une commande chacune : Ctrl+Z). kNoId / faux, et `why`, si refuse.
    // `op` : "TO" (une conversion : `right` vide, `result` la cible) ou "+", "+=", "="... ;
    // `result` vide pour += -= *= /= ; BOOL pour une comparaison.
    hmi::Id addOperator(const std::string& op, const std::string& left, const std::string& right, const std::string& result,
                        std::string* why = nullptr);
    // La premiere conversion libre du porteur (TO_STRING, TO_REAL, TO_DINT, TO_BOOL...).
    hmi::Id addConversion(std::string* why = nullptr);
    // Le premier operateur libre (porteur + porteur : porteur, puis - * / = <>...).
    hmi::Id addArithmetic(std::string* why = nullptr);
    bool    changeSignature(hmi::Id, const std::string& op, const std::string& left, const std::string& right,
                            const std::string& result, std::string* why = nullptr);
    // Renommer : une conversion change de cible ("TO_LREAL" ou "LREAL"), un
    // operateur de symbole ("-", ou son nom CEI "SUB").
    bool    renameOperator(hmi::Id, const std::string& name, std::string* why = nullptr);
    bool    setDescription(hmi::Id, const std::string& description);
    bool    setBody(hmi::Id, const std::string& body);
    bool    deleteOperator(hmi::Id, std::string* why = nullptr);
    // La copie prend la signature libre suivante (une autre cible, un autre operateur).
    hmi::Id duplicateOperator(hmi::Id, std::string* why = nullptr);
    // 1.10 (decision 15) : une enumeration - son toString et son fromString recrits
    // d'apres ses valeurs (une commande : Ctrl+Z). Faux : le porteur n'est pas une enumeration.
    bool    rewriteFromValues(std::string* why = nullptr);
    // 1.10.1 (U2) : la fenetre "Ajouter un operateur" - l'outil l'ouvre (preregle comme la
    // maquette : / et REAL sur un type, une conversion sur un symbole) ; Dupliquer l'ouvre
    // avec le meme genre et l'operande (ou la cible) libre suivant. Faux : aucun porteur.
    bool    openAddDialog(const std::string& kind = {}, const std::string& operand = {});
    bool    openDuplicateDialog(hmi::Id);
    // "Creer et ouvrir le script" : une commande (Ctrl+Z), l'operateur choisi, son script
    // dans l'editeur. kNoId (et `why`) : refuse (en double, impossible).
    hmi::Id createFromDialog(std::string* why = nullptr);
    [[nodiscard]] HmiOperatorDialog& addDialog() noexcept { return *dialog_; }
    // La legende au-dessus du script : a, b, Resultat et leurs types ; un exemple d'une ligne.
    [[nodiscard]] std::string legendText() const;
    [[nodiscard]] std::string exampleText() const;
    [[nodiscard]] const std::string& symbolLine() const noexcept { return symbolLine_; }   // la barre sous le script
    // "conversion - toString", "arithmetique"... : le genre montre dans la liste.
    [[nodiscard]] std::string kindText(hmi::Id) const;

    [[nodiscard]] const std::vector<hmi::ScriptDiagnostic>& diagnostics() const noexcept { return diagnostics_; }

    // ---- 1.11.3 : EXPORTER ET IMPORTER LES OPERATEURS (.xpgst, HmiScriptFile.hpp) ----
    //  Ceux du porteur montre (un symbole ou un type IHM). L'import compare par
    //  signature (op, gauche, droite) : un neuf est ajoute, un different remplace le
    //  sien ; un seul Ctrl+Z. Un operateur que operatorProblem refuse est laisse.
    bool exportOperators(const std::string& path, std::string* why = nullptr);
    bool importOperators(const std::string& path, std::string* why = nullptr);
    std::size_t applyOperatorsImport(const hmi::scriptfile::File&, const std::vector<bool>& chosen);

    // L'aide a la saisie (comme les scripts). `plc` : le programme de l'automate.
    void setAssist(std::function<std::shared_ptr<const domain::Project>()> plc,
                   std::function<bool(std::string_view, std::string&)> live = {});
    // Les DDT de l'automate (des cibles de conversion), et pour les reconnaitre.
    std::function<std::vector<std::string>()>    plcTypes;
    std::function<bool(std::string_view)>        isPlcType;
    std::function<void()>                        compile;     // ouvrir IHM > Compiler (sans `build` : le bouton Compiler)
    // 1.11.17 (refonte, lot 1) : le build du porteur ("symbole:12", "type:7") - ses operateurs.
    std::function<void(hmi::pipeline::Mode, const std::string& key)> build;
    // 1.11.17 (spec. 13) : COMPILER LES OPERATEURS DU PORTEUR AFFICHE (le bouton, F7) - leurs
    // fautes, puis le build du porteur (son etat, les Diagnostics du panneau du bas filtres
    // sur lui) ; le projet entier reste a IHM > Compiler. Rend le nombre de fautes.
    std::size_t compileCurrent();
    [[nodiscard]] std::string buildKey() const;   // "symbole:12", "type:7" ; vide : aucun porteur

    // ---- 1.11.18 (refonte des scripts, lot 5) : LES ONGLETS DU SCRIPT ----
    //  Code, Locales, Constantes : les declarations du modele de l'operateur montre
    //  (HmiDeclGrid) - a, b et Resultat restent les siens, implicites. Le bandeau de
    //  l'ancien format.
    enum CodeTab : std::size_t { CodeTabCode = 0, CodeTabLocals = 1, CodeTabConstants = 2 };
    void showCodeTab(std::size_t tab);
    [[nodiscard]] std::size_t currentCodeTab() const noexcept;
    [[nodiscard]] HmiCodeTabs&   codeTabs() noexcept { return *codeTabs_; }
    [[nodiscard]] HmiDeclGrid&   localsGrid() noexcept { return *codeTabs_->grid(hmi::decledit::Tab::Variables); }
    [[nodiscard]] HmiDeclGrid&   constantsGrid() noexcept { return *codeTabs_->grid(hmi::decledit::Tab::Constants); }
    [[nodiscard]] HmiDeclBanner& declBanner() noexcept { return *banner_; }
    [[nodiscard]] std::optional<hmi::decledit::Place> currentPlace() const;
    bool migrateCurrent();
    bool goToNextUse(const std::string& name);
    bool showDeclaration(const std::string& name);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::MultiLineText& editor() noexcept { return *editor_; }
    [[nodiscard]] ui::TableView&     operatorTable() noexcept { return *table_; }
    // ---- 1.11.21 : HmiLiveSource (les diagnostics de l'operateur montre, au panneau du bas) ----
    [[nodiscard]] std::uint64_t liveRevision() const noexcept override { return liveRev_; }
    [[nodiscard]] std::string liveElement() const override { return buildKey(); }
    [[nodiscard]] std::vector<hmi::pipeline::Diagnostic> liveDiagnostics() const override;
    void goToLive(const hmi::pipeline::Diagnostic& d) override;
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *props_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
    [[nodiscard]] std::string        editorTitle() const;
    [[nodiscard]] std::string        equivalents() const;   // "C++  T_VECTEUR operator+(...)   C  T_VECTEUR t_vecteur_ajouter(...)"

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;   // 1.11.17 : F7, les operateurs affiches

private:
    void showSelected();
    void rebuildProperties();
    void updateDiagnostics();
    void say(std::string text, bool warning = false);
    [[nodiscard]] const hmi::HmiOperator* current() const;
    [[nodiscard]] std::vector<std::string> typeChoices(bool operands) const;
    hmi::Id add(const hmi::OperatorOwner& owner, hmi::HmiOperator op, const std::string& label, std::string* why);

    hmi::DocumentPtr   doc_;
    Apply              apply_;
    hmi::OperatorOwner owner_;
    HmiToolStrip*      tools_{nullptr};
    ui::Splitter*      split_{nullptr};
    HmiTitledPanel*    listPanel_{nullptr};
    ui::TableView*     table_{nullptr};
    ui::PropertyGrid*  props_{nullptr};
    HmiTitledPanel*    editorPanel_{nullptr};
    HmiCodeTabs*       codeTabs_{nullptr};         // 1.11.18 (lot 5) : Code | Locales | Constantes
    HmiDeclBanner*     banner_{nullptr};
    ui::MultiLineText* editor_{nullptr};
    ui::StatusBar*     symbolBar_{nullptr};
    ui::StatusBar*     equivBar_{nullptr};      // C++ et C, au-dessus du script (comme la maquette)
    ui::Widget*        legend_{nullptr};        // 1.10.1 (U2) : a, b, Resultat, au-dessus du script
    HmiOperatorDialog* dialog_{nullptr};        // 1.10.1 (U2) : "Ajouter un operateur"
    std::string        symbolLine_;
    assist::Sources    assist_;
    ui::StatusBar*     status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::uint64_t      liveRev_{0};             // 1.11.21 : croit a chaque calcul des diagnostics
    std::vector<hmi::Id> order_;
    std::vector<hmi::ScriptDiagnostic> diagnostics_;
    int                selectedRow_{-1};
    std::string        message_;
    bool               syncing_{false};
    hmi::Id            shownId_{hmi::kNoId};   // 1.12.2 : le document montre (annuler : le meme, la vue gardee)
    core::ConnectionScope links_;
    std::shared_ptr<char> alive_ = std::make_shared<char>(0);   // 1.11.3 : les reponses de l'explorateur et des dialogues
};

} // namespace app
