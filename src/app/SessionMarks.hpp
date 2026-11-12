// =============================================================================
//  app/SessionMarks.hpp - 1.43.0 : ce que les editeurs de vues savent de la session
// -----------------------------------------------------------------------------
//  Le service des sessions (app/SessionService) y met, a chaque changement, les
//  objets que les AUTRES membres tiennent (ceux qu'ils ont choisis) ; le canevas
//  d'une vue les entoure de leur couleur, avec leur nom, et refuse de les choisir.
//  Dans l'autre sens, le canevas y dit ce que CE PC choisit et la vue ou il est :
//  le service le lit a chaque image et l'envoie. Hors session : rien (active()).
//  1.45.0 : et ce que l'interface montre de la session (Board : les membres, la
//  discussion, le journal ; Hooks : ecrire, suivre, exporter le journal), et le
//  refus de garder une copie chez l'equipier (keepRefusal).
//  Le fil de l'interface seulement.
// =============================================================================
#pragma once

#include "../hmi/HmiModel.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app::sessionmarks {

struct Holder {
    int           member{0};
    std::string   name;
    std::uint32_t color{0};   // 0xRRGGBB
    bool operator==(const Holder&) const = default;
};
using Held = std::map<std::pair<hmi::Id, hmi::Id>, Holder>;   // (vue, objet) -> qui le tient

void setActive(bool on);
[[nodiscard]] bool active();
void setHeld(Held held);
// Qui d'autre tient cet objet (nullptr : personne, ou hors session).
[[nodiscard]] const Holder* heldByOther(hmi::Id view, hmi::Id object);
[[nodiscard]] std::vector<std::pair<hmi::Id, const Holder*>> heldIn(hmi::Id view);
[[nodiscard]] std::uint64_t revision();

// Ce que ce PC choisit, et la vue ou il est (la derniere peinte, ou celle d'un choix).
struct Mine {
    hmi::Id view{hmi::kNoId};
    std::vector<hmi::Id> selection;
    std::uint64_t serial{0};
};
void mine(hmi::Id view, const std::vector<hmi::Id>& selection);
void seen(hmi::Id view);
[[nodiscard]] const Mine& current();

// Un choix refuse (l'objet est pris) : le service le dit dans la barre d'etat.
void setRefusedNotice(std::function<void(const std::string&)> fn);
void refused(const std::string& text);

// ---- 1.45.0 : CE QUE L'INTERFACE MONTRE DE LA SESSION ----
//  Le service y met (a chaque changement) les membres, la discussion et le journal ; l'arbre du projet (le noeud
//  << Session >>), le panneau du bas (l'onglet << Session >>, qui n'existe que pendant une session) et l'explorateur
//  d'objets (les initiales de qui tient un objet) le lisent. Dans l'autre sens, ils demandent par les crochets
//  (Hooks) : ecrire a l'equipe, suivre un membre, exporter le journal.
struct Person {
    int           number{0};
    std::string   name, initials, machine, machineId;
    std::string   viewName;               // la vue ou il est (vide : aucune)
    hmi::Id       view{hmi::kNoId};
    std::uint32_t color{0};
    bool          host{false}, me{false}, watch{false};
};
struct Line {
    std::string   time, from, text;       // "14:32", "Lea", "bonjour"
    std::uint32_t color{0};
    bool          system{false};          // une ligne de la session (entree, depart...)
    bool          mine{false};            // ecrite par ce PC
    int           count{1};               // le journal : les gestes fondus (x3)
    // 1.50.0 : la discussion - l'identifiant du message (le meme sur chaque PC ; vide : une ligne de la session) et
    // celui auquel il repond (vide : aucun), son auteur et le debut de son texte.
    std::string   id{}, replyId{}, replyFrom{}, replyText{};
};
struct Board {
    std::string         sessionName, hostName;
    bool                host{false};      // ce PC est l'hote
    bool                internet{false};
    std::vector<Person> people;
    std::vector<Line>   chat;             // la discussion (et les lignes de la session)
    std::vector<Line>   journal;          // le journal des changements (les plus anciens d'abord)
    std::uint64_t       messages{0};      // les messages des autres recus depuis le debut (le non lu)
};
void setBoard(Board board);
[[nodiscard]] const Board& board();
[[nodiscard]] std::uint64_t boardRevision();

struct Hooks {
    std::function<void(const std::string&, const std::string&)> say;   // ecrire a l'equipe (1.50.0 : en reponse a un message)
    std::function<void(int)>                follow;             // suivre un membre (sa vue s'ouvre)
    std::function<std::string(std::string*)> exportJournal;     // le chemin ecrit (vide : `why`)
};
void setHooks(Hooks hooks);
// 1.50.0 : `replyTo` - l'identifiant du message auquel il repond (vide : aucun).
void say(const std::string& text, const std::string& replyTo = {});
void follow(int member);
[[nodiscard]] std::string exportJournal(std::string* why);

// L'EQUIPIER NE GARDE RIEN : en session, le projet reste chez l'hote - Enregistrer sous, Dupliquer, les exports, le
// dossier du projet dans l'explorateur sont refuses. La raison (vide : permis) : << En session, le projet reste chez
// l'hote >>.
void setGuest(bool guest);
[[nodiscard]] bool guest();
[[nodiscard]] std::string keepRefusal();
// Les actions refusees a l'equipier (project.saveAs, project.duplicate, project.exportArchive, project.exportSources).
[[nodiscard]] bool keepAction(std::string_view action);
// Les initiales d'un nom (une pastille) : "Lea Martin" -> "LM" (la premiere lettre de deux mots, en capitales).
[[nodiscard]] std::string initialsOf(std::string_view name);

} // namespace app::sessionmarks
