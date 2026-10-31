// =============================================================================
//  app/ai/AiKnowledge.hpp - les connaissances de l'IA : le registre
// -----------------------------------------------------------------------------
//  "APPRENDRE" ICI = ENRICHIR LA MEMOIRE LOCALE que l'Assistant joint a ses
//  demandes. Le modele distant n'est pas reentraine : il recoit, avec chaque
//  demande, le texte des connaissances actives. La "taille estimee" d'une
//  connaissance est donc la taille de CE texte (en octets UTF-8).
//
//  DEUX SORTES :
//    - les connaissances INTEGREES au logiciel (source "logiciel") : tirees de
//      ses VRAIES tables, au demarrage - les 116 genres d'objets et les
//      proprietes de chaque genre viennent du REGISTRE DES GENRES (1.15.0,
//      AiRegistry.hpp : hmi::kKindInfos, la famille de la bibliotheque, la
//      creation par defaut hmi::makeObject, les roles hmi::roles, le libelle de
//      l'inspecteur) - le texte meme que l'Assistant envoie ; les actions (declencheurs et
//      operations), le vocabulaire et les synonymes (les mots-cles de recherche de
//      la bibliotheque d'objets), les regles de creation (l'objet pose pour une
//      variable, hmi::design::kindForVariable ; les modeles de vues). Elles
//      s'activent et se desactivent (l'etat : memoire.json) ; elles ne se
//      suppriment pas ;
//    - les connaissances AJOUTEES (par l'utilisateur, pour un projet, une regle,
//      une preference) : une par fichier, connaissances/<id>.json (le contrat :
//      format, id, nom, description, categorie, source, projet, actif, ajoute,
//      modifie, contenu ; un champ inconnu se garde). On les ajoute, consulte,
//      modifie, active, desactive, supprime.
//
//  PAR PROJET (12.7) : une connaissance porte un projet (vide : tous les
//  projets). Une connaissance d'un AUTRE projet n'est pas "active ici" : elle
//  ne part pas avec les demandes faites dans ce projet.
//
//  Chaque operation ecrit sa ligne dans activite.jsonl (AiMemory.hpp).
//
//  1.21.0 : LA MEMOIRE PERSISTANTE (les << souvenirs >>). Une connaissance ajoutee
//  porte en plus :
//    - sa PORTEE : utilisateur (preferences, habitudes), projet (propre a UN
//      projet, par son identifiant fiable - AiMemoryManager.hpp : identifyProject),
//      conversation (une decision, une tache en attente), technique (une solution
//      reutilisable) ; une solution propre a un projet ne devient jamais une regle
//      generale d'elle-meme ;
//    - son STATUT : confirme, provisoire (proposee par l'Assistant, a valider :
//      elle ne part pas avec les demandes), hypothese (envoyee comme telle) ;
//    - sa CONFIANCE (0 a 1), EPINGLEE (elle part en premier), sa PROVENANCE (la
//      conversation et le message, ou << saisie >>), son HISTORIQUE ;
//    - ses CONFLITS : une information nouvelle qui contredit une ancienne (meme
//      sujet, meme portee, meme projet) ne l'ecrase jamais - les deux restent,
//      chacune avec sa provenance, marquees << en conflit >> ; un doublon (le meme
//      contenu) n'est pas ajoute une seconde fois.
//  Le fichier passe au format 2 ; un fichier du format 1 (avant la 1.21) se lit
//  comme confirme, sa portee deduite de sa categorie et de son projet, et se
//  reecrit au format 2 apres une SAUVEGARDE de toute la memoire (sauvegardes/).
//  Le modele distant n'est toujours pas reentraine : ces souvenirs sont relus
//  sur ce PC et envoyes avec les demandes (kNotRetrained).
// =============================================================================
#pragma once

#include "AiMemoryJson.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::memory {

// 1.21.0 : ce que l'ecran et les consignes disent, mot pour mot (jamais << l'IA a appris >>).
inline constexpr std::string_view kNotRetrained =
    "Le mod\xC3\xA8le distant n'est pas r\xC3\xA9" "entra\xC3\xAEn\xC3\xA9 : ces souvenirs sont gard\xC3\xA9s sur ce PC, relus et envoy\xC3\xA9s avec tes demandes.";
// Le format des fichiers connaissances/<id>.json (1 : avant la 1.21 ; 2 : les souvenirs).
inline constexpr int kKnowledgeFormat = 2;

// Les categories (le champ "categorie"), dans l'ordre des filtres.
struct CategoryInfo {
    std::string_view key;
    std::string_view label;
};
inline constexpr CategoryInfo kCategories[] = {
    {"objets", "Genres d'objets"},
    {"proprietes", "Propri\xC3\xA9t\xC3\xA9s et actions"},
    {"modeles", "Mod\xC3\xA8les m\xC3\xA9tier"},             // 1.17.0 : AiLearning.hpp (corrigeables)
    {"vocabulaire", "Vocabulaire et synonymes"},
    {"regles", "R\xC3\xA8gles de cr\xC3\xA9" "ation"},
    {"preferences", "Pr\xC3\xA9" "f\xC3\xA9rences de conception"},
    {"projet", "Contexte de projet"},
    {"metier", "Connaissances m\xC3\xA9tier"},
};
// Les sources (le champ "source").
struct SourceInfo {
    std::string_view key;
    std::string_view label;
};
inline constexpr SourceInfo kSources[] = {
    {"logiciel", "Logiciel"},            // integree, tiree des tables d'IHM Studio
    {"utilisateur", "Utilisateur"},      // ajoutee dans cet ecran
    {"assistant", "Assistant"},          // enregistree par l'Assistant (une regle, une preference retenue)
    {"fichier", "Fichier analys\xC3\xA9"},   // tiree d'une piece jointe
};
[[nodiscard]] std::string categoryLabel(std::string_view key);   // une cle inconnue : elle-meme
[[nodiscard]] std::string sourceLabel(std::string_view key);
[[nodiscard]] bool        knownCategory(std::string_view key) noexcept;

// 1.21.0 : les portees (le champ "portee") et les statuts (le champ "statut"), dans l'ordre des filtres.
struct ScopeInfo {
    std::string_view key;
    std::string_view label;
};
inline constexpr ScopeInfo kScopes[] = {
    {"utilisateur", "Utilisateur"},       // preferences, habitudes, conventions generales
    {"projet", "Projet"},                 // propre a UN projet (son identifiant fiable)
    {"conversation", "Conversation"},     // une decision, une tache en attente, un probleme non resolu
    {"technique", "Technique"},           // une solution, un exemple valide, une erreur connue
};
struct StatusInfo {
    std::string_view key;
    std::string_view label;
};
inline constexpr StatusInfo kStatuses[] = {
    {"confirme", "Confirm\xC3\xA9"},
    {"provisoire", "Provisoire (\xC3\xA0 valider)"},
    {"hypothese", "Hypoth\xC3\xA8se"},
};
[[nodiscard]] std::string scopeLabel(std::string_view key);
[[nodiscard]] std::string statusLabel(std::string_view key);
[[nodiscard]] bool        knownScope(std::string_view key) noexcept;
[[nodiscard]] bool        knownStatus(std::string_view key) noexcept;
// La portee d'une connaissance qui n'en dit pas (format 1) : un projet -> "projet" ; les preferences -> "utilisateur" ;
// la categorie "projet" -> "projet" ; le reste -> "technique".
[[nodiscard]] std::string scopeFor(std::string_view category, std::string_view project);

// 1.21.0 : une ligne de l'historique d'un souvenir (sa creation, ses changements, ses conflits).
struct HistoryLine {
    std::string date, text;
};

struct Knowledge {
    std::string id, name, description, category, source, project, added, modified, content;
    bool        active{true};
    bool        builtIn{false};
    std::string file;      // ajoutee : son fichier (UTF-8) ; integree : vide
    Json        raw;       // ajoutee : le fichier tel que lu (les champs inconnus y restent)
    // 1.21.0 : le souvenir (voir l'en-tete). `project` reste le NOM du projet (ce que l'on montre) ; `projectId` est
    // l'identifiant fiable - un souvenir qui le porte ne part qu'avec les demandes de CE projet.
    std::string              scope;                  // vide : deduite (scopeFor) ; une integree : vide
    std::string              status{"confirme"};
    double                   confidence{1.0};        // 0 a 1
    bool                     pinned{false};
    std::string              projectId;
    std::string              conversation, message;  // la provenance ; vides : saisie a la main
    std::string              subject;                // la cle du sujet (les contradictions) ; vide : le nom
    std::vector<std::string> conflicts;              // les souvenirs qui le contredisent (leurs id)
    std::vector<HistoryLine> history;
    int                      format{kKnowledgeFormat};   // le format lu (1 : avant la 1.21)
    // La taille estimee : celle du texte qu'on enverrait a l'IA.
    [[nodiscard]] std::uintmax_t size() const noexcept { return content.size(); }
    [[nodiscard]] bool conflicted() const noexcept { return !conflicts.empty(); }
    [[nodiscard]] bool provisional() const noexcept { return status == "provisoire"; }
    // La portee effective (deduite quand elle n'est pas dite).
    [[nodiscard]] std::string scopeKey() const { return scope.empty() ? scopeFor(category, project) : scope; }
    // La provenance, en mots : "Saisie par l'utilisateur", "Conversation c-..., message m3", "Logiciel".
    [[nodiscard]] std::string provenance() const;
};

// 1.21.0 : LE CONTEXTE D'UNE DEMANDE - le projet ouvert (son identifiant fiable : AiMemoryManager.hpp, identifyProject)
// et la conversation. Sans identifiant (un projet jamais enregistre), seuls les souvenirs lies par le NOM (d'avant la
// 1.21) peuvent partir, et pas si deux projets connus portent ce nom (nameAmbiguous).
struct Context {
    std::string projectId;
    std::string projectName;
    std::string conversationId;
    bool        nameAmbiguous{false};
};

// L'etat d'une connaissance pour le projet ouvert.
// 1.21.0 : Pending (provisoire : pas encore validee), OtherConversation (un souvenir d'une autre conversation),
// Unbound (un souvenir de projet sans projet sur, ou dont le nom est porte par deux projets : a associer).
enum class Here : std::uint8_t { Active, Inactive, OtherProject, Pending, OtherConversation, Unbound };
[[nodiscard]] Here        hereState(const Knowledge& k, const Context& ctx) noexcept;
// Par le nom seul (avant la 1.21) : un souvenir lie par son identifiant n'y est jamais actif.
[[nodiscard]] Here        hereState(const Knowledge& k, std::string_view project) noexcept;
[[nodiscard]] std::string hereLabel(Here h);

// 1.21.0 : le resultat de Registry::remember (un ajout qui evite les doublons et garde les contradictions).
struct RememberResult {
    enum class Kind : std::uint8_t { Added, Duplicate, Conflict, Refused };
    Kind                     kind{Kind::Refused};
    std::string              id;              // ajoute : le nouveau ; doublon : l'existant
    std::vector<std::string> conflictsWith;   // contradiction : les souvenirs contredits (gardes)
    std::string              why;             // refuse : pourquoi
    [[nodiscard]] bool ok() const noexcept { return kind != Kind::Refused; }
};
// Le texte compare pour les doublons : minuscules, sans accents, la ponctuation et les blancs resserres.
[[nodiscard]] std::string normalizedText(std::string_view s);

// Les connaissances integrees, tirees des tables du logiciel (calculees une fois).
[[nodiscard]] const std::vector<Knowledge>& builtIns();
// L'identifiant de la connaissance des genres d'objets (le compte "genres connus").
inline constexpr std::string_view kKindsKnowledge = "logiciel.genres";

class Registry {
public:
    // Relit tout : les integrees (et leur etat, memoire.json), puis connaissances/*.json.
    void load();
    [[nodiscard]] const std::vector<Knowledge>& all() const noexcept { return items_; }
    [[nodiscard]] const Knowledge* find(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<std::string>& problems() const noexcept { return problems_; }

    // AJOUTER : l'id (k-AAAAMMJJ-HHMMSS-n), les dates et la source (vide : utilisateur) sont
    // attribues ; le fichier est ecrit, l'activite aussi. `k` recoit ce qui a ete enregistre.
    bool add(Knowledge& k, std::string* why = nullptr);
    // MODIFIER une connaissance ajoutee (pas une integree) : nom, description, categorie, projet,
    // contenu, actif ; la date de mise a jour suit ; les champs inconnus du fichier restent.
    bool update(const Knowledge& k, std::string* why = nullptr);
    // ACTIVER / DESACTIVER (une integree aussi).
    bool setActive(std::string_view id, bool on, std::string* why = nullptr);
    // SUPPRIMER une connaissance ajoutee (une integree : refuse).
    bool remove(std::string_view id, std::string* why = nullptr);
    // Supprimer les connaissances propres a un projet (12.7) ; rend leur nombre.
    std::size_t removeProject(std::string_view project);

    // ---- 1.21.0 : les souvenirs ------------------------------------------------------------------------
    // RETENIR : comme add, mais un doublon (meme portee, meme projet, meme conversation pour une portee
    // conversation, meme contenu normalise) n'est pas ajoute (Duplicate, l'id de l'existant ; un doublon confirme
    // confirme une proposition) ; une contradiction (meme sujet - `subject`, sinon le nom - contenu different)
    // est ajoutee ET l'ancienne gardee, toutes deux marquees en conflit (Conflict). Une portee projet exige un projet
    // (projectId ou, a defaut, son nom) ; une portee conversation, la conversation. `k` : la portee vide est deduite,
    // le nom vide tire de la premiere ligne du contenu.
    RememberResult remember(Knowledge k);
    bool setPinned(std::string_view id, bool on, std::string* why = nullptr);
    // "confirme", "provisoire", "hypothese" (une proposition validee devient confirmee).
    bool setStatus(std::string_view id, std::string_view status, std::string* why = nullptr);
    bool setConfidence(std::string_view id, double confidence, std::string* why = nullptr);
    // ASSOCIER a un projet (son identifiant et son nom) ; deux vides : tous les projets (la portee projet devient
    // technique - jamais en silence : l'historique le dit).
    bool associate(std::string_view id, const std::string& projectId, const std::string& projectName, std::string* why = nullptr);
    // RESOUDRE un conflit : `keep` est confirmee ; `other` est retiree (supprimee si `removeOther`, sinon desactivee
    // et notee << remplacee par ... >>) ; les deux ne sont plus en conflit entre elles.
    bool resolveConflict(std::string_view keep, std::string_view other, bool removeOther, std::string* why = nullptr);
    // Les souvenirs lies par le NOM de ce projet (d'avant la 1.21) recoivent son identifiant - si le nom n'est pas
    // ambigu (ctx.nameAmbiguous faux) ; rend leur nombre.
    std::size_t bindLegacy(const Context& ctx);
    [[nodiscard]] std::vector<const Knowledge*> conflictsOf(std::string_view id) const;
    // Les propositions de l'Assistant a valider (provisoires) qui concernent ce contexte (ce projet, cette
    // conversation, ou tous), la plus recente d'abord.
    [[nodiscard]] std::vector<const Knowledge*> pendingHere(const Context& ctx) const;
    // La derniere migration faite par load() (vide : aucune) : "3 souvenirs migres (sauvegarde memoire-...)".
    [[nodiscard]] const std::string& migration() const noexcept { return migration_; }

    // Les genres d'objets connus : ceux que porte la connaissance des genres quand elle est active.
    [[nodiscard]] std::size_t kindsKnown() const noexcept;
    [[nodiscard]] static std::size_t kindsTotal() noexcept;
    // Les projets cites par les connaissances (tries, sans doublon).
    [[nodiscard]] std::vector<std::string> projects() const;
    [[nodiscard]] std::size_t    count(bool activeOnly) const noexcept;
    [[nodiscard]] std::uintmax_t bytes(bool activeOnly) const noexcept;
    // Ce qui partirait avec une demande faite dans ce projet : les connaissances actives ici.
    [[nodiscard]] std::uintmax_t bytesHere(std::string_view project) const noexcept;
    [[nodiscard]] std::uintmax_t bytesHere(const Context& ctx) const noexcept;   // 1.21.0 : par l'identifiant du projet

private:
    bool write(Knowledge& k, std::string* why);
    Knowledge* mut(std::string_view id) noexcept;
    void       migrate();
    std::vector<Knowledge>   items_;
    std::vector<std::string> problems_;
    std::string              migration_;
};

// 1.21.0 : une ligne d'historique de plus (bornee aux 50 dernieres).
void addHistory(Knowledge& k, std::string text);

} // namespace app::ai::memory
