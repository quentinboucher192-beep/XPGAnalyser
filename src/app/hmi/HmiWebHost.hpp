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
// =============================================================================
#pragma once

#include "../../hmi/HmiWeb.hpp"
#include "../../platform/Renderer.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace app {

class WebHost {
public:
    WebHost() = default;
    ~WebHost();
    WebHost(const WebHost&) = delete;
    WebHost& operator=(const WebHost&) = delete;

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
