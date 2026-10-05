#include "GrafcetLayout.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <set>

namespace grafcet {

    const PlacedStep* Layout::step(int id) const {
        for (const auto& s : steps) if (s.id == id) return &s;
        return nullptr;
    }

    const PlacedTransition* Layout::transition(int id) const {
        for (const auto& t : transitions) if (t.id == id) return &t;
        return nullptr;
    }

    namespace {

        constexpr int kUnranked = -1;

    } // namespace

    Layout computeLayout(const Chart& chart) {
        Layout out;
        if (chart.steps.empty()) return out;

        // ---- level: breadth first from the initial steps -----------------------
        //
        // Breadth first and not longest-path: longest-path is the textbook answer
        // for a DAG and this is not one. A chart that loops back to X0 has no
        // longest path at all, and the ones that do terminate would push every step
        // down to the depth of the worst branch, spreading a fourteen-step chart
        // over twenty rows for no reading benefit.
        std::map<int, int> level;
        for (const auto& s : chart.steps) level[s.id] = kUnranked;

        std::queue<int> pending;
        for (const auto& s : chart.steps)
            if (s.initial) { level[s.id] = 0; pending.push(s.id); }

        // No initial step is already reported by the parser. Something still has to
        // be drawn, so the lowest id starts the walk - a chart nobody can read is
        // not a better answer than one that starts in an arbitrary place.
        if (pending.empty()) {
            const int first = chart.steps.front().id;
            level[first] = 0;
            pending.push(first);
        }

        while (!pending.empty()) {
            const int at = pending.front();
            pending.pop();
            for (const auto& t : chart.transitions) {
                if (std::find(t.sources.begin(), t.sources.end(), at) == t.sources.end()) continue;
                for (int dst : t.destinations) {
                    auto it = level.find(dst);
                    if (it == level.end() || it->second != kUnranked) continue;   // already placed
                    it->second = level[at] + 1;
                    pending.push(dst);
                }
            }
        }

        // Anything the walk never reached. Placed below everything else so it is
        // visibly apart rather than woven in as if it belonged.
        int maxLevel = 0;
        for (const auto& [id, lv] : level) maxLevel = std::max(maxLevel, lv);
        for (auto& [id, lv] : level) if (lv == kUnranked) lv = ++maxLevel;

        // ---- column ------------------------------------------------------------
        //
        // Rows are filled top down. Within a row, a step wants the column of its
        // predecessor, so a chart with no branches draws as one straight line; where
        // two steps want the same column the second moves right, which is what a
        // divergence looks like.
        std::map<int, int> column;
        std::map<int, std::vector<int>> byLevel;
        for (const auto& s : chart.steps) byLevel[level[s.id]].push_back(s.id);

        auto predecessorColumn = [&](int stepId) {
            int best = -1;
            for (const auto& t : chart.transitions) {
                if (std::find(t.destinations.begin(), t.destinations.end(), stepId)
                    == t.destinations.end()) continue;
                for (int src : t.sources) {
                    const auto at = column.find(src);
                    if (at == column.end()) continue;
                    if (level[src] >= level[stepId]) continue;   // a return tells us nothing
                    if (best < 0 || at->second < best) best = at->second;
                }
            }
            return best;
            };

        for (auto& [lv, ids] : byLevel) {
            // Order by where they would like to be, then by id so the result does
            // not depend on the order the file happened to declare them in.
            std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
                const int ca = predecessorColumn(a), cb = predecessorColumn(b);
                if (ca != cb) return ca < cb;
                return a < b;
                });

            std::set<int> taken;
            for (int id : ids) {
                int want = predecessorColumn(id);
                if (want < 0) want = 0;
                while (taken.count(want)) ++want;
                taken.insert(want);
                column[id] = want;
            }
            (void)lv;
        }

        for (const auto& s : chart.steps) {
            PlacedStep placed;
            placed.id = s.id;
            placed.level = level[s.id];
            placed.column = column[s.id];
            placed.reachable = placed.level <= maxLevel
                && std::any_of(chart.transitions.begin(), chart.transitions.end(),
                    [&](const Transition& t) {
                        return std::find(t.destinations.begin(),
                            t.destinations.end(), s.id)
                            != t.destinations.end();
                    });
            if (s.initial) placed.reachable = true;   // an initial step needs no predecessor
            out.steps.push_back(placed);
        }

        // ---- transitions -------------------------------------------------------
        for (const auto& t : chart.transitions) {
            PlacedTransition bar;
            bar.id = t.id;

            int srcLevel = 0, dstLevel = 0;
            bool haveSrc = false, haveDst = false;
            int lo = 0, hi = 0;
            bool haveSpan = false;

            auto widen = [&](int c) {
                if (!haveSpan) { lo = hi = c; haveSpan = true; }
                else { lo = std::min(lo, c); hi = std::max(hi, c); }
                };

            for (int id : t.sources)
                if (level.count(id)) {
                    srcLevel = haveSrc ? std::max(srcLevel, level[id]) : level[id];
                    haveSrc = true;
                    widen(column[id]);
                }
            for (int id : t.destinations)
                if (level.count(id)) {
                    dstLevel = haveDst ? std::min(dstLevel, level[id]) : level[id];
                    haveDst = true;
                    widen(column[id]);
                }

            // The bar sits just under the last of its sources. A transition that
            // leads back up is a return: it keeps that row and gets drawn as a rail,
            // because putting it on the row of its destination would run the arrow
            // through every step in between.
            bar.level = haveSrc ? srcLevel : dstLevel;
            bar.isReturn = haveSrc && haveDst && dstLevel <= srcLevel;
            bar.spanFrom = haveSpan ? lo : 0;
            bar.spanTo = haveSpan ? hi : 0;
            bar.column = haveSpan ? (lo + hi) / 2 : 0;
            out.transitions.push_back(bar);
        }

        for (const auto& s : out.steps) {
            out.levelCount = std::max(out.levelCount, s.level + 1);
            out.columnCount = std::max(out.columnCount, s.column + 1);
        }
        return out;
    }

} // namespace grafcet