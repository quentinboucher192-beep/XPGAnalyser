// =============================================================================
//  hmi/HmiPolicy.hpp - la politique des mots de passe et le badge (lot 13)
// -----------------------------------------------------------------------------
//  LA MEME REGLE PARTOUT OU UN MOT DE PASSE SE DONNE : l'editeur
//  (Configuration > Utilisateurs, 4 caracteres au moins), le menu de connexion
//  et l'objet Changer le mot de passe (6), le renouvellement a la connexion,
//  les dialogues de la gestion des comptes en marche. La politique du projet
//  (Security::pw...) s'ajoute a chaque endroit : la plus grande longueur
//  compte, chaque regle cochee joue.
//
//  LA PEREMPTION : un mot de passe date (User::passwordSet) perime au bout de
//  Security::pwMaxAgeDays jours ; un mot de passe donne par un administrateur
//  (Security::pwChangeFirst) se change a la premiere connexion. Dans les deux
//  cas la connexion attend le nouveau (Runtime : le renouvellement).
//
//  LE BADGE : son numero n'est jamais garde en clair - une empreinte salee
//  ("sel:empreinte"), comme un mot de passe.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hmi {

// Le minimum des menus en marche (Mon compte, le renouvellement a la connexion).
inline constexpr std::size_t kMenuPasswordMin = 6;

// Ce qui ne va pas dans `password` pour la politique du projet ("" : rien).
// `localMin` : le minimum de l'endroit ; `user` (facultatif) : ses mots de
// passe recents (Security::pwHistory, le mot de passe du moment compris).
[[nodiscard]] std::string passwordProblem(const Security&, const User* user, std::string_view password, std::size_t localMin);
// "8 caracteres au moins, un chiffre, des majuscules et des minuscules" : ce
// qu'on demande (l'aide des champs, le dialogue).
[[nodiscard]] std::string passwordRules(const Security&, std::size_t localMin);

// "2026-09-25 14:05:12.350" -> "2026-09-25".
[[nodiscard]] std::string dayOf(std::string_view stamp);
// Les jours de `from` a `to` ("2026-09-25") ; nullopt si l'une est illisible.
[[nodiscard]] std::optional<long> daysBetween(std::string_view from, std::string_view to);
// Les jours qui restent avant la peremption (negatif : perime) ; nullopt : il
// n'expire pas (pas de duree, pas de date, pas de mot de passe).
[[nodiscard]] std::optional<long> passwordDaysLeft(const Security&, const User&, std::string_view today);
// Pourquoi le mot de passe doit changer avant d'entrer ("" : il n'a pas a changer).
[[nodiscard]] std::string renewalReason(const Security&, const User&, std::string_view today);
// Un nouveau mot de passe pose sur le compte : le precedent rejoint l'historique
// (Security::pwHistory), la date du jour, le changement a la premiere connexion
// quand c'est un administrateur qui le donne (Security::pwChangeFirst).
void storePassword(const Security&, User&, std::string salt, std::string hash, std::string_view today, bool byAdministrator);

// Le badge : "sel:empreinte" de son numero ; la comparaison.
[[nodiscard]] std::string badgeHash(std::string_view number);
[[nodiscard]] bool        badgeMatches(std::string_view stored, std::string_view number);

} // namespace hmi
