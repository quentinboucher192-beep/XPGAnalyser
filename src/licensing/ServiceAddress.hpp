// =============================================================================
//  licensing/ServiceAddress.hpp - 1.35.0 : L'ADRESSE DU SERVICE DES LICENCES,
//  compilee dans les logiciels des clients (API Studio, IHM Studio)
// -----------------------------------------------------------------------------
//  Le Worker Cloudflare de l'editeur (service/, docs/SERVICE-LICENCES.md) :
//  "https://xpg-licences.<son-compte>.workers.dev". Ce n'est pas un secret.
//
//  VIDE : ce programme n'a pas de service - les licences passent par fichier
//  seulement (la Decouverte marche quand meme, hors ligne).
//
//  Ecrit par l'outil de l'editeur (Parametres de securite > Preparer les
//  fichiers pour les Studios), avec PublisherKey.hpp. L'option CMake
//  XPG_SERVICE_URL (non vide) la remplace a la construction ; dans une
//  construction d'essai, la variable d'environnement XPG_SERVICE aussi.
// =============================================================================
#pragma once

namespace licensing::service {

inline constexpr const char* kCompiledServiceUrl = "https://xpg-licences.xpg-licences-workersdev.workers.dev";

} // namespace licensing::service
