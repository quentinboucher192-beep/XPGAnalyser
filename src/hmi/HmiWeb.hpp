// =============================================================================
//  hmi/HmiWeb.hpp - l'IHM dans un navigateur : le serveur web integre (lot 14)
// -----------------------------------------------------------------------------
//  UNE TABLETTE, UN AUTRE PC, UN TELEPHONE : http://<le poste>:8080 montre la
//  vue en marche - une image rafraichie (JPEG), telle que l'ecran la dessine.
//  Si on le permet, un clic sur l'image est un clic sur la vue.
//
//  LES COMPTES SONT CEUX DE L'IHM : on se connecte avec un utilisateur du
//  projet (son mot de passe, son empreinte salee) ; LECTURE SEULE PAR DEFAUT :
//  les clics ne passent que si l'acces web le permet ET que l'utilisateur a
//  la permission Piloter. Une session sans activite se ferme.
//
//  Le serveur tourne sur son fil ; l'ecran lui donne les images (setFrame) et
//  prend les clics (takeClicks). Rien d'autre ne sort : pas de fichier, pas de
//  projet - seulement l'image de la vue.
//
//    GET  /              la page (la connexion d'abord, si elle est demandee)
//    POST /connexion     login, mot de passe -> un cookie de session
//    GET  /deconnexion
//    GET  /vue.jpg       la derniere image de la vue
//    GET  /etat          l'etat en JSON (la vue, sa taille, le numero d'image,
//                        l'utilisateur, la commande permise)
//    POST /clic          x, y en pixels de l'image (si la commande est permise)
// =============================================================================
#pragma once

#include "HmiMedia.hpp"
#include "HmiModel.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hmi::web {

struct Frame {
    std::shared_ptr<const Bytes> jpeg;
    int                          width{0}, height{0};
    std::uint64_t                serial{0};
    std::string                  view;        // la vue montree
    bool                         running{false};
};

struct Click {
    double      x{0}, y{0};                    // en pixels de l'image
    std::string user;                          // qui (le journal)
    std::string peer;                          // d'ou
};

// Un navigateur connecte.
struct Client {
    std::string user;          // '' : sans connexion
    std::string peer;          // "192.168.1.20"
    std::string since;         // "17:42:05"
    double      lastSeen{0};   // secondes (horloge du poste)
    bool        control{false};
    std::size_t images{0}, clicks{0};
};

// La verification d'un utilisateur : vrai s'il entre ; `control` : il peut
// piloter (la permission Piloter) ; sinon `why`.
using Authenticate = std::function<bool(const std::string& login, const std::string& password, bool* control, std::string* why)>;

class Server {
public:
    Server();
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(const WebAccess&, std::string projectName, Authenticate, std::string* why = nullptr);
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] int  port() const noexcept { return port_; }
    // Les reglages qui ne demandent pas de redemarrer (rafraichir, commande...).
    void configure(const WebAccess&);

    // L'ecran : la derniere image ; un navigateur en a-t-il demande une recemment ?
    void setFrame(Frame);
    [[nodiscard]] bool wantsFrame() const;
    [[nodiscard]] std::vector<Click> takeClicks();
    // Le journal : connexion, refus, deconnexion, session expiree.
    [[nodiscard]] std::vector<std::string> takeEvents();
    [[nodiscard]] std::vector<Client> clients() const;       // ceux vus ces 30 dernieres secondes
    [[nodiscard]] int                 clientCount() const;
    void disconnectAll();

    // La page (pour les essais).
    [[nodiscard]] static std::string page(const std::string& projectName, bool loginForm, const std::string& message, int refreshMs);

private:
    struct Session {
        std::string token, user, peer, since;
        double      lastSeen{0};
        bool        control{false};
        std::size_t images{0}, clicks{0};
    };
    void run();
    void serve(void* socket, const std::string& peer);
    Session* sessionOf(const std::string& cookie, double now);
    void event(std::string text);

    std::atomic<bool>           running_{false};
    std::atomic<bool>           stop_{false};
    std::thread                 thread_;
    int                         port_{0};
    struct Listener;
    std::unique_ptr<Listener>   listener_;
    mutable std::mutex          mutex_;
    WebAccess                   settings_;
    std::string                 project_;
    Authenticate                auth_;
    Frame                       frame_;
    double                      lastRequest_{-1e9};
    std::vector<Session>        sessions_;
    std::vector<Click>          clicks_;
    std::vector<std::string>    events_;
    std::map<std::string, std::pair<int, double>> failures_;   // les echecs par adresse, et depuis quand (un robot qui essaie)
};

// L'heure du poste, en secondes (horloge monotone).
[[nodiscard]] double clockSeconds();

} // namespace hmi::web
