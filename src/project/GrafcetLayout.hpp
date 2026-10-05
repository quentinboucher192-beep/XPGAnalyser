// =============================================================================
//  project/GrafcetLayout.hpp — where the boxes go
// -----------------------------------------------------------------------------
//  A GRAFCET is a graph with cycles, and a chart that ends by going back to X0 -
//  which is nearly all of them - has no topological order. So the placement is
//  not "sort the graph"; it is:
//
//    LEVEL   how far a step is from the initial step, breadth first. A
//            transition that leads back to a level already reached does not
//            create a new one: it becomes a RETURN, drawn as a rail down the
//            side rather than as another row. Without that, PompageA's twelve
//            steps and its three returns would draw as a staircase of twenty.
//
//    COLUMN  which branch it is on. A divergence puts its destinations in
//            adjacent columns; everything else tries to stay in its
//            predecessor's column, so a linear chart is a straight line and only
//            the branches move sideways.
//
//  Nothing here knows about pixels. It produces integer grid coordinates, which
//  is what makes it testable without a window: "does the divergence put its two
//  branches in different columns" is a question with an answer, and "does it look
//  right" is not.
//
//  UNREACHABLE STEPS ARE PLACED, NOT DROPPED. A step no transition leads to is
//  usually a mistake, and it is the kind that hides: leave it out of the drawing
//  and the chart looks complete.
// =============================================================================
#pragma once

#include "Grafcet.hpp"

namespace grafcet {

    struct PlacedStep {
        int  id{ -1 };
        int  level{ 0 };
        int  column{ 0 };
        bool reachable{ true };   // false: nothing leads here from an initial step
    };

    struct PlacedTransition {
        int  id{ -1 };
        int  level{ 0 };        // the row it sits on, between its sources and its targets
        int  column{ 0 };       // where the bar is centred
        int  spanFrom{ 0 };     // leftmost column it has to reach
        int  spanTo{ 0 };       // rightmost
        bool isReturn{ false }; // leads back to a level already reached: drawn as a rail
    };

    struct Layout {
        std::vector<PlacedStep>       steps;
        std::vector<PlacedTransition> transitions;
        int levelCount{ 0 };
        int columnCount{ 0 };

        [[nodiscard]] const PlacedStep* step(int id) const;
        [[nodiscard]] const PlacedTransition* transition(int id) const;
    };

    [[nodiscard]] Layout computeLayout(const Chart&);

} // namespace grafcet