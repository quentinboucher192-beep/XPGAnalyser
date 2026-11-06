// =============================================================================
//  app/Corbeille.hpp - 1.25.1 : LA CORBEILLE, BIEN RANGEE, OU L'ON CHERCHE
// -----------------------------------------------------------------------------
//  Le client (05/11) : << fait une corbeille bien rangee avec recherche possible >>.
//
//  Chaque magasin garde sa corbeille (core/Trash.hpp : <date>~<nom>, dans son
//  dossier) : les enregistrements (ihm/corbeille), les incidents (en
//  attente/.corbeille), les versions (versions/corbeille), les macros
//  (libs/Macros/_corbeille), l'Assistant IA (IA/corbeille/conversations,
//  pieces-jointes, connaissances). Ici, UNE corbeille les reunit : chaque
//  magasin s'inscrit (registerSource) et dit ce qu'il garde (son genre, son nom,
//  d'ou il vient, quand il est parti, sa taille) et comment le rendre, l'y
//  remettre, le supprimer pour de bon. Le volet Corbeille (hmi/HmiCorbeillePane)
//  les montre par genre, et y cherche.
//
//  Restaurer plusieurs : UNE commande (core::trash::LambdaCommand) - un seul Ctrl+Z
//  les remet dans la corbeille. Supprimer definitivement, Vider : sans retour.
//
//  LA PURGE : ce qui y est depuis plus de kKeepDays jours part a l'ouverture
//  suivante (l'application : l'IA ; le projet : ses enregistrements, ses
//  incidents, ses versions). Les macros restent jusqu'a Supprimer
//  definitivement (leur corbeille d'avant la 1.25.1 n'avait pas de purge).
//
//  1.26.1 : LE PROJET IHM AUSSI - le client : << la corbeille ne fonctionne pas
//  je supprime des choses ca n'apparait pas dedans (j'ai supprime une ressource,
//  et une vue) >>. Ce qu'on supprime du projet IHM (vues, ressources, scripts,
//  variables, types, alarmes, recettes, styles, fichiers externes) est garde
//  DANS le projet (hmi/HmiTrash.hpp) : la source "ihm" (hmiSource) le montre ici,
//  le rend au projet (Restaurer), l'en retire pour de bon. Ctrl+Z d'une
//  suppression le reprend de la corbeille, comme avant.
//
//  Les chemins sont en UTF-8.
// =============================================================================
#pragma once

#include "../core/Trash.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi {
class Document;
}

namespace app::corbeille {

inline constexpr int kKeepDays = core::trash::kKeepDays;

// Les genres, dans l'ordre du volet (un dossier chacun). 1.26.1 : ceux du projet IHM d'abord (hmi::bin::Kind, dans
// le meme ordre).
enum class Kind : std::uint8_t {
    View, Resource, External, Script, Function, Variable, HmiType, Alarm, Recipe, Style,
    Recording, Incident, Version, Macro, AiConversation, AiAttachment, AiKnowledge, Count
};
inline constexpr std::size_t kKindCount = static_cast<std::size_t>(Kind::Count);
// "Enregistrement" / "Enregistrements", "Conversation IA" / "Conversations IA"...
[[nodiscard]] std::string kindLabel(Kind, bool plural = false);

struct Entry {
    Kind          kind{Kind::Recording};
    std::string   source;      // la source inscrite ("projet", "ia", "macros")
    std::string   name;        // ce qu'on montre ("Nuit", "V5 - Livree au client", "Pompe_1")
    std::string   origin;      // d'ou il vient : ou Restaurer le remet (un chemin)
    std::string   trashed;     // ou il est dans la corbeille (un chemin)
    std::string   handle;      // ce que sa source en dit en plus (une macro : son nom)
    std::int64_t  deletedAt{0};   // secondes depuis 1970 (temps universel) ; 0 : inconnue
    std::uint64_t bytes{0};
    bool          autoPurge{true};   // part apres kKeepDays (une macro, un element sans date : non)
    // 1.26.1 : un fichier ou un dossier (Ouvrir dans l'explorateur) ; faux : un element du projet IHM, garde dans le
    // projet lui-meme (`origin` : "IHM > Vues > Pompes", sa liste et son dossier).
    bool          onDisk{true};
    [[nodiscard]] std::string key() const { return source + "|" + trashed; }
    [[nodiscard]] std::string originFolder() const;   // le dossier ou il reviendra (la colonne Origine)
};

// Ce qu'un magasin donne a la corbeille commune.
struct Source {
    std::function<std::vector<Entry>()>                 list;
    std::function<bool(Entry&, std::string* why)>       restore;   // le rend a `origin`
    std::function<bool(Entry&, std::string* why)>       retrash;   // Ctrl+Z d'une restauration : de nouveau dedans (`trashed` suit)
    std::function<bool(const Entry&, std::string* why)> purge;     // pour de bon
    std::function<void()>                               changed;   // apres un geste : les volets du magasin se refont
    // 1.26.1 : change quand sa corbeille change sans geste d'ici (une suppression dans le projet IHM) - revision() le
    // suit : le volet et << Corbeille [N] >> se relisent tout de suite. Vide : seuls les gestes comptent.
    std::function<std::uint64_t()>                      stamp;
};
void registerSource(const std::string& key, Source source);
void unregisterSource(const std::string& key);
[[nodiscard]] bool hasSource(const std::string& key);
[[nodiscard]] std::vector<std::string> sources();

// Tout ce que les corbeilles inscrites gardent, le plus recent d'abord.
[[nodiscard]] std::vector<Entry> entries();
// Combien (l'arbre : << Corbeille [N] >>) - relu au plus toutes les 2 s, ou apres un geste.
[[nodiscard]] std::size_t count();
// Change a chaque geste (le volet se refait) ; touch() : une source a change d'elle-meme.
[[nodiscard]] std::uint64_t revision();
void touch();

// LES GESTES. Restaurer : chacun a sa place (refuse : `refused` dit pourquoi), puis UNE commande sur la pile de
// l'application (un seul Ctrl+Z les remet dans la corbeille). Supprimer : pour de bon. Rendent combien.
// `restored` : ceux qui sont revenus, tels que leur source les a rendus (1.26.1 : un nom deja pris, "Vue -> Vue_2").
std::size_t restore(const std::vector<Entry>& items, std::vector<std::pair<std::string, std::string>>* refused = nullptr,
                    std::vector<Entry>* restored = nullptr);
std::size_t purge(const std::vector<Entry>& items, std::vector<std::pair<std::string, std::string>>* refused = nullptr);
// Ce qui est la depuis plus de `days` jours (et qui part tout seul : autoPurge), dans une source ("" : toutes).
std::size_t purgeOld(const std::string& sourceKey = {}, int days = kKeepDays, std::int64_t now = 0);

// LA RECHERCHE : chaque mot cherche commence un mot du nom, du genre ou de l'origine (sans casse ni accents), comme
// celle de la bibliotheque de sons.
[[nodiscard]] bool matches(const Entry&, std::string_view query);

// Les textes du volet : "05/11/2026 17:30" (l'heure du poste ; "date inconnue") ; "1,2 Mo" ; "12 elements, 3,4 Mo".
[[nodiscard]] std::string dateText(std::int64_t seconds);
[[nodiscard]] std::string sizeText(std::uint64_t bytes);
[[nodiscard]] std::string summaryText(std::size_t count, std::uint64_t bytes);

// ---- les sources fournies -----------------------------------------------------------------------------------------
// Le projet : ses enregistrements (ihm/corbeille ; `recordings` : le dossier des enregistrements, d'ou ils viennent),
// ses incidents (en attente/.corbeille), ses versions (versions/corbeille).
[[nodiscard]] Source projectSource(std::string projectFolder, std::function<std::string()> recordingsFolder);
// L'Assistant IA (IA/corbeille : conversations, pieces jointes, connaissances et souvenirs).
[[nodiscard]] Source aiSource();
// Les macros de la bibliotheque (libs/Macros/_corbeille).
[[nodiscard]] Source macroSource(std::string libraryRoot);
// 1.26.1 : le projet IHM ouvert (`doc` : le document ; nul, aucun). Sa corbeille est dans le projet (Project::trash) :
// Restaurer le remet dans le projet (un nom pris : le premier libre), Supprimer definitivement l'en retire, du disque
// aussi (`folder` : le dossier du projet ; vide, pas encore enregistre). A la premiere lecture d'un document (son
// ouverture), ce qui y est depuis plus de kKeepDays jours part pour de bon.
inline constexpr const char* kHmiSource = "ihm";
[[nodiscard]] Source hmiSource(std::function<std::shared_ptr<hmi::Document>()> doc, std::function<std::string()> folder);

} // namespace app::corbeille
