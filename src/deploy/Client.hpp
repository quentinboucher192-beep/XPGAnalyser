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
// =============================================================================
#pragma once

#include "Discovery.hpp"
#include "Frame.hpp"
#include "Pairing.hpp"
#include "Protocol.hpp"
#include "Tls.hpp"
#include "TlsIdentity.hpp"

#include <cstdint>
#include <string>
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

// STATUS.
struct InstalledApp {
    std::string name, version;   // 1.29.0 : la liste est vide en 1.28
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
    // Une demande ordinaire : seq suivant, ts maintenant, un identifiant neuf (ou `opId`).
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
