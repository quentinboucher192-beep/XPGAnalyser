// =============================================================================
//  project/LadderSim.hpp - 1.51.0 : un ladder qui TOURNE dans le simulateur
// -----------------------------------------------------------------------------
//  La semantique de Control Expert : les reseaux dans l'ordre ; dans un reseau,
//  le courant part du rail de gauche et se propage colonne par colonne (de
//  gauche a droite), et dans une colonne de haut en bas :
//
//    contact       ouvert : passe si la variable est vraie ; ferme : si fausse ;
//                  P / N : passe un cycle sur le front montant / descendant de la
//                  variable (memoire par contact, d'un cycle au suivant) ;
//    bobine        ecrit le courant qui l'atteint (inverse : son contraire) ;
//                  S / R : met a 1 / a 0 si alimentee ; P / N : vraie un cycle sur
//                  le front du courant ; la bobine laisse passer le courant ;
//    liaison verticale  relie les deux rangees a sa frontiere (le OU) ;
//    compare       [expr] passe si l'expression ST est vraie ;
//    operate       [instr] executee si alimentee (laisse passer le courant) ;
//    bloc (FFB)    EN (s'il y en a un) decide s'il s'execute ; ses entrees sont
//                  leur parametre effectif (variable, litteral, expression) ou le
//                  fil de leur rangee ; ses sorties ecrivent leur parametre et
//                  donnent le fil de leur rangee. Une instance (TON, CTU, SR, R_TRIG,
//                  un DFB du projet...) est appelee par l'environnement, comme en
//                  ST ; une fonction (ADD, GT_INT, MOVE, SEL, LIMIT, INT_TO_REAL...)
//                  est evaluee par l'interpreteur ST ;
//    saut          ->> ETIQ (jumpCoil) : si alimente, la suite reprend a la ligne de
//                  l'etiquette ETIQ (labelCell) ; un saut en arriere est permis, borne
//                  (une boucle sans fin est dite) ; RETURN (retCoil) : la section
//                  s'arrete la.
//
//  L'ORDRE. Un reseau de Control Expert est fait de LIGNES : des rangees reliees
//  entre elles (liaisons verticales, blocs sur plusieurs rangees). Les lignes
//  s'executent de haut en bas, chacune entiere (colonne par colonne) avant la
//  suivante : une bobine ecrite en haut est lue par un contact plus bas, au meme
//  cycle.
//
//  Tout passe par sim::Environment (lire, ecrire, appeler) : le Runtime d'API
//  Studio, ou celui d'un autre (une receptivite de GRAFCET ecrite en LD).
//
//  EN DIRECT : chaque passage remplit State::live (le courant de chaque noeud et
//  de chaque case, les valeurs des broches), que l'editeur colore.
// =============================================================================
#pragma once

#include "Ladder.hpp"

#include "../sim/Interpreter.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace project::ladder {

    class Program;   // une section LD compilee (opaque, partageable, sans etat)

    // Compile un schema (les expressions et instructions ST lues une fois). Une
    // expression illisible n'empeche pas la compilation : elle est dite dans
    // `warnings` et l'element ne conduit pas.
    [[nodiscard]] core::Result<std::shared_ptr<const Program>> compile(const Diagram& d, std::string sectionName);
    // Le meme a partir du XML (<LDSource ...>).
    [[nodiscard]] core::Result<std::shared_ptr<const Program>> compileXml(std::string_view ldSourceXml,
                                                                         std::string sectionName);
    [[nodiscard]] const std::vector<std::string>& compileWarnings(const Program&) noexcept;
    [[nodiscard]] const Diagram& diagramOf(const Program&) noexcept;
    [[nodiscard]] const std::string& sectionNameOf(const Program&) noexcept;
    // Combien de memoires de front (contacts et bobines P / N) : deux programmes qui en
    // ont autant peuvent se passer leur State (la modification en ligne).
    [[nodiscard]] int edgeMemories(const Program&) noexcept;

    // ---- l'etat en direct (le dernier passage) ----------------------------------
    struct LiveBlock {
        bool                     ran{ false };     // EN vrai (ou pas d'EN) : le bloc s'est execute
        std::vector<std::string> pinValues;        // une par broche (Block::pins), texte court ("TRUE", "12", "T#1S")
    };
    struct LiveNetwork {
        bool ran{ false };                                        // faux : saute (->>) ou apres un RETURN
        std::vector<std::vector<std::uint8_t>> node;              // [rangee][frontiere 0..columns] : 1 = sous tension
        std::vector<std::vector<std::uint8_t>> cell;              // [rangee][colonne] : 1 = l'element conduit / la bobine est active
        std::vector<LiveBlock> blocks;
    };
    struct LiveState {
        std::uint64_t            pass{ 0 };        // le nombre de passages
        std::vector<LiveNetwork> networks;
    };

    // Ce que la section garde d'un cycle a l'autre : les fronts (contacts P/N,
    // bobines P/N), et le dernier passage.
    struct State {
        std::vector<std::uint8_t> previous;        // une memoire par element a front (voir compile)
        LiveState                 live;
        void reset() { previous.clear(); live = {}; }
    };

    struct RunOptions {
        // Une bobine dont la variable porte ce nom (sans la casse) n'ecrit pas :
        // son courant est rendu dans RunResult::value. La receptivite d'une
        // transition de GRAFCET ecrite en LD (sa bobine porte le nom de la transition).
        std::string captureCoil;
        bool        recordLive{ true };
        // Le budget d'instructions du cycle (sim::RunLimits::scanStatements), partage.
        std::uint64_t* scanStatements{ nullptr };
        std::uint64_t  maxStatements{ 2000000 };
    };

    struct RunResult {
        bool                          ok{ true };          // faux : une erreur a arrete la section
        std::vector<sim::Diagnostic>  diagnostics;
        bool                          captured{ false };   // une bobine `captureCoil` a ete atteinte
        bool                          value{ false };      // son courant (le OU de toutes)
        bool                          returned{ false };   // un RETURN a ete pris
        bool                          halted{ false };     // une haltCoil alimentee
        std::uint32_t                 networksRun{ 0 };
        std::uint64_t                 elements{ 0 };       // compte comme des instructions
    };

    // Un passage de la section : tous ses reseaux, dans l'ordre.
    RunResult run(const Program& program, sim::Environment& env, State& state, const RunOptions& options = {});

    // Raccourci : la receptivite d'une transition ecrite en LD - le courant qui
    // atteint la bobine `coilName` (les autres bobines ecrivent normalement).
    // Pas de telle bobine : faux, et une erreur.
    [[nodiscard]] core::Result<bool> evaluateCondition(const Program& program, sim::Environment& env, State& state,
                                                      std::string_view coilName);

} // namespace project::ladder
