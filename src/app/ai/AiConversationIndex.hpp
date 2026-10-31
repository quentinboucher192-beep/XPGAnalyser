// =============================================================================
//  app/ai/AiConversationIndex.hpp - 1.21.0 : RETROUVER LES ANCIENNES CONVERSATIONS
// -----------------------------------------------------------------------------
//  << Reprends le probleme du calcul des temps dans ma DFB. >> L'Assistant cherche
//  dans les conversations sauvegardees (conversations/*.json) et rend ce qu'il a
//  REELLEMENT trouve : les conversations, des extraits, une chronologie. Rien
//  trouve : il le dit, il n'invente pas de souvenir.
//
//  L'INDEX (index/conversations.json, regenerable : le nettoyage peut l'effacer,
//  il se refait) : par conversation, ses mots-cles, les noms d'objets et de
//  variables cites (Pompe_1_Marche, `LT_201`), les equipements (pompe, vanne,
//  M340, P101...), les langages (ST, C, C++, JSON...), les genres d'objets IHM,
//  les pieces jointes, le projet (son nom, son identifiant quand la conversation
//  le porte), les dates, et des extraits des problemes rencontres et des
//  solutions apportees. Il se refait par morceaux : seule une conversation
//  modifiee depuis (sa date, son nombre de messages) est relue.
//
//  LA RECHERCHE : par mots-cles (tous, sans casse ni accents), par objet, genre,
//  equipement, langage, projet, dates, problemes ou solutions ; les messages des
//  conversations retenues donnent les extraits. Le resultat : les conversations
//  (la plus pertinente d'abord), leurs extraits, et la CHRONOLOGIE de tous les
//  extraits (du plus ancien au plus recent) - pour reconstruire un travail.
//
//  JAMAIS SUR LE FIL DE L'INTERFACE : Indexer travaille sur son fil ; le panneau
//  sonde poll() a chaque image. L'outil du modele (chercher_conversations)
//  s'appelle, lui, sur le fil du fournisseur (synchrone, comme lire_piece_jointe).
// =============================================================================
#pragma once

#include "AiStore.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace app::ai::convindex {

struct Snippet {
    std::string message, role, date, text;   // le message (son id), "utilisateur" | "assistant", ISO, une ligne
};

struct Entry {
    std::string id, title, created, modified;
    std::string project, projectPath, projectId;   // projectId : ProjectRef::extra "id" quand l'Assistant l'y met
    std::string stamp;                             // modifie|messages : relue quand il change
    std::size_t messages{0};
    std::vector<std::string> keywords, objects, equipment, languages, types, attachments;
    std::vector<Snippet>     problems, solutions;
};

struct Index {
    std::string        built;      // ISO
    std::vector<Entry> entries;
};

// L'entree d'une conversation (sans disque).
[[nodiscard]] Entry indexConversation(const Conversation& c);
// L'index de ces conversations : celles dont le tampon n'a pas change reprennent l'entree de `previous` ; `reindexed` :
// le nombre de conversations relues.
[[nodiscard]] Index buildIndex(const Index& previous, const std::vector<Conversation>& all, std::size_t* reindexed = nullptr);
[[nodiscard]] std::string indexFile();   // index/conversations.json (vide : pas de racine)
[[nodiscard]] Index loadIndex();         // absent ou illisible : vide (il se refait)
bool saveIndex(const Index& index, std::string* why = nullptr);
// Relit les conversations, met l'index a jour et l'ecrit ; `conversations` (facultatif) recoit celles lues.
[[nodiscard]] Index refreshIndex(std::size_t* reindexed = nullptr, std::vector<Conversation>* conversations = nullptr);

struct Query {
    std::string text;                 // les mots (ui::SearchQuery : tous, "phrase exacte", -exclu)
    std::string object;               // un nom d'objet ou de variable (contenu, sans casse)
    std::string type;                 // un genre d'objet IHM (sa cle ou son libelle) ou une extension de piece jointe
    std::string equipment;            // un equipement (pompe, M340, P101...)
    std::string language;             // ST, C, C++, JSON, XML...
    std::string project;              // le nom ou l'identifiant du projet
    std::string from, to;             // des dates ISO ("2026-10-01"), incluses ; vides : sans borne
    enum class Kind : std::uint8_t { Any, Problem, Solution };
    Kind        kind{Kind::Any};
    std::string excludeConversation;  // la conversation en cours (ne pas se retrouver soi-meme)
    std::size_t maxConversations{8};
    std::size_t maxExcerpts{3};
};

struct Excerpt {
    std::string conversation, title, message, role, date, text;
};
struct Match {
    std::string              id, title, project, created, modified;
    std::size_t              messages{0};
    int                      score{0};
    std::vector<std::string> why;        // ce qui correspond ("mots", "objet P101", "probleme"...)
    std::vector<Excerpt>     excerpts;
};
struct Result {
    std::vector<Match>   matches;        // la plus pertinente d'abord
    std::vector<Excerpt> chronology;     // tous les extraits, du plus ancien au plus recent
    std::size_t          searched{0};    // les conversations parcourues
};
// `conversations` : les messages pour les extraits (celles de l'index qui y manquent n'ont pas d'extrait).
[[nodiscard]] Result search(const Index& index, const Query& q, const std::vector<Conversation>& conversations);
// Le resultat en texte (pour le modele, ou pour l'ecran) ; rien : le dire, ne rien inventer.
[[nodiscard]] std::string resultText(const Result& r, const Query& q);

// ---------------------------------------------------------------- le fil d'indexation ---
class Indexer {
public:
    struct Event {
        enum class Type : std::uint8_t { Rebuilt, Searched };
        Type          type{Type::Rebuilt};
        std::uint64_t ticket{0};
        std::size_t   conversations{0}, reindexed{0};
        Result        result;   // Searched
    };
    Indexer();
    ~Indexer();
    Indexer(const Indexer&) = delete;
    Indexer& operator=(const Indexer&) = delete;

    // Met l'index a jour sur le fil (rend le numero de la demande).
    std::uint64_t rebuild();
    // Met l'index a jour puis cherche ; le resultat arrive par poll().
    std::uint64_t search(Query q);
    // Ce qui est arrive depuis la derniere fois (jamais bloquant ; le fil de l'interface).
    [[nodiscard]] std::vector<Event> poll();
    [[nodiscard]] bool busy() const;
    // Abandonne ce qui attend (un travail en cours finit, son resultat n'est pas rendu).
    void cancel();
    // Pour les essais : attend la fin du travail (au plus `ms`).
    bool waitIdle(int ms);

private:
    struct Job {
        std::uint64_t ticket{0};
        bool          searching{false};
        Query         query;
    };
    void run();
    mutable std::mutex      m_;
    std::condition_variable cv_;
    std::deque<Job>         jobs_;
    std::vector<Event>      done_;
    std::uint64_t           next_{0}, cancelledUpTo_{0};
    bool                    working_{false}, stop_{false};
    std::thread             thread_;
};

// ------------------------------------------------------------------- l'outil ---
inline constexpr std::string_view kSearchConversationsTool = "chercher_conversations";
[[nodiscard]] std::string searchConversationsToolJson();
// La reponse a un appel (l'entree JSON brute), sur le fil du fournisseur : met l'index a jour, cherche, rend le texte.
[[nodiscard]] std::string runSearchConversationsTool(std::string_view inputJson, std::string_view currentConversation, bool* isError = nullptr);

} // namespace app::ai::convindex
