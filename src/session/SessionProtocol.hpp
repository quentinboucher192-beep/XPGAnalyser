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

// ---- 1.50.0 : les messages de la discussion (MESSAGE), et la reponse a un message ----
//  MESSAGE porte toujours `de`, `couleur`, `texte`, `heure` (ce que lit un Studio d'avant). La 1.50.0 y ajoute,
//  FACULTATIFS (un Studio plus ancien les ignore et lit le texte) : `id` (donne par l'hote, le meme pour tous) et
//  `reponse` {id, de, extrait} : le message auquel il repond, son auteur, le debut de son texte (l'extrait voyage avec
//  la reponse : il se montre meme si le message cite n'est plus dans la liste). Un message sans `id` (un hote d'avant)
//  recoit un identifiant tire de son auteur, son heure et son texte : le meme sur chaque PC. A un Studio d'avant la
//  1.50, la reponse part avec la citation en premiere ligne du texte (quotedText).
inline constexpr std::size_t kMessageMax = 2000;     // le texte (octets)
inline constexpr std::size_t kExcerptChars = 80;     // l'extrait cite (caracteres)
struct ChatReply {
    std::string id;        // le message cite
    std::string from;      // son auteur
    std::string excerpt;   // le debut de son texte (sa premiere ligne, raccourcie)
    [[nodiscard]] bool empty() const noexcept { return id.empty() && excerpt.empty(); }
};
struct ChatMessage {
    std::string   id, from, text, time;
    std::uint32_t color{0};
    ChatReply     reply{};
};
// La premiere ligne de `text`, `maxChars` caracteres au plus (<< ... >> si elle est coupee), sans caractere de controle.
[[nodiscard]] std::string chatExcerpt(std::string_view text, std::size_t maxChars = kExcerptChars);
// L'identifiant d'un message qui n'en porte pas (un hote d'avant la 1.50) : "h" + 16 hexadecimaux.
[[nodiscard]] std::string chatIdOf(std::string_view from, std::string_view time, std::string_view text);
// Les champs de MESSAGE (de, couleur, texte, heure ; id et reponse s'ils sont la) ; et dans l'autre sens.
void writeChat(core::json::Value& out, const ChatMessage&);
[[nodiscard]] ChatMessage readChat(const core::json::Value& message);
void writeReply(core::json::Value& out, const ChatReply&);
[[nodiscard]] ChatReply readReply(const core::json::Value& message);   // vide : pas une reponse
// Le texte d'une reponse pour un Studio d'avant la 1.50 : << En reponse a Lea : << extrait >> >> puis le texte.
[[nodiscard]] std::string quotedText(const ChatMessage&);
// Ce Studio (sa version, "1.50.0") connait-il les reponses ? Vide ou illisible : non.
[[nodiscard]] bool knowsReplies(std::string_view version);

// ---- les identifiants : chaque membre cree dans son bloc ----
//  Le membre n cree a partir de n x 2^28 ; une session d'avant a pu y laisser des objets : le
//  premier libre est 1 + le plus grand identifiant du projet dans le bloc.
[[nodiscard]] hmi::Id blockStart(int member) noexcept;
[[nodiscard]] hmi::Id firstFreeInBlock(const hmi::Project&, int member);
// Le plus grand identifiant du projet dans [lo, hi) : tout ce qui porte un identifiant (vues, objets, calques,
// scripts, variables, ressources...), lu dans ses fichiers (les champs "id=") ; kNoId : aucun.
[[nodiscard]] hmi::Id highestId(const hmi::Project&, hmi::Id lo, hmi::Id hi);

} // namespace session
