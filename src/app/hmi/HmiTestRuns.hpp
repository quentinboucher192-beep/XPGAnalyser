// =============================================================================
//  app/hmi/HmiTestRuns.hpp - 1.13.0 (lot J4b) : LES DERNIERS VERDICTS DES TESTS
//  UNITAIRES (de la marche, pas du projet)
// -----------------------------------------------------------------------------
//  Le dernier resultat de chaque cas, avec l'empreinte de sa fonction et de
//  lui-meme : un verdict ne vaut plus des que l'un ou l'autre change (il redevient
//  " a jouer "). Ni annules, ni enregistres. L'onglet Tests, le volet Fonctions,
//  Configuration et l'arbre du projet (son groupe Tests) les lisent ; seul le moteur
//  (hmi/HmiFunctionTests.hpp) est requis - l'arbre s'en sert sans l'onglet.
//
//  1.13.0 (seconde passe) : un verdict vaut pour SON projet (celui ou il a ete
//  joue : une copie du projet, qui a les memes identifiants, repart " a jouer ") et
//  pour ce que la fonction lit (son contexte) : les fonctions IHM qu'elle appelle,
//  de proche en proche, les variables IHM et les constantes de sa vue qu'elle
//  nomme. Changer l'une d'elles remet ses cas " a jouer ".
//
//  1.13.0 (D3b) : l'application oublie tout (forget) a chaque document IHM ouvert
//  (App::bindHmi) : un document neuf qui reprendrait l'adresse d'un projet ferme
//  n'herite de rien, et la table ne garde que le projet ouvert. Le contexte suit
//  aussi les methodes des types, les fonctions d'un symbole (appelees par une
//  instance), les redefinitions, les bibliotheques reliees et les operateurs ; il
//  est garde (un calcul par fonction) tant que la pile de l'application ne bouge
//  pas - avant, chaque peinture de l'arbre le refaisait pour chaque fonction.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiFunctionTests.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace app {

// ------------------------------------------------- les derniers verdicts ---
namespace testruns {
[[nodiscard]] std::size_t fingerprint(const hmi::HmiFunction&, const hmi::FunctionTest&);
// 1.13.0 (seconde passe) : l'empreinte de ce que `f` lit dans le projet (les fonctions qu'elle appelle, de proche en
// proche ; les variables IHM, les constantes de sa vue qu'elle nomme).
[[nodiscard]] std::size_t context(const hmi::Project&, const hmi::HmiFunction&);
// Les essais : combien de contextes ont ete calcules depuis le lancement (le cache n'en refait aucun tant que la pile
// enregistree - core::CommandGroupScope - n'a pas bouge).
[[nodiscard]] std::size_t contextsComputed() noexcept;
void record(const hmi::Project&, const hmi::HmiFunction&, const hmi::FunctionTestResult&);
void record(const hmi::Project&, const hmi::HmiFunction&, const hmi::FunctionTestReport&);
// Le dernier verdict de ce cas dans ce projet, s'il vaut encore ; nul : jamais joue ici, ou change depuis (le cas, la
// fonction, ce qu'elle appelle ou lit).
[[nodiscard]] const hmi::FunctionTestResult* last(const hmi::Project&, const hmi::HmiFunction&, const hmi::FunctionTest&);
struct Tally {
    std::size_t cases{0}, passed{0}, failed{0}, errors{0}, pending{0}, skipped{0};
    [[nodiscard]] bool red() const noexcept { return failed + errors > 0; }
    // "7 cas \xC2\xB7 6 reussis, 1 echec" (accentue) ; "aucun cas".
    [[nodiscard]] std::string text() const;
    Tally& operator+=(const Tally&);
};
[[nodiscard]] Tally tally(const hmi::Project&, const hmi::HmiFunction&);
[[nodiscard]] Tally tally(const hmi::Project&);      // toutes les fonctions (projet, vues, popups, symboles)
void forget();                                       // tout oublier (un autre document : App::bindHmi)
[[nodiscard]] std::uint64_t revision() noexcept;     // croit a chaque verdict garde
[[nodiscard]] const core::SignalPtr<>& changed();    // un verdict garde (l'arbre, les onglets relisent)
// 1.13.0 (seconde passe) : UNE CAMPAGNE (Lancer les tests, le rejeu du Compiler/Demarrer) - tant qu'elle vit, les
// verdicts se gardent sans rien dire ; elle dit changed() une seule fois a la fin (avant : une fois par fonction, et
// chaque volet se refaisait autant de fois). Imbricables.
class Batch {
public:
    Batch();
    ~Batch();
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
};
} // namespace testruns

} // namespace app
