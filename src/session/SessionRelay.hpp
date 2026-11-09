// =============================================================================
//  session/SessionRelay.hpp - 1.44.0 : LES SESSIONS PAR INTERNET (le relais du
//  service XPG, chiffre de bout en bout)
// -----------------------------------------------------------------------------
//  docs/SESSIONS.md, << Par Internet >>. L'hote et ses equipiers ne se voient pas :
//  ils passent par le relais du service des licences (service/src/relais.js,
//  /v1/relais/*, des appels signes par la cle de chaque PC). Le relais ne fait que
//  passer des octets : il ne voit ni le code, ni le projet, ni les noms.
//
//  LE CODE : 10 caracteres du base32 de Crockford (50 bits, "K7Q4-M2XP-T9"), tire a
//  chaque session ; il ne passe jamais. Le relais connait la session par son
//  LOCALISATEUR : les 16 premiers octets de SHA-256(code), en hexadecimal.
//
//  L'ENTREE (pour chaque equipier, a travers le relais) : l'echange CPace des
//  appairages (deploy::pairing : ristretto255, les memes briques), le code pour
//  secret, lie au localisateur et a la connexion donnee par le relais :
//    G   = ristretto255_from_hash(SHA-512("XPG-RELAIS-v1" | code | sid | canal))
//          canal = SHA-256("XPG-RELAIS-CANAL|" + localisateur + "|" + connexion)
//    l'equipier : CODE_DEBUT {sid, Y}  ->  l'hote : CODE_REPONSE {Y, confirmation, essais}
//    l'equipier verifie l'hote, puis CODE_FIN {confirmation}  ->  l'hote verifie.
//  Un code faux : l'hote le voit (3 essais par cle de PC, comme sur le reseau du
//  bureau), l'equipier aussi (la confirmation de l'hote ne passe pas : rien de plus
//  ne part). Puis chaque sens a sa cle (BLAKE2b de la cle de l'echange) et chaque
//  trame du protocole des sessions est SCELLEE (XChaCha20-Poly1305, libsodium) :
//  un compteur par sens dans le nonce, un message rejoue est ignore, un message qui
//  manque ou arrive dans le desordre ferme la connexion. Le BONJOUR (le nom, le PC)
//  part scelle ; l'hote repond LIEN_OK scelle : la suite est celle du reseau du
//  bureau (ATTENTE, ENTREE, PROJET...).
//
//  SUR LE RELAIS : les donnees d'un envoi sont des enregistrements a la suite
//  ([longueur sur 4 octets][enregistrement]) ; un enregistrement : 'C' + un JSON en
//  clair (l'echange CPace, un refus avant les cles), ou 'S' + le compteur (8 octets)
//  + le texte scelle (1 octet : << la suite vient >>, puis un morceau de la trame :
//  [longueur de l'en-tete sur 4 octets][en-tete JSON][donnees]). Une trame de plus
//  de 1 Mio part en morceaux ; 32 Mio au plus.
//
//  LES FILS : deux par lien (jamais celui de l'interface) - l'un lit (l'attente longue
//  du relais, 25 s), l'autre envoie (les trames en attente, regroupees : un appel
//  porte 1,4 Mio au plus, et l'hote ecrit a plusieurs equipiers d'un appel). Les
//  memes evenements que HostLink / GuestLink (LinkEvent), plus Opened.
//
//  LE TRANSPORT EST INJECTABLE (RelayTransport) : l'application lui donne le HTTP
//  signe par la cle du PC (app/SessionRelayHttp) ; les essais, un relais en memoire.
// =============================================================================
#pragma once

#include "SessionLink.hpp"

#include "../core/Json.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace session {

// ---- le code d'une session par Internet ----
inline constexpr int kRelayCodeChars = 10;   // base32 de Crockford : 50 bits
// Un code neuf (le hasard du systeme) : "K7Q4M2XPT9".
[[nodiscard]] std::string newRelayCode();
// "K7Q4-M2XP-T9".
[[nodiscard]] std::string relayCodeShown(std::string_view code);
// Le code tape, avec souplesse (minuscules, espaces, tirets ; O pour 0, I et L pour 1). Faux : pas 10 caracteres
// de l'alphabet.
bool relayCodeTyped(std::string_view text, std::string& code);
// Le localisateur de la session sur le relais : les 16 premiers octets de SHA-256(code), 32 hexadecimaux.
[[nodiscard]] std::string relayLocator(std::string_view code);

// ---- le transport : un appel au relais (HTTP, signe par la cle du PC) ----
struct RelayCall {
    std::string method;        // "GET", "POST"
    std::string path;          // "/v1/relais/<localisateur>/recevoir?depuis=3&attente=25"
    std::string body;          // JSON (vide : un GET)
    int         waitSeconds{0};   // l'attente longue demandee au relais (le transport attend un peu plus)
};
struct RelayReply {
    int         status{0};     // 0 : pas de reponse (`error` dit pourquoi)
    std::string body;
    std::string error;
};
class RelayTransport {
public:
    virtual ~RelayTransport() = default;
    // Bloquant (un fil du lien) ; plusieurs fils peuvent appeler en meme temps.
    virtual RelayReply call(const RelayCall&) = 0;
    // Depuis un autre fil : les appels en cours rendent tout de suite (status 0), les suivants aussi. Definitif.
    virtual void cancel() = 0;
};
using RelayTransportPtr = std::shared_ptr<RelayTransport>;

// ---- l'hote ----
struct RelayHostConfig {
    RelayTransportPtr transport;
    std::string       code;          // normalise (relayCodeTyped) : 10 caracteres
    std::string       sessionName;   // ce que le relais garde (et dit a qui frappe avec le bon localisateur)
    std::string       hostName;
    std::string       project;
    int               attempts{kCodeAttempts};   // les codes faux permis par cle de PC
};

class RelayHostLink {
public:
    RelayHostLink();
    ~RelayHostLink();
    RelayHostLink(const RelayHostLink&) = delete;
    RelayHostLink& operator=(const RelayHostLink&) = delete;

    // Ouvrir la session sur le relais (dans son fil : rend tout de suite). Opened, ou Closed (conn 0), le dit.
    bool start(const RelayHostConfig&, std::string* why);
    // Ce qui attend part (1 s au plus), la session est fermee pour tous sur le relais (2 s au plus), les fils
    // s'arretent.
    void stop();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool opened() const noexcept;   // le relais a ouvert la session
    [[nodiscard]] std::string locator() const;

    [[nodiscard]] std::vector<LinkEvent> poll();
    void send(int conn, const core::json::Value& header, std::string data = {});
    void closeAfter(int conn, const core::json::Value& header);
    void close(int conn);
    [[nodiscard]] std::size_t pending(int conn) const;

    struct Impl;

private:
    std::shared_ptr<Impl> d_;
};

// ---- l'equipier ----
struct RelayGuestConfig {
    RelayTransportPtr transport;
    std::string       code;      // tape (relayCodeTyped le normalise)
    core::json::Value hello;     // le BONJOUR : nom, pc, id_pc, logiciel, version, couleur (il part scelle)
};

class RelayGuestLink {
public:
    RelayGuestLink();
    ~RelayGuestLink();
    RelayGuestLink(const RelayGuestLink&) = delete;
    RelayGuestLink& operator=(const RelayGuestLink&) = delete;

    // Frapper (dans son fil : rend tout de suite ; faux : le code n'est pas un code). Les evenements disent la suite :
    // Opened (le relais connait la session), Connected (le code est bon, l'hote decide), Frame, Closed.
    bool start(const RelayGuestConfig&, std::string* why);
    void stop();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::vector<LinkEvent> poll();
    void send(const core::json::Value& header, std::string data = {});
    void closeAfter(const core::json::Value& header);

    struct Impl;

private:
    std::shared_ptr<Impl> d_;
};

// ---- les briques (exposees pour les essais) ----
namespace relay {
inline constexpr std::size_t kPiece = 1024u * 1024u + 64u * 1024u;   // le plus grand morceau scelle
inline constexpr std::size_t kBatch = 1400u * 1024u;                 // octets (decodes) par appel envoyer, au plus
inline constexpr std::size_t kMaxFrame = 32u * 1024u * 1024u;        // une trame reassemblee, au plus
// Les enregistrements d'un envoi : [longueur sur 4 octets][enregistrement]... ; faux : mal forme.
[[nodiscard]] std::string packRecords(const std::vector<std::string>& records);
bool unpackRecords(std::string_view data, std::vector<std::string>& records);
// Une trame : [longueur de l'en-tete sur 4 octets][en-tete JSON][donnees].
[[nodiscard]] std::string encodeFrame(const core::json::Value& header, std::string_view data);
bool decodeFrame(std::string_view bytes, core::json::Value& header, std::string& data);
} // namespace relay

} // namespace session
