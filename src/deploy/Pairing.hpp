// =============================================================================
//  deploy/Pairing.hpp - 1.28.0 : l'appairage par code a 6 chiffres (CPace)
// -----------------------------------------------------------------------------
//  Le code n'est jamais transmis, meme hache : les deux machines font un echange
//  de cles authentifie par mot de passe (CPace sur ristretto255, libsodium), lie
//  aux deux certificats de la connexion TLS (docs/DEPLOIEMENT.md, section 5) :
//
//    G   = ristretto255_from_hash(SHA-512("XPG-PAIR-v1" | code | sid | empreinte Studio | empreinte agent))
//          sid : 16 octets tires par le Studio ; le code : ses 6 chiffres ASCII
//    Ys  = ys.G (PAIR_START, avec le nom du Studio et sa cle d'editeur)
//    Ya  = ya.G (la reponse, avec la confirmation de l'agent)
//    K   = y.Y(autre) ; un point identite (ou mal forme) est refuse
//    ISK = SHA-512("XPG-PAIR-ISK" | K | Ys | Ya | sid)
//    confirmations : HMAC-SHA-256(ISK, etiquette | conversation), l'agent d'abord
//          ("XPG-PAIR-AGENT", puis "XPG-PAIR-STUDIO" dans PAIR_FINISH) ; la
//          conversation : sid, les deux empreintes, Ys, Ya, nom et cle d'editeur
//          du Studio, nom et identifiant de l'agent (chacun precede de sa longueur)
//
//  Un intermediaire qui presente ses propres certificats obtient un G different
//  de chaque cote : l'echange echoue, sans rien qui permette de deviner le code
//  hors ligne. Un essai par connexion ; le code s'efface apres 3 echecs.
//
//  Ce fichier garde aussi :
//    - le code en cours, cote agent (<dossier>/appairage.code, protege comme la
//      cle : 6 chiffres, valable 5 minutes, une seule fois, 3 essais) ;
//    - les PC appaires, cote agent (<dossier>/appaires.json : empreinte, nom,
//      cle d'editeur Ed25519 en hexadecimal) ;
//    - les machines appairees, cote Studio (MachineStore, <dossier>/machines-appairees.json :
//      empreinte, nom, identifiant, adresse, port).
// =============================================================================
#pragma once

#include "Crypto.hpp"

#include "../core/Json.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace deploy::pairing {

inline constexpr int kCodeDigits = 6;
inline constexpr std::int64_t kCodeValidityMs = 5 * 60 * 1000;
inline constexpr int kMaxAttempts = 3;

using Point = std::array<std::uint8_t, 32>;
using Scalar = std::array<std::uint8_t, 32>;
using Sid = std::array<std::uint8_t, 16>;
using SessionKey = std::array<std::uint8_t, 64>;
using Tag = std::array<std::uint8_t, 32>;

// ---- CPace (les briques, exposees pour les essais) ----
// G : faux si le code n'a pas 6 chiffres.
bool generator(const std::string& code, const Sid& sid, const crypto::Digest& fpStudio, const crypto::Digest& fpAgent, Point& g);
// Un scalaire tire au hasard et y.G.
bool newShare(const Point& g, Scalar& y, Point& share);
// K = y.other, puis ISK. Faux : `other` est mal forme ou donne l'identite (refuse).
bool sessionKey(const Scalar& y, const Point& other, const Point& ys, const Point& ya, const Sid& sid, SessionKey& isk);

// Ce que les deux confirmations couvrent.
struct Conversation {
    Sid sid{};
    crypto::Digest fpStudio{}, fpAgent{};
    Point ys{}, ya{};
    std::string studioName, studioKeyHex, agentName, agentId;
};
[[nodiscard]] Tag confirmation(const SessionKey& isk, const Conversation& c, bool agentSide);

// Le cote Studio : PAIR_START, puis la reponse de l'agent, puis PAIR_FINISH.
class StudioPairing {
public:
    // fpAgent : l'empreinte vue dans CETTE poignee de main TLS (jamais une empreinte annoncee).
    StudioPairing(std::string code, const crypto::Digest& fpStudio, const crypto::Digest& fpAgent, std::string studioName,
                  std::string publisherKeyHex);
    ~StudioPairing();
    // Les donnees de PAIR_START : {"sid","Ys","name","publisherKey"}.
    bool start(core::json::Value& pairStart, std::string* why);
    // La reponse de l'agent ({"Ya","confirm","name","id"}) : vrai si sa confirmation est bonne,
    // et `pairFinish` porte la notre ({"confirm"}). Faux : code faux, ou quelqu'un s'interpose.
    bool answer(const core::json::Value& reply, core::json::Value& pairFinish, std::string* why);
    [[nodiscard]] const std::string& agentName() const noexcept { return conv_.agentName; }
    [[nodiscard]] const std::string& agentId() const noexcept { return conv_.agentId; }

private:
    std::string code_;
    Conversation conv_;
    Scalar y_{};
    Point g_{};
    bool started_{false};
};

// Le cote agent.
class AgentPairing {
public:
    AgentPairing(std::string code, const crypto::Digest& fpStudio, const crypto::Digest& fpAgent, std::string agentName,
                 std::string agentId);
    ~AgentPairing();
    // PAIR_START recu : `reply` porte {"Ya","confirm","name","id"}. Faux : `why` (demande mal formee,
    // point refuse).
    bool start(const core::json::Value& pairStart, core::json::Value& reply, std::string* why);
    // PAIR_FINISH recu : vrai si la confirmation du Studio est bonne.
    bool finish(const core::json::Value& pairFinish, std::string* why);
    [[nodiscard]] const std::string& studioName() const noexcept { return conv_.studioName; }
    [[nodiscard]] const std::string& studioKeyHex() const noexcept { return conv_.studioKeyHex; }

private:
    std::string code_;
    Conversation conv_;
    SessionKey isk_{};
    bool started_{false};
};

// ---- le code en cours (cote agent) ----
struct CodeState {
    std::string code;
    std::int64_t expiresAt{0};   // ms depuis 1970
    int attempts{0};
};
enum class CodeLoad { Ok, None, Expired, Exhausted, Error };
[[nodiscard]] std::string codePath(const std::string& agentDir);
// Un code neuf (remplace le precedent), garde protege (la machine). Faux : `why`.
bool createCode(const std::string& agentDir, CodeState& out, std::string* why, std::int64_t validityMs = kCodeValidityMs);
CodeLoad loadCode(const std::string& agentDir, CodeState& out, std::string* why);
bool saveCode(const std::string& agentDir, const CodeState& state, std::string* why);
void eraseCode(const std::string& agentDir);

// ---- les PC appaires (cote agent) ----
struct PairedStudio {
    std::string fingerprint;    // SHA-256 du certificat, hexadecimal
    std::string name;           // le nom du PC du Studio
    std::string publisherKey;   // la cle d'editeur Ed25519 (hexadecimal) : elle signera les paquets
    std::int64_t pairedAt{0};
};

class StudioStore {
public:
    explicit StudioStore(std::string agentDir);
    bool load(std::string* why);
    [[nodiscard]] bool contains(const std::string& fingerprintHex) const;
    [[nodiscard]] bool find(const std::string& fingerprintHex, PairedStudio& out) const;
    [[nodiscard]] std::vector<PairedStudio> all() const;
    [[nodiscard]] std::size_t size() const;
    bool upsert(const PairedStudio& s, std::string* why);   // ecrit le fichier
    bool remove(const std::string& fingerprintHex, std::string* why);
    [[nodiscard]] std::string path() const;

private:
    bool saveLocked(std::string* why) const;
    std::string dir_;
    mutable std::mutex mutex_;
    std::vector<PairedStudio> items_;
};

} // namespace deploy::pairing

namespace deploy {

// ---- les machines appairees (cote Studio) ----
struct PairedMachine {
    std::string fingerprint;   // l'empreinte epinglee (SHA-256 du certificat de l'agent, hexadecimal)
    std::string name;          // "PC-ATELIER-1"
    std::string id;            // "M-7F21"
    std::string address;       // l'adresse utilisee a l'appairage
    int port{47861};
    std::int64_t pairedAt{0};  // ms depuis 1970
};

// Le fichier <dossier>/machines-appairees.json. Sur entre fils (un verrou interne) ;
// chaque modification ecrit le fichier.
class MachineStore {
public:
    explicit MachineStore(std::string dir);
    // Relire le fichier (absent : aucune machine, vrai).
    bool load(std::string* why);
    [[nodiscard]] std::vector<PairedMachine> machines() const;
    [[nodiscard]] bool find(const std::string& fingerprintHex, PairedMachine& out) const;
    // Ajoute ou remplace (meme empreinte), puis ecrit.
    bool upsert(const PairedMachine& m, std::string* why);
    // Retire (absente : vrai), puis ecrit.
    bool remove(const std::string& fingerprintHex, std::string* why);
    [[nodiscard]] std::string path() const;

private:
    bool saveLocked(std::string* why) const;
    std::string dir_;
    mutable std::mutex mutex_;
    std::vector<PairedMachine> items_;
};

} // namespace deploy
