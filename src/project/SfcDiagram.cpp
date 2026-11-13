#include "SfcDiagram.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace sfc {

    namespace {

        constexpr float kStepH = 40.f;
        constexpr float kStepMinW = 64.f;
        constexpr float kPad = 8.f;
        constexpr float kActionH = 34.f;
        constexpr float kActionGap = 16.f;
        constexpr float kQualW = 30.f;
        constexpr float kBarHalf = 18.f;
        constexpr float kLabelPad = 8.f;
        constexpr float kColGap = 34.f;
        constexpr float kMargin = 28.f;
        constexpr float kLineH = 16.f;
        constexpr float kMaxLabel = 260.f;
        constexpr float kMaxAction = 200.f;
        constexpr float kBarPad = 12.f;
        constexpr float kRowPad = 8.f;
        constexpr float kEmptyCol = 40.f;
        constexpr float kEmptyRow = 22.f;
        constexpr float kJumpH = 30.f;

        std::string cut(const std::string& text, float maxW, const grafcet::MeasureText& measure) {
            if (text.empty() || measure(text) <= maxW) return text;
            static const std::string ell = "\xE2\x80\xA6";
            std::vector<std::size_t> bounds;
            for (std::size_t i = 0; i < text.size(); ++i)
                if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) bounds.push_back(i);
            auto prefix = [&](std::size_t k) { return text.substr(0, k < bounds.size() ? bounds[k] : text.size()); };
            std::size_t lo = 0, hi = bounds.size();
            while (lo < hi) {
                const std::size_t mid = (lo + hi + 1) / 2;
                if (measure(prefix(mid) + ell) <= maxW) lo = mid;
                else hi = mid - 1;
            }
            std::string out = prefix(lo);
            while (!out.empty() && out.back() == ' ') out.pop_back();
            return out + ell;
        }

        int floorOf(const std::string& v) { return static_cast<int>(std::floor(std::atof(v.c_str()))); }

        std::string actionKindText(const Action& a) {
            std::string out;
            if (!a.time.empty()) out = a.time;
            if (a.isSection) out += out.empty() ? "section" : " \xC2\xB7 section";
            return out;
        }

    } // namespace

    int actionId(int stepItem, int rank) noexcept { return (stepItem + 1) * 100000 + rank; }

    std::pair<int, int> actionOf(int id) noexcept {
        if (id < 100000) return { -1, -1 };
        return { id / 100000 - 1, id % 100000 };
    }

    std::pair<int, int> Grid::cellAt(gfx::Point p) const {
        int col = 0, row = 0;
        if (!colLeft.empty()) {
            const float end = colRight.back() + kColGap;
            col = static_cast<int>(colLeft.size()) - 1;
            if (p.x >= end) col += 1 + static_cast<int>((p.x - end) / (kEmptyCol + kColGap));   // au-dela : des colonnes vides
            else
                for (std::size_t i = 0; i < colLeft.size(); ++i) {
                    const float right = i + 1 < colLeft.size() ? colLeft[i + 1] : end;
                    if (p.x < right) { col = static_cast<int>(i); break; }
                }
        }
        if (!rowTop.empty()) {
            row = static_cast<int>(rowTop.size()) - 1;
            for (std::size_t i = 0; i < rowTop.size(); ++i)
                if (p.y < rowTop[i] + rowH[i]) { row = static_cast<int>(i); break; }
            if (p.y >= rowTop.back() + rowH.back())
                row = static_cast<int>(rowTop.size()) - 1
                    + static_cast<int>((p.y - rowTop.back() - rowH.back()) / std::max(1.f, kEmptyRow + 2 * kRowPad)) + 1;
        }
        return { col + minCol, row };
    }

    grafcet::Diagram buildDiagram(const Chart& c, const Topology& topo, const grafcet::MeasureText& measure, Grid* gridOut,
                                  const std::map<int, gfx::Point>* moved) {
        grafcet::Diagram d;
        const auto& items = c.items;
        if (items.empty()) {
            d.size = { 2.f * kMargin + 200.f, 2.f * kMargin + 80.f };
            return d;
        }
        // ---- l'etendue de la grille
        int minC = 0, maxC = 0, maxR = 0;
        for (const auto& it : items) {
            switch (it.kind) {
            case ItemKind::Link:
                minC = std::min({ minC, it.sx, it.dx });
                maxC = std::max({ maxC, it.sx, it.dx });
                maxR = std::max({ maxR, it.sy, it.dy });
                for (const auto& [bx, by] : it.bends) {
                    minC = std::min(minC, floorOf(bx));
                    maxC = std::max(maxC, floorOf(bx));
                    maxR = std::max(maxR, floorOf(by));
                }
                break;
            case ItemKind::TextBox:
                maxC = std::max(maxC, it.x + it.boxW - 1);
                maxR = std::max(maxR, it.y + it.boxH - 1);
                break;
            case ItemKind::Other:
                break;
            default:
                maxC = std::max(maxC, it.x + (it.isBar() ? it.width - 1 : 0));
                maxR = std::max(maxR, it.y);
                minC = std::min(minC, it.x);
                break;
            }
        }
        maxR = std::max(0, maxR);
        const int nCols = maxC - minC + 1, nRows = maxR + 1;
        std::vector<float> leftExt(static_cast<std::size_t>(nCols), kEmptyCol / 2.f);
        std::vector<float> rightExt(static_cast<std::size_t>(nCols), kEmptyCol / 2.f);
        std::vector<float> content(static_cast<std::size_t>(nRows), kEmptyRow);
        std::vector<float> padTop(static_cast<std::size_t>(nRows), kRowPad), padBottom(static_cast<std::size_t>(nRows), kRowPad);
        auto col = [&](int x) { return static_cast<std::size_t>(std::clamp(x - minC, 0, nCols - 1)); };
        auto row = [&](int y) { return static_cast<std::size_t>(std::clamp(y, 0, nRows - 1)); };

        // ---- les textes et les tailles
        struct StepText { float w{ 0 }; float actionsW{ 0 }; float qualW{ kQualW }; std::vector<std::string> texts, kinds; };
        struct TransText { std::string label, tag; float w{ 0 }, h{ kLineH }; };
        std::map<int, StepText> stepText;
        std::map<int, TransText> transText;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            const int id = static_cast<int>(i);
            if (it.kind == ItemKind::Step) {
                StepText st;
                st.w = std::max(kStepMinW, measure(it.name) * 1.2f + 2.f * kPad + 8.f);   // le nom s'ecrit en 15 px
                for (const auto& a : it.actions)
                    st.qualW = std::max(st.qualW, measure(normalQualifier(a.qualifier)) + 2.f * kPad);
                for (const auto& a : it.actions) {
                    auto text = cut(a.name, kMaxAction, measure);
                    auto kind = cut(actionKindText(a), kMaxAction, measure);
                    st.actionsW = std::max(st.actionsW, st.qualW + std::max(measure(text), measure(kind)) + 2.f * kPad);
                    st.texts.push_back(std::move(text));
                    st.kinds.push_back(std::move(kind));
                }
                leftExt[col(it.x)] = std::max(leftExt[col(it.x)], st.w / 2.f);
                rightExt[col(it.x)] = std::max(rightExt[col(it.x)], st.w / 2.f + (it.actions.empty() ? 0.f : kActionGap + st.actionsW));
                content[row(it.y)] = std::max({ content[row(it.y)], kStepH,
                                                static_cast<float>(it.actions.size()) * kActionH + 6.f });
                stepText[id] = std::move(st);
            }
            else if (it.kind == ItemKind::Transition) {
                TransText tt;
                const auto full = conditionText(c, it);
                tt.label = cut(full.empty() ? std::string("?") : full, kMaxLabel, measure);
                if (it.cond == CondKind::Section) {
                    const auto* s = c.source("transitionSource", it.condText);
                    tt.tag = it.condText + (s ? " (" + std::string(domain::toString(s->language)) + ")" : std::string{});
                }
                tt.w = std::max(measure(tt.label), tt.tag.empty() ? 0.f : measure(tt.tag));
                tt.h = tt.tag.empty() ? kLineH : 2.f * kLineH;
                leftExt[col(it.x)] = std::max(leftExt[col(it.x)], kBarHalf + 10.f);
                rightExt[col(it.x)] = std::max(rightExt[col(it.x)], kBarHalf + kLabelPad + tt.w);
                content[row(it.y)] = std::max(content[row(it.y)], std::max(26.f, tt.h + 6.f));
                transText[id] = std::move(tt);
            }
            else if (it.kind == ItemKind::Jump) {
                leftExt[col(it.x)] = std::max(leftExt[col(it.x)], 14.f);
                rightExt[col(it.x)] = std::max(rightExt[col(it.x)], 14.f + measure("\xE2\x86\x92 " + it.name) + 8.f);
                content[row(it.y)] = std::max(content[row(it.y)], kJumpH);
            }
            else if (it.isDivergence()) {
                padTop[row(it.y)] = std::max(padTop[row(it.y)], 2.f * kBarPad);
            }
            else if (it.isConvergence()) {
                padBottom[row(it.y)] = std::max(padBottom[row(it.y)], 2.f * kBarPad);
            }
        }

        // ---- les colonnes (deux passes : un texte qui manque de place elargit sa derniere colonne)
        std::vector<float> colLeft(static_cast<std::size_t>(nCols)), axis(static_cast<std::size_t>(nCols)),
            colRight(static_cast<std::size_t>(nCols));
        auto place = [&] {
            float x = kMargin;
            for (std::size_t k = 0; k < static_cast<std::size_t>(nCols); ++k) {
                colLeft[k] = x;
                axis[k] = x + leftExt[k];
                colRight[k] = axis[k] + rightExt[k];
                x = colRight[k] + kColGap;
            }
        };
        place();
        for (const auto& it : items) {
            if (it.kind != ItemKind::TextBox) continue;
            const auto first = col(it.x), last = col(it.x + it.boxW - 1);
            const float need = std::min(measure(it.text) + 2.f * kPad, 280.f);
            const float avail = colRight[last] - colLeft[first];
            if (need > avail) rightExt[last] += need - avail;
        }
        place();
        // ---- les rangees
        std::vector<float> rowTop(static_cast<std::size_t>(nRows)), rowH(static_cast<std::size_t>(nRows));
        {
            float y = kMargin;
            for (std::size_t r = 0; r < static_cast<std::size_t>(nRows); ++r) {
                rowTop[r] = y;
                rowH[r] = padTop[r] + content[r] + padBottom[r];
                y += rowH[r];
            }
        }
        auto ax = [&](int x) { return axis[col(x)]; };
        auto top = [&](int y) { return rowTop[row(y)]; };
        auto contentTop = [&](int y) { return rowTop[row(y)] + padTop[row(y)]; };
        auto mid = [&](int y) { return contentTop(y) + content[row(y)] / 2.f; };
        auto barY = [&](const Item& b) {
            return b.isDivergence() ? top(b.y) + kBarPad : rowTop[row(b.y)] + rowH[row(b.y)] - kBarPad;
        };
        auto gridX = [&](const std::string& v) {
            const double g = std::atof(v.c_str());
            const int k = static_cast<int>(std::floor(g));
            const double f = g - k;
            const auto kk = col(k);
            if (std::fabs(f - 0.5) < 1e-6) return axis[kk];
            const float right = colRight[kk] + kColGap;
            return static_cast<float>(f < 0.5 ? colLeft[kk] + (axis[kk] - colLeft[kk]) * f * 2.0
                                              : axis[kk] + (right - axis[kk]) * (f - 0.5) * 2.0);
        };
        auto gridY = [&](const std::string& v) {
            const double g = std::atof(v.c_str());
            const int k = static_cast<int>(std::floor(g));
            if (k >= nRows) return rowTop.back() + rowH.back() + static_cast<float>(g - nRows + 1) * kEmptyRow;
            const double f = g - k;
            return static_cast<float>(top(k) + rowH[row(k)] * f);
        };

        // ---- les objets
        std::map<int, gfx::Rect> stepBox;
        std::map<int, float> transY;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            const int id = static_cast<int>(i);
            if (it.kind == ItemKind::Step) {
                const auto& st = stepText[id];
                grafcet::DiagramStep ds;
                ds.id = id;
                ds.box = { ax(it.x) - st.w / 2.f, contentTop(it.y), st.w, kStepH };
                if (moved)
                    if (const auto m = moved->find(id); m != moved->end()) {
                        ds.box.x = m->second.x;
                        ds.box.y = m->second.y;
                        ds.moved = true;
                    }
                ds.initial = it.stepKind == StepKind::Initial;
                ds.macro = it.stepKind == StepKind::Macro;
                ds.level = it.y;
                ds.column = it.x;
                ds.number = std::to_string(id);
                ds.label = it.name;
                stepBox[id] = ds.box;
                const float axx = ds.box.right() + kActionGap;
                float ay = ds.box.y + (kStepH - kActionH) / 2.f;
                if (!it.actions.empty()) {
                    grafcet::DiagramSegment link;
                    link.a = { ds.box.right(), ds.box.y + kStepH / 2.f };
                    link.b = { axx, ds.box.y + kStepH / 2.f };
                    link.step = id;
                    d.segments.push_back(link);
                }
                for (std::size_t k = 0; k < it.actions.size(); ++k) {
                    grafcet::DiagramAction da;
                    da.id = actionId(id, static_cast<int>(k));
                    da.step = id;
                    da.box = { axx, ay, st.actionsW, kActionH };
                    da.qualifierBox = { axx, ay, st.qualW, kActionH };
                    da.qualifier = normalQualifier(it.actions[k].qualifier);
                    da.text = st.texts[k];
                    da.kindText = st.kinds[k];
                    d.actions.push_back(std::move(da));
                    ay += kActionH;
                }
                d.steps.push_back(std::move(ds));
            }
            else if (it.kind == ItemKind::Transition) {
                const auto& tt = transText[id];
                grafcet::DiagramTransition dt;
                dt.id = id;
                const float yc = mid(it.y);
                const float x = ax(it.x);
                transY[id] = yc;
                dt.at = { x, yc };
                dt.bar = { x - kBarHalf, yc - 3.f, 2.f * kBarHalf, 6.f };
                dt.label = { x + kBarHalf + kLabelPad, yc - tt.h / 2.f, tt.w, tt.h };
                dt.idBox = { x - kBarHalf - 30.f, yc - 8.f, 26.f, 16.f };
                dt.hit = { x - kBarHalf - 4.f, std::min(yc - 8.f, dt.label.y), 2.f * kBarHalf + 8.f + kLabelPad + tt.w,
                           std::max(16.f, tt.h) };
                dt.labelText = tt.label;
                dt.tag = tt.tag;
                dt.level = it.y;
                dt.column = it.x;
                if (const auto* tl = topo.of(id)) {
                    dt.sources = tl->sources;
                    dt.destinations = tl->targets;
                    dt.andJoin = tl->andJoin;
                    dt.andSplit = tl->andSplit;
                }
                d.transitions.push_back(std::move(dt));
            }
            else if (it.kind == ItemKind::Jump) {
                const float x = ax(it.x);
                const float yc = mid(it.y);
                grafcet::DiagramSegment s;
                s.a = { x, top(it.y) };
                s.b = { x, yc + 6.f };
                s.arrowDown = true;
                d.segments.push_back(s);
                grafcet::DiagramRef ref;
                ref.step = c.findStep(it.name);
                ref.outgoing = true;
                ref.text = "\xE2\x86\x92 " + it.name;
                ref.box = { x + 10.f, yc - 6.f, measure(ref.text), 16.f };
                d.refs.push_back(ref);
                d.items.push_back(grafcet::DiagramItemBox{ id, { x - 12.f, top(it.y), 24.f + ref.box.w + 6.f, rowH[row(it.y)] },
                                                            "saut vers " + it.name });
            }
            else if (it.kind == ItemKind::TextBox) {
                const auto first = col(it.x), last = col(it.x + it.boxW - 1);
                const auto lastRow = row(it.y + it.boxH - 1);
                grafcet::DiagramNote n;
                n.box = { colLeft[first], top(it.y) + 4.f, colRight[last] - colLeft[first],
                          rowTop[lastRow] + rowH[lastRow] - top(it.y) - 8.f };
                n.text = it.text;
                d.notes.push_back(n);
                d.items.push_back(grafcet::DiagramItemBox{ id, n.box, "texte" });
            }
        }

        // ---- les ports
        auto outPort = [&](int id) -> gfx::Point {
            const auto& it = items[static_cast<std::size_t>(id)];
            if (it.kind == ItemKind::Step) { const auto& b = stepBox[id]; return { b.x + b.w / 2.f, b.bottom() }; }
            if (it.kind == ItemKind::Transition) return { ax(it.x), transY[id] };
            return { ax(it.x), mid(it.y) };
        };
        auto inPort = [&](int id) -> gfx::Point {
            const auto& it = items[static_cast<std::size_t>(id)];
            if (it.kind == ItemKind::Step) { const auto& b = stepBox[id]; return { b.x + b.w / 2.f, b.y }; }
            if (it.kind == ItemKind::Transition) return { ax(it.x), transY[id] };
            return { ax(it.x), top(it.y) };
        };
        auto entryCol = [&](const Item& b) { return b.x + b.relativePos; };

        // ---- les traits de divergence et de convergence
        std::map<int, std::pair<float, float>> barSpan;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].isBar()) {
                const float e = ax(entryCol(items[i]));
                barSpan[static_cast<int>(i)] = { e, e };
            }
        auto widen = [&](int bar, float x) {
            auto& s = barSpan[bar];
            s.first = std::min(s.first, x);
            s.second = std::max(s.second, x);
        };

        // ---- les liaisons
        for (const auto& e : topo.edges) {
            const auto& A = items[static_cast<std::size_t>(e.from)];
            const auto& B = items[static_cast<std::size_t>(e.to)];
            gfx::Point a, b;
            if (A.isCellObject()) a = outPort(e.from);
            else if (A.isDivergence()) { a = { ax(e.column >= 0 ? e.column : entryCol(A)), barY(A) }; widen(e.from, a.x); }
            else { a = { ax(entryCol(A)), barY(A) }; }
            if (B.isCellObject()) b = inPort(e.to);
            else if (B.isDivergence()) { b = { ax(entryCol(B)), barY(B) }; }
            else { b = { ax(e.column >= 0 ? e.column : B.x), barY(B) }; widen(e.to, b.x); }
            if (A.isCellObject() && B.isConvergence()) { b.x = a.x; widen(e.to, a.x); }
            if (A.isDivergence() && B.isCellObject()) { a.x = b.x; widen(e.from, b.x); }
            const int tid = A.kind == ItemKind::Transition ? e.from : B.kind == ItemKind::Transition ? e.to : -1;
            std::vector<gfx::Point> path{ a };
            if (e.link >= 0) {
                const auto& L = items[static_cast<std::size_t>(e.link)];
                for (const auto& [bx, by] : L.bends) path.push_back({ gridX(bx), gridY(by) });
                if (L.bends.empty() && std::fabs(a.x - b.x) > 0.5f) {
                    if (b.y > a.y) {
                        const float my = a.y + std::min(14.f, (b.y - a.y) / 2.f);
                        path.push_back({ a.x, my });
                        path.push_back({ b.x, my });
                    }
                    else {
                        const float side = std::min(a.x, b.x) - 24.f;
                        path.push_back({ a.x, a.y + 12.f });
                        path.push_back({ side, a.y + 12.f });
                        path.push_back({ side, b.y - 10.f });
                        path.push_back({ b.x, b.y - 10.f });
                    }
                }
                else if (L.bends.empty() && b.y < a.y) {
                    const float side = a.x - 30.f;
                    path.push_back({ a.x, a.y + 12.f });
                    path.push_back({ side, a.y + 12.f });
                    path.push_back({ side, b.y - 10.f });
                    path.push_back({ b.x, b.y - 10.f });
                }
                d.items.push_back(grafcet::DiagramItemBox{ e.link, { std::min(a.x, b.x) - 4.f, std::min(a.y, b.y), 8.f, 8.f },
                                                            "lien" });
            }
            else if (std::fabs(a.x - b.x) > 0.5f) {
                const float my = (a.y + b.y) / 2.f;
                path.push_back({ a.x, my });
                path.push_back({ b.x, my });
            }
            path.push_back(b);
            // la plus longue montee porte la fleche
            int up = -1;
            float best = 8.f;
            for (std::size_t k = 0; k + 1 < path.size(); ++k) {
                const float rise = path[k].y - path[k + 1].y;
                if (rise > best) { best = rise; up = static_cast<int>(k); }
            }
            for (std::size_t k = 0; k + 1 < path.size(); ++k) {
                grafcet::DiagramSegment s;
                s.a = path[k];
                s.b = path[k + 1];
                s.transition = tid;
                s.arrowUp = static_cast<int>(k) == up;
                s.manual = e.link >= 0;
                d.segments.push_back(s);
            }
        }
        // les segments d'un lien choisi : une boite a son milieu (le lien se choisit)
        for (auto& box : d.items) {
            if (box.what != "lien") continue;
            for (const auto& s : d.segments) {
                if (!s.manual) continue;
                if (std::fabs(s.a.y - s.b.y) > 20.f) {
                    box.box = { s.a.x - 5.f, std::min(s.a.y, s.b.y) + 4.f, 10.f, std::fabs(s.a.y - s.b.y) - 8.f };
                    break;
                }
            }
        }
        for (const auto& [bar, span] : barSpan) {
            const auto& B = items[static_cast<std::size_t>(bar)];
            grafcet::DiagramJunction j;
            j.x0 = span.first;
            j.x1 = span.second;
            if (j.x1 - j.x0 < 1.f) { j.x0 = ax(B.x); j.x1 = ax(B.x + B.width - 1); }
            j.y = barY(B);
            j.isAnd = B.kind == ItemKind::ParBranch || B.kind == ItemKind::ParJoint;
            j.divergence = B.isDivergence();
            j.at = -1;
            for (const auto& e : topo.edges) {
                if (B.isDivergence() && e.to == bar && items[static_cast<std::size_t>(e.from)].kind == ItemKind::Transition) j.at = e.from;
                if (B.isConvergence() && e.from == bar && items[static_cast<std::size_t>(e.to)].kind == ItemKind::Transition) j.at = e.to;
            }
            d.junctions.push_back(j);
            const char* what = B.kind == ItemKind::AltBranch ? "divergence en OU" : B.kind == ItemKind::AltJoint ? "convergence en OU"
                             : B.kind == ItemKind::ParBranch ? "divergence en ET" : "convergence en ET";
            d.items.push_back(grafcet::DiagramItemBox{ bar, { j.x0 - 10.f, j.y - 5.f, j.x1 - j.x0 + 20.f, 10.f }, what });
        }

        d.size = { colRight.back() + kMargin, rowTop.back() + rowH.back() + kMargin };
        if (gridOut) {
            gridOut->minCol = minC;
            gridOut->colLeft = colLeft;
            gridOut->axis = axis;
            gridOut->colRight = colRight;
            gridOut->rowTop = rowTop;
            gridOut->rowH = rowH;
        }
        return d;
    }

    grafcet::Chart asGrafcet(const Chart& c, const Topology& topo, const std::string& name) {
        grafcet::Chart g;
        g.name = name;
        for (std::size_t i = 0; i < c.items.size(); ++i) {
            const auto& it = c.items[i];
            if (it.kind != ItemKind::Step) continue;
            grafcet::Step s;
            s.id = static_cast<int>(i);
            s.name = it.name;
            s.initial = it.stepKind == StepKind::Initial;
            g.steps.push_back(s);
            for (std::size_t k = 0; k < it.actions.size(); ++k) {
                const auto& a = it.actions[k];
                grafcet::Action ga;
                ga.id = actionId(static_cast<int>(i), static_cast<int>(k));
                ga.name = a.name;
                ga.boundStep = static_cast<int>(i);
                ga.delay = a.time;
                const auto q = normalQualifier(a.qualifier);
                ga.kind = q == "N" ? grafcet::ActionKind::Continuous : q == "D" ? grafcet::ActionKind::DelayOn
                        : q == "L" ? grafcet::ActionKind::LimitedOn : q == "P0" ? grafcet::ActionKind::Falling
                        : (q == "P" || q == "P1") ? grafcet::ActionKind::Rising : grafcet::ActionKind::Unknown;
                g.actions.push_back(ga);
            }
        }
        for (const auto& tl : topo.transitions) {
            grafcet::Transition t;
            t.id = tl.item;
            t.sources = tl.sources;
            t.destinations = tl.targets;
            t.conditionExpr = conditionText(c, c.items[static_cast<std::size_t>(tl.item)]);
            t.join = grafcet::JoinKind::And;
            t.split = tl.andSplit ? grafcet::SplitKind::And : grafcet::SplitKind::Single;
            g.transitions.push_back(t);
        }
        return g;
    }

    std::vector<grafcet::Check> checks(const Chart& c, const Topology& topo) {
        std::vector<grafcet::Check> out;
        for (const auto& [item, message] : topo.problems) {
            grafcet::Check k;
            k.message = message;
            k.severe = true;
            if (item < 0 || static_cast<std::size_t>(item) >= c.items.size()) {
                k.kind = message.find("initiale") != std::string::npos ? grafcet::CheckKind::NoInitial
                                                                       : grafcet::CheckKind::DanglingLink;
                out.push_back(k);
                continue;
            }
            const auto& it = c.items[static_cast<std::size_t>(item)];
            k.id = item;
            if (it.kind == ItemKind::Step) {
                k.part = 'X';
                k.kind = message.find("s'appellent") != std::string::npos ? grafcet::CheckKind::DuplicateId
                                                                         : grafcet::CheckKind::DanglingLink;
            }
            else if (it.kind == ItemKind::Transition) {
                k.part = 'T';
                k.kind = message.find("ceptivit") != std::string::npos ? grafcet::CheckKind::NoCondition
                                                                       : grafcet::CheckKind::DanglingLink;
            }
            else {
                k.kind = grafcet::CheckKind::DanglingLink;
            }
            out.push_back(k);
        }
        return out;
    }

} // namespace sfc
