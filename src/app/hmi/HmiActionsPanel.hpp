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
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiActionsPanel final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiActionsPanel(std::string id, hmi::DocumentPtr doc, hmi::Id view, Apply apply);

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
    core::ConnectionScope links_;
};

} // namespace app
