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
//  "Ajouter une methode..." ouvre la fenetre HmiMethodDialog (nom, retour, parametres,
//  Privee, Redefinissable (virtuelle)) ; Redefinir (sur une virtuelle heritee) cree la
//  redefinition preremplie avec SUPER ; Dupliquer, Supprimer, Monter, Descendre ; le nom
//  et les parametres se changent dans leur case. Chaque geste est une commande
//  (hmi::changeProject) : Ctrl+Z le reprend. L'editeur est celui des scripts (colore,
//  numerote, l'aide a la saisie qui connait SELF, les membres et les parametres) avec
//  la marge des points d'arret du debogueur (la cible : la methode) ; ses fautes
//  (hmi::checkMethod) sont soulignees a la frappe, F7 les compte.
//  Une INTERFACE montre ses signatures (sans corps) : "Ajouter une signature...".
// =============================================================================
#pragma once

#include "HmiAssist.hpp"
#include "HmiDebugSession.hpp"
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
#include <string>
#include <vector>

namespace app {

// ---- LA FENETRE "AJOUTER UNE METHODE..." --------------------------------------
//  Posee par-dessus Types IHM : le nom, le retour (vide : une procedure ; le bouton
//  "Choisir..." ouvre le selecteur de types), les parametres en une ligne
//  ("Entree : REAL; VAR_IN_OUT Cible : T_Pompe"), les cases Privee et Redefinissable
//  (virtuelle) ; la signature et ce qui ne va pas, en direct. "Creer la methode"
//  (grise tant qu'il y a une faute) ou Annuler (Echap). L'hote fait la commande.
class HmiMethodDialog final : public ui::Widget {
public:
    explicit HmiMethodDialog(std::string id);
    // Ouvrir pour le type `type` (une structure : une methode ; une interface : une signature).
    void open(const hmi::Project&, hmi::Id type);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    void setName(const std::string&);
    void setReturnType(const std::string&);
    void setParams(const std::string&);
    void setPrivate(bool);
    void setVirtual(bool);
    [[nodiscard]] std::string name() const;
    [[nodiscard]] std::string returnType() const;
    [[nodiscard]] std::string params() const;
    [[nodiscard]] bool        isPrivate() const;
    [[nodiscard]] bool        isVirtual() const;
    [[nodiscard]] hmi::Id     type() const noexcept { return type_; }
    [[nodiscard]] bool        forInterface() const noexcept { return interface_; }
    // "T_Pompe.Demarrer(Consigne : REAL) : BOOL" ; ce qui ne va pas (vide : Creer permis).
    [[nodiscard]] std::string signature() const;
    [[nodiscard]] std::string problem() const;
    [[nodiscard]] std::string title() const;
    [[nodiscard]] ui::InputText& nameField() noexcept { return *name_; }
    [[nodiscard]] ui::InputText& returnField() noexcept { return *return_; }
    [[nodiscard]] ui::InputText& paramsField() noexcept { return *params_; }
    [[nodiscard]] ui::Checkbox&  privateBox() noexcept { return *private_; }
    [[nodiscard]] ui::Checkbox&  virtualBox() noexcept { return *virtual_; }
    [[nodiscard]] ui::Button&    createButton() noexcept { return *create_; }
    [[nodiscard]] ui::Button&    cancelButton() noexcept { return *cancel_; }
    [[nodiscard]] ui::Button&    chooseButton() noexcept { return *choose_; }
    [[nodiscard]] gfx::Rect boxRect() const;

    // "Creer la methode" : l'hote lit les champs et fait la commande.
    const core::SignalPtr<> created = core::Signal<>::create();
    // "Choisir..." : le selecteur de types (l'hote le branche) ; nul : le champ seul.
    std::function<void()> chooseReturn;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void sync();
    const hmi::Project* project_{nullptr};
    hmi::Id             type_{hmi::kNoId};
    bool                interface_{false};
    bool                open_{false};
    ui::InputText*      name_{nullptr};
    ui::InputText*      return_{nullptr};
    ui::InputText*      params_{nullptr};
    ui::Checkbox*       private_{nullptr};
    ui::Checkbox*       virtual_{nullptr};
    ui::Button*         choose_{nullptr};
    ui::Button*         create_{nullptr};
    ui::Button*         cancel_{nullptr};
    core::ConnectionScope links_;
};

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
    hmi::Id addMethod(const std::string& name, const std::string& returnType, const std::string& params, bool isPrivate,
                      bool isVirtual, std::string* why = nullptr);
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
    // La fenetre : l'ouvrir (faux : aucun type), "Creer la methode" (kNoId et `why` : refuse).
    bool    openAddDialog();
    hmi::Id createFromDialog(std::string* why = nullptr);
    void    setDialogWidget(HmiMethodDialog* d);
    [[nodiscard]] HmiMethodDialog* dialog() const noexcept { return dialog_; }

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
    enum Action : int { AAdd = 1, AOverride, ADuplicate, ADelete, AUp, ADown, ACompile };

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
    HmiMethodDialog*   dialog_{nullptr};
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
