// =============================================================================
//  app/hmi/HmiWebHost.hpp - l'acces par navigateur appartient a l'application
//                           (lot 14)
// -----------------------------------------------------------------------------
//  Le serveur web (hmi::web::Server) suit Configuration > Acces web. App lui
//  donne, au rythme demande (Rafraichir), l'image de la vue en marche - la
//  simulation IHM ou le poste d'exploitation, telle que l'ecran l'a dessinee
//  (JPEG) - quand un navigateur la regarde ; et fait des clics des
//  navigateurs (commande permise) des clics sur cette vue.
//
//  LES COMPTES : une copie des utilisateurs du projet (login, empreinte,
//  groupe, permission Piloter), refaite quand ils changent - le fil du
//  serveur ne touche jamais au projet.
//
//  1.32.0 : LE PROJET SEUL N'OUVRE PAS DE SERVEUR. Il faut aussi que
//  l'utilisateur l'autorise sur CE poste (un reglage de l'application, faux
//  par defaut : un projet recu d'ailleurs n'ecoute pas le reseau a son
//  ouverture). Sinon l'etat le dit, avec le reglage a cocher.
// =============================================================================
#pragma once

#include "../../hmi/HmiWeb.hpp"
#include "../../platform/Renderer.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace app {

class WebHost {
public:
    WebHost() = default;
    ~WebHost();
    WebHost(const WebHost&) = delete;
    WebHost& operator=(const WebHost&) = delete;

    // 1.32.0 : le nom du reglage du poste qui permet l'acces web (Reglages).
    static constexpr std::string_view kSettingLabel = hmi::web::kThisPcSetting;
    // 1.32.0 : l'acces web est-il permis sur ce poste (le reglage de l'utilisateur) ? Faux par defaut.
    void setAllowedOnThisPc(bool on) noexcept { allowedHere_ = on; }
    [[nodiscard]] bool allowedOnThisPc() const noexcept { return allowedHere_; }

    // Chaque image : le serveur suit la configuration ; les comptes aussi.
    void tick(const hmi::Project* project);
    void shutdown();

    [[nodiscard]] bool running() const noexcept { return server_ && server_->running(); }
    [[nodiscard]] int  port() const noexcept { return server_ ? server_->port() : 0; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] int  clientCount() const { return server_ ? server_->clientCount() : 0; }
    [[nodiscard]] std::vector<hmi::web::Client> clients() const { return server_ ? server_->clients() : std::vector<hmi::web::Client>{}; }
    void disconnectAll() { if (server_) server_->disconnectAll(); }
    // Les adresses ou le trouver : http://127.0.0.1:8080 (et celle du poste sur le reseau).
    [[nodiscard]] std::vector<std::string> addresses() const;

    // Faut-il une image (un navigateur regarde, et c'est l'heure) ?
    [[nodiscard]] bool due(double time) const;
    // L'image de la vue : ce rectangle de ce qui vient d'etre dessine. `running`
    // faux : l'IHM n'est pas en marche (la page le dit).
    void publish(gfx::IRenderer& renderer, gfx::Rect area, const std::string& view, bool running, double time);
    // Les clics des navigateurs, en pixels de l'ecran (le rectangle de la derniere image).
    struct ScreenClick {
        gfx::Point  at;
        std::string user, peer;
    };
    [[nodiscard]] std::vector<ScreenClick> takeClicks();
    [[nodiscard]] std::vector<std::string> takeEvents();
    [[nodiscard]] std::uint64_t frames() const noexcept { return serial_; }

private:
    struct Account {
        std::string login, salt, hash, protection;
        bool        enabled{true}, pilot{false};
    };
    bool check(const std::string& login, const std::string& password, bool* control, std::string* why);

    std::unique_ptr<hmi::web::Server> server_;
    hmi::WebAccess                    settings_;
    std::string                       project_;
    bool                              started_{false};
    bool                              allowedHere_{false};   // 1.32.0 : le reglage du poste
    bool                              refusedSaid_{false};   // 1.32.0 : le refus, dit une fois au journal
    std::string                       error_;
    std::mutex                        accountsMutex_;
    std::vector<Account>              accounts_;
    bool                              securityOn_{false};
    hmi::Security                     security_;          // la derniere copie (pour savoir quand refaire)
    gfx::Rect                         lastArea_{};
    double                            lastFrame_{-1e9};
    std::uint64_t                     serial_{0};
    std::vector<std::string>          events_;
};

} // namespace app
