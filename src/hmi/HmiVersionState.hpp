// =============================================================================
//  hmi/HmiVersionState.hpp - l'etat du projet et les versions (lot API 6)
// -----------------------------------------------------------------------------
//  L'ETAT DIT OU EN EST LE TRAVAIL PAR RAPPORT AUX VERSIONS :
//    NEW     aucune version, rien de fait ; la premiere modification commence
//            la V1 (DEV) ;
//    DEV     on modifie la V(n+1), partie de la Vn ;
//    FINISH  "Terminer" a fait de la V(n+1) une version VALIDEE : le projet
//            est exactement elle. La premiere modification commence la suivante ;
//    LOCK    "Livrer et verrouiller" : la version est LIVREE (creee, ou la
//            validee promue si rien n'a change depuis), et le mot de passe pose.
//            Deverrouiller commence la suivante.
//  Une version intermediaire (brouillon) laisse le projet en DEV.
//
//  Ce fichier dit quoi afficher (standing) et quoi faire des versions
//  (closing) ; il ne touche pas au disque : versionstate_test le verifie.
// =============================================================================
#pragma once

#include "HmiVersions.hpp"
#include "../project/Security.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace hmi::ver {

struct Standing {
    project::State           state{project::State::New};
    int                      last{0};         // la derniere version (0 : aucune)
    int                      current{1};      // DEV, NEW : celle que l'on fait ; FINISH, LOCK : celle que l'on est
    std::string              title;           // "V5 en cours", "V4 validee", "V4 livree", "V1 a venir"
    std::string              subtitle;        // "depuis V4 . validee", "terminee le 26/09 a 18:40"
    std::string              tone;            // "new", "dev", "finish", "lock"
    std::string              tip;             // l'infobulle du bloc
    std::vector<std::string> heading;         // les lignes d'en-tete de son menu
};
// `changes` : les elements changes depuis la derniere version ; `unsaved` : les
// modifications pas encore enregistrees.
[[nodiscard]] Standing standing(project::State state, const Store* store, std::size_t changes, std::size_t unsaved);

// Terminer (-> FINISH) ou Livrer (-> LOCK) : la version a creer, ou la derniere
// a promouvoir (FINISH -> LOCK sans rien de change : la validee devient livree).
struct Closing {
    bool  promote{false};
    int   number{0};
    State versionState{State::Validated};
};
[[nodiscard]] Closing closing(project::State from, project::State to, const Store& store, bool changedSinceLast);

// Le nom propose : "Terminee le 28/09", "Livree le 28/09" ; `now` vide : l'heure du poste.
[[nodiscard]] std::string defaultName(project::State to, const std::string& now = {});

// "2026-09-26 18:40" -> "26/09 a 18:40".
[[nodiscard]] std::string whenText(const std::string& stamp);

} // namespace hmi::ver
