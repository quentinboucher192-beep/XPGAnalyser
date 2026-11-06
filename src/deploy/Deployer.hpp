// =============================================================================
//  deploy/Deployer.hpp - 1.29.0 : l'envoi, l'installation et le poste, cote agent
// -----------------------------------------------------------------------------
//  deploy/Agent lui passe les commandes de la version 2 du protocole (un PC
//  appaire seulement) : PREPARE, SEND_CHUNK, VERIFY_PACKAGE, INSTALL, STOP,
//  START, ROLLBACK, LIST_APPS, GET_HISTORY. Les fichiers : deploy/Install.hpp.
//
//  LE POSTE. L'agent ne lance QUE le programme du poste - IHM-Studio.exe pose a
//  cote de lui (IHM-Studio sous Linux), ou DeployerConfig::runtimeProgram (les
//  essais) - avec --ihm "<dossier de la version active>" ; jamais une commande
//  recue du reseau. Il le lance avec deux variables d'environnement :
//      XPG_ARRET_FICHIER=<dossier de l'agent>/arret.json        (absent au lancement)
//      XPG_REPRISE_FICHIER=<dossier de l'agent>/reprise-poste.txt
//  Le poste ecrit son etat dans XPG_REPRISE_FICHIER a l'arret et le reprend au
//  lancement suivant (puis l'efface) : l'agent ne l'efface jamais entre l'arret
//  d'une version et le lancement de la suivante (ni au retour en arriere) ; il
//  l'efface seulement s'il a plus de 10 minutes au moment d'un lancement.
//
//  L'ARRET (STOP) : l'agent ecrit dans ce fichier, d'un coup (fichier temporaire
//  renomme), le JSON
//      {"raison": "<phrase pour l'operateur>", "delai": 60}
//  (delai : les secondes que l'operateur a pour finir). Le poste previent
//  l'operateur, enregistre son etat et quitte. L'agent attend delai + 30 s
//  (stopGraceMs) ; le poste toujours la : STOP echoue ("arret-delai") et le
//  dit - sauf force=true : le poste est alors termine, et le journal le dit.
//  Le fichier est efface quand le poste est sorti, et avant chaque lancement.
//
//  LE CONTROLE DE DEMARRAGE (START) : le poste doit tenir startCheckMs (8 s) ;
//  s'il sort avant, l'agent revient a la version active avant lui, la relance,
//  et le dit (reponse "demarrage", journal, historique).
//
//  L'agent qui redemarre retrouve le poste qu'il avait lance (poste.json : pid
//  et heure de creation) ; il reconnait aussi un poste lance par la session
//  (la cle Run, le .desktop) sur une version installee.
// =============================================================================
#pragma once

#include "Journal.hpp"
#include "Package.hpp"
#include "Pairing.hpp"

#include "../core/Json.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace deploy {

struct DeployerConfig {
    std::string dir;                                   // le dossier de l'agent
    std::string runtimeProgram;                        // vide : IHM-Studio(.exe) a cote de l'agent
    int keepVersions{3};                               // les versions gardees par application
    int startCheckMs{8000};                            // le poste doit tenir ce temps apres START
    int stopGraceMs{30000};                            // STOP attend delai + ceci
    std::int64_t simulatedFreeBytes{-1};               // ESSAIS : la place libre simulee (-1 : la vraie)
    std::int64_t diskReserveBytes{64ll * 1024 * 1024}; // la marge laissee libre sur le disque
    PackageLimits limits;
};

// Qui demande (pour le journal et l'historique).
struct Peer {
    std::string fingerprint;   // hexadecimal
    std::string name;          // le nom du PC du Studio
    std::string address;
};

struct OpResult {
    bool ok{false};
    std::string code;          // protocol::err (si !ok)
    std::string message;       // la phrase (si !ok ; parfois aussi si ok)
    core::json::Value data = core::json::Value::object();   // aussi en cas d'echec : le detail
    std::string journal;       // le detail de la ligne du journal
};

class Deployer {
public:
    Deployer(DeployerConfig cfg, pairing::StudioStore& studios, Journal& journal, const std::atomic<bool>& stopping);
    ~Deployer();
    Deployer(const Deployer&) = delete;
    Deployer& operator=(const Deployer&) = delete;

    // Au demarrage de l'agent : nettoyer les installations coupees, retrouver le poste.
    void start();

    OpResult prepare(const Peer&, const core::json::Value& args);
    OpResult sendChunk(const Peer&, const core::json::Value& args, std::string_view bytes);
    OpResult verify(const Peer&, const core::json::Value& args);
    OpResult install(const Peer&, const core::json::Value& args);
    OpResult stop(const Peer&, const core::json::Value& args);
    OpResult startStation(const Peer&, const core::json::Value& args);
    OpResult rollback(const Peer&, const core::json::Value& args);
    OpResult listApps(const Peer&, const core::json::Value& args);
    OpResult history(const Peer&, const core::json::Value& args);

    // STATUS : [{name, version, running, pid, versions}] (name et version : ce que lit IHM Studio 1.28).
    [[nodiscard]] core::json::Value appsSummary();
    // etat.json : {running, pid, application, version}.
    [[nodiscard]] core::json::Value stationState();
    // Le programme du poste (son chemin complet).
    [[nodiscard]] std::string runtimeProgram() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Le programme du poste par defaut : IHM-Studio.exe (Windows) / IHM-Studio a cote de `agentExe`.
[[nodiscard]] std::string defaultRuntimeProgram(const std::string& agentExe);

} // namespace deploy
