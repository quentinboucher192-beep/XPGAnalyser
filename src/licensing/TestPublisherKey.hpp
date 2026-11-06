// =============================================================================
//  licensing/TestPublisherKey.hpp - LA CLE D'ESSAI DE L'EDITEUR (XPG-CLE-ESSAI-LICENCES)
// -----------------------------------------------------------------------------
//  GENERE par l'outil Admin des licences (XPG-License-Admin exporter-cle) le 2026-11-06T18:00:00Z.
//  Ne le modifie pas a la main : regenere-le sur le poste Admin.
//  Seules des cles PUBLIQUES sont ici : elles VERIFIENT les licences, les registres
//  et les revocations ; elles ne permettent pas d'en signer.
//
//  !!! CLE D'ESSAI : elle ne sert qu'au developpement, aux essais et aux captures.
//  Elle n'est compilee QU'AVEC l'option CMake XPG_LICENCES_CLE_ESSAI (OFF par
//  defaut) : sans elle, ce fichier est vide pour le compilateur et sa marque
//  n'entre dans aucun programme. Un programme construit avec l'option n'est PAS
//  livrable : le controle du paquet (outils/mingw/package_mingw.py,
//  outils/lib/package.ps1) arrete la construction. !!!
//
//  1 cle (identifiant, empreinte SHA-256) :
//    xpg-cb0d1ae560b52310  CB:0D:1A:E5:60:B5:23:10:50:92:55:EC:35:7B:53:52:82:EA:4A:31:67:19:FA:99:C0:47:03:56:3A:79:79:8E
//
//  Les vraies cles ne vont jamais ici : PublisherKey.hpp.
// =============================================================================
#pragma once

#include "PublisherKeyEntry.hpp"

#ifdef XPG_LICENCES_CLE_ESSAI

#include <array>

namespace licensing::publisher {

inline constexpr std::array<KeyEntry, 1> kTestKeys{{
    {"xpg-cb0d1ae560b52310", "CLE D'ESSAI XPG-CLE-ESSAI-LICENCES - NE PAS LIVRER (developpement 1.29.0)", true,
     {0x00, 0x5c, 0x4e, 0xc5, 0x6e, 0x1f, 0xcf, 0xf0,
      0x34, 0x1a, 0x28, 0x50, 0x29, 0x81, 0xa5, 0x49,
      0xae, 0x71, 0x64, 0x13, 0x75, 0x8f, 0xc9, 0x70,
      0x0b, 0x9c, 0xf6, 0x00, 0xaf, 0xd7, 0x4e, 0xc4}},
}};

} // namespace licensing::publisher

#endif // XPG_LICENCES_CLE_ESSAI
