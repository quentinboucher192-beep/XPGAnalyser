// =============================================================================
//  license_admin/KeyBackup.hpp - 1.32.0 : LA SAUVEGARDE DE LA CLE DE L'EDITEUR,
//  CHIFFREE PAR UNE PHRASE DE PASSE (l'outil de l'editeur seulement ; jamais livre)
// -----------------------------------------------------------------------------
//  DPAPI lie le fichier de la cle au compte Windows qui l'a creee : une panne de
//  ce PC, et la cle est perdue. La sauvegarde (.xpgcle) se relit PARTOUT, avec la
//  phrase de passe que l'editeur choisit et tape lui-meme (jamais affichee,
//  jamais ecrite) :
//
//    XPG-SAUVEGARDE-CLE 1
//    cle=<identifiant xpg-...>
//    argon2id ops=<n> mem=<octets> sel=<32 hexadecimaux>
//    <le base64 : nonce (24 octets) + chiffre XChaCha20-Poly1305>
//
//  La cle de chiffrement vient de la phrase par Argon2id (libsodium,
//  crypto_pwhash : 3 passes, 256 Mio par defaut) ; les trois lignes d'en-tete
//  sont liees au chiffre (donnees associees) : changer l'identifiant ou les
//  parametres le rend illisible. A la relecture, des parametres plus faibles que
//  le minimum (2 passes, 64 Mio) ou demesures (plus de 16 passes, plus de 1 Gio)
//  sont refuses. Le contenu : la cle secrete, la publique, le nom, essai ou non.
//
//  Restaurer ecrit le fichier de la cle (DPAPI de CE compte) ; une cle deja la
//  n'est jamais remplacee.
// =============================================================================
#pragma once

#include "../licensing/KeyFile.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace license_admin {

inline constexpr const char* kKeyBackupMarker = "XPG-SAUVEGARDE-CLE";
inline constexpr const char* kKeyBackupExtension = ".xpgcle";

// Une phrase de passe acceptable : 12 caracteres au moins, pas un seul caractere repete, pas que des chiffres.
[[nodiscard]] bool acceptablePassphrase(std::string_view phrase, std::string* why);

struct KeyBackupOptions {
    unsigned long long opsLimit{0};   // 0 : crypto_pwhash_OPSLIMIT_MODERATE
    std::size_t        memLimit{0};   // 0 : crypto_pwhash_MEMLIMIT_MODERATE
};

// Ecrire la sauvegarde de `key`. Faux : `why`.
bool writeKeyBackup(const licensing::SigningKey& key, std::string_view phrase, std::string& out, std::string* why,
                    const KeyBackupOptions& options = {});
// Ce que dit l'en-tete (sans la phrase) : l'identifiant de la cle, les parametres. Faux : pas une sauvegarde.
bool describeKeyBackup(std::string_view backup, std::string& keyId, std::string& text, std::string* why);
// Verifier la phrase et la sauvegarde sans rien ecrire : `keyId` et `name`. Faux : `why`.
bool checkKeyBackup(std::string_view backup, std::string_view phrase, std::string& keyId, std::string& name, std::string* why);
// Restaurer : la cle ecrite dans `folder` (le fichier de la cle de l'editeur), protegee pour ce compte.
[[nodiscard]] std::unique_ptr<licensing::SigningKey> restoreKeyBackup(const std::string& folder, std::string_view backup, std::string_view phrase,
                                                                      std::string* why);

} // namespace license_admin
