// =============================================================================
//  app/hmi/HmiEditor.hpp — l'editeur d'une vue IHM, dans un onglet
// -----------------------------------------------------------------------------
//  Comme les standards du metier (TIA Portal, WinCC Unified, Figma) :
//
//    +----------------------------- outils ------------------------------+
//    | Explorateur d'objets |                            | Proprietes    |
//    |  (recherche, type)   |          la vue            |               |
//    |----------------------|                            |---------------|
//    | Calques              |                            | Bibliotheque  |
//    +----------------------------- etat --------------------------------+
//
//  Les separateurs se tirent, comme partout dans l'appli. L'onglet ne possede
//  pas la vue : il la montre et envoie des commandes. Fermer l'onglet ne perd
//  rien, Ctrl+Z reprend ce qu'il a fait meme apres.
//
//  SELECTION SYNCHRONISEE : choisir dans l'explorateur choisit sur la vue, et
//  inversement ; le panneau des proprietes suit les deux.
// =============================================================================
#pragma once

#include "HmiValueKind.hpp"   // 1.11.3 : valuekind::Request (la case du carre de legende)

#include "HmiActionsPanel.hpp"
#include "HmiCanvas.hpp"
#include "HmiContentPanel.hpp"
#include "HmiPanels.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>

namespace domain { class Project; }

namespace app {

class HmiSymbolTabs;          // 1.9 : HmiObjectAlarmPanes.hpp
class HmiSymbolAlarmsPane;
class HmiOperatorsPane;       // 1.10 (S2) : HmiOperatorPanes.hpp
namespace hmitree { struct Line; }   // 1.10.3 (Q1103) : HmiTreeData.hpp
class HmiPropsFilterBar;      // 1.10.3 : les filtres de l'onglet Proprietes (HmiEditor.cpp)
class HmiEditor final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;

    HmiEditor(std::string id, hmi::DocumentPtr doc, hmi::Id view, Apply apply,
              std::shared_ptr<const domain::Project> plc = nullptr);
    ~HmiEditor() override;

    [[nodiscard]] HmiCanvas&     canvas() noexcept { return *canvas_; }
    [[nodiscard]] HmiObjectList& objects() noexcept { return *objects_; }
    [[nodiscard]] HmiPalette&    palette() noexcept { return *palette_; }
    [[nodiscard]] HmiToolStrip&  tools() noexcept { return *tools_; }
    [[nodiscard]] HmiLayerList&  layers() noexcept { return *layers_; }
    [[nodiscard]] HmiToolStrip&  layerTools() noexcept { return *layerTools_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *props_; }
    // 1.10.3 (demande du client) : LES FILTRES DE L'ONGLET PROPRIETES, en haut, chacun
    // ON / OFF : fx (les cases pilotees par une expression), Reperes (celles qui
    // contiennent un repere $Nom$), Non vides (un texte ou une expression non vide, ou
    // une valeur differente de celle d'un objet neuf). Ils se cumulent : une case
    // s'affiche si elle repond a AU MOINS UN filtre allume ; tous eteints, tout
    // s'affiche. Chacun montre son nombre ; une section vide se cache ; l'etat est
    // garde dans les reglages ("hmi.inspecteur.filtres").
    enum class PropFilter : int { Fx = 0, Markers = 1, Filled = 2 };
    void setPropertyFilter(PropFilter f, bool on);
    [[nodiscard]] bool        propertyFilter(PropFilter f) const noexcept;
    [[nodiscard]] std::size_t propertyFilterCount(PropFilter f) const noexcept;
    [[nodiscard]] gfx::Rect   propertyFilterRect(PropFilter f) const;   // apres un dessin (scripts, essais)
    // L'inspecteur : l'onglet Proprietes et l'onglet Actions.
    [[nodiscard]] ui::TabControl&   inspector() noexcept { return *inspector_; }
    // 1.9 : les sous-onglets d'un symbole et son volet Alarmes (nuls : pas un symbole).
    [[nodiscard]] HmiSymbolTabs*       symbolTabs() noexcept { return symbolTabs_; }
    [[nodiscard]] HmiSymbolAlarmsPane* symbolAlarms() noexcept { return symbolAlarms_; }
    [[nodiscard]] HmiOperatorsPane*    symbolOperators() noexcept { return symbolOperators_; }   // 1.10 (S2)
    [[nodiscard]] HmiActionsPanel&  actions() noexcept { return *actions_; }
    // Lot 6 : l'onglet Contenu (lignes d'un tableau, plumes, etats, recette).
    [[nodiscard]] HmiContentPanel&  content() noexcept { return *content_; }
    // Aller a une action (Generer / Compiler) : l'objet choisi (kNoId : la vue),
    // l'onglet Actions ouvert, l'action `index` (0 = la premiere) choisie.
    void showAction(hmi::Id object, int index);
    // 1.10.3 (Q1103) : LE clic d'une ligne d'un objet deplie, pour les deux
    // explorateurs (l'arbre de l'application, l'explorateur d'objets de la vue) :
    // l'objet choisi, puis une action (`line.action`) : l'onglet Actions sur elle ;
    // une propriete (`line.property`) : sa case dans l'inspecteur.
    void showLine(hmi::Id object, const hmitree::Line& line);
    // La case d'une propriete (sa cle) dans l'onglet Proprietes de l'objet choisi :
    // l'expression d'une animation ("Valeur (expression)"), sinon son libelle.
    bool revealProperty(hmi::Id object, std::string_view key);
    [[nodiscard]] const ui::StatusBar& statusBar() const noexcept { return *statusBar_; }
    [[nodiscard]] hmi::Id        viewId() const noexcept { return viewId_; }

    // Rafraichir apres une modification venue d'ailleurs (Ctrl+Z, arbre...).
    void refresh();
    void setPlcProject(std::shared_ptr<const domain::Project> plc);
    void setObjectFilter(std::string text, std::optional<hmi::Kind> kind);

    // Les actions de la barre, pour les raccourcis de l'hote et les tests.
    enum Action : int {
        ActSelect = 1, ActCopy, ActPaste, ActDuplicate, ActDelete, ActGroup, ActUngroup,
        ActFront, ActBack, ActForward, ActBackward,
        ActAlignLeft, ActAlignCenterH, ActAlignRight, ActAlignTop, ActAlignCenterV, ActAlignBottom,
        ActDistributeH, ActDistributeV, ActRotateLeft, ActRotateRight, ActRotate45, ActRotate180,
        ActMirrorH, ActMirrorV, ActLock, ActUnlock, ActStyleCopy, ActStylePaste,
        ActGrid, ActSnap, ActGuideV, ActGuideH, ActZoomOut, ActZoomIn, ActZoomFit, ActShowHidden,
        ActLayerAdd, ActLayerRemove, ActLayerUp, ActLayerDown, ActLayerSelect, ActLayerMoveHere,
        ActUndo, ActRedo,
        ActMakeSymbol,   // lot 10 : Creer un symbole (la selection)
        ActMakeStyle,    // lot 12 : Creer un style nomme (l'objet choisi)
        ActSaveTemplate, // lot 20 : Enregistrer la vue comme modele
    };
    void run(int action);

    // Lot 10 : LES SYMBOLES. "Creer un symbole" demande a l'hote (un dialogue :
    // le nom, les parametres) ; sans hote, il cree tout de suite, avec les
    // parametres proposes. makeSymbol() fait la commande : la selection part
    // dans le symbole, une instance la remplace et devient la selection.
    void setSymbolAsker(std::function<void()> ask) { askSymbol_ = std::move(ask); }
    bool makeSymbol(const std::string& name, const std::string& params, std::string* why = nullptr);
    // Lot 12 : LES STYLES NOMMES. "Creer un style" demande le nom a l'hote ;
    // makeStyle() fait la commande (le style au projet, la selection le cite).
    void setStyleAsker(std::function<void()> ask) { askStyle_ = std::move(ask); }
    // Lot 20 : "Enregistrer comme modele" - le dialogue est a l'ecran.
    void setTemplateAsker(std::function<void()> ask) { askTemplate_ = std::move(ask); }
    // 1.10.2 (chantier D) : "Dupliquer..." (Ctrl+D, la barre d'outils) ; vide : la copie decalee.
    void setDuplicateAsker(std::function<void()> ask) { askDuplicate_ = std::move(ask); }
    // 1.11.3 : LE CARRE DE LEGENDE d'une case de l'inspecteur. Un clic ouvre la liste
    // des carres et leur sens (et « Creer X... » pour un nom inconnu) ; un choix
    // demande a l'hote le selecteur de valeur (setValueAsker) : la case (retrouvee
    // par sa categorie et son nom, la grille est souvent refaite), sa valeur, le
    // type attendu, la source choisie. Sans hote, le carre ne fait que se lire.
    using ValueRequest = valuekind::Request;
    void setValueAsker(std::function<void(const ValueRequest&)> ask);
    // La liste des carres d'une case (`at` : ou l'ouvrir). Faux : pas de telle case, ou pas d'hote.
    bool openLegendMenu(const std::string& category, const std::string& property, gfx::Point at);
    [[nodiscard]] ui::PopupMenu* legendMenu() const noexcept { return legendMenu_; }
    // Ecrire une valeur choisie dans la case : une commande (Ctrl+Z). Faux : la case n'est plus la.
    bool commitValue(const std::string& category, const std::string& property, const std::string& text, bool fx);
    bool makeStyle(const std::string& name, std::string* why = nullptr);
    // Ouvrir une autre vue (double-clic sur une instance : son symbole).
    const core::SignalPtr<hmi::Id> openView = core::Signal<hmi::Id>::create();

    // Annuler / retablir : la pile est celle de l'application. L'hote dit
    // comment y aller ; sans lui, les deux boutons restent grises.
    using History = std::function<void(bool redo)>;
    void setHistory(History history, std::function<bool(bool redo)> possible = {});

    const core::SignalPtr<const std::string&> status = core::Signal<const std::string&>::create();

    // ---- Lot API 8 : renommer partout (IHM) ----
    // Renommer un objet de la vue (la liste des objets : F2, double-clic ; la
    // case Nom des proprietes) : ce qui le cite (Vue.Objet dans les
    // expressions, les textes, les scripts, les alarmes... ; les actions GIF et
    // "lier un tableau" ; les essais) suit, en une commande (un seul Ctrl+Z).
    // La barre d'etat dit combien d'endroits ont suivi.
    bool renameObject(hmi::Id id, const std::string& name, std::string* why = nullptr);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    void warn(const std::string& why);
    void syncFromCanvas();
    bool commitProp(const std::string& key, const std::string& value, bool expr);
    bool commitMeta(const std::string& field, const std::string& value);
    bool commitView(const std::string& field, const std::string& value);

    hmi::DocumentPtr                        doc_;
    hmi::Id                                 viewId_;
    Apply                                   apply_;
    std::shared_ptr<const domain::Project>  plc_;

    HmiToolStrip*     tools_{nullptr};
    ui::Splitter*     split_{nullptr};
    HmiCanvas*        canvas_{nullptr};
    ui::PopupMenu*    context_{nullptr};   // 1.10.2 (chantier D) : le clic droit sur la vue
    HmiObjectList*    objects_{nullptr};
    ui::InputText*    search_{nullptr};
    ui::DropDown*     typeFilter_{nullptr};
    HmiLayerList*     layers_{nullptr};
    HmiToolStrip*     layerTools_{nullptr};
    ui::PropertyGrid* props_{nullptr};
    HmiPropsFilterBar* propFilters_{nullptr};   // 1.10.3 : fx, Reperes, Non vides (au-dessus de props_)
    void applyPropertyFilters(const hmi::View& v, std::vector<ui::PropertyGrid::Category>& cats);
    ui::TabControl*   inspector_{nullptr};
    HmiActionsPanel*  actions_{nullptr};
    HmiContentPanel*  content_{nullptr};
    // 1.10.4 (K3) : l'onglet Contenu cache (un objet qui ne peut pas avoir de contenu) :
    // sa page est gardee ici, et remise quand un objet qui en a est choisi.
    ui::WidgetPtr     contentPage_;
    ui::InputText*    paletteSearch_{nullptr};
    HmiPalette*       palette_{nullptr};
    ui::StatusBar*    statusBar_{nullptr};
    HmiSymbolTabs*       symbolTabs_{nullptr};     // 1.9 : un symbole - Dessin | Alarmes | Instances
    HmiSymbolAlarmsPane* symbolAlarms_{nullptr};
    HmiOperatorsPane*    symbolOperators_{nullptr};   // 1.10 (S2) : le sous-onglet Operateurs
    core::ConnectionScope links_;
    bool              syncing_{false};
    History           history_;
    std::function<bool(bool)> historyPossible_;
    std::function<void()>     askSymbol_;
    std::function<void()>     askStyle_;         // lot 12
    std::function<void()>     askTemplate_;      // lot 20
    std::function<void()>     askDuplicate_;     // 1.10.2 (chantier D)
    std::function<void(const ValueRequest&)> askValue_;   // 1.11.3 : le selecteur de valeur
    ui::PopupMenu*            legendMenu_{nullptr};       // 1.11.3 : la liste des carres
    ValueRequest              legendRequest_;             // ... la case pour laquelle elle est ouverte
    std::vector<std::string>  legendUnknown_;             // ... les noms a creer qu'elle propose
};

} // namespace app
