// =============================================================================
//  project/GrafcetLayoutFile.hpp — where the boxes were put, kept beside the project
// -----------------------------------------------------------------------------
//  Moving a step or bending a wire is a fact about the DRAWING, not about the
//  program. The .XPG has nowhere to put it, and the two places one could invent
//  are both worse than a file of its own:
//
//    * a structured comment in the SFC section - it would travel with the code,
//      which sounds right, but it would put bytes that only this tool
//      understands into a file Control Expert owns and re-writes. The first time
//      someone edits that section in Control Expert the comments move, split or
//      vanish, and a viewer that silently mislays a layout is worse than one
//      that never had it.
//    * inside the .XPG structure itself - the same objection, with the added
//      risk of an export that no longer round-trips byte for byte, which is the
//      one guarantee this program has been built around.
//
//  So: a sidecar, <project>.xpglayout, next to the project. It holds only
//  positions. LOSING IT MUST COST NOTHING - the automatic layout is always
//  available and is what an absent file falls back to. That is the property that
//  makes a sidecar acceptable rather than a liability: no information that
//  matters to the machine is ever only in here.
//
//  The format is line-oriented text, one record per line, because a layout file
//  is something a person may have to read, diff or delete by hand when it
//  disagrees with them.
//
//      # xpglayout 1
//      chart ManuA
//      step 0 120 40
//      step 1 120 160
//      wire 2 0 240 300 340 300      <- transition id, then the bend points
//
//  Unknown records are KEPT and written back untouched, so a file written by a
//  later version does not lose its contents by passing through this one.
// =============================================================================
#pragma once

#include "../platform/Geometry.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace project {

    struct ChartLayout {
        // Only the ones that were moved. A step with no entry is placed by
        // computeLayout, which is the normal case and stays the normal case.
        std::map<int, gfx::Point>              steps;
        // Bend points for a transition's wire, in order. Empty means "draw it the
        // usual way".
        std::map<int, std::vector<gfx::Point>> wires;

        [[nodiscard]] bool empty() const noexcept { return steps.empty() && wires.empty(); }
    };

    class LayoutFile {
    public:
        // Everything read, by chart name. Charts not mentioned simply have no entry.
        [[nodiscard]] const ChartLayout* find(const std::string& chart) const;
        ChartLayout& at(const std::string& chart) { return charts_[chart]; }
        void forget(const std::string& chart) { charts_.erase(chart); }

        [[nodiscard]] std::size_t chartCount() const noexcept { return charts_.size(); }

        // Text in, text out. Parsing never fails: a line that makes no sense is kept
        // verbatim and written back. A layout file is a convenience, and refusing to
        // open a project because its sidecar has a typo would be the tail wagging
        // the dog.
        void parse(const std::string& text);
        [[nodiscard]] std::string serialise() const;

        // The path a project's sidecar lives at: the .XPG path with .xpglayout
        // instead. Alongside rather than inside, so deleting it is a complete and
        // obvious way to start the layout again.
        [[nodiscard]] static std::string pathFor(const std::string& projectPath);

        // What was read and not understood. Written back unchanged; exposed so a
        // test can assert that it is.
        [[nodiscard]] const std::vector<std::string>& unknown() const noexcept { return unknown_; }

    private:
        std::map<std::string, ChartLayout> charts_;
        std::vector<std::string>           unknown_;
    };

} // namespace project