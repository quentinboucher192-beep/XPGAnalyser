// =============================================================================
//  project/Grafcet.hpp — reading the charts back out of the ST that builds them
// -----------------------------------------------------------------------------
//  This project does not store its GRAFCETs as SFC graphs. It stores them as ST
//  that fills three arrays, which DFB_GRAFCETENGINE then runs:
//
//      Builder(T:=1,Text:='id=0|n=X0|i=1',        _Step :=Steps_DetoxalA[0]);
//      Builder(T:=2,Text:='id=2|s=1|d=2|c=PT1<=SP',_Trans:=Trans_DetoxalA[2]);
//      Builder(T:=2,d:=t#0s,                       _Trans:=Trans_DetoxalA[2]);
//      Builder(T:=3,Text:='id=0|n=OV3V4|k=4|s=1',  _Act  :=Acts_DetoxalA[0]);
//      Trans_DetoxalA[2].Condition := <a full ST expression>;
//      Acts_DetoxalA[0].EnableCond := <a full ST expression>;
//
//  and the action bodies live in a second section, keyed by the action's Out:
//
//      IF Acts_DetoxalA[0].Out THEN  armoires[0].sorties.V3.F1 := TRUE;  END_IF;
//
//  So the chart is recoverable, exactly, without running anything. That is what
//  this does: 17 charts, 151 steps, 193 transitions and 189 actions in the
//  reference project.
//
//  TWO THINGS THAT LOOK ALIKE AND ARE NOT.
//
//   * `d=` inside Text is a DESTINATION step. `d:=` as a Builder argument is a
//     DELAY. They appear on consecutive lines of the same file and mean nothing
//     like each other; reading one as the other silently reverses arrows.
//   * `s=` is a SOURCE on a transition and a BOUND STEP on an action. Same
//     letter, different question.
//
//  WHAT IS NOT GUESSED. The descriptor is the authority for structure. Where a
//  chart is malformed - a transition pointing at a step that does not exist, two
//  steps sharing an id, no initial step - the chart is still returned, with the
//  problem recorded in `warnings`. A drawing that silently omits a broken
//  transition tells the reader their chart is fine when it is not.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"

#include <string>
#include <vector>

namespace grafcet {

    // The twelve kinds the engine implements, from the comment on ST_GC_Action.Kind.
    // Six of them are used in the reference project; the other six are spelled out
    // here because a chart that uses one must not be drawn as "kind 7".
    enum class ActionKind : std::uint8_t {
        Continuous = 0, Rising = 1, DelayOn = 2, PulseOn = 3, LimitedOn = 4,
        Falling = 5, PulseOff = 6, LimitedOff = 7, CondDelayEnd = 8,
        CondDelayPulse = 9, CondLimitedOn = 10, CondLimitedOnTp = 11,
        Unknown = 255,
    };

    // The engine's own name for it, e.g. "LIMITEDON".
    [[nodiscard]] std::string_view codeName(ActionKind) noexcept;
    // What actually makes it fire, in words: "while the step is active",
    // "once, when the step activates", "for a fixed time, then stops". The point of
    // the panel is that a reader should not have to remember what k=11 meant.
    [[nodiscard]] std::string_view triggerText(ActionKind) noexcept;
    // Whether the kind uses the Delay field at all - a delay shown against a
    // continuous action is noise that reads like meaning.
    [[nodiscard]] bool usesDelay(ActionKind) noexcept;

    // Something about the chart worth telling the reader.
    struct Diagnostic {
        enum class Level : std::uint8_t {
            Structure,    // the chart will not behave as drawn
            EngineLimit,  // it behaves, but the PLC cannot store what was written
        };
        Level       level{ Level::Structure };
        std::string message;
        std::size_t line{ 0 };   // 0 when it is about the chart rather than a line
    };

    struct Step {
        int         id{ -1 };
        std::string name;             // "X0"
        bool        initial{ false };
        bool        isFinal{ false };
        std::size_t line{ 0 };          // where in the section it was declared
    };

    // How a transition with several sources or destinations behaves. Read from the
    // engine, not invented: BUILDING sets JoinKind := 1 and SplitKind := 0 on every
    // transition it builds, and DFB_GRAFCETENGINE reads them as below.
    enum class JoinKind : std::uint8_t { Or = 0, And = 1 };   // any source / all sources
    enum class SplitKind : std::uint8_t { Single = 0, And = 1 };// first destination / all

    struct Transition {
        int              id{ -1 };
        std::vector<int> sources;      // step ids; more than one is a convergence
        std::vector<int> destinations; // more than one is a divergence
        std::string      conditionText;// 'c=' - the short label meant for the drawing
        std::string      conditionExpr;// the full ST expression, from .Condition :=
        std::string      delay;        // 'd:=' - a time expression, verbatim
        JoinKind         join{ JoinKind::And };
        SplitKind        split{ SplitKind::Single };
        std::size_t      line{ 0 };

        [[nodiscard]] bool isConvergence() const noexcept { return sources.size() > 1; }
        [[nodiscard]] bool isDivergence()  const noexcept { return destinations.size() > 1; }
    };

    struct Action {
        int         id{ -1 };
        std::string name;             // "OV3V4"
        ActionKind  kind{ ActionKind::Unknown };
        int         boundStep{ -1 };    // the step it belongs to
        std::string delay;            // verbatim, may be a whole expression
        std::string enableExpr;       // from .EnableCond :=
        std::string body;             // from IF Acts_X[i].Out THEN ... END_IF
        std::size_t line{ 0 };
    };

    struct Chart {
        std::string   name;                        // "DetoxalA"
        std::string   instance;                    // "Gc_DetoxalA"
        std::string   arrayPrefix;                 // "DetoxalA", as in Steps_DetoxalA
        domain::Index section{ domain::kNoIndex };       // SFC_DetoxalA
        domain::Index actionSection{ domain::kNoIndex }; // SFC_DetoxalA_Actions
        domain::Index owner{ domain::kNoIndex };         // the POU holding them
        // 1.10 : le prefixe des noms dans la simulation ("Logigrammes_A." quand
        // les tableaux sont des variables d'une unite de programme, vide sinon) :
        // le simulateur range Steps_DetoxalA sous Logigrammes_A.Steps_DetoxalA.
        std::string   scope;

        std::vector<Step>       steps;
        std::vector<Transition> transitions;
        std::vector<Action>     actions;

        // Everything that does not add up, in the reader's terms. Never silently
        // dropped: a chart drawn without its broken transition looks correct.
        //
        // The two levels are not decoration. A dangling transition means the chart
        // does not do what it appears to; a label the engine cannot store means the
        // chart runs correctly but the operator's screen is blank where a condition
        // should be. Colouring them the same would make 95 cosmetic notes hide one
        // real fault - and the reference project has exactly that shape.
        std::vector<Diagnostic> warnings;

        [[nodiscard]] std::size_t countOf(Diagnostic::Level) const;

        [[nodiscard]] const Step* stepById(int id) const;
        [[nodiscard]] std::vector<const Action*> actionsOfStep(int stepId) const;
    };

    // ---------------------------------------------------------------------------
    //  Where a block that starts on `openLine` ends.
    //
    //  Substring matching is not enough, and the reason is worth stating because the
    //  first version of this used it and got away with it: "ELSIF " CONTAINS "IF ".
    //  A body with an ELSIF in it would count one level too many and swallow every
    //  statement to the end of the section. The reference project happens to have
    //  one nested IF and no ELSIF, so nothing failed - which is the definition of a
    //  latent fault rather than the absence of one.
    //
    //  Reading it wrong shows a body that is too long. WRITING it wrong destroys the
    //  code that follows, so this has to be right before any edit goes near it.
    //
    //  Returns the index of the line holding the matching END_IF, or npos.
    // ---------------------------------------------------------------------------
    [[nodiscard]] std::size_t blockEnd(const std::vector<std::string>& lines,
        std::size_t openLine);

    // Splits a section body into lines, keeping them exactly as they were - the
    // trailing \r included, because a project written on Windows must come back out
    // of an edit as it went in.
    [[nodiscard]] std::vector<std::string> splitLines(const std::string& body);

    // Every chart the project builds. One per SFC_ section that calls Builder.
    [[nodiscard]] std::vector<Chart> findCharts(const domain::Project&);

    // ONE CHART, COMPLETE. Use this, not parseChart, unless you are a test.
    //
    // The difference is not cosmetic and it has cost a bug already: an action's BODY
    // and the chart's actionSection are filled by findCharts, because finding them
    // means looking at every section in the project. parseChart reads one section
    // and cannot know about the other, so the charts it returns have empty bodies
    // and no companion section - which looks exactly like a chart whose actions
    // happen to do nothing.
    //
    // Returns a chart with no steps when the section builds none.
    [[nodiscard]] Chart findChart(const domain::Project&, domain::Index section);

    // One section, and only that section. It does NOT link the action bodies or the
    // companion section - see findChart above. Kept public so a test can drive it
    // with text no project contains yet: malformed descriptors, joins, splits.
    [[nodiscard]] Chart parseChart(const domain::Project&, domain::Index section);

} // namespace grafcet