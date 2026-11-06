// =============================================================================
//  deploy/Agent.hpp - 1.28.0 : le coeur de XPG-Deployment-Agent
// -----------------------------------------------------------------------------
//  L'agent du PC de maintenance, sans interface (src/agent/AgentMain.cpp le lance) :
//    - start() : son identite TLS (creee au premier lancement), ses PC appaires,
//      puis ses ports - TCP (le protocole) et UDP (la decouverte) ; un port deja
//      pris : start() le dit et l'agent n'ecoute sur aucun ;
//    - un fil par connexion (8 au plus), un fil UDP, un fil qui ecrit etat.json
//      toutes les 5 s ; stop() arrete tout proprement (chaque attente se
//      termine en 100 a 200 ms) ;
//    - les commandes : IDENTIFY, PAIR_START, PAIR_FINISH (un pair inconnu ne
//      peut rien d'autre), STATUS, GET_LOGS, UNPAIR ; chaque operation fait une
//      ligne du journal (deploy/Journal).
//  L'agent ne lance AUCUNE commande recue du reseau et n'utilise jamais Internet.
//
//  Son dossier : identite.cer / identite.cle (protegee, la machine),
//  appaires.json, appairage.code (protege), etat.json, journal/AAAA-MM-JJ.log.
// =============================================================================
#pragma once

#include "Protocol.hpp"
#include "TlsIdentity.hpp"

#include "../core/Json.hpp"

#include <memory>
#include <string>

namespace deploy {

inline constexpr const char* kAgentVersion = "1.0.0";

struct AgentConfig {
    std::string dir;                                   // son dossier (cree s'il manque)
    std::string bindAddress{"0.0.0.0"};                // ou ecouter (toutes les interfaces)
    int port{protocol::kAgentPort};                    // TCP ; 0 : choisi par le systeme (essais)
    int discoveryPort{protocol::kDiscoveryPort};       // UDP ; 0 : choisi par le systeme ; -1 : pas de decouverte
    std::string machineName;                           // "" : le nom du PC
    std::string version{kAgentVersion};
    int maxConnections{8};
    int handshakeTimeoutMs{10000};
    int idleTimeoutMs{120000};                         // une connexion muette si longtemps : fermee
    int ioTimeoutMs{15000};                            // le reste d'une trame, une reponse
    int stateIntervalMs{5000};                         // etat.json
};

class Agent {
public:
    explicit Agent(AgentConfig config);
    ~Agent();   // stop()
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    // Demarrer (non bloquant : les fils tournent ensuite). Faux : `why` en francais
    // (<< le port TCP 47861 est deja pris ... >>) et rien n'ecoute.
    bool start(std::string* why);
    // Arreter : les fils finissent, etat.json dit << arrete >>. Sans danger deux fois.
    void stop();
    [[nodiscard]] bool running() const;

    // Les ports reellement pris (utiles avec 0), l'identite, le nom.
    [[nodiscard]] int port() const;
    [[nodiscard]] int discoveryPort() const;
    [[nodiscard]] const TlsIdentity& identity() const;
    [[nodiscard]] std::string machineName() const;
    [[nodiscard]] std::string machineId() const;
    [[nodiscard]] int activeConnections() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Le dossier par defaut : C:\ProgramData\XPG\Agent sous Windows, $XDG_DATA_HOME/xpg-agent
// ou ~/.local/share/xpg-agent ailleurs.
[[nodiscard]] std::string defaultAgentDir();
// Le systeme de ce PC ("Windows 10 Pro (build 19045)", "Ubuntu 24.04 LTS").
[[nodiscard]] std::string systemDescription();
// <dossier>/etat.json, lu (sans reseau). Faux : absent ou illisible (`why`).
bool readAgentState(const std::string& dir, core::json::Value& out, std::string* why);

} // namespace deploy
