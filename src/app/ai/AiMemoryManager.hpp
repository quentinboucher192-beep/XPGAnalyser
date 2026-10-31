// =============================================================================
//  app/ai/AiMemoryManager.hpp - 1.21.0 : LA MEMOIRE PERSISTANTE, autour du registre
// -----------------------------------------------------------------------------
//  Le registre des connaissances (AiKnowledge.hpp) garde les souvenirs ; ici, ce
//  qui les entoure, sans ecran :
//
//    - L'IDENTITE DES PROJETS (projets.json) : un identifiant fiable par projet,
//      jamais son nom seul. Un projet est reconnu par son dossier (le chemin
//      normalise) ; deplace (son ancien dossier n'existe plus, meme nom, meme date
//      de creation), il garde son identifiant ; copie ailleurs, il en recoit un
//      autre (les deux evoluent a part). Deux projets ne partagent jamais leurs
//      souvenirs ; deux projets de meme nom le disent (ambiguous) ;
//    - LES SAUVEGARDES (sauvegardes/memoire-AAAAMMJJ-HHMMSS-n/) : toute la memoire
//      (connaissances/, memoire.json, projets.json) copiee avant une migration et
//      avant une restauration ; restaurer remet exactement cet etat ; les dix plus
//      recentes sont gardees ;
//    - L'ECRIVAIN << ASSISTANT >> : l'IA PROPOSE de retenir (un souvenir
//      provisoire, qui ne part pas avec les demandes), l'utilisateur VALIDE
//      (confirme ou hypothese) ou REFUSE ;
//    - LA RECHERCHE dans les souvenirs (mots, portee, statut, projet) ;
//    - LES OUTILS du modele (le fournisseur les branche) : proposer_souvenir,
//      chercher_souvenirs - leur definition JSON (format Anthropic : name,
//      description, input_schema) et leur reponse, en texte, pour le modele.
//
//  Rien ici ne pretend que le modele distant apprend : kNotRetrained le dit.
// =============================================================================
#pragma once

#include "AiKnowledge.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::memory {

// ---------------------------------------------------------------- les projets ---
struct ProjectIdentity {
    std::string id;          // "prj-1a2b3c4d5e6f7a8b" ; vide : pas de dossier (un projet jamais enregistre)
    std::string name;        // le nom (celui du dossier)
    std::string path;        // le dossier, normalise (la cle de reconnaissance)
    std::string created;     // la date de creation du projet (hmi::Config::created) quand elle est connue
    std::string seen;        // la derniere fois qu'il a ete ouvert (ISO)
    bool        ambiguous{false};   // un autre projet connu porte le meme nom
    bool        relinked{false};    // retrouve apres un deplacement (son identifiant garde)
    bool        isNew{false};       // vu pour la premiere fois
};
// Le chemin normalise d'un dossier de projet (absolu, separateurs '/', sans '/' final ; sous Windows en minuscules).
[[nodiscard]] std::string normalizedProjectPath(std::string_view path);
// RECONNAIT le projet (son dossier) : son identifiant connu, ou un nouveau (ecrit dans projets.json). `created` aide a
// reconnaitre un projet deplace. Sans racine IA : un identifiant tire du chemin seul (rien d'ecrit). Un chemin vide :
// une identite sans identifiant (ses souvenirs de projet ne peuvent pas etre lies).
[[nodiscard]] ProjectIdentity identifyProject(std::string_view name, std::string_view path, std::string_view created = {});
// Les projets connus (projets.json), le plus recemment vu d'abord ; `ambiguous` rempli.
[[nodiscard]] std::vector<ProjectIdentity> knownProjects();
// Le nom d'un projet connu par son identifiant (vide : inconnu).
[[nodiscard]] std::string projectNameOf(std::string_view id);
// Le contexte d'une demande faite dans ce projet (et cette conversation).
[[nodiscard]] Context contextFor(const ProjectIdentity& p, std::string conversationId = {});

// --------------------------------------------------------------- les sauvegardes ---
struct Backup {
    std::string    name;        // "memoire-20261031-174512-1"
    std::string    folder;      // son dossier (UTF-8)
    std::string    date;        // ISO
    std::string    reason;      // "migration du format 1 au format 2", "avant restauration", "a la demande"
    std::size_t    files{0};
    std::uintmax_t bytes{0};
};
inline constexpr std::size_t kKeepBackups = 10;
// Copie toute la memoire (connaissances/, memoire.json, projets.json) ; `name` : celui de la sauvegarde. Faux et `why`.
bool backupMemory(std::string_view reason, std::string* name = nullptr, std::string* why = nullptr);
// Les sauvegardes, la plus recente d'abord.
[[nodiscard]] std::vector<Backup> listBackups();
// RESTAURE une sauvegarde : l'etat actuel est sauvegarde d'abord ("avant restauration"), puis connaissances/,
// memoire.json et projets.json redeviennent ceux de la sauvegarde. Le registre est a recharger (load).
bool restoreBackup(std::string_view name, std::string* why = nullptr);

// --------------------------------------------------------- l'ecrivain << assistant >> ---
struct MemoryProposal {
    std::string content;          // ce qu'il faut retenir (une ou quelques phrases)
    std::string name;             // un titre court (vide : la premiere ligne)
    std::string scope;            // utilisateur | projet | conversation | technique (vide : technique)
    std::string subject;          // le sujet (les contradictions) ; vide : le nom
    std::string category;         // une categorie des connaissances (vide : selon la portee)
    double      confidence{0.6};
    std::string conversation, message;   // ou l'Assistant l'a propose
};
// L'IA PROPOSE : un souvenir PROVISOIRE (statut "provisoire", source "assistant"), lie au projet du contexte pour une
// portee projet. Il ne part pas avec les demandes avant d'etre valide. Un doublon n'est pas ajoute ; une contradiction
// l'est, les deux marquees en conflit (Registry::remember).
RememberResult proposeMemory(Registry& reg, const MemoryProposal& p, const Context& ctx);
// L'utilisateur VALIDE une proposition : "confirme" (par defaut) ou "hypothese".
bool validateMemory(Registry& reg, std::string_view id, std::string_view status = "confirme", std::string* why = nullptr);
// L'utilisateur REFUSE une proposition : elle est supprimee.
bool rejectMemory(Registry& reg, std::string_view id, std::string* why = nullptr);

// --------------------------------------------------------------- la recherche ---
struct MemoryFilter {
    std::string scope;            // vide : toutes
    std::string status;           // vide : tous
    std::string projectId;        // vide : tous ; "*" : ceux de tous les projets (sans projet)
    bool        hereOnly{false};  // seulement ce qui partirait avec une demande dans `ctx`
    bool        pinnedOnly{false};
    bool        conflictsOnly{false};
    bool        builtIns{false};  // les connaissances integrees du logiciel aussi
};
struct MemoryHit {
    const Knowledge* k{nullptr};
    Here             here{Here::Active};
    int              score{0};
    std::string      excerpt;
};
// Les souvenirs qui repondent (tous les mots, sans casse ni accents, dans le nom, le contenu, la description, le sujet,
// le projet), les epingles et les plus pertinents d'abord. Une requete vide : tout ce que le filtre garde.
[[nodiscard]] std::vector<MemoryHit> searchMemory(const Registry& reg, std::string_view query, const Context& ctx,
                                                  const MemoryFilter& filter = {});

// ------------------------------------------------------------------- les outils ---
inline constexpr std::string_view kRememberTool = "proposer_souvenir";
inline constexpr std::string_view kSearchMemoryTool = "chercher_souvenirs";
// Les definitions (format Anthropic : {"name", "description", "input_schema"}) ; textparts::withReadTool montre comment
// les ajouter au tableau des outils.
[[nodiscard]] std::string rememberToolJson();
[[nodiscard]] std::string searchMemoryToolJson();
// Les reponses aux appels (l'entree JSON brute) : le texte du resultat d'outil pour le modele. `conversation` et
// `message` : ou l'appel a ete fait (la provenance de la proposition).
[[nodiscard]] std::string runRememberTool(Registry& reg, std::string_view inputJson, const Context& ctx, std::string_view conversation,
                                          std::string_view message, bool* isError = nullptr);
[[nodiscard]] std::string runSearchMemoryTool(const Registry& reg, std::string_view inputJson, const Context& ctx, bool* isError = nullptr);

} // namespace app::ai::memory
