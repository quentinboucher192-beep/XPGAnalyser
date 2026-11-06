// =============================================================================
//  licensing/PublisherKey.hpp - LA CLE PUBLIQUE DE L'EDITEUR, compilee dans les
//  logiciels clients (API Studio, IHM Studio, License Manager, agent)
// -----------------------------------------------------------------------------
//  GENERE par l'outil Admin des licences (commande exporter-cle) le 2026-11-06T17:22:24Z.
//  Ne le modifie pas a la main : regenere-le sur le poste Admin.
//  Seule la cle PUBLIQUE est ici : elle VERIFIE les licences, les registres et les
//  revocations ; elle ne permet pas d'en signer. La cle privee reste sur le poste Admin.
//  Empreinte (SHA-256 de la cle) : CB:0D:1A:E5:60:B5:23:10:50:92:55:EC:35:7B:53:52:82:EA:4A:31:67:19:FA:99:C0:47:03:56:3A:79:79:8E
//
//  !!! CLE D'ESSAI (XPG-CLE-ESSAI-LICENCES) : elle ne sert qu'au developpement et aux
//  essais. Un programme qui l'embarque n'est PAS livrable : le controle du paquet
//  (outils/mingw/package_mingw.py) arrete la construction. Avant de livrer : cree la
//  vraie cle sur le poste Admin (creer-cle), puis exporter-cle vers ce fichier. !!!
//
//  Plusieurs cles (une rotation) : une ligne par cle dans kKeys ; les clients
//  croient chacune. Une cle retiree s'enleve d'ici (et ses licences tombent).
// =============================================================================
#pragma once

#include <cstdint>

namespace licensing::publisher {

struct KeyEntry {
    const char*  id;     // "xpg-" + 16 hexadecimaux du SHA-256 de la cle
    const char*  name;   // le nom de la cle (une cle d'essai le dit)
    bool         test;   // une cle d'ESSAI : jamais dans un programme livre
    std::uint8_t key[32];
};

inline constexpr KeyEntry kKeys[] = {
    {"xpg-cb0d1ae560b52310", "CLE D'ESSAI XPG-CLE-ESSAI-LICENCES - NE PAS LIVRER (developpement 1.29.0)", true,
     {0x00, 0x5c, 0x4e, 0xc5, 0x6e, 0x1f, 0xcf, 0xf0,
      0x34, 0x1a, 0x28, 0x50, 0x29, 0x81, 0xa5, 0x49,
      0xae, 0x71, 0x64, 0x13, 0x75, 0x8f, 0xc9, 0x70,
      0x0b, 0x9c, 0xf6, 0x00, 0xaf, 0xd7, 0x4e, 0xc4}},
};

} // namespace licensing::publisher
