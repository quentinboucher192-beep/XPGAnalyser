// =============================================================================
//  licensing/PublisherKey.hpp - LES CLES PUBLIQUES DE L'EDITEUR, compilees dans
//  les logiciels clients (API Studio, IHM Studio, License Manager, agent)
// -----------------------------------------------------------------------------
//  GENERE par l'outil Admin des licences (XPG-License-Admin exporter-cle) le 2026-11-06T19:38:42Z.
//  Ne le modifie pas a la main : regenere-le sur le poste Admin.
//  Seules des cles PUBLIQUES sont ici : elles VERIFIENT les licences, les registres
//  et les revocations ; elles ne permettent pas d'en signer.
//
//  AUCUNE CLE : les licences ne sont pas encore actives dans ce programme ; cree
//  la cle sur le poste Admin (creer-cle), puis exporter-cle vers ce fichier.
//  En attendant, rien n'est verrouille : toutes les fonctionnalites sont ouvertes
//  (LicenseStatus::NotConfigured, licensing/Licensing.hpp).
//
//  Plusieurs cles (une rotation) : exporter-cle --garder ajoute la cle du poste a
//  celles deja ecrites ici ; les clients croient chacune. Une cle retiree s'enleve
//  (exporter-cle sans --garder) : ses licences tombent. Les cles d'ESSAI ne vont
//  jamais ici : TestPublisherKey.hpp (option CMake XPG_LICENCES_CLE_ESSAI).
// =============================================================================
#pragma once

#include "PublisherKeyEntry.hpp"

#include <array>

namespace licensing::publisher {

inline constexpr std::array<KeyEntry, 0> kKeys{};

} // namespace licensing::publisher
