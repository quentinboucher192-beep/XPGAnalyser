// =============================================================================
//  sim/DebugShared.hpp - 1.13.0 : ce que les deux debogueurs disent pareil
// -----------------------------------------------------------------------------
//  Le debogueur de l'automate (sim::Runtime, RuntimeDebug.cpp) et celui des
//  scripts de l'IHM (hmi::Runtime, HmiRuntimeDebug.cpp) posent leurs points
//  d'arret de la meme facon : une ligne demandee qui se decale sur la premiere
//  instruction qui suit, une condition ST lue une fois et essayee, et des
//  messages en francais, mot pour mot les memes des deux cotes. Ils sont ici,
//  une seule fois.
// =============================================================================
#pragma once

#include "../core/Result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sim::debug {

// Ce que dit le moteur d'une condition qui ne va pas, en francais : « Presion n'est pas declaree ici »,
// « elle ne se lit pas (pres de « ) ») », « division par zero ».
[[nodiscard]] std::string conditionProblem(const core::Error& e);
// « condition invalide : <probleme> ; il ne s'arretera pas ».
[[nodiscard]] std::string invalidCondition(const std::string& problem);
// « decale a la ligne 15 : la ligne 14 n'a pas d'instruction ».
[[nodiscard]] std::string shiftedNote(std::uint32_t at, std::uint32_t asked);
// « aucune instruction a partir de la ligne 40 : il ne s'arretera pas ».
[[nodiscard]] std::string noStatementNote(std::uint32_t from);
// Une note de plus, a la suite (« ; »).
void addNote(std::string& note, const std::string& more);
// La premiere ligne d'instruction a partir de `line` (1 au moins) dans `lines` (triees) ; 0 : aucune.
[[nodiscard]] std::uint32_t effectiveLine(const std::vector<std::uint32_t>& lines, std::uint32_t line);

} // namespace sim::debug
