// =============================================================================
//  app/hmi/HmiContentPanel.hpp - l'onglet Contenu de l'inspecteur (lot 6)
// -----------------------------------------------------------------------------
//  CE QU'UN OBJET CONTIENT, A COTE DE SES PROPRIETES. Une liste en haut, les
//  reglages de l'element choisi en dessous, comme l'onglet Actions :
//
//    Tableau           les LIGNES ; chaque case : un texte, un texte a trous
//                      ({Niveau:0.0} bar) ou =expression, avec l'aide a la
//                      saisie ; les colonnes : en-tete et largeur relative.
//                      + ligne, + colonne, retirer, monter, descendre.
//    Courbe            les PLUMES : nom (legende), expression, couleur.
//    Image animee      les ETATS : condition, images qui defilent, periode.
//    Gestion de        la recette montree, les boutons presents, et les jeux
//    recettes          (en lecture : ils se modifient dans Recettes, ou en
//                      marche avec les boutons de l'objet).
//    Plan a zones      (lot 12) les ZONES : nom, points (en % du plan), groupe
//                      d'alarmes, vue ouverte d'un clic, couleur.
//    Conteneur a       (lot 12) les PAGES : leur libelle ; retirer ou deplacer
//    onglets           une page emmene ses objets.
//    Barre de          (lot 12) les VUES : la vue, son libelle.
//    navigation
//    Groupe, conteneur (1.10.4) les OBJETS qu'il tient, en lecture.
//    cadre, panneaux
//  Un objet qui ne peut rien contenir (une vanne, un bouton) : pas d'onglet.
//
//  Tout passe par des commandes de vue : Ctrl+Z reprend une ligne ajoutee, une
//  case modifiee, une plume retiree.
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

class HmiContentPanel final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    // Lot 12 : les zones d'un plan, les pages d'un conteneur a onglets, les vues
    // d'une barre de navigation.
    // 1.10.4 (K3) : Members - les objets d'un groupe, d'un conteneur, d'un cadre,
    // d'un panneau (en lecture : on les regle en les choisissant). None : l'objet ne
    // peut pas avoir de contenu, l'editeur cache l'onglet.
    enum class Mode : std::uint8_t { None, Table, Trend, AnimatedImage, RecipeManager, Zones, Tabs, NavItems, Members };

    HmiContentPanel(std::string id, hmi::DocumentPtr doc, hmi::Id view, Apply apply);

    // L'objet montre ; kNoId (ou plusieurs objets choisis) : rien.
    void setObject(hmi::Id object);
    [[nodiscard]] hmi::Id object() const noexcept { return object_; }
    [[nodiscard]] Mode    mode() const;
    void refresh();
    [[nodiscard]] int  selectedIndex() const;
    void selectIndex(int index);
    [[nodiscard]] std::size_t count() const;          // lignes, plumes, etats, jeux

    // Les modifications (boutons de la barre, tests). Faux : rien n'a change.
    bool addItem();
    bool removeItem(int index);
    bool moveItem(int index, int delta);
    bool addColumn();                                  // tableau
    bool removeColumn(int column);                     // tableau ; -1 : la derniere
    // Un reglage de l'element `index` : "cell:2" (tableau, colonne 2 a partir
    // de 0), "header:1", "width:1", "name", "expression", "color",
    // "condition", "images", "addImage", "period" ; gestionnaire : "recipe",
    // "button:Ajouter" (TRUE/FALSE).
    bool setField(int index, const std::string& field, const std::string& value);

    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&    table() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid& grid() noexcept { return *grid_; }
    // Le nombre d'elements a change (la pastille de l'onglet).
    const core::SignalPtr<std::size_t> countChanged = core::Signal<std::size_t>::create();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    [[nodiscard]] const hmi::Object* current() const;
    bool change(const std::string& label, const std::function<bool(hmi::Object&)>& fn);
    // Lot 12 : une modification qui touche aussi les enfants (les pages d'onglets).
    bool changeInView(const std::string& label, const std::function<bool(hmi::View&, hmi::Object&)>& fn);
    void rebuildGrid();
    hmi::DocumentPtr doc_;
    hmi::Id          view_;
    hmi::Id          object_{hmi::kNoId};
    Apply            apply_;
    HmiToolStrip*    tools_{nullptr};
    ui::TableView*   table_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    int              selected_{-1};
    int              lastColumn_{-1};      // la colonne de la derniere case editee (retirer la colonne)
    std::size_t      lastCount_{static_cast<std::size_t>(-1)};
    core::ConnectionScope links_;
};

} // namespace app
