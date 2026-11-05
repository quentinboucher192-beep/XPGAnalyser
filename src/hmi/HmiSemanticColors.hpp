// =============================================================================
//  hmi/HmiSemanticColors.hpp - 1.23.1 : LES COULEURS SEMANTIQUES (globales)
// -----------------------------------------------------------------------------
//  Le client (05/11) : << des couleurs semantiques (globales) utilisables partout
//  comme des constantes globales, pouvoir en definir soi-meme >>.
//
//  UNE COULEUR SEMANTIQUE (hmi::SemanticColor) : un nom et une couleur. Une case
//  couleur la cite par << @Nom >> (sans casse) ; une formule aussi, par un texte
//  constant : SEL(Defaut_P1, '@Alarme', '@Marche'). Changer la couleur change
//  tout ce qui la cite. Les FOURNIES (builtins) suivent l'ISA-101 (Fond, Texte,
//  Marche, Arret, Defaut, Alarme critique, Alarme, Avertissement, Information,
//  Accent, Desactive, Equipement) : verrouillees, jamais ecrites dans le projet ;
//  << Dupliquer a partir de >> en fait une du projet. Une du projet ne peut pas
//  porter le nom d'une fournie.
//
//  publish() remplit le registre partage (core/SemanticColors.hpp) que lisent ceux
//  qui dessinent : a l'ouverture du projet, a chaque changement, au depart de la
//  marche.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::semantic {

[[nodiscard]] const std::vector<SemanticColor>& builtins();
[[nodiscard]] const SemanticColor* builtin(std::string_view name) noexcept;     // sans casse, avec ou sans @
// Celle du projet, sinon une fournie (sans casse, avec ou sans @) ; nul : inconnue.
[[nodiscard]] const SemanticColor* find(const Project&, std::string_view name) noexcept;
// Celles du projet, puis les fournies qu'aucune du projet ne cache.
[[nodiscard]] std::vector<const SemanticColor*> all(const Project&);
// Le registre partage : toutes celles du projet (fournies comprises).
void publish(const Project&);

// << @Nom >>.
[[nodiscard]] std::string reference(std::string_view name);
[[nodiscard]] bool isReference(std::string_view text, std::string* name = nullptr);
// Le texte d'une case couleur, une reference remplacee par sa couleur (le projet, pas le registre) ; sinon tel quel.
[[nodiscard]] std::string resolve(const Project&, std::string_view text);

// Ce qui cite la couleur : les proprietes des objets (leur valeur << @Nom >>, ou '@Nom' dans leur formule), les valeurs
// des styles, le fond des vues. Rend leur nombre.
[[nodiscard]] std::size_t users(const Project&, std::string_view name);
// Renommer : ce qui cite << @Ancien >> cite << @Nouveau >> (valeurs, formules, styles, fonds). Rend le nombre de citations
// changees. Ne touche pas a la couleur elle-meme (l'appelant renomme sa fiche).
std::size_t renameReferences(Project&, std::string_view from, std::string_view to);
// Avant de retirer une couleur du projet : ce qui la cite garde SA COULEUR, ecrite en dur (rien ne change a l'ecran).
// Rend le nombre de citations remplacees ; la fiche reste (l'appelant la retire). Une fournie : rien.
std::size_t forget(Project&, std::string_view name);

[[nodiscard]] bool nameFree(const Project&, std::string_view name, Id except = kNoId);
[[nodiscard]] std::string uniqueName(const Project&, std::string wanted);
// DUPLIQUER A PARTIR DE : une couleur du projet (identifiant, nom unique, plus verrouillee).
[[nodiscard]] SemanticColor duplicate(Project&, const SemanticColor&, const std::string& name = {});
// Ce qui ne va pas : une couleur illisible, un nom vide ou celui d'une fournie, un nom avec @.
[[nodiscard]] std::vector<std::string> problems(const Project&, const SemanticColor&);
// Les citations d'une couleur inconnue : ou (<< Vue_Station : P1 (fill) >>, << style Mon ISA >>), le nom cite, la vue et
// l'objet (kNoId hors d'un objet), la propriete.
struct Citation {
    std::string where;
    std::string name;
    Id          view{kNoId};
    Id          object{kNoId};
    std::string key;
};
[[nodiscard]] std::vector<Citation> unknownReferences(const Project&);
// 1.23.1 : ou une couleur est citee (meme forme) - la fiche de la couleur dans IHM > Styles > Couleurs semantiques.
[[nodiscard]] std::vector<Citation> citations(const Project&, std::string_view name);

} // namespace hmi::semantic
