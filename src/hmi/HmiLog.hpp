// =============================================================================
//  hmi/HmiLog.hpp - 1.11.14 : les niveaux de IHM_LOG et de la Console
// -----------------------------------------------------------------------------
//  IHM_LOG(niveau, 'message') ecrit une ligne dans la Console : l'heure, le
//  niveau, la categorie, la source (le script, la vue, l'objet), la ligne du
//  code, la session et le cycle. LE NIVEAU est une valeur de l'enumeration
//  native NIVEAU_LOG, ecrite seule (INFO) ou en litteral (NIVEAU_LOG#INFO) :
//      TRACE  DEBUG  INFO  SUCCESS  WARNING  ERROR  CRITICAL
//  Les noms se comparent sans casse, comme en ST. Ce que dit le moteur IHM
//  lui-meme (une erreur d'execution, une action refusee, IHM_JOURNAL) passe
//  aussi par la Console, avec le niveau de sa categorie (logLevelOfKind).
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace hmi {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Success, Warning, Error, Critical };
inline constexpr int              kLogLevelCount = 7;
inline constexpr std::string_view kLogLevelType = "NIVEAU_LOG";

// "TRACE", "DEBUG"... (le nom ST).
[[nodiscard]] std::string_view logLevelName(LogLevel) noexcept;
// En francais, pour l'ecran (accentue) : Trace, Debogage, Info, Succes, Avertissement, Erreur, Critique.
[[nodiscard]] std::string_view logLevelLabel(LogLevel) noexcept;
// "INFO", "info", "NIVEAU_LOG#INFO" (espaces autour admis) ; vide : pas un niveau.
[[nodiscard]] std::optional<LogLevel> logLevelByName(std::string_view) noexcept;
// Un nombre (la valeur DINT du niveau, 0 a 6) ; vide : hors des bornes.
[[nodiscard]] std::optional<LogLevel> logLevelOf(long long value) noexcept;
// Le niveau d'une ligne du journal du moteur, d'apres sa categorie :
// "Erreur" -> ERROR, le reste -> INFO.
[[nodiscard]] LogLevel logLevelOfKind(std::string_view kind) noexcept;

} // namespace hmi
