// =============================================================================
//  deploy/Crypto.hpp - 1.28.0 : le socle cryptographique du deploiement reseau
// -----------------------------------------------------------------------------
//  Une facade mince sur libsodium (third_party/libsodium) : rien n'est reecrit
//  ici, chaque fonction appelle la bibliotheque.
//
//    sha256 / sha256Hex / sha256File   les empreintes (manifeste, fichiers, certificats)
//    SignKeys, sign, verify            Ed25519 (les signatures des paquets)
//    randomBytes, randomDigits         le hasard du systeme (le code d'appairage)
//    protect / unprotect               une cle gardee sur CE PC : DPAPI sous Windows
//                                      (l'utilisateur, ou la machine pour l'agent) ;
//                                      sous Linux, aucun chiffrement - le fichier
//                                      est ecrit en 0600 par writePrivateFile
//    wipe                              effacer un secret de la memoire
//
//  init() doit avoir rendu vrai avant tout appel (il est sans danger de
//  l'appeler plusieurs fois, de plusieurs fils).
// =============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deploy::crypto {

using Bytes = std::vector<std::uint8_t>;
using Digest = std::array<std::uint8_t, 32>;
using PublicKey = std::array<std::uint8_t, 32>;
using SecretKey = std::array<std::uint8_t, 64>;
using Signature = std::array<std::uint8_t, 64>;

// libsodium pret (sodium_init). Faux : le hasard du systeme est indisponible.
[[nodiscard]] bool init() noexcept;

// ---- les empreintes SHA-256 ----
[[nodiscard]] Digest sha256(const void* data, std::size_t size) noexcept;
[[nodiscard]] Digest sha256(std::string_view data) noexcept;
[[nodiscard]] std::string sha256Hex(std::string_view data);
// Un fichier, lu par blocs : son empreinte (hexadecimal minuscule) et sa taille. Faux : `why`.
bool sha256File(const std::string& path, std::string& hexDigest, std::uint64_t* size, std::string* why);

// ---- l'hexadecimal (minuscules) ----
[[nodiscard]] std::string hex(const void* data, std::size_t size);
template <std::size_t N>
[[nodiscard]] std::string hex(const std::array<std::uint8_t, N>& a) { return hex(a.data(), a.size()); }
// Faux : pas de l'hexadecimal, ou pas la bonne longueur (`expected` octets ; 0 : n'importe laquelle).
bool unhex(std::string_view text, Bytes& out, std::size_t expected = 0);
// Une empreinte lisible : "4F:9A:...:C1" (les `groups` premiers et derniers octets ; 0 : tous).
[[nodiscard]] std::string fingerprintText(const Digest& d, std::size_t groups = 2);

// ---- Ed25519 ----
struct SignKeys {
    PublicKey pub{};
    SecretKey secret{};
};
[[nodiscard]] SignKeys newSignKeys();
[[nodiscard]] Signature sign(const SecretKey& secret, std::string_view message) noexcept;
[[nodiscard]] bool verify(const PublicKey& pub, std::string_view message, const Signature& sig) noexcept;

// ---- le hasard ----
void randomBytes(void* out, std::size_t size) noexcept;
// `count` chiffres decimaux tires uniformement ("482913").
[[nodiscard]] std::string randomDigits(int count);

// Effacer un secret (que l'optimiseur ne retire pas).
void wipe(void* data, std::size_t size) noexcept;
template <std::size_t N>
void wipe(std::array<std::uint8_t, N>& a) noexcept { wipe(a.data(), a.size()); }

// ---- garder un secret sur ce PC ----
// Windows : CryptProtectData (machine : CRYPTPROTECT_LOCAL_MACHINE - un service le relit ; sinon l'utilisateur).
// Ailleurs : recopie telle quelle (le fichier, lui, est en 0600 : writePrivateFile). `protectedHere()` le dit.
[[nodiscard]] bool protectedHere() noexcept;
bool protect(const Bytes& plain, Bytes& out, bool machine, std::string* why);
bool unprotect(const Bytes& in, Bytes& plain, std::string* why);

// Ecrire un fichier lisible par son seul proprietaire (0600 hors Windows), par un fichier temporaire renomme.
bool writePrivateFile(const std::string& path, const Bytes& data, std::string* why);
bool readFile(const std::string& path, Bytes& out, std::string* why, std::uint64_t maxSize = 64u * 1024u * 1024u);

} // namespace deploy::crypto
