// =============================================================================
//  license_admin/Edition.hpp - 1.49.0 : QUEL ADMIN TOURNE - l'Admin+ (l'editeur : XPG-License-Manager-Admin.exe,
//  l'installateur << EDITEUR >>) ou l'Admin classique (les admins : un executable a part, recu avec une invitation).
//  Les deux programmes partagent leurs bibliotheques ; c'est leur main qui dit lequel il est (setEdition, une fois,
//  au lancement). Le Plus est le defaut (les essais, la console). La vraie protection reste la cle de l'editeur :
//  l'Admin classique n'offre simplement jamais ce qui est a l'admin+ (docs/ADMINS.md, par. 12).
// =============================================================================
#pragma once

namespace license_admin {

enum class Edition : unsigned char { Plus, Classique };

inline Edition& editionRef() noexcept {
    static Edition e = Edition::Plus;
    return e;
}
[[nodiscard]] inline Edition edition() noexcept { return editionRef(); }
inline void setEdition(Edition e) noexcept { editionRef() = e; }
[[nodiscard]] inline bool classique() noexcept { return edition() == Edition::Classique; }

} // namespace license_admin
