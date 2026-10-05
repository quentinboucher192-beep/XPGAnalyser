// =============================================================================
//  app/hmi/HmiSymbolPopupsPane.hpp - 1.11.10 : les popups propres a un symbole
// -----------------------------------------------------------------------------
//  LE SOUS-ONGLET POPUPS DE L'EDITEUR D'UN SYMBOLE. Une popup du symbole est une
//  vue de role "popup" que le symbole porte (View::ownerSymbol) : rangee sous le
//  symbole (et plus dans IHM > Popups), elle CONNAIT D'OFFICE LES PARAMETRES DU
//  SYMBOLE - ouverte depuis une instance, Vanne y vaut V[3] sans rien passer - et
//  appelle les fonctions de l'instance (Ouvrir('popup')). Ses parametres a elle se
//  passent comme ceux d'une popup ordinaire.
//
//    +-------------------- outils ---------------------+
//    | Popups de S_Vanne (tableau) | Ce qu'elle connait |
//    +-------------------- etat -----------------------+
//
//  Nouvelle popup (un nom libre, 420 x 260), Ouvrir le dessin (un onglet, comme
//  une vue), Supprimer : chaque geste est une commande (un Ctrl+Z).
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiSymbolPopupsPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiSymbolPopupsPane(std::string id, hmi::DocumentPtr doc, hmi::Id symbol, Apply apply);

    void refresh();
    // Les popups du symbole, dans l'ordre du projet.
    [[nodiscard]] std::vector<hmi::Id> popups() const;
    [[nodiscard]] hmi::Id selectedPopup() const;
    void selectPopup(hmi::Id);
    // Nouvelle popup du symbole ("Pop_<symbole>" si `name` est vide) ; kNoId : refusee.
    hmi::Id addPopup(std::string name = {}, std::string* why = nullptr);
    bool    deletePopup(hmi::Id);
    // Ouvrir le dessin d'une popup (l'hote ouvre son onglet).
    std::function<void(hmi::Id)> openView;

    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&    table() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid& info() noexcept { return *info_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildInfo();
    void say(std::string text, bool warning = false);

    hmi::DocumentPtr  doc_;
    hmi::Id           symbol_;
    Apply             apply_;
    HmiToolStrip*     tools_{nullptr};
    ui::Splitter*     split_{nullptr};
    ui::TableView*    table_{nullptr};
    ui::PropertyGrid* info_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<hmi::Id> order_;
    int               selectedRow_{-1};
    std::string       message_;
    core::ConnectionScope links_;
};

} // namespace app
