// =============================================================================
//  app/ai/AiMemory.hpp - le centre "Memoire et apprentissage de l'IA" : le moteur
// -----------------------------------------------------------------------------
//  SANS ECRAN, ET REEL : tout ce que montre la fenetre (AiMemoryDialog) se
//  calcule ici, sur les fichiers presents dans IA/ (AiPaths.hpp, le contrat avec
//  l'Assistant), sur le disque qui les porte et sur le processus. Rien n'est
//  simule ; un dossier vide donne des zeros, et c'est ce qu'il faut dire.
//
//    - LA MESURE (measure) : chaque categorie du dossier - conversations, pieces
//      jointes, connaissances, index, journaux, le journal d'activite, les
//      parametres, les six categories du cache, le reste - en octets et en
//      fichiers, leur somme, l'espace libre du disque (std::filesystem::space),
//      l'emplacement, la date du dernier nettoyage ;
//    - LA MEMOIRE VIVE (readRam) : celle du processus (actuelle, et son pic) et
//      celle du PC quand le systeme la donne ; les traitements IA locaux : aucun
//      aujourd'hui (zero, dit tel quel). Le modele distant n'occupe RIEN sur ce
//      PC : il n'est jamais compte ici ;
//    - LES CONVERSATIONS (loadConversations) : lues dans conversations/*.json
//      (le format du contrat), leurs messages, leurs pieces jointes ;
//    - L'ACTIVITE (readActivity, appendActivity) : activite.jsonl, une ligne par
//      evenement (les types du contrat) ; chaque operation du centre y ecrit la sienne ;
//    - L'ETAT DU CENTRE (memoire.json, a la racine de IA/) : la date du dernier
//      nettoyage, les connaissances integrees desactivees ;
//    - LE NETTOYAGE : vider des categories du cache (cache/ seulement, jamais
//      ailleurs) ; supprimer des donnees persistantes (conversations, pieces
//      jointes, connaissances ajoutees, journaux...) SEULEMENT confirme ;
//    - LA RECHERCHE dans la memoire : titres et messages des conversations,
//      connaissances, references aux pieces jointes (ui::SearchQuery, comme
//      toutes les recherches du logiciel).
//
//  Les chemins sont en UTF-8 (AiPaths.hpp) ; la racine vide : rien n'existe.
// =============================================================================
#pragma once

#include "AiMemoryJson.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::memory {

// ---------------------------------------------------------------- les dates ---
// L'heure locale ISO du contrat : "2026-10-30T17:35:12".
[[nodiscard]] std::string nowIso();
// "2026-10-30T17:35:12" -> "30/10/2026 17:35" (vide : vide ; illisible : tel quel).
[[nodiscard]] std::string displayDate(std::string_view iso);
// 1 234 567 octets -> "1,2 Mo" (app::dossiers::tailleLisible).
[[nodiscard]] std::string sizeText(std::uintmax_t bytes);

// ------------------------------------------------- les categories du disque ---
enum class Part : std::uint8_t {
    Conversations, Attachments, Knowledge, Index, Logs, Activity, Settings,
    CacheRequests, CacheProcessing, CacheImages, CacheDocuments, CachePreviews, CacheDownloads, CacheOther,
    Other,
    Count
};
inline constexpr std::size_t kPartCount = static_cast<std::size_t>(Part::Count);

struct PartInfo {
    Part             part;
    std::string_view key;        // "conversations", "cache.images" : les scripts, les essais
    std::string_view label;      // ce que lit l'utilisateur
    std::string_view where;      // le dossier ou le fichier, sous IA/
    bool             cache;      // vide par "Vider le cache"
    bool             persistent; // ne part qu'a la demande expresse (Supprimer les donnees persistantes)
};
[[nodiscard]] const PartInfo& partInfo(Part p) noexcept;
[[nodiscard]] const PartInfo* partByKey(std::string_view key) noexcept;

struct Usage {
    std::uintmax_t bytes{0};
    std::size_t    files{0};
    Usage& operator+=(const Usage& o) noexcept {
        bytes += o.bytes;
        files += o.files;
        return *this;
    }
};

struct Disk {
    bool           known{false};
    std::uintmax_t capacity{0}, free{0}, available{0};   // available : ce que l'utilisateur peut ecrire
};

struct Measure {
    std::string                    root;          // l'emplacement (UTF-8) ; vide : pas de racine
    bool                           exists{false}; // le dossier est la
    std::array<Usage, kPartCount>  parts{};
    Usage                          total;
    Disk                           disk;
    std::string                    lastCleaning;  // ISO ; vide : jamais
    std::vector<std::string>       problems;      // ce qui ne s'est pas lu

    [[nodiscard]] const Usage& of(Part p) const noexcept { return parts[static_cast<std::size_t>(p)]; }
    [[nodiscard]] Usage cache() const noexcept;       // les sept parts du cache
    [[nodiscard]] Usage technical() const noexcept;   // index, journaux, activite, parametres, le reste
};
// Mesure IA/ sur le disque, maintenant (les fichiers .tmp et .bak d'une ecriture comptent : ils y sont).
[[nodiscard]] Measure measure();

// ------------------------------------------------------------- la memoire vive ---
struct Ram {
    bool          processKnown{false};
    std::uint64_t process{0};       // la memoire de travail du processus (Windows : WorkingSetSize ; Linux : VmRSS)
    std::uint64_t peak{0};          // son pic (PeakWorkingSetSize ; VmHWM)
    bool          systemKnown{false};
    std::uint64_t total{0};         // la RAM du PC
    std::uint64_t available{0};     // ce qui reste disponible
    std::uint64_t localAi{0};       // les traitements IA locaux : aucun aujourd'hui
    std::string   by;               // qui les mesure, en mots de l'utilisateur ("Windows", "Linux") : ce que la fenetre dit
    std::string   source;           // le detail technique ("GetProcessMemoryInfo...", "/proc/self/status...") : l'infobulle seule
};
[[nodiscard]] Ram readRam();

// ----------------------------------------------------------- les conversations ---
struct Attachment {
    std::string    id, name, type, file;    // file : relatif a IA/ ("pieces-jointes/p1/plan.pdf")
    std::uintmax_t declared{0};             // la taille ecrite dans le message
    std::uintmax_t bytes{0};                // la taille sur le disque (0 : absente)
    bool           present{false};
    std::string    conversation, conversationTitle;
};
struct Message {
    std::string id, role, date, text;
    std::size_t attachments{0};
};
struct Conversation {
    std::string             id, title, created, modified;
    std::string             file;           // le fichier (UTF-8)
    std::string             project, projectPath, view;
    bool                    demo{false};
    bool                    readable{true};
    std::string             why;            // illisible : pourquoi
    std::uintmax_t          bytes{0};
    std::vector<Message>    messages;
    std::vector<Attachment> attachments;
};
// Les conversations de conversations/*.json, de la plus recemment modifiee a la plus ancienne.
[[nodiscard]] std::vector<Conversation> loadConversations();

// Un dossier de pieces-jointes/<id>/ : ses fichiers, et la conversation qui le cite (vide : aucune).
struct AttachmentFolder {
    std::string    id;                // le nom du dossier
    std::string    folder;            // son chemin (UTF-8)
    Usage          usage;
    std::string    names;             // les fichiers qu'il porte ("plan.pdf, photo.png")
    std::string    conversation, conversationTitle;
};
[[nodiscard]] std::vector<AttachmentFolder> attachmentFolders(const std::vector<Conversation>& conversations);

// -------------------------------------------------------------------- l'activite ---
struct Activity {
    std::string date, type, text;
};
// Les types du contrat, dans l'ordre ou les filtres les montrent.
inline constexpr std::string_view kActivityTypes[] = {"conversation", "connaissance", "fichier", "regle", "nettoyage", "suppression"};
[[nodiscard]] std::string activityTypeLabel(std::string_view type);   // "connaissance" -> "Connaissance"
// Le journal, du plus ancien au plus recent (une ligne illisible est sautee).
[[nodiscard]] std::vector<Activity> readActivity();
// Une ligne de plus (la date : maintenant). Faux : pas de racine, ou ecriture impossible.
bool appendActivity(std::string_view type, std::string_view text);

// ----------------------------------------------------------- l'etat du centre ---
//  memoire.json, a la racine de IA/ (a cote de parametres.json) : ce que le
//  centre garde pour lui. Un champ inconnu se garde.
[[nodiscard]] std::string stateFile();
struct State {
    std::string              lastCleaning;      // ISO ; vide : jamais
    std::vector<std::string> builtInOff;        // les connaissances integrees desactivees (leurs id)
    Json                     raw;               // le fichier tel que lu (les champs inconnus)
};
[[nodiscard]] State readState();
bool writeState(const State& s);

// ----------------------------------------------------------------- le nettoyage ---
struct CacheLine {
    Part        part;
    std::string folder;         // le dossier (UTF-8)
    Usage       usage;
    std::string consequence;    // ce que ca change de le vider
};
// Le detail AVANT de vider : les six categories du cache (et "autres", s'il y en a).
[[nodiscard]] std::vector<CacheLine> cachePlan(const Measure& m);
[[nodiscard]] std::string_view cacheConsequence(Part p);

struct CleanResult {
    Usage       freed;
    std::size_t failures{0};
    std::string firstError;
    [[nodiscard]] bool ok() const noexcept { return failures == 0; }
};
// Vide ces categories du cache (une part qui n'est pas du cache est ignoree) ; garde la date du
// nettoyage (memoire.json) et ecrit l'activite. RIEN hors de cache/ n'est touche.
CleanResult clearCache(const std::vector<Part>& parts);

// Les donnees persistantes (supprimees seulement a la demande expresse).
enum class Persistent : std::uint8_t { Conversations, Attachments, Knowledge, Activity, Index, Logs };
inline constexpr Persistent kPersistentAll[] = {Persistent::Conversations, Persistent::Attachments, Persistent::Knowledge,
                                                Persistent::Activity, Persistent::Index, Persistent::Logs};
struct PersistentLine {
    Persistent  what;
    Part        part;
    std::string label;
    Usage       usage;
    std::string consequence;
};
[[nodiscard]] std::vector<PersistentLine> persistentPlan(const Measure& m);
[[nodiscard]] std::string_view persistentKey(Persistent p);     // "conversations", "pieces-jointes"...
[[nodiscard]] Part             persistentPart(Persistent p);    // sa categorie du disque
// Supprime ces categories (le contenu de leur dossier ; le journal d'activite : son fichier). REFUSE sans
// `confirmed` (rien n'est touche, failures = 1). Les connaissances integrees au logiciel ne sont jamais
// touchees (elles ne sont pas des fichiers) ; les parametres et les projets non plus.
CleanResult deletePersistent(const std::vector<Persistent>& what, bool confirmed);

// Une conversation (son fichier) ; ses pieces jointes restent (elles se suppriment a part).
bool deleteConversation(std::string_view id, std::string* why = nullptr);
// Un dossier de pieces-jointes/<id>/ et ce qu'il porte.
bool deleteAttachmentFolder(std::string_view id, std::string* why = nullptr);

// ------------------------------------------------------------------ la recherche ---
struct Knowledge;   // AiKnowledge.hpp
struct Hit {
    enum class Kind : std::uint8_t { Conversation, Message, Knowledge, Attachment };
    Kind        kind{Kind::Conversation};
    std::string title;       // la conversation, la connaissance, le fichier
    std::string excerpt;     // ce qui a ete trouve, autour du mot
    std::string where;       // "Conversation", "Message de l'utilisateur", "Connaissance"...
    std::string ref;         // l'id de la conversation, de la connaissance ; le fichier joint
};
[[nodiscard]] std::vector<Hit> search(std::string_view query, const std::vector<Conversation>& conversations,
                                      const std::vector<Knowledge>& knowledge);

// ------------------------------------------------------------------- utilitaires ---
// Un fichier entier ; faux : absent ou illisible.
bool readText(const std::string& utf8Path, std::string& out);
// Un fichier ecrit d'un bloc (core::writeFileAtomic, sans .bak) ; cree les dossiers parents.
bool writeText(const std::string& utf8Path, std::string_view content, std::string* why = nullptr);

} // namespace app::ai::memory
