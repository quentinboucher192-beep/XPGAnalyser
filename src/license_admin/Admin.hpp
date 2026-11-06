// =============================================================================
//  license_admin/Admin.hpp - 1.30.0 (prepare en 1.29.0) : l'outil de l'EDITEUR
// -----------------------------------------------------------------------------
//  CE CODE N'EST JAMAIS LIVRE A UN CLIENT : il n'entre que dans XPG-License-Admin
//  (cible xpg_license_admin) et dans l'essai licensing_test. Le controle du
//  paquet (outils/mingw/package_mingw.py) arrete la construction si le nom de
//  l'outil, sa marque (kAdminMarker) ou une cle privee se trouve dans le dossier
//  a livrer.
//
//  LA CLE DE L'EDITEUR : une paire Ed25519 creee sur le poste Admin
//  (createPublisherKey), la partie secrete protegee par DPAPI pour le compte
//  Windows de l'administrateur (sous Linux : fichier 0600). Elle ne sort jamais
//  du poste ; seule sa cle PUBLIQUE est exportee, en C++
//  (src/licensing/PublisherKey.hpp), et compilee dans les logiciels clients.
//
//  SIGNER : le registre (les editions et fonctionnalites, numerote), la liste de
//  revocation (numerotee), et une licence a partir d'une ou plusieurs demandes
//  d'activation (les PC) et d'un fichier de parametres. Le fichier de licence
//  rendu au client contient la licence, le registre (et la liste de revocation
//  si elle est donnee) : un seul fichier a importer.
// =============================================================================
#pragma once

#include "../licensing/Documents.hpp"
#include "../licensing/KeyFile.hpp"
#include "../licensing/SignedFile.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace license_admin {

using licensing::Seconds;
using licensing::SigningKey;

// La marque de l'outil Admin : dans l'exe, dans le fichier de la cle de l'editeur.
// Le controle du paquet la cherche (outils/mingw/package_mingw.py).
inline constexpr const char* kAdminMarker = "XPG-LICENSE-ADMIN-NE-JAMAIS-LIVRER";
// La marque d'une cle d'ESSAI (dans son nom, donc dans PublisherKey.hpp et dans
// un programme qui l'embarque) : le controle du paquet la refuse aussi.
inline constexpr const char* kTestKeyMarker = "XPG-CLE-ESSAI-LICENCES";
inline constexpr const char* kPublisherKeyFile = "cle-editeur-licences.cle";

[[nodiscard]] const licensing::KeyFileSpec& publisherKeySpec();
// Windows : %LOCALAPPDATA%\XPG\LicenseAdmin ; ailleurs : $XDG_CONFIG_HOME/xpg-license-admin (ou ~/.config/...).
[[nodiscard]] std::string defaultKeyFolder();
[[nodiscard]] std::string publisherKeyPath(const std::string& folder);

// Creer la cle (refuse s'il y en a deja une). `test` : une cle d'ESSAI (son nom le dit, partout).
[[nodiscard]] std::unique_ptr<SigningKey> createPublisherKey(const std::string& folder, const std::string& name, bool test, std::string* why);
[[nodiscard]] std::unique_ptr<SigningKey> loadPublisherKey(const std::string& folder, std::string* why);

// Le fichier src/licensing/PublisherKey.hpp pour cette cle.
[[nodiscard]] std::string publisherKeyHeader(const SigningKey& key, Seconds now);

// Signer un registre ecrit a la main (JSON : number, unlicensed_edition,
// features, editions ; format, version et issued_at sont ajoutes s'ils manquent).
// `out` : le fichier .xpgreg. Faux : `why`.
bool signRegistry(const SigningKey& key, std::string_view inputJson, Seconds now, std::string& out, std::string* why);
// Signer une liste de revocation (JSON : number, revoked[] ; revoked_at manquant : maintenant).
bool signRevocations(const SigningKey& key, std::string_view inputJson, Seconds now, std::string& out, std::string* why);

struct LicenseInputs {
    std::string              params;        // le JSON des parametres (license_id, customer, edition...)
    std::vector<std::string> requests;      // le contenu des demandes d'activation (.xpgreq)
    std::string              existing;      // facultatif : la licence a etendre (.xpglic : ses PC sont gardes)
    std::string              registry;      // le registre signe (.xpgreg), obligatoire
    std::string              revocations;   // facultatif : la liste signee (.xpgrev), jointe au fichier
};
// Signer une licence. `out` : le fichier .xpglic (licence + registre [+ revocations]) ;
// `summary` : ce qui a ete signe, en une ligne par fait.
bool signLicense(const SigningKey& key, const LicenseInputs& in, Seconds now, std::string& out, std::string* why, std::string* summary);

// Lire un fichier signe et dire ce qu'il contient, et si chaque signature est
// bonne (celle de `key` pour licence, registre, revocations ; celle du PC pour une
// demande). Faux : un bloc est refuse.
bool describeFile(std::string_view bytes, const SigningKey* key, std::string& text);

} // namespace license_admin
