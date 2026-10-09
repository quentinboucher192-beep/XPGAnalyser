// =============================================================================
//  app/hmi/HmiActionsPanel.hpp — l'onglet Actions de l'inspecteur
// -----------------------------------------------------------------------------
//  COMME L'ONGLET "EVENEMENTS" DES OUTILS DU METIER (WinCC Unified, Optix) : a
//  cote des proprietes, la liste des actions de l'objet choisi - ou de la vue
//  quand rien n'est choisi -, et dessous les reglages de l'action choisie :
//
//    Declencheur   clic, double clic, front montant / descendant, appui long,
//                  changement de valeur, ouverture / fermeture de la vue, timer
//    Operation     basculer, mettre a 1 / 0, incrementer, decrementer, affecter,
//                  naviguer, ouvrir / fermer une popup, executer un script,
//                  script general, journaliser (+ lot 4 : alarme, recette,
//                  utilisateur)
//    Transition    instantanee, fondu, glissement, zoom, rotation, personnalisee ;
//                  duree, direction, courbe
//
//  La grille ne montre que ce qui sert : une periode pour un timer, une vue
//  pour une navigation, un pas pour un increment. Tout passe par des commandes
//  de vue : Ctrl+Z reprend une action ajoutee, reglee ou retiree.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace domain { class Project; }

namespace app {

class HmiActionsPanel final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    // 1.11.23 : LES RACCOURCIS D'UNE VUE (Scope::Shortcuts) - le meme volet, sur les actions
    // de la vue dont le declencheur est une touche (Touche enfoncee, relachee, maintenue,
    // repetee) : la liste n'a qu'elles, l'inspecteur regle la touche et son declencheur.
    // Scope::Actions (le defaut) : les autres actions (celles de la vue ou de l'objet choisi).
    // Les index (selectedIndex, selectIndex, set, remove...) sont ceux de la liste entiere.
    enum class Scope : std::uint8_t { Actions, Shortcuts };
    HmiActionsPanel(std::string id, hmi::DocumentPtr doc, hmi::Id view, Apply apply, Scope scope = Scope::Actions);
    [[nodiscard]] Scope scope() const noexcept { return scope_; }
    // Les index (dans la liste entiere) des lignes montrees, dans l'ordre.
    [[nodiscard]] const std::vector<int>& shownIndexes() const noexcept { return rows_; }

    // L'objet dont on montre les actions ; kNoId : celles de la vue.
    void setOwner(hmi::Id object);
    [[nodiscard]] hmi::Id owner() const noexcept { return owner_; }
    void refresh();
    [[nodiscard]] int  selectedIndex() const;
    void selectIndex(int index);
    [[nodiscard]] const std::vector<hmi::Action>* actions() const;

    // Rendent l'indice de l'action (ou -1) ; faux si rien n'a change.
    int  add(hmi::Action);
    bool set(int index, const hmi::Action&);
    bool remove(int index);
    bool move(int index, int delta);

    // 1.11.9 : LES PETITES FENETRES DES ACTIONS - le choix de l'operation en arbre, le script
    // (Executer un script), la formule de Maths. L'hote les ouvre (le gestionnaire des menus) et
    // rend la reponse ; sans hote, pas de bouton « … » (la liste et la case, comme avant).
    using DialogHost = std::function<void(menu::MenuPtr, std::function<void(const menu::DialogResult&)>)>;
    void setDialogHost(DialogHost host, std::function<std::shared_ptr<const domain::Project>()> plc = {});
    // 1.11.10 : l'hote des petites fenetres (l'editeur s'en sert pour les redefinitions d'une instance).
    [[nodiscard]] const DialogHost& dialogHost() const noexcept { return host_; }
    // Ouvre la fenetre d'une ligne de l'action choisie : "Op\xC3\xA9ration", "Code ST",
    // "Formule" (Maths) ; faux : pas de fenetre (pas d'hote, rien de choisi, pas cette ligne).
    bool openEditor(const std::string& name);

    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&    table() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid& grid() noexcept { return *grid_; }
    // Le nombre d'actions a change (la pastille de l'onglet).
    const core::SignalPtr<std::size_t> countChanged = core::Signal<std::size_t>::create();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildGrid();
    [[nodiscard]] std::string ownerName() const;
    [[nodiscard]] bool shows(const hmi::Action&) const;   // 1.11.23 : la ligne est de ce volet
    [[nodiscard]] int  rowOf(int index) const;            // la ligne d'un index (-1 : pas montree)
    Scope            scope_{Scope::Actions};
    std::vector<int> rows_;                               // ligne -> index dans la liste entiere
    hmi::DocumentPtr doc_;
    hmi::Id          view_;
    hmi::Id          owner_{hmi::kNoId};
    Apply            apply_;
    HmiToolStrip*    tools_{nullptr};
    ui::TableView*   table_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    int              selected_{-1};
    std::size_t      lastCount_{0};
    DialogHost       host_;                                                  // 1.11.9
    std::function<std::shared_ptr<const domain::Project>()> plc_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);           // une reponse apres la fermeture : ignoree
    core::ConnectionScope links_;
};

} // namespace app
