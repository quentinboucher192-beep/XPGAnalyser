// =============================================================================
//  session/SessionLink.hpp - 1.43.0 : le lien d'une session (le reseau, ses fils)
// -----------------------------------------------------------------------------
//  L'HOTE (HostLink) ecoute en TCP (47863, ou l'un des 7 suivants), repond a la
//  decouverte en UDP (47864), et fait la poignee de main de chaque equipier dans le
//  fil de sa connexion : TLS 1.3 (les deux certificats), BONJOUR, puis, si la session
//  demande le code, l'echange CPace (deploy::pairing : l'equipier joue le Studio,
//  l'hote l'agent), 3 essais par equipier (son empreinte). Ensuite les trames vont et
//  viennent ; c'est le fil principal (SessionHost) qui decide d'une entree.
//
//  L'EQUIPIER (GuestLink) se connecte dans son fil : TLS, l'empreinte de l'hote
//  EPINGLEE (celle de l'annonce : un autre certificat est refuse), BONJOUR, le code si
//  l'hote le demande.
//
//  UN FIL PAR CONNEXION, qui lit ET ecrit (mbedTLS : un contexte n'est pas a deux
//  fils) : il lit une trame en 30 ms au plus, puis vide ce qui attend d'etre envoye.
//  Le fil principal ne bloque jamais : send() met en file, poll() rend ce qui est
//  arrive. Un silence de 30 s ferme la connexion (une trame PING part apres 5 s sans
//  rien envoyer).
// =============================================================================
#pragma once

#include "SessionProtocol.hpp"

#include "../core/Json.hpp"
#include "../deploy/TlsIdentity.hpp"

#include <memory>
#include <string>
#include <vector>

namespace session {

// Ce qui arrive d'une connexion.
struct LinkEvent {
    enum class Kind {
        Knock,       // (hote) un equipier frappe : `hello` (son BONJOUR), son empreinte, son adresse, `codeOk`
        Connected,   // (equipier) la poignee de main est faite (le code compris) : on attend l'hote
        Frame,       // une trame : `header`, `data`
        Closed,      // la connexion est fermee : `why`
    };
    Kind kind{Kind::Frame};
    int conn{0};                    // (hote) la connexion
    core::json::Value header;       // la trame, ou le BONJOUR (Knock)
    std::string data;
    std::string fingerprint;        // l'empreinte du pair (hexadecimal)
    std::string address;            // son adresse
    bool codeOk{false};             // (Knock) le code est prouve
    std::string why;
};

struct HostConfig {
    std::string bindAddress{"0.0.0.0"};
    int  port{kPort};               // le premier essaye ; 0 : un port libre (les essais)
    int  discoveryPort{kDiscoveryPort};   // -1 : pas de decouverte
    std::string hostName;           // Mon profil (dans la reponse CPace)
    std::string sessionId;          // 16 caracteres hexadecimaux
    std::string code;               // vide : pas de code ; sinon 6 chiffres
};

class HostLink {
public:
    HostLink();
    ~HostLink();
    HostLink(const HostLink&) = delete;
    HostLink& operator=(const HostLink&) = delete;

    // Ecouter, repondre a la decouverte. Faux : `why` (aucun port libre...).
    bool start(const HostConfig&, const deploy::TlsIdentity& identity, std::string* why);
    void stop();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] int port() const noexcept;           // le port TCP pris
    [[nodiscard]] int discoveryPort() const noexcept;  // -1 : sans decouverte
    // Ce que la decouverte annonce (le nombre de membres change).
    void setAnnounce(const Announce&);

    [[nodiscard]] std::vector<LinkEvent> poll();
    void send(int conn, const core::json::Value& header, std::string data = {});
    // Envoyer puis fermer (un REFUS, la fin de la session).
    void closeAfter(int conn, const core::json::Value& header);
    void close(int conn);
    // Les octets en attente d'envoi vers `conn` (le projet qui part : la barre de progression).
    [[nodiscard]] std::size_t pending(int conn) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

struct GuestConfig {
    std::string address;
    int         port{kPort};
    std::string fingerprint;        // celle de l'annonce ; vide : acceptee telle quelle (montree)
    std::string sessionId;          // celui de l'annonce (le code y est lie)
    std::string code;               // si l'hote le demande
    core::json::Value hello;        // le BONJOUR : nom, pc, id_pc, logiciel, version, couleur
};

class GuestLink {
public:
    GuestLink();
    ~GuestLink();
    GuestLink(const GuestLink&) = delete;
    GuestLink& operator=(const GuestLink&) = delete;

    // Se connecter (dans son fil : rend tout de suite). Les evenements disent la suite.
    bool start(const GuestConfig&, const deploy::TlsIdentity& identity, std::string* why);
    void stop();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::vector<LinkEvent> poll();
    void send(const core::json::Value& header, std::string data = {});
    void closeAfter(const core::json::Value& header);
    [[nodiscard]] std::string hostFingerprint() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// La valeur qui lie l'echange CPace a la session (sa "cle d'editeur" dans la conversation) : SHA-256 de
// "XPG-SESSION-v1|" + l'identifiant de la session, en hexadecimal. Un code de session ne sert donc pas
// a un appairage de deploiement, ni a une autre session.
[[nodiscard]] std::string sessionBinding(const std::string& sessionId);

} // namespace session
