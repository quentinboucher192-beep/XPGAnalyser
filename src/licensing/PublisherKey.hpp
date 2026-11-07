// =============================================================================
//  licensing/PublisherKey.hpp - LES CLES PUBLIQUES DE L'EDITEUR, compilees dans
//  les logiciels clients (API Studio, IHM Studio, License Manager, agent)
// -----------------------------------------------------------------------------
//  GENERE le 2026-11-07T19:15:03Z depuis cle-publique.txt du kit de l'editeur (la meme fonction que l'outil : publisherKeyHeader).
//  Ne le modifie pas a la main : regenere-le sur le poste Admin.
//  Seules des cles PUBLIQUES sont ici : elles VERIFIENT les licences, les registres
//  et les revocations ; elles ne permettent pas d'en signer.
//
//  1 cle (identifiant, empreinte SHA-256) :
//    xpg-1f422b8063a39e7c  1F:42:2B:80:63:A3:9E:7C:19:6A:3E:60:CB:4C:8E:A6:5F:AB:8A:D2:B9:48:9E:87:CC:10:C6:E8:49:B8:55:0E
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

inline constexpr std::array<KeyEntry, 1> kKeys{{
    {"xpg-1f422b8063a39e7c", "Cle de l'editeur des licences XPG", false,
     {0x7f, 0x3d, 0xb6, 0x5e, 0xc7, 0x76, 0x14, 0xeb,
      0x7e, 0xf8, 0xc9, 0xda, 0xd8, 0xcc, 0x4e, 0x1c,
      0xf0, 0xfa, 0xae, 0x60, 0xb6, 0x7e, 0xf9, 0xb1,
      0xd6, 0xeb, 0x9e, 0xc1, 0x2b, 0x24, 0x90, 0xdf}},
}};

} // namespace licensing::publisher
