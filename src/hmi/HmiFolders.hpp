// =============================================================================
//  hmi/HmiFolders.hpp - les dossiers des listes de l'IHM (lot 21)
// -----------------------------------------------------------------------------
//  COMME LES VARIABLES IHM (lot 16), LES AUTRES LISTES SE RANGENT EN DOSSIERS :
//  les vues, les popups, les ecrans modeles, les modeles d'en-tete et de pied
//  de page, les symboles, les scripts generaux, les types IHM, les styles et
//  les ressources. Un dossier est un chemin ("Ligne 1/Armoires") SANS EFFET SUR
//  LES NOMS - comme un filtre de Visual Studio : un element le porte
//  (`folder`) ; un dossier vide existe tant qu'il est dans la liste du projet
//  (Project::listFolders, par cle de liste).
//
//  L'ORDRE EST CELUI DU PROJET. Reordonner (glisser entre deux lignes) deplace
//  les elements dans leur liste - la vue suivante d'un glissement en marche
//  suit cet ordre ; ranger dans un dossier ne change que leur `folder`.
//
//  Tout se fait sur un Project ; l'ecran l'enveloppe dans une commande
//  (changeProject) : Ctrl+Z defait un rangement comme le reste.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hmi::fold {

enum class List : std::uint8_t { Views, Popups, Templates, Headers, Footers, Symbols, Scripts, Types, Styles, Resources, Count };

// "vues", "popups", "ecrans_modeles", "entetes", "pieds", "symboles", "scripts", "types", "styles", "ressources".
[[nodiscard]] std::string_view key(List) noexcept;
[[nodiscard]] std::optional<List> fromKey(std::string_view) noexcept;
// "Vues", "Popups", "Ecrans modeles"...
[[nodiscard]] std::string_view label(List) noexcept;
// "vue" / "vues", "script" / "scripts"...
[[nodiscard]] std::string noun(List, std::size_t count);
// Le participe accorde avec la liste : ("deplace", Vues, 2) -> "deplacees".
[[nodiscard]] std::string agreed(List, std::size_t count, std::string_view participle);
// La liste d'une vue, par son role.
[[nodiscard]] List listOfView(const View&) noexcept;

struct Item {
    Id          id{kNoId};
    std::string name;
    std::string folder;
};
// Les elements d'une liste, dans l'ordre du projet.
[[nodiscard]] std::vector<Item> items(const Project&, List);
[[nodiscard]] bool contains(const Project&, List, Id);
[[nodiscard]] std::string folderOf(const Project&, List, Id);
// Tous les dossiers d'une liste : ceux de Project::listFolders et ceux de ses
// elements (et leurs parents), tries sans casse.
[[nodiscard]] std::vector<std::string> allFolders(const Project&, List);
// Les elements d'un dossier ("" : la racine) ; `deep` : ses sous-dossiers compris.
[[nodiscard]] std::size_t countIn(const Project&, List, std::string_view folder, bool deep);
// Le meme dossier (sans casse) ; `inside` : lui ou un de ses sous-dossiers.
[[nodiscard]] bool sameFolder(std::string_view a, std::string_view b) noexcept;
[[nodiscard]] bool inside(std::string_view folder, std::string_view parent) noexcept;

// ---- les gestes (faux et `why` : refuse) ------------------------------------
bool addFolder(Project&, List, const std::string& path, std::string* why = nullptr);
// Ses elements et ses sous-dossiers suivent.
bool renameFolder(Project&, List, const std::string& from, const std::string& to, std::string* why = nullptr);
// Ses elements et ses sous-dossiers montent d'un cran.
bool removeFolder(Project&, List, const std::string& path);
// Rend le nombre d'elements ranges (ceux qui y etaient deja ne comptent pas).
std::size_t moveToFolder(Project&, List, const std::vector<Id>& ids, const std::string& folder);
// Un dossier (et ce qu'il contient) dans un autre ("" : a la racine).
bool moveFolder(Project&, List, const std::string& path, const std::string& parent, std::string* why = nullptr);
// Reordonner : `ids` (dans l'ordre ou ils sont) avant, ou apres, `anchor` ; ils
// prennent le dossier de `anchor`.
bool moveNear(Project&, List, const std::vector<Id>& ids, Id anchor, bool after);

} // namespace hmi::fold
