// =============================================================================
//  licensing/Admins.hpp - 1.44.0 : PLUSIEURS ADMINS (docs/ADMINS.md)
// -----------------------------------------------------------------------------
//  L'admin+ a la cle de l'editeur ; chaque admin a SA cle (nee sur son PC),
//  nommee dans LA LISTE DES ADMINS, un document signe par la cle de l'editeur
//  (genre << admins >>), avec ses DROITS (13 fonctions). Ce que signe un admin
//  vaut partout ou la liste est connue : les Studios, le service, l'Admin.
//
//  Ici, sans reseau : les droits, la liste (lire, ecrire, verifier un document
//  signe par un admin), l'empreinte d'une cle (lue au telephone), les profils
//  de droits de l'admin+, le journal des admins (l'entree signee, la ligne
//  chainee par le service), et les documents de la 1.44.0 signes par la cle de
//  l'editeur : le manifeste d'un fichier, une version, un message a un PC, le
//  jeton d'admin (signe par l'admin : telecharger l'installateur de l'Admin).
// =============================================================================
#pragma once

#include "SignedFile.hpp"
#include "Time.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace licensing::admins {

// ---- les droits ----
struct RightInfo {
    const char* id;          // "licence_creer"
    const char* label;       // << Creer et signer une licence >>
    const char* group;       // "licences", "offres", "clients"
    const char* checkedBy;   // << Studios et service >>, << service >>, << l'Admin >>
};
// Les 13, dans l'ordre de la page Droits.
[[nodiscard]] const std::vector<RightInfo>& rights();
[[nodiscard]] bool isRight(std::string_view id) noexcept;
[[nodiscard]] std::string rightLabel(std::string_view id);
[[nodiscard]] std::string groupLabel(std::string_view group);   // << Licences >>, << Offres et options >>...
// Les droits dont l'un suffit pour signer un document de ce genre ; vide : la cle de l'editeur seulement.
[[nodiscard]] std::vector<std::string> rightsForKind(DocKind kind);
// Tous les droits (l'admin+, le profil << Tous les droits >>).
[[nodiscard]] std::vector<std::string> allRights();
// Les droits tries dans l'ordre de rights(), sans doublon ni inconnu.
[[nodiscard]] std::vector<std::string> normalizeRights(const std::vector<std::string>& in);

// ---- la liste des admins (genre << admins >>) ----
struct Admin {
    std::string              id;        // keyIdOf(cle) : "xpg-a41c0e7b92d45f18"
    std::string              key;       // la cle publique, 64 hexadecimaux
    std::string              name;      // << Lea >>
    std::string              pc;        // << PC-COMMERCIAL-02 >> (le nom du PC, pour l'affichage)
    std::string              machine;   // l'identifiant de la machine (32 hexadecimaux) ; vide : inconnu
    Seconds                  since{0};
    std::string              profile;   // le profil d'origine (affichage) ; vide : aucun
    std::vector<std::string> rights;
    Seconds                  retired{0};   // 0 : actif
    std::vector<std::string> kept;         // les empreintes (SHA-256 hexa) des documents qui restent valides
    [[nodiscard]] bool active() const noexcept { return retired == 0; }
    [[nodiscard]] bool has(std::string_view right) const noexcept;
};

struct AdminList {
    int                number{0};   // le numero (anti-retour)
    Seconds            issued{0};
    std::vector<Admin> admins;
    [[nodiscard]] const Admin* byKeyId(std::string_view keyId) const noexcept;
    [[nodiscard]] const Admin* byKey(std::string_view keyHex) const noexcept;
};

inline constexpr std::size_t kMaxAdmins = 32;
inline constexpr std::size_t kMaxKept = 512;

[[nodiscard]] std::string writeAdminList(const AdminList& list);
bool parseAdminList(std::string_view json, AdminList& out, std::string* why);

// L'empreinte d'une cle, lue a voix haute : les 24 premiers hexadecimaux (majuscules) du SHA-256 de la cle publique,
// en 6 groupes de 4 : "B7E2 48A9 1D3C 90F6 5A2E C471". Vide : pas une cle.
[[nodiscard]] std::string fingerprint(std::string_view keyHex);
// 1.49.0 : L'INVITATION PAR CODES (docs/ADMINS.md, par. 12). Le code de confirmation d'une cle (6 chiffres, l'Admin
// classique l'affiche, l'admin+ le tape) : n = uint64 big-endian des 8 premiers octets de
// SHA-256("xpg-confirmation-1" || cle publique brute, 32 octets) ; code = n % 1000000, sur 6 chiffres, << 123 456 >>.
// L'essai du service (service/test/invitations.test.js) refait le meme calcul : une valeur connue est verifiee des deux cotes. Vide : pas
// une cle.
[[nodiscard]] std::string confirmationCode(std::string_view keyHex);
// Ce qu'a tape l'admin+ (<< 123 456 >>, << 123456 >>, des espaces) est-il le code de cette cle ? Faux : autre chose que
// 6 chiffres, ou d'autres chiffres.
[[nodiscard]] bool confirmationMatches(std::string_view keyHex, std::string_view typed);
// Un code d'invitation tape (ou lu d'invitation.txt) remis en forme : << INV-XXXX-XXXX >> (8 caracteres de l'alphabet
// Crockford sans I L O U ; minuscules, espaces, tirets toleres ; O lu 0, I et L lus 1). Vide : pas un code.
[[nodiscard]] std::string normalizeInvitationCode(std::string_view typed);
// L'empreinte d'un document signe (SHA-256 hexa du JSON, octet pour octet) : ce que garde un admin retire.
[[nodiscard]] std::string documentHash(std::string_view document);

// Un document signe par une cle qui n'est pas celle de l'editeur : vrai si un admin de la liste l'a signe, que la
// signature est bonne et qu'il en avait le droit (retire : seulement un document garde). `who` : l'admin.
bool verifyAdminBlock(const AdminList& list, const SignedBlock& block, std::string* why, const Admin** who = nullptr);

// ---- les profils (chez l'admin+) ----
struct Profile {
    std::string              name;
    std::vector<std::string> rights;
    bool                     builtin{false};   // << Tous les droits >> : ne se modifie pas
    Seconds                  changed{0};
};
inline constexpr const char* kAllRightsProfile = "Tous les droits";
[[nodiscard]] std::vector<Profile> defaultProfiles();
[[nodiscard]] std::string writeProfiles(const std::vector<Profile>& profiles);
// Le profil de depart est toujours la, en tete.
bool parseProfiles(std::string_view json, std::vector<Profile>& out, std::string* why);

// ---- le journal des admins ----
inline constexpr std::string_view kJournalContext = "XPG-JOURNAL-1\n";
enum class Level : std::uint8_t { Info, Medium, High, Critical };
[[nodiscard]] const char* levelKey(Level) noexcept;     // "info", "moyenne", "haute", "critique"
[[nodiscard]] std::string levelLabel(Level);            // << Info >>, << Moyenne >>, << Haute >>, << Critique >>
bool parseLevel(std::string_view key, Level& out) noexcept;

struct ActionType {
    const char* id;
    const char* label;   // << Signer une licence >>
    const char* group;   // "licences", "offres", "clients", "admins"
    Level       level;   // la criticite de depart
    bool        notify;  // << Me prevenir tout de suite >> de depart
    bool        service; // ecrite par le service (pas par un admin)
};
[[nodiscard]] const std::vector<ActionType>& actionTypes();
[[nodiscard]] const ActionType* actionType(std::string_view id) noexcept;

struct JournalEntry {
    Seconds     when{0};
    std::string action;
    std::string object;     // << XPG-2026-00412 >>
    std::string summary;    // << a signe la licence XPG-2026-00412 >>
    std::string detail;     // un objet JSON (texte) : client, offre... ; vide : {}
    std::string before;     // un objet JSON (texte) : l'avant (ce qui change) ; vide : {}
    std::string after;      // l'apres
    std::string pc, machine, software;
    std::string nonce;      // 32 hexadecimaux
};
[[nodiscard]] std::string writeJournalEntry(const JournalEntry& e);
bool parseJournalEntry(std::string_view json, JournalEntry& out, std::string* why);
// Le message signe pour une entree : kJournalContext + entree.
[[nodiscard]] std::string journalMessage(std::string_view entry);

// Une ligne de la chaine, telle que le service la garde (son texte, octet pour octet).
struct JournalLine {
    std::uint64_t number{0};
    std::string   previous;     // l'empreinte de la ligne d'avant ("" : la premiere)
    Seconds       received{0};
    std::string   key;          // qui (64 hexadecimaux : un admin, l'editeur, ou le service)
    std::string   entry;        // le JSON de l'entree, tel que signe
    std::string   signature;    // 128 hexadecimaux
    std::string   text;         // la ligne entiere
    std::string   hash;         // SHA-256 hexa de `text`
    JournalEntry  parsed;
};
bool parseJournalLine(std::string_view text, JournalLine& out, std::string* why);
// La signature de l'entree est-elle celle de line.key ?
[[nodiscard]] bool verifyJournalSignature(const JournalLine& line) noexcept;

// ---- la 1.44.0 : fichiers, versions, messages, jeton d'admin ----
struct FileManifest {
    std::string   id;        // 32 hexadecimaux
    std::string   name;      // << XPG-License-Admin-Setup-1.44.0.exe >>
    std::uint64_t size{0};
    std::string   sha256;    // 64 hexadecimaux
    std::string   kind;      // "admin", "studios", "autre" ; 1.49.0 : "admin_plus" (l'Admin+ : la cle de l'editeur seule le telecharge)
    std::string   version;   // << 1.44.0 >> (facultatif)
    std::string   access;    // "public", "admins", "jeton"
    std::uint64_t chunk{1048576};
    Seconds       issued{0};
    [[nodiscard]] std::uint64_t chunks() const noexcept { return chunk ? (size + chunk - 1) / chunk : 0; }
};
inline constexpr std::uint64_t kMaxFileBytes = 200ull * 1024ull * 1024ull;
[[nodiscard]] std::string writeFileManifest(const FileManifest& m);
bool parseFileManifest(std::string_view json, FileManifest& out, std::string* why);
[[nodiscard]] bool safeFileName(std::string_view name) noexcept;   // [A-Za-z0-9._ -]{1,120}, pas ".." ni de point en tete

struct VersionFile {
    std::string   id, name, sha256, kind, audience;   // kind : "studios" | "admin" | "admin_plus" (1.49.0) ; audience : "tous" | "admins" | "admin_plus"
    std::uint64_t size{0};
};
struct VersionManifest {
    std::string              version;   // << 1.44.1 >>
    Seconds                  published{0};
    bool                     important{false};
    std::vector<std::string> notes;
    std::vector<VersionFile> files;
    [[nodiscard]] const VersionFile* file(std::string_view kind) const noexcept;
};
[[nodiscard]] std::string writeVersion(const VersionManifest& v);
bool parseVersion(std::string_view json, VersionManifest& out, std::string* why);
// "1.44.1" -> {1,44,1} ; faux : pas une version X.Y.Z.
bool parseSemver(std::string_view text, int& major, int& minor, int& patch) noexcept;
// <0, 0, >0 ; une version illisible est plus petite que tout.
[[nodiscard]] int compareSemver(std::string_view a, std::string_view b) noexcept;

struct MessageFile {
    std::string   id, name, sha256, version, token;
    std::uint64_t size{0};
};
// Le message, en clair (le bloc porte `scelle` : ce contenu scelle pour la cle du PC).
struct MessageContent {
    std::string              text;
    std::vector<MessageFile> files;
    std::string              action;   // "admin" : l'installateur de l'Admin pour devenir admin ; vide : rien
};
struct Message {
    std::string device;   // l'identifiant du PC (deviceId, 32 hexadecimaux)
    std::string title, from;
    Seconds     issued{0};
    std::string sealed;   // base64
};
[[nodiscard]] std::string writeMessage(const Message& m);
bool parseMessage(std::string_view json, Message& out, std::string* why);
[[nodiscard]] std::string writeMessageContent(const MessageContent& c);
bool parseMessageContent(std::string_view json, MessageContent& out, std::string* why);

struct AdminToken {
    std::string key;       // la cle de l'admin (64 hexadecimaux) : celle qui signe
    std::string machine;   // le PC (32 hexadecimaux)
    Seconds     expires{0};
    std::string version;   // la version de l'Admin installe sur ce PC (<< 1.44.0 >>) : le Studio dit s'il est a jour
    std::string name;      // le nom de l'admin (<< Lea >>)
};
[[nodiscard]] std::string writeAdminToken(const AdminToken& t);
bool parseAdminToken(std::string_view json, AdminToken& out, std::string* why);

} // namespace licensing::admins
