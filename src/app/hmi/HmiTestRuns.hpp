// =============================================================================
//  app/hmi/HmiTestRuns.hpp - 1.13.0 (lot J4b) : LES DERNIERS VERDICTS DES TESTS
//  UNITAIRES (de la marche, pas du projet)
// -----------------------------------------------------------------------------
//  Le dernier resultat de chaque cas, avec l'empreinte de sa fonction et de
//  lui-meme : un verdict ne vaut plus des que l'un ou l'autre change (il redevient
//  " a jouer "). Ni annules, ni enregistres. L'onglet Tests, le volet Fonctions,
//  Configuration et l'arbre du projet (son groupe Tests) les lisent ; seul le moteur
//  (hmi/HmiFunctionTests.hpp) est requis - l'arbre s'en sert sans l'onglet.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiFunctionTests.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace app {

// ------------------------------------------------- les derniers verdicts ---
//  De la marche, pas du projet (ni annules, ni enregistres) : le dernier resultat de
//  chaque cas, avec l'empreinte de sa fonction et de lui-meme - un verdict ne vaut
//  plus des que l'un ou l'autre change (il redevient " a jouer ").
namespace testruns {
[[nodiscard]] std::size_t fingerprint(const hmi::HmiFunction&, const hmi::FunctionTest&);
void record(const hmi::HmiFunction&, const hmi::FunctionTestResult&);
void record(const hmi::HmiFunction&, const hmi::FunctionTestReport&);
// Le dernier verdict de ce cas, s'il vaut encore ; nul : jamais joue, ou change depuis.
[[nodiscard]] const hmi::FunctionTestResult* last(const hmi::HmiFunction&, const hmi::FunctionTest&);
struct Tally {
    std::size_t cases{0}, passed{0}, failed{0}, errors{0}, pending{0}, skipped{0};
    [[nodiscard]] bool red() const noexcept { return failed + errors > 0; }
    // "7 cas \xC2\xB7 6 reussis, 1 echec" (accentue) ; "aucun cas".
    [[nodiscard]] std::string text() const;
    Tally& operator+=(const Tally&);
};
[[nodiscard]] Tally tally(const hmi::HmiFunction&);
[[nodiscard]] Tally tally(const hmi::Project&);      // toutes les fonctions (projet, vues, popups, symboles)
void forget();                                       // tout oublier (un autre projet)
[[nodiscard]] std::uint64_t revision() noexcept;     // croit a chaque verdict garde
[[nodiscard]] const core::SignalPtr<>& changed();    // un verdict garde (l'arbre, les onglets relisent)
} // namespace testruns

} // namespace app
