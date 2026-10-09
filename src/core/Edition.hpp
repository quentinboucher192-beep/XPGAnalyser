// =============================================================================
//  core/Edition.hpp - 1.12.0 : DEUX APPLICATIONS, UN SEUL CODE
// -----------------------------------------------------------------------------
//  XPGAnalyser API (l'automate : l'analyse du programme, sa simulation) et
//  XPGAnalyser IHM (l'IHM : sa conception, sa simulation seule, le poste
//  d'exploitation) sont deux executables independants, faits du meme code.
//  main.cpp fixe l'edition au lancement (XPG_EDITION, donne a la construction de
//  chaque executable) ; les ecrans, les menus, l'arbre, l'aide, les dossiers des
//  projets la lisent pour ne montrer que leur partie. Ils ne partagent rien :
//  ni les projets (Projets\api, Projets\ihm), ni les reglages, ni l'instance.
//
//  Both : les essais (et rien d'autre) - tout est montre, comme avant la 1.12.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace core {

enum class Edition : std::uint8_t { Both, Api, Ihm };

void setEdition(Edition) noexcept;
[[nodiscard]] Edition edition() noexcept;

// L'automate est-il la ? L'IHM ? (Both : les deux.)
[[nodiscard]] inline bool hasApi() noexcept { return edition() != Edition::Ihm; }
[[nodiscard]] inline bool hasIhm() noexcept { return edition() != Edition::Api; }

// "api", "ihm" ; "" pour Both (les dossiers, la cle de l'instance unique).
[[nodiscard]] std::string_view editionKey() noexcept;
// "API", "IHM" ; "" pour Both.
[[nodiscard]] std::string_view editionLabel() noexcept;
// "XPGAnalyser API", "XPGAnalyser IHM" ; "XPGAnalyser" pour Both.
[[nodiscard]] std::string productName();

} // namespace core
