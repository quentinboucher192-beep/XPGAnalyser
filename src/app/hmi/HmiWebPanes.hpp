// =============================================================================
//  app/hmi/HmiWebPanes.hpp - Configuration > Acces web (lot 14)
// -----------------------------------------------------------------------------
//  L'IHM DANS UN NAVIGATEUR. A droite, les reglages : le port, ce poste seul
//  ou tout le reseau, la connexion (les comptes de l'IHM), la commande (les
//  clics passent - sinon lecture seule), le rafraichissement, la qualite de
//  l'image, le nombre de navigateurs, la duree d'une session.
//
//  Au milieu : les adresses ou le trouver, puis les navigateurs connectes
//  (qui, d'ou, depuis quand, en commande ou non, images et clics) -
//  rafraichis chaque seconde. DECONNECTER TOUT ferme leurs sessions.
//
//  L'image est celle de la vue en marche : la simulation IHM ou le poste
//  d'exploitation. Sans IHM en marche, la page le dit.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui { class StatusBar; }

namespace app {

class WebHost;

class HmiWebPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiWebPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // Un reglage : "actif", "port", "reseau", "connexion", "commande",
    // "rafraichir", "qualite", "clients", "session".
    bool setSetting(const std::string& key, const std::string& value, std::string* why = nullptr);

    struct Hosts {
        std::function<WebHost*()> web;
    };
    void setHosts(Hosts h);

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     clients() noexcept { return *clients_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    void refreshLive();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    void say(std::string text, bool error = false);

    hmi::DocumentPtr       doc_;
    Apply                  apply_;
    Hosts                  hosts_;
    HmiToolStrip*          tools_{nullptr};
    ui::TabControl*        tabs_{nullptr};
    ui::TableView*         clients_{nullptr};
    ui::PropertyGrid*      grid_{nullptr};
    ui::StatusBar*         status_{nullptr};
    std::shared_ptr<ui::ITableModel> clientsModel_;
    std::string            shownState_;
    double                 lastLive_{-10};
    std::string            message_;
    core::ConnectionScope  links_;
};

} // namespace app
