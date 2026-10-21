// =============================================================================
//  hmi/HmiGenericEdit.hpp - 1.12.21 : les generiques changent, leurs emplois suivent
// -----------------------------------------------------------------------------
//  « Si je supprime T, K et que j'avais fait des variables, il faut modifier les types
//  tout seul cote variables. » Changer les generiques d'une structure (T_Extractor<T, K>
//  -> T_Extractor<T>) reecrit chacun de ses emplois : T_Extractor<REAL, STRING> devient
//  T_Extractor<REAL> dans les variables IHM, les membres des autres types, les
//  declarations (scripts, fonctions, vues), les parametres de popup et de symbole, les
//  retours de fonction, les signatures des rappels et le code.
//
//  Les generiques se suivent par leur nom ; ceux qui ne se retrouvent pas sont pris dans
//  l'ordre pour des renommages (T, K -> U, K : T devient U) ; le reste est retire ou ajoute.
//  - RETIRE : son argument part de chaque emploi ; dans les membres de la structure, il
//    prend le type que lui donnaient ses emplois (le plus frequent ; aucun : un type de sa
//    contrainte, INT sans contrainte) - les variables gardent ainsi leurs membres.
//  - RENOMME : les membres suivent (Valeur : T -> Valeur : U) ; les emplois ne changent pas.
//  - AJOUTE : chaque emploi recoit un type de sa contrainte (INT sans contrainte), a changer.
//  - DEPLACE : les arguments suivent leur generique.
//  Un symbole, une popup : ses parametres (et ses declarations) suivent de meme - un T
//  retire prend le type que donnaient ses instances ou ceux qui l'ouvrent (aucun : ANY pour
//  un parametre de type T seul, un type de sa contrainte ailleurs) ; les arguments T := REAL
//  des instances et des ouvertures sont retires ou renommes.
//  A appeler dans une commande annulable (hmi::changeProject) : Ctrl+Z reprend tout.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::generics {

// Le passage de `before` a `after`.
struct Mapping {
    std::vector<int>                                 from;      // un par nouveau generique : son ancien rang (-1 : ajoute)
    std::vector<std::pair<std::string, std::string>> renamed;   // (ancien, nouveau)
    std::vector<int>                                 removed;   // les anciens rangs retires
    std::vector<int>                                 added;     // les nouveaux rangs ajoutes
    // Les emplois ne changent pas : les memes generiques, dans le meme ordre (renommes ou non).
    [[nodiscard]] bool keepsUses() const noexcept;
};
[[nodiscard]] Mapping compare(const std::vector<GenericParam>& before, const std::vector<GenericParam>& after);

// `type` ou chaque `name<a, b>` suit le passage (sans casse, a toute profondeur). `plain` : un `name` sans
// chevrons (la structure n'etait pas generique) recoit aussi ses nouveaux generiques. Un emploi qui n'avait pas
// le bon nombre de types reste tel quel.
[[nodiscard]] std::string retype(std::string_view type, std::string_view name, const std::vector<GenericParam>& before,
                                 const std::vector<GenericParam>& after, bool plain = true);

struct Report {
    std::vector<std::string> where;   // les emplois reecrits (« Variables IHM / Extr1 »)
    std::vector<std::string> fixed;   // ce que deviennent les generiques retires (« K devient STRING »)
};
// La structure `typeId` prend `after` ; ses membres et ses emplois suivent.
Report applyToType(Project&, Id typeId, const std::vector<GenericParam>& after);
// La vue `viewId` (un symbole, une popup) prend `after` ; ses parametres, ses declarations, ses instances et ses
// ouvertures suivent.
Report applyToView(Project&, Id viewId, const std::vector<GenericParam>& after);

} // namespace hmi::generics
