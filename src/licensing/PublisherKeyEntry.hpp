// =============================================================================
//  licensing/PublisherKeyEntry.hpp - 1.30.0 : une cle publique de l'editeur,
//  telle que l'ecrit XPG-License-Admin exporter-cle
// -----------------------------------------------------------------------------
//  Deux fichiers GENERES en portent une liste :
//    PublisherKey.hpp      les VRAIES cles (kKeys), compilees dans chaque client ;
//                          vide tant que la vraie cle n'existe pas (les licences
//                          ne sont alors pas actives : LicenseStatus::NotConfigured) ;
//    TestPublisherKey.hpp  la cle d'ESSAI (kTestKeys), compilee SEULEMENT avec
//                          l'option CMake XPG_LICENCES_CLE_ESSAI (captures, essais) :
//                          jamais dans un programme livre.
//  Ce fichier-ci ne change pas : le format d'une ligne de ces listes.
// =============================================================================
#pragma once

#include <cstdint>

namespace licensing::publisher {

struct KeyEntry {
    const char*  id;        // "xpg-" + 16 hexadecimaux du SHA-256 de la cle
    const char*  name;      // le nom de la cle (une cle d'essai le dit)
    bool         test;      // une cle d'ESSAI : jamais dans un programme livre
    std::uint8_t key[32];   // la cle publique Ed25519
};

} // namespace licensing::publisher
