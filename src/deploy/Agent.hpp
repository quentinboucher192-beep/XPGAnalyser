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
//    - 1.32.0, les places : 2 connexions a la fois au plus par adresse ; les 2
//      dernieres places sont gardees aux PC appaires (un pair inconnu est
//      refuse, apres la poignee de main, s'il reste moins de 3 places) ; un pair
//      inconnu muet 15 s est ferme (120 s pour un PC appaire) ; PAIR_START : un
//      par seconde au plus par adresse ; un PAIR_FINISH manque ferme la connexion ;
//    - les commandes : IDENTIFY, PAIR_START, PAIR_FINISH (un pair inconnu ne
//      peut rien d'autre), STATUS, GET_LOGS, UNPAIR ; chaque operation fait une
//      ligne du journal (deploy/Journal) ;
//    - 1.29.0, la version 2 du protocole (un PC appaire seulement) : PREPARE,
//      SEND_CHUNK, VERIFY_PACKAGE, INSTALL, STOP, START, ROLLBACK, LIST_APPS,
//      GET_HISTORY - le travail est dans deploy/Deployer.
//  L'agent ne lance AUCUNE commande recue du reseau et n'utilise jamais Internet.
//
//  Son dossier : identite.cer / identite.cle (protegee : l'utilisateur ; avant
//  1.32.0 la machine), appaires.json, appairage.code (protege), etat.json,
//  journal/AAAA-MM-JJ.log ; 1.29.0 : incoming/, Applications/, historique.jsonl,
//  poste.json, arret.json, reprise-poste.txt (deploy/Install.hpp).
//
//  1.32.0 : LE DOSSIER EST CELUI DE L'OPERATEUR. Sous Windows,
//  %LOCALAPPDATA%\XPG\Agent (avant : C:\ProgramData\XPG\Agent, ou tout compte du
//  PC pouvait creer des fichiers et relire les cles protegees pour la machine).
//  A chaque demarrage, son DACL est refait protege (sans heritage) : SYSTEM,
//  Administrateurs et l'utilisateur, controle total ; personne d'autre.
//  L'ancien dossier est repris une fois (migrateLegacyAgentDir) s'il appartient
//  a ce compte, aux Administrateurs ou au systeme.
// =============================================================================
#pragma once

#include "Protocol.hpp"
#include "TlsIdentity.hpp"

#include "../core/Json.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace deploy {

// 1.1.0 (IHM Studio 1.29.0) : la version 2 du protocole (l'envoi, l'installation, le poste).
inline constexpr const char* kAgentVersion = "1.2.0";   // 1.32.0 : dossier dans le profil, acces reserve, connexions limitees

struct AgentConfig {
    std::string dir;                                   // son dossier (cree s'il manque)
    std::string bindAddress{"0.0.0.0"};                // ou ecouter (toutes les interfaces)
    int port{protocol::kAgentPort};                    // TCP ; 0 : choisi par le systeme (essais)
    int discoveryPort{protocol::kDiscoveryPort};       // UDP ; 0 : choisi par le systeme ; -1 : pas de decouverte
    std::string machineName;                           // "" : le nom du PC
    std::string version{kAgentVersion};
    int maxConnections{8};
    int maxPerAddress{2};                              // 1.32.0 : connexions a la fois d'une meme adresse
    int pairedReserve{2};                              // 1.32.0 : places gardees aux PC appaires
    int handshakeTimeoutMs{10000};
    int idleTimeoutMs{120000};                         // une connexion muette si longtemps : fermee
    int unpairedIdleTimeoutMs{15000};                  // 1.32.0 : la meme, pour un pair inconnu
    int pairStartIntervalMs{1000};                     // 1.32.0 : un PAIR_START par adresse dans ce temps (0 : sans limite)
    int ioTimeoutMs{15000};                            // le reste d'une trame, une reponse
    int stateIntervalMs{5000};                         // etat.json
    // 1.29.0 : l'installation et le poste (deploy/Deployer).
    std::string runtimeProgram;                        // le programme du poste ; vide : IHM-Studio(.exe) a cote de l'agent
    int keepVersions{3};                               // les versions gardees par application
    int startCheckMs{8000};                            // le poste doit tenir ce temps apres START
    int stopGraceMs{30000};                            // STOP attend le delai donne au poste, plus ceci
    std::int64_t simulatedFreeBytes{-1};               // ESSAIS SEULEMENT : la place libre simulee (-1 : la vraie)
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
    // 1.29.0 : le programme du poste que l'agent lance (IHM-Studio.exe a cote de lui).
    [[nodiscard]] std::string runtimeProgram() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Le dossier par defaut : %LOCALAPPDATA%\XPG\Agent sous Windows (1.32.0 ; avant : C:\ProgramData\XPG\Agent),
// $XDG_DATA_HOME/xpg-agent ou ~/.local/share/xpg-agent ailleurs.
[[nodiscard]] std::string defaultAgentDir();
// 1.32.0 : l'ancien dossier par defaut (C:\ProgramData\XPG\Agent) ; "" hors Windows.
[[nodiscard]] std::string legacyAgentDir();
// 1.32.0 : reprendre l'ancien dossier, une fois : si `newDir` n'existe pas et que l'ancien existe et appartient
// a ce compte, aux Administrateurs ou au systeme, il est deplace (meme volume ; sinon copie, et une note laissee
// dans l'ancien), ferme (restrictAgentDir), ce qui appartient a un autre compte (ou un lien) y est efface, et
// les secrets proteges pour la machine sont reproteges pour l'utilisateur. A un autre proprietaire : rien n'est
// repris. Rend les phrases a dire a l'operateur (vide : rien a dire). Ne fait rien hors Windows.
[[nodiscard]] std::vector<std::string> migrateLegacyAgentDir(const std::string& newDir);
// 1.32.0 : fermer le dossier de l'agent. Windows : DACL protege (sans heritage du parent) - SYSTEM,
// Administrateurs et l'utilisateur, controle total ; rien n'est refait s'il est deja ainsi. Ailleurs : 0700.
// Faux : `why` (l'agent demarre quand meme, et le journal le dit).
bool restrictAgentDir(const std::string& dir, std::string* why);
// Le systeme de ce PC ("Windows 10 Pro (build 19045)", "Ubuntu 24.04 LTS").
[[nodiscard]] std::string systemDescription();
// <dossier>/etat.json, lu (sans reseau). Faux : absent ou illisible (`why`).
bool readAgentState(const std::string& dir, core::json::Value& out, std::string* why);

} // namespace deploy
