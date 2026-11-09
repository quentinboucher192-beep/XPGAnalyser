// =============================================================================
//  session/SessionProtocol.hpp - 1.43.0 : les sessions (plusieurs PC sur un projet), le protocole
// -----------------------------------------------------------------------------
//  docs/SESSIONS.md. Sur le reseau du bureau :
//    - la decouverte : UDP 47864, en diffusion ({"xpgs":1,"op":"CHERCHE","nonce"} ->
//      {"xpgs":1,"op":"ICI","nonce",...l'annonce}) ; ports a part de ceux du deploiement ;
//    - le lien : TCP 47863 (ou les suivants s'il est pris), TLS 1.3 (deploy::TlsStream), les
//      trames du deploiement (deploy::writeFrame / readFrame : un en-tete JSON, des donnees) ;
//    - le code (la session "Il faut le code") : 6 chiffres, jamais transmis : l'echange CPace
//      du deploiement (deploy::pairing), lie aux deux empreintes TLS.
//  Ce fichier : les constantes, l'annonce, la decouverte, les membres (leur JSON), le code, les
//  blocs d'identifiants, les couleurs.
// =============================================================================
#pragma once

#include "../hmi/HmiModel.hpp"
#include "../core/Json.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace session {

inline constexpr int kPort = 47863;            // le lien (TCP)
inline constexpr int kPortSpan = 8;            // s'il est pris : les 7 suivants (TCP)
inline constexpr int kDiscoveryPort = 47864;   // la decouverte (UDP)
inline constexpr int kProtocol = 1;
inline constexpr int kMaxMembers = 8;          // l'hote compris
inline constexpr hmi::Id kIdBlock = 0x10000000u;   // 2^28 : le bloc d'identifiants d'un membre
inline constexpr int kCodeDigits = 6;
inline constexpr int kCodeAttempts = 3;        // par equipier

// Le role d'un membre : il modifie, ou il regarde seulement.
enum class Role { Edit, Watch };
[[nodiscard]] const char* roleKey(Role) noexcept;          // "modifier", "regarder"
[[nodiscard]] Role        roleOf(std::string_view key) noexcept;
[[nodiscard]] std::string roleText(Role);                   // "modifie", "regarde"

// Qui peut entrer : l'hote accepte chaque entree, ou il faut le code.
enum class Entry { Accept, Code };

// Ce que la decouverte annonce d'une session.
struct Announce {
    std::string session;       // "Mise en service Armoire_Gaz"
    std::string host;          // le nom de l'hote (Mon profil)
    std::string machine;       // son PC
    std::string machineId;     // "M-7F21"
    std::string app;           // "IHM Studio"
    std::string version;       // "1.43.0"
    std::string project;       // "Armoire_Gaz"
    std::string fingerprint;   // l'empreinte SHA-256 du certificat de l'hote (hexadecimal)
    std::string sessionId;     // tire au demarrage (16 caracteres hexadecimaux)
    int         port{kPort};
    int         members{1};
    int         maxMembers{kMaxMembers};
    bool        needsCode{false};
    int         protocol{kProtocol};
};

[[nodiscard]] std::string searchMessage(const std::string& nonce);
bool parseSearch(std::string_view text, std::string& nonce);
[[nodiscard]] std::string hereMessage(const std::string& nonce, const Announce&);
bool parseHere(std::string_view text, std::string& nonce, Announce&);

struct Found {
    std::string address;   // l'IPv4 de l'hote
    Announce    announce;
};
struct SearchOptions {
    int  port{kDiscoveryPort};
    int  waitMs{900};
    bool broadcast{true};
    std::vector<std::string> direct;   // des adresses a interroger en plus (127.0.0.1 : les essais)
};
// Les sessions du reseau du bureau (une par hote ; deux envois, au tiers du delai). Vide : aucune (`why`
// dit pourquoi si la recherche n'a pas pu partir).
[[nodiscard]] std::vector<Found> search(const SearchOptions&, std::string* why);

// ---- le code ----
[[nodiscard]] std::string newCode();                       // 6 chiffres du hasard du systeme
[[nodiscard]] std::string codeShown(std::string_view code);   // "482 913"
[[nodiscard]] std::string codeTyped(std::string_view text);   // les chiffres seuls ("482-913" -> "482913")

// ---- les membres ----
struct Member {
    int         number{0};     // 0 : l'hote
    std::string name;          // Mon profil ("Lea")
    std::string machine;       // "PC-ATELIER-02"
    std::string machineId;     // "M-7F21"
    std::string app;           // "IHM Studio"
    std::string version;
    std::uint32_t color{0};    // 0xRRGGBB
    Role        role{Role::Edit};
    hmi::Id     view{hmi::kNoId};          // la vue ou il est
    std::vector<hmi::Id> selection;        // ses objets choisis (dans `view`)
    [[nodiscard]] bool host() const noexcept { return number == 0; }
};
[[nodiscard]] core::json::Value toJson(const Member&);
bool fromJson(const core::json::Value&, Member&);
// Les initiales d'une pastille ("Lea Martin" -> "LM", "lea" -> "L").
[[nodiscard]] std::string initials(std::string_view name);

// Les couleurs des membres (8 : l'hote prend la premiere libre comme les autres).
[[nodiscard]] const std::vector<std::uint32_t>& palette();
// La premiere couleur de la palette que personne n'a (`wanted` d'abord, si elle est libre).
[[nodiscard]] std::uint32_t freeColor(const std::vector<Member>& members, std::uint32_t wanted = 0);

// ---- les identifiants : chaque membre cree dans son bloc ----
//  Le membre n cree a partir de n x 2^28 ; une session d'avant a pu y laisser des objets : le
//  premier libre est 1 + le plus grand identifiant du projet dans le bloc.
[[nodiscard]] hmi::Id blockStart(int member) noexcept;
[[nodiscard]] hmi::Id firstFreeInBlock(const hmi::Project&, int member);
// Le plus grand identifiant du projet dans [lo, hi) : tout ce qui porte un identifiant (vues, objets, calques,
// scripts, variables, ressources...), lu dans ses fichiers (les champs "id=") ; kNoId : aucun.
[[nodiscard]] hmi::Id highestId(const hmi::Project&, hmi::Id lo, hmi::Id hi);

} // namespace session
