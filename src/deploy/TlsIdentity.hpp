// =============================================================================
//  deploy/TlsIdentity.hpp - 1.28.0 : le certificat TLS d'une machine
// -----------------------------------------------------------------------------
//  Chaque machine (l'agent, le Studio) a une cle ECDSA P-256 et un certificat
//  X.509 auto-signe, crees au premier lancement dans le dossier donne :
//      <dossier>/<nom>.cer   le certificat (DER, public)
//      <dossier>/<nom>.cle   la cle privee (DER) protegee par crypto::protect
//                            (Windows : DPAPI, le compte - 1.32.0 : l'agent aussi,
//                            plus la machine ; ailleurs : un fichier en 0600)
//  Aucune autorite ne juge ce certificat : son EMPREINTE (SHA-256 du DER) est
//  epinglee a l'appairage (deploy/Pairing). L'identifiant lisible d'une machine
//  en est tire : "M-" et les deux premiers octets ("M-7F21").
// =============================================================================
#pragma once

#include "Crypto.hpp"

#include <string>

namespace deploy {

class TlsIdentity {
public:
    TlsIdentity() = default;
    ~TlsIdentity();
    TlsIdentity(const TlsIdentity&) = default;
    TlsIdentity& operator=(const TlsIdentity&) = default;

    // Lit <dir>/<baseName>.cer et .cle, ou les cree s'ils manquent tous les deux. `machineScope` :
    // la cle protegee pour la machine (l'agent, un service) plutot que pour le compte (le Studio).
    // `commonName` : le nom porte par un certificat neuf ("XPG-Agent PC-ATELIER-1"). Faux : `why`
    // (une cle illisible n'est jamais remplacee en silence : l'empreinte changerait).
    static bool loadOrCreate(const std::string& dir, const std::string& baseName, bool machineScope,
                             const std::string& commonName, TlsIdentity& out, std::string* why);
    // Une identite neuve, en memoire seulement (les essais : un tiers, un certificat qui change).
    static bool generate(const std::string& commonName, TlsIdentity& out, std::string* why);

    [[nodiscard]] bool valid() const noexcept { return !cert_.empty() && !key_.empty(); }
    [[nodiscard]] const crypto::Bytes& certificateDer() const noexcept { return cert_; }
    [[nodiscard]] const crypto::Bytes& privateKeyDer() const noexcept { return key_; }
    [[nodiscard]] const crypto::Digest& fingerprint() const noexcept { return fp_; }
    [[nodiscard]] std::string fingerprintHex() const { return crypto::hex(fp_); }
    [[nodiscard]] std::string machineId() const;

private:
    crypto::Bytes cert_;
    crypto::Bytes key_;
    crypto::Digest fp_{};
};

// "M-7F21" : l'identifiant lisible tire d'une empreinte.
[[nodiscard]] std::string machineIdOf(const crypto::Digest& fingerprint);
// Le meme, depuis l'empreinte en hexadecimal ("" si elle est invalide).
[[nodiscard]] std::string machineIdOfHex(const std::string& fingerprintHex);

} // namespace deploy
