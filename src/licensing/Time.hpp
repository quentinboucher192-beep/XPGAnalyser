// =============================================================================
//  licensing/Time.hpp - 1.30.0 (prepare en 1.29.0) : les dates des licences
// -----------------------------------------------------------------------------
//  Une date est un nombre de secondes depuis le 1er janvier 1970, en UTC (le
//  fuseau du PC n'y entre jamais). Dans les fichiers, elle s'ecrit en ISO 8601,
//  a la seconde, toujours en UTC : "2026-11-06T10:12:44Z" - et rien d'autre
//  (ni fraction, ni decalage) : un seul texte possible pour une date signee.
//  Le calcul des jours est fait ici (l'algorithme civil de H. Hinnant) : ni
//  timegm ni mktime, qui different d'un systeme a l'autre.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace licensing {

using Seconds = std::int64_t;
inline constexpr Seconds kDay = 86400;

// L'heure du systeme (UTC).
[[nodiscard]] Seconds systemNow() noexcept;

// "2026-11-06T10:12:44Z".
[[nodiscard]] std::string isoTime(Seconds t);
// "2026-11-06".
[[nodiscard]] std::string isoDate(Seconds t);
// Lit "AAAA-MM-JJTHH:MM:SSZ" (et rien d'autre ; annees 1970 a 9999). Faux : pas une date.
[[nodiscard]] bool parseIsoTime(std::string_view text, Seconds& out) noexcept;

} // namespace licensing
