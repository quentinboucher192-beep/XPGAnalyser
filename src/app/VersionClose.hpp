// =============================================================================
//  app/VersionClose.hpp - Terminer et Livrer une version (lot API 6)
// -----------------------------------------------------------------------------
//  TERMINER (-> FINISH) : le projet est enregistre, passe FINISH, puis une
//  version VALIDEE le photographie - elle dit FINISH, et la restaurer rend un
//  projet termine.
//  LIVRER ET VERROUILLER (-> LOCK) : la version LIVREE d'abord - la derniere
//  validee promue si rien n'a change depuis, sinon une nouvelle, qui dit FINISH
//  et se restaure donc sans verrou - puis le mot de passe.
//
//  Les deux passent par l'API publique d'App : l'ecran (le menu de la version,
//  le volet Versions) et le dialogue Projet > Etat du projet s'en servent.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../project/Security.hpp"

#include <string>

namespace app {

class App;

// Rend le numero de la version faite (creee ou promue).
[[nodiscard]] core::Result<int> closeVersion(App& app, project::State target, std::string name, std::string comment,
                                             const std::string& password = {});

} // namespace app
