// =============================================================================
//  deploy/Protocol.hpp - 1.28.0 : le protocole entre IHM Studio et l'agent
// -----------------------------------------------------------------------------
//  En-tete d'une demande :  {"proto":1,"op":"STATUS","id":"op-1a2b3c4d","seq":7,"ts":<ms depuis 1970>}
//  En-tete d'une reponse :  {"proto":1,"op":"STATUS","re":"op-1a2b3c4d","seq":7,"ok":true}
//                           ou "ok":false,"error":"<code>","message":"<phrase>"
//                           (+ "replay":true : la reponse gardee d'une operation deja faite)
//  Les arguments et les resultats sont dans les donnees de la trame (du JSON).
//
//  Contre le rejeu (ReplayGuard, ResponseCache) :
//    - seq croit strictement sur une connexion ;
//    - ts a 5 minutes au plus de l'horloge de l'agent ;
//    - un identifiant d'operation deja vu (pour ce pair) rend la meme reponse,
//      sans rien refaire.
//  Les versions : IDENTIFY porte les versions acceptees de chaque cote ; la plus
//  haute commune est choisie, aucune -> "incompatible" (jamais forcee).
//
//  Les commandes en 1.28 : IDENTIFY, PAIR_START, PAIR_FINISH, STATUS, GET_LOGS,
//  UNPAIR. Un pair non appaire ne peut faire que IDENTIFY et PAIR_*.
//
//  1.29.0 : LA VERSION 2 DU PROTOCOLE. L'agent accepte 1 et 2 ; IHM Studio 1.29
//  offre 1 et 2. La version 2 ajoute l'envoi, l'installation et le poste :
//    PREPARE         {application, version, size, sha256, contentSize}
//                    -> {transfer, offset, chunkSize, diskFree, installed}
//    SEND_CHUNK      l'en-tete porte "chunk":{transfer, offset, size, sha256} et
//                    les donnees de la trame sont les octets du bloc (1 Mio au
//                    plus, PAS du JSON) -> {received, complete}
//    VERIFY_PACKAGE  {transfer} -> {application, version, files, bytes, publisher}
//    INSTALL         {transfer, autostart?} -> {version, label, previous, already, removed}
//    STOP            {raison?, delai?, force?} -> {wasRunning, pid, forced, waitedMs}
//    START           {application?, version?, check?, autostart?} -> {pid, version, rolledBack...}
//    ROLLBACK        {application?, version?, delai?, force?} -> {from, to, restarted, pid}
//    LIST_APPS       {} -> {apps:[...], station:{...}}
//    GET_HISTORY     {limit?} -> {entries:[...]}
//  Une reponse d'erreur de la version 2 peut porter des donnees (le detail : le
//  retour automatique d'un START manque). Sous la version 1 (IHM Studio 1.28),
//  ces commandes sont refusees (code "proto") avec une phrase qui dit de mettre
//  a jour IHM Studio ; IDENTIFY, l'appairage, STATUS (qui liste aussi les
//  applications), GET_LOGS et UNPAIR restent les memes.
// =============================================================================
#pragma once

#include "Frame.hpp"

#include "../core/Json.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace deploy::protocol {

inline constexpr int kAgentPort = 47861;       // TCP : l'agent
inline constexpr int kDiscoveryPort = 47862;   // UDP : la decouverte
inline constexpr std::int64_t kClockWindowMs = 5 * 60 * 1000;
// Les versions de protocole que ce code accepte.
[[nodiscard]] const std::vector<int>& acceptedVersions();

// Les commandes.
inline constexpr std::string_view kIdentify = "IDENTIFY";
inline constexpr std::string_view kPairStart = "PAIR_START";
inline constexpr std::string_view kPairFinish = "PAIR_FINISH";
inline constexpr std::string_view kStatus = "STATUS";
inline constexpr std::string_view kGetLogs = "GET_LOGS";
inline constexpr std::string_view kUnpair = "UNPAIR";
// 1.29.0 (version 2 du protocole).
inline constexpr std::string_view kPrepare = "PREPARE";
inline constexpr std::string_view kSendChunk = "SEND_CHUNK";
inline constexpr std::string_view kVerifyPackage = "VERIFY_PACKAGE";
inline constexpr std::string_view kInstall = "INSTALL";
inline constexpr std::string_view kStop = "STOP";
inline constexpr std::string_view kStart = "START";
inline constexpr std::string_view kRollback = "ROLLBACK";
inline constexpr std::string_view kListApps = "LIST_APPS";
inline constexpr std::string_view kGetHistory = "GET_HISTORY";
// La version qui porte ces commandes.
inline constexpr int kDeployProto = 2;
// La taille d'un bloc de SEND_CHUNK (le dernier peut etre plus court).
inline constexpr std::uint32_t kChunkSize = 1024u * 1024u;
// Vrai : `op` est une commande de la version 2.
[[nodiscard]] bool isDeployOp(std::string_view op);

// Les codes d'erreur ("error" d'une reponse ; Client::Outcome::error).
namespace err {
inline constexpr std::string_view kIncompatible = "incompatible";       // aucune version commune
inline constexpr std::string_view kUnknownOp = "op-inconnue";
inline constexpr std::string_view kNotPaired = "non-appaire";           // seulement IDENTIFY et PAIR_*
inline constexpr std::string_view kIdentifyFirst = "identifier-d-abord";
inline constexpr std::string_view kBadHeader = "en-tete";
inline constexpr std::string_view kBadVersion = "proto";                // pas la version choisie
inline constexpr std::string_view kReplay = "rejeu";                    // seq qui ne croit pas
inline constexpr std::string_view kClock = "horloge";                   // ts hors des 5 minutes
inline constexpr std::string_view kTooBig = "trame-trop-grosse";
inline constexpr std::string_view kBadArgs = "arguments";
inline constexpr std::string_view kNoCode = "pas-de-code";              // aucun code, expire, epuise
inline constexpr std::string_view kWrongCode = "code-faux";             // ou quelqu'un s'interpose
inline constexpr std::string_view kPairing = "appairage";               // ordre ou essai deja fait
inline constexpr std::string_view kInternal = "interne";
// 1.29.0 : l'envoi, l'installation, le poste.
inline constexpr std::string_view kNoSpace = "place";                   // pas assez de place sur le disque
inline constexpr std::string_view kTransfer = "transfert";              // envoi inconnu, bloc hors d'ordre, taille differente
inline constexpr std::string_view kBadChunk = "bloc-abime";             // le SHA-256 d'un bloc ne correspond pas
inline constexpr std::string_view kBadPackage = "paquet-abime";         // le SHA-256 du paquet entier ne correspond pas
inline constexpr std::string_view kPackageRefused = "paquet-refuse";    // la verification du paquet (signature, editeur, chemins, contenu)
inline constexpr std::string_view kInstallFailed = "installation";      // extraction ou ecriture impossible
inline constexpr std::string_view kUnknownApp = "application-inconnue";
inline constexpr std::string_view kNoVersion = "version-absente";       // pas cette version, pas de version precedente
inline constexpr std::string_view kBusy = "occupe";                     // une autre operation, un autre poste en marche
inline constexpr std::string_view kNoRuntime = "programme-absent";      // le programme du poste n'est pas a cote de l'agent
inline constexpr std::string_view kStopTimeout = "arret-delai";         // le poste n'a pas quitte dans le delai
inline constexpr std::string_view kStartFailed = "demarrage";           // le poste n'a pas tenu : retour a la precedente
// Cote Studio seulement (jamais envoyes par l'agent).
inline constexpr std::string_view kNetwork = "reseau";                  // connexion, TLS, delai
inline constexpr std::string_view kPinMismatch = "empreinte";           // ce n'est plus la meme machine
inline constexpr std::string_view kBadReply = "reponse";                // reponse illisible
inline constexpr std::string_view kCancelled = "annule";                // l'utilisateur a annule
inline constexpr std::string_view kLocalPackage = "paquet-local";       // le paquet a envoyer ne se lit pas ou est refuse ici
} // namespace err

// Les millisecondes depuis 1970 (UTC).
[[nodiscard]] std::int64_t nowMs();
// Un identifiant d'operation neuf : "op-" et 8 chiffres hexadecimaux tires au hasard.
[[nodiscard]] std::string newOperationId();
// La plus haute version commune, 0 si aucune.
[[nodiscard]] int chooseVersion(const std::vector<int>& ours, const std::vector<int>& theirs);

// Les en-tetes.
[[nodiscard]] core::json::Value requestHeader(std::string_view op, const std::string& id, std::int64_t seq, std::int64_t ts,
                                              int proto = 1);
[[nodiscard]] core::json::Value okHeader(std::string_view op, const std::string& re, std::int64_t seq, int proto = 1);
[[nodiscard]] core::json::Value errorHeader(std::string_view op, const std::string& re, std::int64_t seq, std::string_view code,
                                            const std::string& message, int proto = 1);

// Une demande lue : les champs de son en-tete.
struct Request {
    int proto{0};
    std::string op;
    std::string id;
    std::int64_t seq{0};
    std::int64_t ts{0};
};
// Faux : l'en-tete n'a pas la forme d'une demande (`message` dit quoi).
bool parseRequest(const core::json::Value& header, Request& out, std::string& message);

// Une reponse lue.
struct Reply {
    bool ok{false};
    bool replay{false};
    std::string op, re, error, message;
    std::int64_t seq{0};
};
bool parseReply(const core::json::Value& header, Reply& out, std::string& message);

// seq et ts d'une connexion (cote agent).
class ReplayGuard {
public:
    // Vrai : la demande passe (et seq est retenu). Faux : `code` (err::kReplay, err::kClock) et `message`.
    bool accept(std::int64_t seq, std::int64_t ts, std::int64_t now, std::string& code, std::string& message);
    [[nodiscard]] std::int64_t lastSeq() const noexcept { return last_; }

private:
    std::int64_t last_{0};
};

// Les reponses deja donnees, par pair et identifiant d'operation (cote agent, toutes connexions).
class ResponseCache {
public:
    explicit ResponseCache(std::size_t capacity = 512) : capacity_(capacity) {}
    bool find(const std::string& peer, const std::string& id, Frame& out) const;
    void put(const std::string& peer, const std::string& id, const Frame& response);

private:
    mutable std::mutex mutex_;
    std::size_t capacity_;
    std::map<std::string, Frame> byKey_;
    std::deque<std::string> order_;
};

} // namespace deploy::protocol
