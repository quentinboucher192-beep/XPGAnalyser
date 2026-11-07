// =============================================================================
//  licensing/GatewayAddress.hpp - 1.34.0 : L'ADRESSE DE LA PASSERELLE GITHUB DES
//  LICENCES, compilee dans les logiciels des clients (API Studio, IHM Studio)
// -----------------------------------------------------------------------------
//  Le depot GitHub PUBLIC ou les logiciels des clients envoient leurs demandes
//  d'activation et recuperent leur licence (licensing/Gateway.hpp). Ce n'est pas
//  un secret : "proprietaire/nom", comme dans l'adresse github.com/proprietaire/nom.
//
//  VIDE : ce programme n'a pas de passerelle - les demandes et les licences
//  passent par fichier, comme avant la 1.34.0.
//
//  L'option CMake XPG_PASSERELLE_DEPOT (non vide) la remplace a la construction ;
//  dans une construction d'essai, la variable d'environnement XPG_PASSERELLE
//  aussi (le faux GitHub des essais).
// =============================================================================
#pragma once

namespace licensing::gateway {

inline constexpr const char* kCompiledRepo = "";
inline constexpr const char* kCompiledBranch = "main";

} // namespace licensing::gateway
