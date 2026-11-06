// =============================================================================
//  licensing/KeyFile.hpp - 1.30.0 (prepare en 1.29.0) : une paire Ed25519 gardee
//  sur CE PC, dont la partie secrete ne sort pas de l'objet
// -----------------------------------------------------------------------------
//  Deux usages, deux << formats >> de fichier, le meme code :
//    - l'IDENTITE DU PC (DeviceIdentity, ci-dessous) : creee sur le PC client la
//      premiere fois qu'il en a besoin ; partie secrete protegee par DPAPI pour
//      la MACHINE (tous les comptes du PC, et aucun autre PC) ; la licence est
//      liee a sa cle publique - jamais au materiel (ni MAC, ni numero de serie,
//      ni nom de PC transmis) ;
//    - la CLE DE L'EDITEUR (l'outil Admin, src/license_admin/) : protegee par
//      DPAPI pour le COMPTE Windows de l'administrateur.
//  Sous Linux : pas de DPAPI - la partie secrete est en clair dans un fichier
//  lisible par son seul proprietaire (0600, crypto::writePrivateFile), et c'est
//  ecrit dans le fichier (protection=fichier).
//
//  LE FICHIER : du texte, une cle=valeur par ligne (les lignes # sont des
//  commentaires, donnes par l'appelant) : format, nom, essai, publique
//  (hexadecimal), protection, secrete (hexadecimal de la partie protegee).
//  UNE CLE QUI NE SE RELIT PAS N'EST JAMAIS REMPLACEE EN SILENCE (comme
//  deploy::DevKey) : la licence de ce PC, ou les licences signees, en dependent.
//
//  CE QUE CA PROTEGE, HONNETEMENT : la cle ne se recopie pas sur un autre PC
//  (DPAPI), et elle n'est pas lisible en ouvrant le fichier. Un programme qui
//  tourne sur CE PC peut l'utiliser (c'est le principe de DPAPI machine).
// =============================================================================
#pragma once

#include "../deploy/Crypto.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace licensing {

namespace crypto = deploy::crypto;

struct KeyFileSpec {
    std::string format;      // "xpg-identite-pc/1" : ce que le fichier doit dire pour etre relu
    bool        machine{false};   // DPAPI : la machine (vrai) ou le compte (faux)
    std::string comment;     // les lignes # en tete (sans le #), separees par \n
    std::string what;        // "la cl\xC3\xA9 de ce PC" : pour les messages
};

class SigningKey {
public:
    // Relit la cle de `path`. nullptr : `why` (absente, illisible, protegee ailleurs...).
    [[nodiscard]] static std::unique_ptr<SigningKey> load(const std::string& path, const KeyFileSpec& spec, std::string* why);
    // Cree une paire neuve et l'ecrit. Refuse si le fichier existe deja.
    [[nodiscard]] static std::unique_ptr<SigningKey> create(const std::string& path, const KeyFileSpec& spec, const std::string& name,
                                                            bool test, std::string* why);
    // Relit, ou cree si le fichier n'existe pas.
    [[nodiscard]] static std::unique_ptr<SigningKey> loadOrCreate(const std::string& path, const KeyFileSpec& spec,
                                                                  const std::string& name, std::string* why);

    ~SigningKey();
    SigningKey(const SigningKey&) = delete;
    SigningKey& operator=(const SigningKey&) = delete;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] bool test() const noexcept { return test_; }   // une cle d'ESSAI (l'editeur) ; jamais pour l'identite d'un PC
    [[nodiscard]] const crypto::PublicKey& publicKey() const noexcept { return pub_; }
    [[nodiscard]] std::string publicKeyHex() const { return crypto::hex(pub_); }
    [[nodiscard]] std::string keyId() const;                       // "xpg-..." (SignedFile.hpp)
    [[nodiscard]] std::string fingerprint(std::size_t groups = 4) const;
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] bool created() const noexcept { return created_; }

    [[nodiscard]] crypto::Signature sign(std::string_view message) const noexcept;

private:
    SigningKey() = default;
    bool save(const KeyFileSpec& spec, std::string* why) const;

    std::string       path_;
    std::string       name_;
    bool              test_{false};
    crypto::PublicKey pub_{};
    crypto::SecretKey secret_{};
    bool              created_{false};
};

// L'empreinte lisible d'une cle publique (SHA-256) : "4F:9A:12:34:...:AB:CD:EF:01".
[[nodiscard]] std::string publicKeyFingerprint(const crypto::PublicKey& key, std::size_t groups = 4);

// ---- l'identite du PC ----
inline constexpr const char* kDeviceKeyFile = "identite-pc.cle";
[[nodiscard]] const KeyFileSpec& deviceKeySpec();

} // namespace licensing
