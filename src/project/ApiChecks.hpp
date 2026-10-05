// =============================================================================
//  project/ApiChecks.hpp - lot API 2 : ce que le tableau de bord de l'API dit
// -----------------------------------------------------------------------------
//  Un clic sur « API » dans l'arbre ouvre un tableau de bord : l'automate, les
//  entrees de la tache, les types, les variables, et « A regarder » - ce qu'une
//  relecture trouverait. Les calculs vivent ici, sans ecran, pour etre testes
//  sur un vrai projet (tests/api2_test.cpp) et reutilises par les onglets des
//  lots suivants (l'ordre d'execution, les types, les variables).
//
//  CE QUI EST REGARDE :
//   * LES VERSIONS FACE A LA BIBLIOTHEQUE : SharedLibrary::outdated, deja la ;
//     rien de plus a calculer, seulement a montrer.
//   * LES VARIABLES QUE LE PROGRAMME N'UTILISE PAS : l'index des references de
//     l'analyse (ProjectAnalyzer::buildReferenceIndex), celui de la colonne
//     « Utilisations » de l'onglet Variables. « Pas utilisee par le programme »
//     ne veut pas dire « a supprimer » : l'IHM ou le systeme peut la lire - le
//     tableau le dit.
//   * LES LECTURES AVANT L'ECRITURE, dans l'ordre de la tache : une entree lit
//     une variable qu'aucune entree d'avant n'a ecrite, mais qu'une entree
//     d'apres ecrit. Au premier cycle elle lit la valeur initiale, ensuite celle
//     du cycle precedent. Souvent voulu, parfois une section mal rangee.
//
//  LA LECTURE DU CODE EST VOLONTAIREMENT SIMPLE. Une ecriture est un nom a
//  gauche d'un ':=' en debut d'instruction ; une lecture, tout autre nom. Les
//  commentaires (* *) et les chaines '...' sont retires avant. Les noms sont
//  compares SANS la casse (le ST ne la distingue pas) et seuls ceux des
//  variables globales comptent. Un parametre E/S d'unite qui porte le nom de sa
//  variable (armoires / Armoires) se lit donc comme elle - c'est ce qu'il est.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"
#include "SharedLibrary.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace project::api {

// Une entree de l'ordre d'execution d'une tache : une section de la tache, ou
// une unite de programme (toutes ses sections, en bloc, au rang de la premiere).
struct Entry {
    std::string   name;
    bool          unit{false};
    std::size_t   sections{0};      // 1 pour une section, celles de l'unite sinon
    std::size_t   lines{0};
    domain::Index section{domain::kNoIndex};     // une section : son indice
    domain::Index unitIndex{domain::kNoIndex};   // une unite : son POU
};
[[nodiscard]] std::vector<Entry> entriesOf(const domain::Project&, std::string_view task);

// Ce qu'une entree ecrit et lit, parmi les variables globales (noms tels que
// declares, tries, sans doublon).
struct Access {
    std::vector<std::string> writes, reads;
};
[[nodiscard]] Access accessOf(const domain::Project&, const Entry&);
// 1.8.0 (l'export lisible) : ce qu'UNE section lit et ecrit. Dans une unite de
// programme, un parametre compte pour la variable du projet qu'il recoit
// (config_utilisee -> ConfigGazUtilisee) ; le nom rendu est celui du projet.
[[nodiscard]] Access accessOfSection(const domain::Project&, domain::Index section);

struct LateRead {
    std::string reader;
    std::size_t readerRank{0};     // 1 = la premiere entree
    std::string variable;
    std::string writer;            // la premiere entree qui l'ecrit
    std::size_t writerRank{0};
};
[[nodiscard]] std::vector<LateRead> lateReads(const domain::Project&, std::string_view task);

struct Summary {
    // l'automate
    std::string cpu, family, firmware, resource, product;
    bool        hasHardware{false};
    std::size_t racks{0}, modules{0};
    // la tache principale
    std::string        mainTask;
    std::vector<Entry> order;
    std::size_t        taskSections{0}, units{0}, unitSections{0}, lines{0}, subroutines{0};
    std::size_t        tasks{0};
    // les types
    std::size_t ddt{0}, ddtFromLibrary{0}, dfb{0}, dfbFromLibrary{0};
    std::vector<VersionNotice> outdated;
    // les variables
    std::size_t variables{0}, located{0}, blockInstances{0};
    std::vector<std::string> unused;       // pas utilisees par le programme
    // l'ordre
    std::vector<LateRead> late;
    // le reste
    std::size_t animationTables{0}, animationEntries{0};
};
// `library` peut etre nul : les versions ne sont alors pas comparees.
[[nodiscard]] Summary summarize(const domain::Project&, const SharedLibrary* library);

} // namespace project::api
