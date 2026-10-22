#pragma once
// =============================================================================
//  hmi/HmiEditHistory.hpp - 1.12.25 : L'HISTORIQUE DES MODIFICATIONS DES OBJETS
// -----------------------------------------------------------------------------
//  A chaque Enregistrer, chaque objet de chaque vue est compare a son etat du
//  dernier enregistrement - le fichier de la vue sur le disque -, propriete par
//  propriete. Une entree par objet change, signee : la date et l'heure, le nom du
//  PC, le compte Windows, la version de l'application. Un Enregistrer sans
//  changement n'ecrit rien ; les Ctrl+Z / Ctrl+Y entre deux enregistrements ne
//  font pas d'entree.
//
//  Les sortes d'entree : creation (posee depuis la bibliotheque, dupliquee,
//  collee, restauree), modification (la geometrie, l'apparence, les liaisons et
//  expressions, les actions, l'objet lui-meme : nom, calque, verrou), suppression
//  (son dernier etat est garde : il se restaure), et regroupee (les plus
//  anciennes, au-dela de 100 entrees par objet).
//
//  L'historique suit l'objet (son id), pas son nom : renomme, il garde tout.
//  Il est range dans le projet, un fichier texte par vue, du format des vues :
//      ihm/historique-edition/0003.txt   (0003 : l'id de la vue)
//  Il n'a rien a voir avec l'Historique du projet (Ctrl+H), qui montre les
//  actions de la seance a annuler.
// =============================================================================
#include "core/Result.hpp"
#include "hmi/HmiModel.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::edits {

// Ou va un changement : les filtres de la fenetre Historique, les pastilles d'une entree.
enum class Sort : std::uint8_t { Object, Geometry, Appearance, Bindings, Actions };

// UN CHANGEMENT d'une propriete (ou du nom, du calque, d'une action...) entre deux enregistrements.
//  - une propriete : sa cle ("fill"), sa valeur et son expression avant et apres ;
//  - "nom", "calque", "groupe", "verrou", "cache" : l'objet lui-meme ;
//  - "action 2", "alarme Defaut", "fonction Ouvrir" : la ligne de son fichier (le format des vues).
// Absent : la propriete (l'action...) n'existait pas avant, ou n'existe plus apres.
struct Change {
    std::string key;
    Sort        sort{Sort::Appearance};
    std::string before, after;
    std::string beforeExpr, afterExpr;
    bool        beforeAbsent{false}, afterAbsent{false};
    bool operator==(const Change&) const = default;
};

enum class What : std::uint8_t { Created, Modified, Deleted, Merged };

// UNE ENTREE : un objet, a un enregistrement.
struct Entry {
    std::int64_t        t{0};              // secondes depuis 1970 (UTC)
    std::string         pc, account, version;
    Id                  object{kNoId};
    std::string         name;              // son nom a cet enregistrement
    std::string         kind;              // kindKey : "Damper"
    What                what{What::Modified};
    std::string         note;              // "dupliqu\xC3\xA9 de VE_1", "retour \xC3\xA0 la version du 20/10/2026 14:05"...
    std::vector<Change> changes;
    std::string         snapshot;          // supprime : l'objet tel qu'il etait (ses lignes, format des vues)
    std::int64_t        since{0};          // regroupee : le plus ancien enregistrement qu'elle couvre
    int                 count{1};          // regroupee : combien d'enregistrements
    bool operator==(const Entry&) const = default;
};

// L'historique d'une vue : ses entrees, de la plus ancienne a la plus recente.
struct Log {
    Id                 view{kNoId};
    std::vector<Entry> entries;
};

// LA SIGNATURE d'un enregistrement : maintenant, ce PC, ce compte, cette version.
struct Signature {
    std::int64_t t{0};
    std::string  pc, account, version;
};
[[nodiscard]] Signature currentSignature();

// Ce que l'editeur a dit d'un objet depuis le dernier enregistrement (d'ou il vient,
// un retour a une version) : l'entree du prochain enregistrement le reprend.
using Notes = std::map<Id, std::string>;

inline constexpr std::size_t kKeepPerObject = 100;   // au-dela, les plus anciennes sont regroupees

// ---- comparer -----------------------------------------------------------------
[[nodiscard]] Sort sortOf(Kind, std::string_view key, bool expression);
// Les changements d'un objet entre deux etats (vide : rien n'a change).
[[nodiscard]] std::vector<Change> diff(const Object& before, const Object& after);
// Les entrees qu'un enregistrement ajoute a une vue : une par objet cree, modifie, supprime.
// `before` : la vue a son dernier enregistrement (nul : elle n'existait pas).
[[nodiscard]] std::vector<Entry> compare(const View* before, const View& after, const Signature&, const Notes& = {});

// ---- revenir --------------------------------------------------------------------
// Remet un changement dans l'objet : sa valeur d'avant (toBefore) ou d'apres. Faux : il ne s'applique pas.
bool apply(Object&, const Change&, bool toBefore);
// L'objet entier (ses lignes du format des vues), et relu : la restauration d'un objet supprime.
[[nodiscard]] std::string           snapshotOf(const Object&);
[[nodiscard]] std::optional<Object> fromSnapshot(std::string_view);

// ---- garder ---------------------------------------------------------------------
// Au plus `perObject` entrees par objet : la creation reste, les plus anciennes apres elle se regroupent en une.
void trim(Log&, std::size_t perObject = kKeepPerObject);

// ---- le fichier -----------------------------------------------------------------
[[nodiscard]] std::string fileOf(Id view);                       // "historique-edition/0003.txt" (sous ihm/)
[[nodiscard]] std::string serialize(const Log&);
[[nodiscard]] Log         parse(std::string_view text, Id view);
[[nodiscard]] Log         load(const std::string& projectFolder, Id view);
// La vue a son dernier enregistrement : son fichier dans le dossier du projet (nul : pas de fichier).
[[nodiscard]] std::optional<View> savedView(const std::string& projectFolder, Id view);

// A CHAQUE ENREGISTREMENT, avant que les vues soient ecrites : chaque vue comparee a son fichier
// dans `baseFolder` (le dossier du dernier enregistrement), ses entrees ajoutees a son historique
// dans `targetFolder` (le meme, ou celui d'un Enregistrer sous : l'historique y est d'abord copie).
// Rend le nombre d'entrees ecrites.
[[nodiscard]] core::Result<int> record(const Project&, const std::string& baseFolder, const std::string& targetFolder,
                                       const Signature&, const Notes& = {});

// ---- pour la fenetre Historique, Proprietes, l'Explorateur ----------------------------
[[nodiscard]] std::string_view sortLabel(Sort) noexcept;   // "Position et taille"...
[[nodiscard]] std::string_view whatLabel(What) noexcept;   // "Cr\xC3\xA9" "ation"...
[[nodiscard]] std::vector<Sort> sortsOf(const Entry&);      // ses sortes, dans l'ordre des filtres
[[nodiscard]] bool renamed(const Entry&) noexcept;          // son nom a change
// Ce que montre une case Avant / Apres : "=Marche" (une expression), la valeur, "" (absente).
[[nodiscard]] std::string shown(const Change&, bool before);
// Une action, une alarme surchargee, une fonction redefinie, en clair (sa ligne relue).
[[nodiscard]] std::string blockSummary(const Change&, bool before);
[[nodiscard]] const Entry* lastOf(const Log&, Id object) noexcept;
[[nodiscard]] const Entry* createdOf(const Log&, Id object) noexcept;
[[nodiscard]] int          countOf(const Log&, Id object) noexcept;   // ses enregistrements (regroupes compris)
// "22/10/2026 14:32" (l'heure du PC).
[[nodiscard]] std::string dateText(std::int64_t t);

} // namespace hmi::edits
