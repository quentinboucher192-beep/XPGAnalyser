// =============================================================================
//  deploy/Client.hpp - 1.28.0 : le cote IHM Studio du deploiement reseau
// -----------------------------------------------------------------------------
//  TOUT EST BLOQUANT, avec des delais : appelle ces fonctions depuis un fil de
//  travail, jamais depuis le fil de l'interface. Un Client sert un fil a la fois
//  (une connexion) ; MachineStore (deploy/Pairing.hpp) est sur entre fils.
//
//  Le parcours type (fenetre Deploiement reseau) :
//
//    TlsIdentity me;   // une fois : TlsIdentity::loadOrCreate(dossierStudio, "studio", false, "XPG-Studio " + nom, me, &why)
//    MachineStore store(dossierStudio); store.load(&why);
//    auto found = Client::discover();                              // les agents du reseau (1,5 s)
//
//    Client c(me, nomDuPC, cleEditeurHex);
//    Outcome o = c.connect(adresse, port, 5000);                  // nouvelle machine : sans epingle
//    AgentInfo info; o = c.identify(info);                         // versions, nom, M-XXXX
//    PairedMachine m; o = c.pair("482913", m);                     // le code affiche par l'agent
//    if (o) store.upsert(m, &why);                                 // l'empreinte est epinglee
//
//    // ensuite, une machine appairee :
//    o = c.connect(m, 5000);                                       // refuse si l'empreinte a change
//    AgentStatus st; o = c.status(st);
//    std::vector<std::string> lignes; o = c.logs(200, lignes);
//    o = c.unpair(); if (o) store.remove(m.fingerprint, &why);
//
//  Chaque fonction rend un Outcome : ok, sinon `error` (un code de
//  protocol::err : "incompatible", "non-appaire", "code-faux", "empreinte",
//  "reseau"...) et `message`, une phrase en francais a montrer telle quelle.
//
//  1.29.0 - DEPLOYER (la version 2 du protocole ; chacune ouvre sa connexion
//  epinglee, fait son travail et la ferme) :
//
//    DeployOptions o; o.autostart = true; o.stopDelaySeconds = 60; o.force = false;
//    DeployResult r = c.deploy(m, "C:/.../Armoire_Gaz-V3.xpgdeploy", o,
//        [&](DeployStep etape, std::uint64_t octets, std::uint64_t total) { ...; return !annule; });
//    // r.outcome (ok / error / message : la phrase finale), r.steps (chaque etape), r.previousVersion,
//    // r.rolledBack / r.rolledBackTo (le poste n'a pas tenu : l'agent est revenu a la precedente)
//    RollbackResult rb; o = c.rollback(m, rb);                 // la version precedente
//    AppList apps;      o = c.listApps(m, apps);              // applications, versions, poste en marche
//    std::vector<HistoryEntry> h; o = c.history(m, 100, h);   // les deploiements
//
//  deploy() enchaine : verification locale du paquet, connexion, PREPARE,
//  SEND_CHUNK (blocs de 1 Mio ; une coupure : reconnexion et reprise ou l'agent
//  en est, 3 fois au plus ; un envoi annule ou coupe reprend au deploiement
//  suivant, meme apres un redemarrage de l'agent), VERIFY_PACKAGE, INSTALL,
//  STOP (si un poste tourne), START (le poste doit tenir quelques secondes).
//  L'annulation (la progression rend faux) est prise en compte jusqu'au debut
//  d'INSTALL ; ensuite deploy() va au bout.
// =============================================================================
#pragma once

#include "Discovery.hpp"
#include "Frame.hpp"
#include "Pairing.hpp"
#include "Protocol.hpp"
#include "Tls.hpp"
#include "TlsIdentity.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deploy {

struct Outcome {
    bool ok{false};
    std::string error;     // le code (protocol::err), vide si ok
    std::string message;   // la phrase pour l'utilisateur
    explicit operator bool() const noexcept { return ok; }
};

// IDENTIFY : ce que l'agent dit de lui.
struct AgentInfo {
    int proto{0};                // la version choisie
    std::vector<int> protos;     // celles qu'il accepte
    std::string machine, id, version;
    std::string fingerprint;     // celle vue dans la poignee de main TLS (hexadecimal)
    bool paired{false};          // l'agent connait deja ce Studio
};

// Une version installee sur le PC de maintenance (LIST_APPS).
struct InstalledVersion {
    std::string id;              // le dossier : "V3" (ou "V3-2" : meme nom, autre contenu)
    std::string label;           // la version du paquet : "V3"
    std::string sha256;          // le SHA-256 du paquet
    std::int64_t installedAt{0}; // ms depuis 1970
    std::string from;            // le PC du Studio qui l'a envoyee
    std::string publisher;       // l'empreinte de la cle d'editeur
    std::uint64_t files{0}, bytes{0};
    bool active{false};
};

// STATUS (name, version, running, pid, versionCount) et LIST_APPS (tout).
struct InstalledApp {
    std::string name, version;   // version : l'id de la version active ("" : aucune)
    std::string activeLabel;     // LIST_APPS
    std::string previous;        // LIST_APPS : l'id de la version active avant
    bool autostart{false};       // LIST_APPS : le poste demarre avec la session
    bool running{false};         // le poste de cette application tourne
    int pid{0};
    std::string runningVersion;  // LIST_APPS
    int versionCount{0};
    std::vector<InstalledVersion> versions;   // LIST_APPS, les plus anciennes d'abord
};

// Le poste qui tourne sur le PC de maintenance.
struct StationInfo {
    bool running{false};
    int pid{0};
    std::string application, version;
    std::int64_t startedAt{0};
    bool adopted{false};         // lance par la session (la cle Run), retrouve par l'agent
};

struct AppList {
    std::vector<InstalledApp> apps;
    StationInfo station;
    std::string program;         // le programme du poste sur ce PC
};

// Une ligne de l'historique (GET_HISTORY).
struct HistoryEntry {
    std::int64_t at{0};          // ms depuis 1970
    std::string op;              // INSTALL, START, STOP, ROLLBACK, VERIFY_PACKAGE
    std::string application, version, label;
    std::string from, address;   // le PC du Studio
    bool ok{false};
    std::string result;          // "installee", "deja-installee", "lance", "arrete", "force", "retour", "automatique", "refuse", "echec"...
    std::string message;         // la phrase
};

// ---- deployer ----
enum class DeployStep { Check, Connect, Prepare, Send, Verify, Install, Stop, Start, Done };
// Le nom de l'etape, en francais (<< Verification du paquet >>, << Envoi >>...), pour la progression.
[[nodiscard]] std::string deployStepName(DeployStep step);

// La progression : l'etape, les octets faits et le total (Check : le paquet hache ; Send : les
// octets recus par l'agent, reprise comprise ; 0 et 0 pour les autres). Faux : annuler.
using DeployProgress = std::function<bool(DeployStep step, std::uint64_t bytes, std::uint64_t total)>;

struct DeployOptions {
    bool autostart{false};        // le poste demarre avec la session du PC de maintenance (faux : l'entree s'efface)
    int stopDelaySeconds{60};     // le delai donne au poste en marche pour quitter (il previent l'operateur)
    bool force{false};            // le poste qui n'a pas quitte apres delai + 30 s est termine
    int startCheckSeconds{0};     // le poste doit tenir ce temps apres le lancement ; 0 : le reglage de l'agent (8 s)
    std::string stopReason;       // vide : << Mise a jour vers <version> demandee depuis <ce PC> >>
    int connectTimeoutMs{5000};
    int retries{3};               // les reconnexions apres une coupure pendant l'envoi
};

struct DeployStepResult {
    DeployStep step{DeployStep::Check};
    bool ok{false};
    bool skipped{false};          // l'etape n'avait rien a faire (deja installee, aucun poste en marche)
    std::string message;          // la phrase de l'etape
    std::int64_t ms{0};
};

struct DeployResult {
    Outcome outcome;                      // ok, sinon error et message : la phrase finale, prete a afficher
    std::vector<DeployStepResult> steps;  // les etapes faites, dans l'ordre
    std::string application, label;       // ce que le paquet annonce ("Armoire_Gaz", "V3")
    std::string version;                  // l'id installe ("V3", "V3-2")
    std::string previousVersion;          // la version active avant ("" : aucune)
    bool alreadyInstalled{false};         // meme paquet deja installe : rien n'a ete envoye
    bool cancelled{false};                // annule par la progression (rien d'installe ; l'envoi reprendra)
    std::uint64_t packageBytes{0};
    std::uint64_t bytesSent{0};           // envoyes par CE deploiement
    std::uint64_t resumedFrom{0};         // l'agent avait deja ces octets (un envoi coupe avant)
    int reconnects{0};                    // les reconnexions apres une coupure
    bool stationWasRunning{false};        // un poste tournait : il a ete arrete
    bool stationForced{false};            // ... de force
    bool started{false};                  // le poste tourne a la fin
    int pid{0};
    bool rolledBack{false};               // la nouvelle version n'a pas tenu : l'agent est revenu a...
    std::string rolledBackTo;             // ... cette version
    std::vector<std::string> removedVersions;   // les versions effacees (on en garde 3)
};

struct RollbackOptions {
    std::string application;      // vide : la seule installee
    std::string version;          // vide : la precedente (la plus recente installee avant l'active)
    int stopDelaySeconds{60};
    bool force{false};
    std::string stopReason;
    int startCheckSeconds{0};
    int connectTimeoutMs{5000};
};

struct RollbackResult {
    std::string application, from, to;
    bool wasRunning{false};       // le poste tournait : arrete puis relance sur `to`
    bool restarted{false};
    bool running{false};
    int pid{0};
    std::string message;
};
struct AgentStatus {
    std::string machine, id, version, system;
    std::int64_t startedAt{0};   // ms depuis 1970
    std::int64_t uptimeS{0};
    std::int64_t diskFree{-1};   // octets libres sur le disque du dossier de l'agent (-1 : inconnu)
    int pairedCount{0};          // les PC appaires a l'agent
    std::vector<InstalledApp> apps;
};

class Client {
public:
    // `me` : l'identite TLS du Studio ; `studioName` : le nom du PC ; `publisherKeyHex` : la cle
    // publique d'editeur Ed25519 (64 chiffres hexadecimaux, deploy/DevKey) donnee a l'appairage.
    Client(TlsIdentity me, std::string studioName, std::string publisherKeyHex);
    ~Client();

    // Les agents du reseau (bloquant : options.waitMs, 1,5 s par defaut). Une machine deja appairee
    // se reconnait a son empreinte (Found::fingerprint == PairedMachine::fingerprint) : si son adresse
    // a change, MachineStore::upsert avec la nouvelle (l'empreinte reste verifiee a chaque connexion).
    static std::vector<discovery::Found> discover(const discovery::Options& options = {}, std::string* why = nullptr) {
        return discovery::discover(options, why);
    }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Se connecter (TCP + TLS 1.3) en `timeoutMs`. `pinnedFingerprint` (hexadecimal) : celle gardee
    // a l'appairage ; si le pair en presente une autre, la connexion est refusee (error "empreinte").
    // Vide : une machine pas encore appairee (seuls IDENTIFY et l'appairage serviront).
    Outcome connect(const std::string& address, int port, int timeoutMs, const std::string& pinnedFingerprint = {});
    // Une machine appairee : son adresse, son port, son empreinte epinglee.
    Outcome connect(const PairedMachine& machine, int timeoutMs);

    Outcome identify(AgentInfo& out);
    // L'appairage par le code a 6 chiffres (les espaces sont ignores). Reussi : `out` est a garder
    // (MachineStore::upsert) ; l'agent, lui, garde deja l'empreinte, le nom et la cle d'editeur du Studio.
    Outcome pair(const std::string& code, PairedMachine& out);
    Outcome status(AgentStatus& out);
    Outcome logs(int lines, std::vector<std::string>& out);
    // L'agent oublie ce Studio (lui seul). Retire aussi la machine de ton MachineStore.
    Outcome unpair();

    // ---- 1.29.0 : deployer (chacune se connecte a `machine`, epinglee, puis ferme) ----
    DeployResult deploy(const PairedMachine& machine, const std::string& packagePath, const DeployOptions& options = {},
                        const DeployProgress& progress = {});
    Outcome rollback(const PairedMachine& machine, RollbackResult& out, const RollbackOptions& options = {});
    Outcome listApps(const PairedMachine& machine, AppList& out);
    Outcome history(const PairedMachine& machine, int limit, std::vector<HistoryEntry>& out);

    // ---- 1.29.0 : les commandes une a une, sur la connexion en cours (deploy() les enchaine ; les essais) ----
    struct PrepareReply {
        std::string transfer;            // le SHA-256 du paquet
        std::uint64_t offset{0};         // ou reprendre
        std::uint32_t chunkSize{0};
        std::int64_t diskFree{-1};
        bool installed{false};           // deja installe, identique : rien a envoyer
        std::string version;             // ... sous cet id
    };
    Outcome prepare(const std::string& application, const std::string& label, std::uint64_t size, const std::string& sha256,
                    std::uint64_t contentSize, PrepareReply& out);
    // Un bloc (1 Mio au plus). `claimedSha` (les essais) : un SHA-256 annonce a la place du vrai.
    Outcome sendChunk(const std::string& transfer, std::uint64_t offset, std::string_view bytes, std::uint64_t& received,
                      const std::string& claimedSha = {});
    Outcome verifyPackage(const std::string& transfer, core::json::Value& out);
    // `application` : utile quand PREPARE a dit << deja installee >> (l'agent n'a rien recu a son nom).
    Outcome install(const std::string& transfer, std::optional<bool> autostart, core::json::Value& out, const std::string& application = {});
    // STOP {raison, delai, force} ; START {application, version, check, autostart} ; ROLLBACK {application, version, delai, force, start}.
    Outcome stopStation(const core::json::Value& args, core::json::Value& out);
    Outcome startStation(const core::json::Value& args, core::json::Value& out);
    Outcome rollbackRequest(const core::json::Value& args, core::json::Value& out);
    Outcome listApps(AppList& out);
    Outcome history(int limit, std::vector<HistoryEntry>& out);

    void close();
    [[nodiscard]] bool connected() const;
    [[nodiscard]] std::string peerFingerprint() const;   // hexadecimal, apres connect()
    [[nodiscard]] const std::string& address() const noexcept { return address_; }
    [[nodiscard]] int port() const noexcept { return port_; }
    // Le delai d'une demande (envoi + reponse), 15 s par defaut.
    void setRequestTimeout(int ms) noexcept { requestTimeoutMs_ = ms; }

    // ---- bas niveau (les essais) ----
    // Envoie une trame telle quelle et lit la reponse.
    Outcome exchange(const core::json::Value& header, const std::string& data, Frame& reply);
    // Une demande ordinaire : seq suivant, ts maintenant, un identifiant neuf (ou `opId`). Une reponse
    // d'erreur qui porte des donnees (version 2) les met aussi dans `result`.
    Outcome request(std::string_view op, const core::json::Value& args, core::json::Value& result, const std::string& opId = {});
    [[nodiscard]] std::int64_t nextSeq() noexcept { return ++seq_; }
    [[nodiscard]] int proto() const noexcept { return proto_; }
    [[nodiscard]] TlsStream& stream() noexcept { return tls_; }
    // Les versions annoncees par identify() (les essais d'incompatibilite).
    void setOfferedVersions(std::vector<int> v) { offered_ = std::move(v); }
    // Une empreinte d'agent imposee au calcul de l'appairage (les essais : un tiers qui s'interpose).
    void forcePairingAgentFingerprint(const crypto::Digest& fp) { forcedAgentFp_ = fp; forceAgentFp_ = true; }

private:
    Outcome ensureIdentified();
    // Connexion epinglee, IDENTIFY, et la version 2 du protocole (sinon un message clair).
    Outcome openForDeploy(const PairedMachine& machine, int timeoutMs);
    // Une demande avec un delai a elle (STOP, START : l'attente du poste).
    Outcome requestLong(std::string_view op, const core::json::Value& args, core::json::Value& result, int timeoutMs);
    TlsIdentity me_;
    std::string name_, keyHex_;
    TlsStream tls_;
    std::string address_;
    int port_{0};
    int proto_{0};
    std::int64_t seq_{0};
    int requestTimeoutMs_{15000};
    std::vector<int> offered_;
    AgentInfo info_;
    crypto::Digest forcedAgentFp_{};
    bool forceAgentFp_{false};
};

// La phrase du refus d'une empreinte (pour l'interface et le journal du Studio).
[[nodiscard]] std::string pinMismatchMessage(const std::string& expectedHex, const std::string& presentedHex);

} // namespace deploy
