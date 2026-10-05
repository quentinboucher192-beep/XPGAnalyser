// =============================================================================
//  hmi/HmiCrypto.hpp — empreintes et codes : les mots de passe des utilisateurs
// -----------------------------------------------------------------------------
//  UN MOT DE PASSE N'EST JAMAIS GARDE EN CLAIR. On garde un sel (16 octets
//  aleatoires) et l'empreinte SHA-256 du sel et du mot de passe, iteree : qui
//  lit ihm.txt ne lit pas le mot de passe, et deux utilisateurs au meme mot de
//  passe n'ont pas la meme empreinte.
//
//  LE MOT DE PASSE DYNAMIQUE est un code a N chiffres qui change toutes les P
//  secondes (TOTP, RFC 6238, avec HMAC-SHA-256) : le secret est partage avec
//  l'application d'authentification de l'utilisateur. On accepte le code de la
//  periode courante et celui des periodes voisines (une horloge qui derive
//  de quelques secondes ne ferme pas la porte).
//
//  SHA-256 et HMAC sont ecrits ici (FIPS 180-4, RFC 2104) : une centaine de
//  lignes, verifiees par les vecteurs de test officiels dans hmi_test.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

using Digest = std::array<std::uint8_t, 32>;

[[nodiscard]] Digest      sha256(const std::uint8_t* data, std::size_t size);
[[nodiscard]] Digest      sha256(std::string_view text);
[[nodiscard]] Digest      hmacSha256(std::string_view key, std::string_view message);
[[nodiscard]] std::string toHex(const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::string toHex(const Digest&);
[[nodiscard]] std::string fromHex(std::string_view hex);        // octets bruts ; vide si illisible

// Des octets aleatoires, en hexa (sel, secret).
[[nodiscard]] std::string randomHex(std::size_t bytes);

// L'empreinte d'un mot de passe : SHA-256(sel || mot de passe), iteree.
inline constexpr int kPasswordRounds = 4096;
[[nodiscard]] std::string passwordHash(std::string_view saltHex, std::string_view password);
[[nodiscard]] bool        passwordMatches(std::string_view saltHex, std::string_view hashHex, std::string_view password);

// Le code dynamique a l'instant `unixSeconds` (TOTP) ; `step` decale d'une
// periode (-1 : la precedente). Chiffres de tete gardes : "004213".
[[nodiscard]] std::string dynamicCode(std::string_view secretHex, double unixSeconds, int periodS = 30,
                                      int digits = 6, int step = 0);
[[nodiscard]] bool        dynamicCodeMatches(std::string_view secretHex, std::string_view code, double unixSeconds,
                                             int periodS = 30, int digits = 6);
// Les secondes restantes avant le prochain code.
[[nodiscard]] int         dynamicCodeRemaining(double unixSeconds, int periodS = 30);

} // namespace hmi
