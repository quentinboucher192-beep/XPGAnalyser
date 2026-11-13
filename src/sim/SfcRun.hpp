// =============================================================================
//  sim/SfcRun.hpp - 1.51.0 (S) : un GRAFCET natif (SFC) qui TOURNE
// -----------------------------------------------------------------------------
//  La semantique de Control Expert (IEC 61131-3, jeton unique) :
//
//    - au premier cycle, les etapes initiales s'activent (c'est l'evolution de
//      ce cycle : aucune transition n'est franchie avec elles) ;
//    - une transition est VALIDEE quand toutes ses etapes d'amont sont actives
//      (et depuis au moins leur retard, literals/delay) ; validee et sa
//      receptivite vraie, elle est FRANCHIE : ses etapes d'amont se desactivent,
//      celles d'aval s'activent - toutes ensemble pour une divergence en ET ;
//    - UNE evolution par cycle : une etape qui vient de s'activer ne sera
//      quittee qu'au cycle suivant au plus tot ;
//    - deux transitions validees qui partagent une etape (une divergence en OU)
//      et vraies ensemble : la plus a gauche passe (la priorite de Control Expert) ;
//    - les variables d'etape : Etape.x (active), Etape.t (depuis quand ; garde sa
//      valeur apres la desactivation), Etape.tminErr / Etape.tmaxErr (la
//      supervision : quittee avant min, active plus que max) ;
//    - les actions, par nom (une variable ou une section d'action), comme le bloc
//      ACTION_CONTROL de la norme : N tant que l'etape est active, S mise a 1
//      memorisee jusqu'a un R, R l'efface (et l'emporte), L pendant la duree, D
//      apres la duree, P / P1 un cycle a l'activation, P0 un cycle a la
//      desactivation, DS retardee puis memorisee, SD memorisee puis retardee, SL
//      memorisee et limitee. Une variable recoit l'etat de l'action a chaque
//      cycle ; une section d'action s'execute tant que l'action est active, et une
//      derniere fois quand elle cesse.
//
//  Ordre dans le cycle (la section SFC a son rang dans MAST) : les receptivites,
//  les franchissements, les variables d'etape, puis les actions.
// =============================================================================
#pragma once

#include "../project/Sfc.hpp"
#include "Interpreter.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace project::ladder { class Program; struct State; }

namespace sim {

struct SfcChartRun {
    std::string section;            // le nom de la section
    std::string scope;              // "Unite." ou vide
    sfc::Chart    chart;
    sfc::Topology topo;

    struct Step {
        int          item{ -1 };
        std::string  name;
        bool         initial{ false };
        bool         active{ false };
        bool         wasActive{ false };     // a la fin du cycle d'avant
        std::int64_t activatedAt{ 0 };
        std::int64_t elapsed{ 0 };            // Etape.t
        std::int64_t tmin{ -1 }, tmax{ -1 }, delay{ -1 };   // ms, -1 : aucun
        bool         tminErr{ false }, tmaxErr{ false };
    };
    enum class CondKind : std::uint8_t { None, Expression, Statements, Ladder, Unsupported };
    struct Trans {
        int              item{ -1 };
        int              x{ 0 }, y{ 0 };
        std::vector<int> src, dst;            // indices dans steps
        CondKind         kind{ CondKind::None };
        std::string      text;                // ce que la receptivite dit (pour l'affichage)
        std::shared_ptr<const Expression> expr;
        std::shared_ptr<Program>          program;   // une section ST en instructions (T := ...;)
        std::string                       target;    // la variable qu'elle ecrit
        std::shared_ptr<const project::ladder::Program> ladder;
        std::shared_ptr<project::ladder::State>         ladderState;
        std::string      why;                 // pourquoi elle ne se calcule pas
        std::optional<bool> value;            // au dernier cycle
        bool             enabled{ false };    // validee au dernier cycle
        bool             fired{ false };      // franchie au dernier cycle
    };
    struct Assoc {
        int          step{ -1 };              // indice dans steps
        std::string  qualifier;               // normalise
        std::int64_t timeMs{ 0 };
        std::shared_ptr<const Expression> timeExpr;   // une duree en variable
    };
    struct ActionCtl {
        std::string        name;
        bool               isSection{ false };
        std::vector<Assoc> assocs;
        std::shared_ptr<Program> program;     // section d'action ST
        std::shared_ptr<const project::ladder::Program> ladder;
        std::shared_ptr<project::ladder::State>         ladderState;
        std::string        why;               // pourquoi elle ne s'execute pas
        bool               q{ false }, qPrev{ false };
        bool               level{ false }, levelPrev{ false };   // sans les impulsions (P, P1, P0)
        bool               sFF{ false }, sdFF{ false }, dsFF{ false }, slFF{ false };
        std::int64_t       lSince{ -1 }, dSince{ -1 }, sdSince{ -1 }, dsSince{ -1 }, slSince{ -1 };
    };

    std::vector<Step>      steps;
    std::vector<Trans>     trans;
    std::vector<ActionCtl> actions;
    bool                   started{ false };
    std::uint64_t          evolutions{ 0 };
    std::uint32_t          tag{ 0 };

    // ---- lecture (l'editeur, les essais) --------------------------------------
    [[nodiscard]] const Step* stepNamed(std::string_view name) const;
    [[nodiscard]] const Step* stepOfItem(int item) const;
    [[nodiscard]] const Trans* transOfItem(int item) const;
    [[nodiscard]] const ActionCtl* action(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> activeSteps() const;
};

// "t#1s500ms", "T#2m", "TIME#250ms" -> millisecondes ; faux : pas une duree.
[[nodiscard]] bool sfcDuration(std::string_view text, std::int64_t& ms);

} // namespace sim
