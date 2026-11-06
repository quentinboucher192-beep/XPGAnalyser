// =============================================================================
//  deploy/DevKey.hpp - 1.28.0 : la cle d'editeur du developpeur (Ed25519)
// -----------------------------------------------------------------------------
//  UNE PAIRE PAR PC DE DEVELOPPEMENT, creee a la premiere utilisation (Creer un
//  package de deploiement, le premier appairage) et relue ensuite. Elle signe
//  chaque paquet .xpgdeploy (le manifeste, octet pour octet) ; la cle publique
//  part avec chaque paquet et a chaque appairage : l'agent ne croit qu'un paquet
//  signe par une cle qu'il a recue a l'appairage.
//
//  LE FICHIER : <dossier>/cle_editeur.bin (le dossier est donne par
//  l'appelant : les reglages de l'application). Du texte, une cle=valeur par
//  ligne : format, nom (l'editeur), publique (hexadecimal), protection, secrete.
//  La partie secrete est protegee par crypto::protect (DPAPI, CE compte
//  Windows) ; sous Linux elle est en clair dans un fichier lisible par son
//  seul proprietaire (0600, crypto::writePrivateFile).
//
//  LA CLE PRIVEE NE SORT PAS DE L'OBJET : sign() est le seul usage. Elle est
//  effacee de la memoire a la destruction (crypto::wipe), et les tampons de
//  lecture et d'ecriture aussi.
//
//  UNE CLE QUI NE SE RELIT PAS N'EST JAMAIS REMPLACEE en silence : les PC
//  appaires ne reconnaitraient plus les paquets. loadOrCreate le dit (`why`) ;
//  c'est a l'utilisateur de supprimer le fichier s'il veut une cle neuve.
//
//  Exemple (l'interface) :
//      std::string why;
//      auto key = deploy::DevKey::loadOrCreate(dossierDesReglages, &why);
//      if (!key) { afficher(why); return; }
//      afficher(key->name() + " - " + key->fingerprint());
// =============================================================================
#pragma once

#include "Crypto.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace deploy {

class DevKey {
public:
    static constexpr const char* kFileName = "cle_editeur.bin";

    // Le chemin du fichier de la cle dans `folder` (UTF-8).
    [[nodiscard]] static std::string filePath(const std::string& folder);

    // Relit la cle de `folder`, ou la cree si le fichier n'existe pas (le
    // dossier est cree). `name` : le nom d'editeur d'une cle NEUVE (vide : le
    // nom de ce PC) ; une cle relue garde le sien. nullptr : `why` dit pourquoi.
    [[nodiscard]] static std::unique_ptr<DevKey> loadOrCreate(const std::string& folder, std::string* why,
                                                              const std::string& name = {});

    // Le nom de ce PC (UTF-8), "DEV-LUCAS" ; "PC" s'il est introuvable.
    [[nodiscard]] static std::string computerName();

    ~DevKey();
    DevKey(const DevKey&) = delete;
    DevKey& operator=(const DevKey&) = delete;

    // Le nom de l'editeur (publisher.name du manifeste).
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    // La cle publique, brute et en hexadecimal (64 caracteres, publisher.key).
    [[nodiscard]] const crypto::PublicKey& publicKey() const noexcept { return pub_; }
    [[nodiscard]] std::string publicKeyHex() const { return crypto::hex(pub_); }
    // L'empreinte lisible (SHA-256 de la cle publique) : "4F:9A:12:34:...:AB:CD:EF:01" ; 0 : complete.
    [[nodiscard]] std::string fingerprint(std::size_t groups = 4) const;
    // Le fichier d'ou elle vient.
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    // Vrai : creee par cet appel de loadOrCreate (premiere utilisation).
    [[nodiscard]] bool created() const noexcept { return created_; }

    // Changer le nom d'editeur (le fichier est reecrit ; la paire ne change pas).
    bool rename(const std::string& name, std::string* why);

    // Signer `message` (Ed25519, 64 octets).
    [[nodiscard]] crypto::Signature sign(std::string_view message) const noexcept;

private:
    DevKey() = default;
    static std::unique_ptr<DevKey> load(const std::string& path, std::string* why);
    bool save(std::string* why) const;

    std::string        path_;
    std::string        name_;
    crypto::PublicKey  pub_{};
    crypto::SecretKey  secret_{};
    bool               created_{false};
};

// L'empreinte lisible d'une cle publique d'editeur, comme DevKey::fingerprint
// (pour montrer l'editeur d'un paquet ou d'un PC appaire).
[[nodiscard]] std::string keyFingerprint(const crypto::PublicKey& key, std::size_t groups = 4);

} // namespace deploy
