#include "GrafcetDiagram.hpp"

#include "GrafcetCheck.hpp"
#include "GrafcetLayout.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace grafcet {

    const DiagramStep* Diagram::step(int id) const {
        for (const auto& s : steps) if (s.id == id) return &s;
        return nullptr;
    }

    const DiagramTransition* Diagram::transition(int id) const {
        for (const auto& t : transitions) if (t.id == id) return &t;
        return nullptr;
    }

    const DiagramAction* Diagram::action(int id) const {
        for (const auto& a : actions) if (a.id == id) return &a;
        return nullptr;
    }

    MeasureText fixedWidthMeasure(float perChar) {
        return [perChar](std::string_view s) {
            float n = 0.f;
            for (const char c : s)
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) n += 1.f;
            return n * perChar;
        };
    }

    std::string drawnCondition(const Chart& c, const Transition& t) {
        const std::string& source = t.conditionExpr.empty() ? t.conditionText : t.conditionExpr;
        const std::string readable = readableCondition(c, source);
        std::string out;
        bool space = false;
        for (const char ch : readable) {
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { space = true; continue; }
            if (space && !out.empty()) out.push_back(' ');
            space = false;
            out.push_back(ch);
        }
        return out;
    }

    namespace {

        // Un texte coupe a une largeur, sur une frontiere de caractere UTF-8, avec
        // des points de suspension. Le texte entier reste dans l'infobulle.
        std::string cut(const std::string& text, float maxW, const MeasureText& measure) {
            if (text.empty() || measure(text) <= maxW) return text;
            static const std::string ell = "\xE2\x80\xA6";
            std::vector<std::size_t> bounds;
            for (std::size_t i = 0; i < text.size(); ++i)
                if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) bounds.push_back(i);
            auto prefix = [&](std::size_t k) {
                return text.substr(0, k < bounds.size() ? bounds[k] : text.size());
            };
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

        struct StepInfo {
            const Step* step{ nullptr };
            int   level{ 0 }, column{ 0 };
            bool  reachable{ true };
            std::vector<const Action*> actions;
            std::vector<std::string>   actionTexts, kindTexts;
            float actionBoxW{ 0.f };
            float qualifierW{ 0.f };   // la case du qualificatif, assez large pour "C LP"
            float bodyH{ 0.f };
            float rightExt{ 0.f };
        };

        struct TransInfo {
            const Transition* t{ nullptr };
            bool  renvoi{ false };
            bool  skip{ false };     // liaison directe qui descend de plus d'une rangee
            int   srcLevel{ 0 };     // la rangee des sources
            int   band{ 0 };       // la bande du trait : sous les sources (renvoi), au-dessus des destinations
            int   column{ 0 };     // la colonne de sa voie
            int   lane{ 0 };
            std::string tag, label, refText;
            float labelW{ 0.f }, labelH{ 0.f }, refW{ 0.f };
            float laneW{ 0.f };    // de l'axe de la voie a son bord droit
            float leftW{ 0.f };    // de l'axe de la voie au bord gauche de son numero
            std::string idText;
            float offset{ 0.f };   // de l'axe de la colonne a l'axe de la voie
        };

        // Une liaison directe est possible quand toutes les sources sont sur une
        // rangee et toutes les destinations sur la suivante. Le reste (remonter,
        // aller de cote, sauter des rangees, boucler) est un renvoi d'emblee.
        bool directCandidate(const Transition& t, const std::map<int, StepInfo>& info) {
            if (t.sources.empty() || t.destinations.empty()) return false;
            int level = -1;
            for (const int s : t.sources) {
                const auto it = info.find(s);
                if (it == info.end()) return false;
                if (level < 0) level = it->second.level;
                else if (it->second.level != level) return false;
            }
            int below = -1;
            for (const int d : t.destinations) {
                const auto it = info.find(d);
                if (it == info.end() || it->second.level <= level) return false;
                if (below < 0) below = it->second.level;
                else if (it->second.level != below) return false;
            }
            // en ET, seulement d'une rangee a la suivante
            if (t.destinations.size() > 1 && below != level + 1) return false;
            for (const int s : t.sources)
                for (const int d : t.destinations)
                    if (s == d) return false;
            return true;
        }

        DiagramSegment segment(gfx::Point a, gfx::Point b, int transition, std::string group = {}) {
            DiagramSegment s;
            s.a = a;
            s.b = b;
            s.transition = transition;
            s.group = std::move(group);
            return s;
        }

        // Ce que le controle compare : une case ou un trait, a qui il appartient,
        // et quelle transition rendre en renvoi s'il croise quelque chose.
        struct Item {
            bool        isBox{ true };
            gfx::Rect   r;
            gfx::Point  a, b;
            int         transition{ -1 };
            int         step{ -1 };
            std::string group;
            int         blame{ -1 };
            std::string what;
        };

        struct Placed {
            Diagram                    diagram;
            std::map<std::string, int> groupBlame;   // un trait partage -> la transition qui l'a etendu
            std::map<int, float>       span;         // le detour horizontal de chaque liaison directe
        };

        Placed place(const Chart& chart, const Layout& grid, const MeasureText& measure,
            const DiagramMetrics& m, const std::set<int>& renvoi) {
            Placed out;
            Diagram& d = out.diagram;
            const float half = m.stepSize / 2.f;

            // ---- les etapes et leurs actions -----------------------------------
            std::map<int, StepInfo> steps;
            int levels = 0, columns = 0;
            for (const auto& s : chart.steps) {
                if (steps.count(s.id)) continue;   // numero en double : le controle le dit
                const PlacedStep* p = grid.step(s.id);
                StepInfo si;
                si.step = &s;
                si.level = p ? p->level : 0;
                si.column = p ? p->column : 0;
                si.reachable = p ? p->reachable : true;
                levels = std::max(levels, si.level + 1);
                columns = std::max(columns, si.column + 1);
                si.qualifierW = m.qualifierW;
                for (const auto* a : chart.actionsOfStep(s.id))
                    si.qualifierW = std::max(si.qualifierW, measure(kindQualifier(a->kind)) + 2.f * m.textPad);
                for (const auto* a : chart.actionsOfStep(s.id)) {
                    std::string text = cut(a->name.empty() ? "A" + std::to_string(a->id) : a->name, m.maxAction, measure);
                    std::string kind = cut(kindLabel(a->kind, a->delay), m.maxAction, measure);
                    si.actionBoxW = std::max(si.actionBoxW,
                        si.qualifierW + std::max(measure(text), measure(kind)) + 2.f * m.textPad);
                    si.actions.push_back(a);
                    si.actionTexts.push_back(std::move(text));
                    si.kindTexts.push_back(std::move(kind));
                }
                const float stack = si.actions.empty() ? 0.f
                    : half - m.actionH / 2.f + static_cast<float>(si.actions.size()) * m.actionH;
                si.bodyH = std::max(m.stepSize, stack);
                si.rightExt = half + (si.actions.empty() ? 0.f : m.actionGap + si.actionBoxW);
                steps[s.id] = std::move(si);
            }
            if (steps.empty()) {
                d.size = { 2.f * m.margin, 2.f * m.margin };
                return out;
            }
            auto columnOf = [&](int id, int fallback) {
                const auto it = steps.find(id);
                return it == steps.end() ? fallback : it->second.column;
            };

            // ---- les transitions : renvoi ou liaison, bande, colonne, textes -----
            std::map<int, int> outCount;
            for (const auto& t : chart.transitions)
                for (const int s : t.sources) ++outCount[s];

            std::map<int, TransInfo> trans;
            for (const auto& t : chart.transitions) {
                if (trans.count(t.id)) continue;
                TransInfo ti;
                ti.t = &t;
                ti.renvoi = renvoi.count(t.id) > 0 || !directCandidate(t, steps);

                int band = -1, anchor = -1;
                for (const int s : t.sources) {
                    const auto it = steps.find(s);
                    if (it == steps.end()) continue;
                    if (it->second.level > band) { band = it->second.level; anchor = s; }
                }
                if (band < 0)
                    for (const int dd : t.destinations)
                        if (const auto it = steps.find(dd); it != steps.end()) {
                            band = it->second.level;
                            anchor = dd;
                            break;
                        }
                ti.band = std::max(band, 0);
                ti.srcLevel = ti.band;
                if (!ti.renvoi && !t.destinations.empty()) {
                    const auto it = steps.find(t.destinations.front());
                    if (it != steps.end() && it->second.level - 1 > ti.band) {
                        ti.band = it->second.level - 1;
                        ti.skip = true;
                    }
                }
                const int anchorColumn = anchor >= 0 ? columnOf(anchor, 0) : 0;

                if (ti.renvoi) ti.column = anchorColumn;
                else if (t.sources.size() > 1) ti.column = columnOf(t.destinations.front(), anchorColumn);
                else if (t.destinations.size() > 1) ti.column = anchorColumn;
                else ti.column = outCount[t.sources.front()] > 1 ? columnOf(t.destinations.front(), anchorColumn)
                                                                 : anchorColumn;

                const std::string expr = drawnCondition(chart, t);
                ti.label = cut(expr, m.maxLabel, measure);
                if (m.labelTag && !t.conditionText.empty() && t.conditionText != expr)
                    ti.tag = cut(t.conditionText, m.maxLabel, measure);
                ti.labelW = std::max(measure(ti.label), ti.tag.empty() ? 0.f : measure(ti.tag));
                ti.labelH = m.lineH * (ti.tag.empty() ? 1.f : 2.f);
                if (ti.renvoi) {
                    std::string ref;
                    for (const int dd : t.destinations) {
                        if (!ref.empty()) ref += ", ";
                        ref += "X" + std::to_string(dd);
                    }
                    ti.refText = ref.empty() ? std::string("?") : ref;
                    ti.refW = measure(ti.refText);
                }
                ti.laneW = std::max(m.barHalf + (ti.labelW > 0.f ? m.labelPad + ti.labelW : 0.f),
                    ti.renvoi ? 6.f + ti.refW : 0.f);
                ti.idText = "T" + std::to_string(t.id);
                ti.leftW = m.barHalf + m.idGap + measure(ti.idText);
                trans[t.id] = std::move(ti);
            }

            // les voies : dans une bande et une colonne, les liaisons d'abord, puis
            // les renvois, chacun dans l'ordre des numeros
            std::map<std::pair<int, int>, std::vector<int>> lanes;
            for (const auto& [id, ti] : trans) lanes[{ ti.band, ti.column }].push_back(id);

            std::vector<float> rightExt(static_cast<std::size_t>(columns), half);
            std::vector<float> leftExt(static_cast<std::size_t>(columns), half);
            for (const auto& [id, si] : steps)
                rightExt[static_cast<std::size_t>(si.column)] = std::max(rightExt[static_cast<std::size_t>(si.column)], si.rightExt);
            for (auto& [key, ids] : lanes) {
                std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
                    const int ka = trans[a].renvoi ? 2 : trans[a].skip ? 1 : 0;
                    const int kb = trans[b].renvoi ? 2 : trans[b].skip ? 1 : 0;
                    if (ka != kb) return ka < kb;
                    return a < b;
                });
                // la voie 0 sur l'axe de la colonne, son numero a gauche ; chaque
                // voie suivante apres le bord droit de la precedente, son numero compris
                float offset = 0.f;
                const auto c = static_cast<std::size_t>(std::clamp(key.second, 0, columns - 1));
                for (std::size_t k = 0; k < ids.size(); ++k) {
                    auto& ti = trans[ids[k]];
                    ti.lane = static_cast<int>(k);
                    if (k == 0) leftExt[c] = std::max(leftExt[c], ti.leftW);
                    else offset += m.laneGap + ti.leftW;
                    ti.offset = offset;
                    rightExt[c] = std::max(rightExt[c], offset + ti.laneW);
                    offset += ti.laneW;
                }
            }

            // les arrivees de renvois : au-dessus de l'etape, d'ou l'on vient
            std::map<int, std::vector<int>> incoming;       // etape -> transitions en renvoi
            std::map<int, std::vector<int>> directInto;     // etape -> liaisons directes
            for (const auto& [id, ti] : trans) {
                for (const int dd : ti.t->destinations) {
                    if (!steps.count(dd)) continue;
                    if (ti.renvoi) incoming[dd].push_back(id);
                    else directInto[dd].push_back(id);
                }
            }
            std::map<int, std::string> incomingText;
            for (const auto& [dd, from] : incoming) {
                std::vector<int> sources;
                for (const int id : from)
                    for (const int s : trans[id].t->sources)
                        if (std::find(sources.begin(), sources.end(), s) == sources.end()) sources.push_back(s);
                std::sort(sources.begin(), sources.end());
                std::string text;
                for (const int s : sources) {
                    if (!text.empty()) text += ", ";
                    text += "X" + std::to_string(s);
                }
                text = cut(text, m.maxLabel, measure);
                incomingText[dd] = text;
                const auto c = static_cast<std::size_t>(steps[dd].column);
                rightExt[c] = std::max(rightExt[c], 6.f + measure(text));
            }

            // ---- les colonnes ------------------------------------------------------
            std::vector<float> colX(static_cast<std::size_t>(columns));
            float x = m.margin;
            for (std::size_t c = 0; c < colX.size(); ++c) {
                colX[c] = x + leftExt[c];
                x = colX[c] + rightExt[c] + m.gapX;
            }
            const float width = x - m.gapX + m.margin;
            auto laneX = [&](const TransInfo& ti) {
                return colX[static_cast<std::size_t>(std::clamp(ti.column, 0, columns - 1))] + ti.offset;
            };
            auto stepX = [&](int id) { return colX[static_cast<std::size_t>(steps[id].column)]; };

            // ---- les rangees et leurs bandes ---------------------------------------
            const auto L = static_cast<std::size_t>(levels);
            std::vector<float> rowBody(L, m.stepSize), bandLabel(L, 0.f);
            std::vector<char>  hasDiv(L, 0), hasAfter(L, 0), hasConv(L, 0), hasIncoming(L + 1, 0);
            for (const auto& [id, si] : steps)
                rowBody[static_cast<std::size_t>(si.level)] = std::max(rowBody[static_cast<std::size_t>(si.level)], si.bodyH);
            for (const auto& [id, ti] : trans) {
                const auto b = static_cast<std::size_t>(ti.band);
                if (b >= L) continue;
                bandLabel[b] = std::max(bandLabel[b], ti.labelH);
                const float xb = laneX(ti);
                if (ti.renvoi || ti.t->destinations.size() > 1) hasAfter[b] = 1;
                if (ti.t->sources.size() > 1) hasDiv[b] = 1;
                const auto sb = static_cast<std::size_t>(std::clamp(ti.srcLevel, 0, levels - 1));
                if (ti.t->sources.size() > 1) hasDiv[sb] = 1;
                for (const int s : ti.t->sources)
                    if (steps.count(s) && (outCount[s] > 1 || std::abs(stepX(s) - xb) > 0.5f)) hasDiv[sb] = 1;
                if (!ti.renvoi && ti.t->destinations.size() == 1) {
                    const int dd = ti.t->destinations.front();
                    if (directInto[dd].size() > 1 || std::abs(stepX(dd) - xb) > 0.5f) hasConv[b] = 1;
                }
            }
            for (const auto& [dd, from] : incoming) hasIncoming[static_cast<std::size_t>(steps[dd].level)] = 1;

            std::vector<float> rowTop(L + 1, 0.f), yDiv(L, 0.f), yBar(L, 0.f), yAfter(L, 0.f), yConv(L, 0.f), yInc(L + 1, 0.f);
            float y = m.margin;
            if (hasIncoming[0]) { yInc[0] = y; y += m.lineH + 8.f; }
            for (std::size_t r = 0; r < L; ++r) {
                rowTop[r] = y;
                y += rowBody[r] + 6.f;
                if (hasDiv[r]) { yDiv[r] = y + 4.f; y = yDiv[r] + 10.f; }
                else yDiv[r] = y;
                const float lh = std::max(bandLabel[r], m.lineH);
                yBar[r] = y + lh / 2.f + 2.f;
                y = yBar[r] + lh / 2.f + 4.f;
                yAfter[r] = y;
                if (hasAfter[r]) y += m.lineH + 8.f;
                if (r + 1 < L && hasIncoming[r + 1]) { yInc[r + 1] = y; y += m.lineH + 4.f; }
                if (hasConv[r]) { yConv[r] = y + 4.f; y = yConv[r] + 10.f; }
                else { yConv[r] = y; y += 6.f; }
            }
            rowTop[L] = y;
            d.size = { width, y + m.margin };

            // ---- les etapes, les actions -------------------------------------------
            for (const auto& [id, si] : steps) {
                DiagramStep ds;
                ds.id = id;
                ds.level = si.level;
                ds.column = si.column;
                ds.box = { colX[static_cast<std::size_t>(si.column)] - half, rowTop[static_cast<std::size_t>(si.level)],
                           m.stepSize, m.stepSize };
                ds.initial = si.step->initial;
                ds.isFinal = si.step->isFinal;
                ds.reachable = si.reachable;
                ds.number = std::to_string(id);
                if (!si.step->name.empty() && si.step->name != "X" + ds.number) ds.name = si.step->name;
                const float ax = ds.box.right() + m.actionGap;
                float ay = ds.box.y + half - m.actionH / 2.f;
                if (!si.actions.empty()) {
                    DiagramSegment link = segment({ ds.box.right(), ds.box.y + half }, { ax, ds.box.y + half }, -1);
                    link.step = id;
                    d.segments.push_back(std::move(link));
                }
                for (std::size_t k = 0; k < si.actions.size(); ++k) {
                    DiagramAction da;
                    da.id = si.actions[k]->id;
                    da.step = id;
                    da.box = { ax, ay, si.actionBoxW, m.actionH };
                    da.qualifierBox = { ax, ay, si.qualifierW, m.actionH };
                    da.qualifier = std::string(kindQualifier(si.actions[k]->kind));
                    da.text = si.actionTexts[k];
                    da.kindText = si.kindTexts[k];
                    d.actions.push_back(std::move(da));
                    ay += m.actionH;
                }
                d.steps.push_back(std::move(ds));
            }

            // ---- les transitions et leurs liaisons ---------------------------------
            std::map<int, std::pair<float, float>> divSpan, convSpan;   // etape -> [x0, x1]
            std::map<int, float> divReach, convReach;                     // pour le blame : le plus loin
            std::set<int> divStem;
            auto extend = [](std::map<int, std::pair<float, float>>& spans, int key, float a, float b) {
                const auto it = spans.find(key);
                if (it == spans.end()) spans[key] = { std::min(a, b), std::max(a, b) };
                else {
                    it->second.first = std::min({ it->second.first, a, b });
                    it->second.second = std::max({ it->second.second, a, b });
                }
            };

            for (const auto& [id, ti] : trans) {
                const Transition& t = *ti.t;
                const auto b = static_cast<std::size_t>(std::min(ti.band, levels - 1));
                const float xb = laneX(ti);
                const float yb = yBar[b];

                DiagramTransition dt;
                dt.id = id;
                dt.renvoi = ti.renvoi;
                dt.level = ti.band;
                dt.column = ti.column;
                dt.lane = ti.lane;
                dt.sources = t.sources;
                dt.destinations = t.destinations;
                dt.andJoin = t.sources.size() > 1;
                dt.andSplit = t.destinations.size() > 1;
                dt.at = { xb, yb };
                dt.bar = { xb - m.barHalf, yb - 2.f, 2.f * m.barHalf, 4.f };
                dt.tag = ti.tag;
                dt.labelText = ti.label;
                dt.label = { xb + m.barHalf + m.labelPad, yb - ti.labelH / 2.f, ti.labelW, ti.labelH };
                dt.idText = ti.idText;
                dt.idBox = { xb - ti.leftW, yb - m.lineH / 2.f, ti.leftW - m.barHalf - m.idGap, m.lineH };
                const float hitRight = std::max(dt.bar.right(), ti.labelW > 0.f ? xb + m.barHalf + m.labelPad + ti.labelW : 0.f);
                dt.hit = { xb - ti.leftW, yb - std::max(ti.labelH / 2.f, 8.f), hitRight - (xb - ti.leftW) + 4.f,
                           2.f * std::max(ti.labelH / 2.f, 8.f) };
                float detour = 0.f;

                // en amont : des sources au trait (la divergence est sous les sources ;
                // une liaison qui saute des rangees descend de la jusqu'a son trait)
                const auto sl = static_cast<std::size_t>(std::clamp(ti.srcLevel, 0, levels - 1));
                if (dt.andJoin) {
                    const std::string group = "and:" + std::to_string(id);
                    float x0 = xb, x1 = xb;
                    for (const int s : t.sources) {
                        if (!steps.count(s) || steps[s].level != ti.srcLevel) continue;
                        const float sx = stepX(s);
                        d.segments.push_back(segment({ sx, rowTop[static_cast<std::size_t>(steps[s].level)] + m.stepSize },
                                                     { sx, yDiv[sl] }, id, group));
                        x0 = std::min(x0, sx);
                        x1 = std::max(x1, sx);
                        detour += std::abs(sx - xb);
                    }
                    d.junctions.push_back(DiagramJunction{ x0, x1, yDiv[sl], true, false, id });
                    d.segments.push_back(segment({ xb, yDiv[sl] + 3.f }, { xb, yb - 2.f }, id, group));
                }
                else if (!t.sources.empty() && steps.count(t.sources.front())
                         && steps[t.sources.front()].level == ti.srcLevel) {
                    const int s = t.sources.front();
                    const float sx = stepX(s);
                    const float bottom = rowTop[static_cast<std::size_t>(steps[s].level)] + m.stepSize;
                    if (outCount[s] == 1 && std::abs(sx - xb) < 0.5f) {
                        d.segments.push_back(segment({ sx, bottom }, { xb, yb - 2.f }, id));
                    }
                    else {
                        const std::string group = "div:" + std::to_string(s);
                        if (divStem.insert(s).second)
                            d.segments.push_back(segment({ sx, bottom }, { sx, yDiv[sl] }, -1, group));
                        extend(divSpan, s, sx, xb);
                        if (!ti.renvoi && std::abs(xb - sx) > divReach[s]) {
                            divReach[s] = std::abs(xb - sx);
                            out.groupBlame[group] = id;
                        }
                        d.segments.push_back(segment({ xb, yDiv[sl] }, { xb, yb - 2.f }, id, group));
                        detour += std::abs(xb - sx);
                    }
                }

                // en aval : du trait aux destinations, ou le renvoi
                if (ti.renvoi) {
                    DiagramSegment arrow = segment({ xb, yb + 2.f }, { xb, yAfter[b] + m.lineH }, id);
                    arrow.arrowDown = true;
                    d.segments.push_back(std::move(arrow));
                    DiagramRef ref;
                    ref.transition = id;
                    ref.step = t.destinations.empty() ? -1 : t.destinations.front();
                    ref.outgoing = true;
                    ref.text = ti.refText;
                    ref.box = { xb + 6.f, yAfter[b] + 2.f, ti.refW, m.lineH };
                    d.refs.push_back(std::move(ref));
                }
                else if (dt.andSplit) {
                    const std::string group = "and:" + std::to_string(id);
                    const float yj = yAfter[b] + 2.f;
                    d.segments.push_back(segment({ xb, yb + 2.f }, { xb, yj }, id, group));
                    float x0 = xb, x1 = xb;
                    for (const int dd : t.destinations) {
                        const float dx = stepX(dd);
                        d.segments.push_back(segment({ dx, yj + 3.f },
                                                     { dx, rowTop[static_cast<std::size_t>(steps[dd].level)] }, id, group));
                        x0 = std::min(x0, dx);
                        x1 = std::max(x1, dx);
                        detour += std::abs(dx - xb);
                    }
                    d.junctions.push_back(DiagramJunction{ x0, x1, yj, true, true, id });
                }
                else {
                    const int dd = t.destinations.front();
                    const float dx = stepX(dd);
                    const float top = rowTop[static_cast<std::size_t>(steps[dd].level)];
                    if (directInto[dd].size() == 1 && std::abs(dx - xb) < 0.5f) {
                        d.segments.push_back(segment({ xb, yb + 2.f }, { xb, top }, id));
                    }
                    else {
                        const std::string group = "conv:" + std::to_string(dd);
                        d.segments.push_back(segment({ xb, yb + 2.f }, { xb, yConv[b] }, id, group));
                        extend(convSpan, dd, dx, xb);
                        if (std::abs(xb - dx) > convReach[dd]) {
                            convReach[dd] = std::abs(xb - dx);
                            out.groupBlame[group] = id;
                        }
                        detour += std::abs(xb - dx);
                    }
                }
                // une liaison qui saute des rangees est la premiere rendue en renvoi
                out.span[id] = detour + (ti.skip ? 1.0e4f : 0.f);
                d.transitions.push_back(std::move(dt));
            }

            // les traits partages : divergences et convergences en OU
            for (const auto& [s, span] : divSpan) {
                const auto b = static_cast<std::size_t>(steps[s].level);
                if (span.second - span.first < 0.5f) continue;
                d.junctions.push_back(DiagramJunction{ span.first, span.second, yDiv[b], false, true, s });
            }
            for (const auto& [dd, span] : convSpan) {
                const auto lvl = static_cast<std::size_t>(steps[dd].level);
                const auto b = lvl > 0 ? lvl - 1 : 0;
                const float dx = stepX(dd);
                if (span.second - span.first >= 0.5f)
                    d.junctions.push_back(DiagramJunction{ span.first, span.second, yConv[b], false, false, dd });
                d.segments.push_back(segment({ dx, yConv[b] }, { dx, rowTop[lvl] }, -1, "conv:" + std::to_string(dd)));
            }

            // les arrivees de renvois
            for (const auto& [dd, from] : incoming) {
                const auto lvl = static_cast<std::size_t>(steps[dd].level);
                const float dx = stepX(dd);
                DiagramRef ref;
                ref.transition = -1;
                ref.step = dd;
                ref.outgoing = false;
                ref.from = from;
                ref.text = incomingText[dd];
                ref.box = { dx + 6.f, yInc[lvl], measure(ref.text), m.lineH };
                d.refs.push_back(std::move(ref));
                if (directInto[dd].empty()) {
                    DiagramSegment arrow = segment({ dx, yInc[lvl] }, { dx, rowTop[lvl] }, -1, "in:" + std::to_string(dd));
                    arrow.arrowDown = true;
                    d.segments.push_back(std::move(arrow));
                }
            }

            for (const auto& t : d.transitions) if (t.renvoi) ++d.renvoiCount;
            return out;
        }

        // ---- le controle ---------------------------------------------------------
        std::vector<Item> itemsOf(const Diagram& d, const std::map<std::string, int>* groupBlame) {
            std::vector<Item> items;
            std::set<int> renvoi;
            for (const auto& t : d.transitions) if (t.renvoi) renvoi.insert(t.id);
            auto blameOf = [&](int transition, const std::string& group) {
                if (transition >= 0) return renvoi.count(transition) ? -1 : transition;
                if (groupBlame) {
                    const auto it = groupBlame->find(group);
                    if (it != groupBlame->end() && !renvoi.count(it->second)) return it->second;
                }
                return -1;
            };
            for (const auto& s : d.steps) {
                if (s.moved) continue;
                Item it;
                it.r = s.box;
                it.step = s.id;
                it.what = "l'\xC3\xA9tape " + s.number;
                items.push_back(std::move(it));
            }
            for (const auto& a : d.actions) {
                if (const auto* s = d.step(a.step); s && s->moved) continue;
                Item it;
                it.r = a.box;
                it.step = a.step;
                it.what = "l'action A" + std::to_string(a.id);
                items.push_back(std::move(it));
            }
            for (const auto& t : d.transitions) {
                Item bar;
                bar.r = t.bar;
                bar.transition = t.id;
                bar.blame = blameOf(t.id, {});
                bar.what = "le trait de T" + std::to_string(t.id);
                items.push_back(bar);
                if (t.idBox.w > 0.f) {
                    Item id = bar;
                    id.r = t.idBox;
                    id.what = "le num\xC3\xA9ro de T" + std::to_string(t.id);
                    items.push_back(std::move(id));
                }
                if (t.label.w > 0.f) {
                    Item label = bar;
                    label.r = t.label;
                    label.what = "la r\xC3\xA9" "ceptivit\xC3\xA9 de T" + std::to_string(t.id);
                    items.push_back(std::move(label));
                }
            }
            for (const auto& r : d.refs) {
                Item it;
                it.r = r.box;
                it.transition = r.transition;
                it.step = r.outgoing ? -1 : r.step;
                it.group = r.outgoing ? std::string{} : "in:" + std::to_string(r.step);
                it.what = r.outgoing ? "le renvoi de T" + std::to_string(r.transition)
                                     : "l'arriv\xC3\xA9" "e de renvoi sur " + std::to_string(r.step);
                items.push_back(std::move(it));
            }
            for (const auto& s : d.segments) {
                if (s.manual) continue;
                Item it;
                it.isBox = false;
                it.a = s.a;
                it.b = s.b;
                it.transition = s.transition;
                it.step = s.step;
                it.group = s.group;
                it.blame = blameOf(s.transition, s.group);
                it.what = s.transition >= 0 ? "la liaison de T" + std::to_string(s.transition)
                        : s.step >= 0 ? "le lien de l'\xC3\xA9tape " + std::to_string(s.step) + " \xC3\xA0 ses actions"
                                      : "le trait " + s.group;
                items.push_back(std::move(it));
            }
            for (const auto& j : d.junctions) {
                const std::string group = j.isAnd ? "and:" + std::to_string(j.at)
                                        : (j.divergence ? "div:" : "conv:") + std::to_string(j.at);
                for (int k = 0; k < (j.isAnd ? 2 : 1); ++k) {
                    Item it;
                    it.isBox = false;
                    it.a = { j.x0, j.y + 3.f * static_cast<float>(k) };
                    it.b = { j.x1, j.y + 3.f * static_cast<float>(k) };
                    it.transition = j.isAnd ? j.at : -1;
                    it.group = group;
                    it.blame = blameOf(it.transition, group);
                    it.what = std::string(j.isAnd ? (j.divergence ? "la divergence en ET" : "la convergence en ET")
                                                  : (j.divergence ? "la divergence en OU" : "la convergence en OU"))
                            + (j.isAnd ? " de T" : " de l'\xC3\xA9tape ") + std::to_string(j.at);
                    items.push_back(std::move(it));
                }
            }
            return items;
        }

        bool boxesOverlap(const gfx::Rect& a, const gfx::Rect& b) {
            constexpr float e = 0.5f;
            return a.x + e < b.right() && b.x + e < a.right() && a.y + e < b.bottom() && b.y + e < a.bottom();
        }

        bool segmentHitsBox(gfx::Point a, gfx::Point b, const gfx::Rect& r) {
            const gfx::Rect in = r.inset(1.f, 1.f);
            const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
            const float y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
            return x0 < in.right() && x1 > in.x && y0 < in.bottom() && y1 > in.y;
        }

        bool segmentsTouch(const Item& p, const Item& q) {
            constexpr float t = 1.f;
            const float px0 = std::min(p.a.x, p.b.x), px1 = std::max(p.a.x, p.b.x);
            const float py0 = std::min(p.a.y, p.b.y), py1 = std::max(p.a.y, p.b.y);
            const float qx0 = std::min(q.a.x, q.b.x), qx1 = std::max(q.a.x, q.b.x);
            const float qy0 = std::min(q.a.y, q.b.y), qy1 = std::max(q.a.y, q.b.y);
            return px0 <= qx1 + t && qx0 <= px1 + t && py0 <= qy1 + t && qy0 <= py1 + t;
        }

        // Deux elements qui ont le droit de se toucher : le meme proprietaire, le
        // meme trait partage, ou une etape et ses propres actions.
        bool allowed(const Item& p, const Item& q) {
            if (p.transition >= 0 && p.transition == q.transition) return true;
            if (!p.group.empty() && p.group == q.group) return true;
            if (p.step >= 0 && p.step == q.step && p.transition < 0 && q.transition < 0) return true;
            return false;
        }

        bool conflict(const Item& p, const Item& q) {
            if (allowed(p, q)) return false;
            if (p.isBox && q.isBox) return boxesOverlap(p.r, q.r);
            if (p.isBox) return segmentHitsBox(q.a, q.b, p.r);
            if (q.isBox) return segmentHitsBox(p.a, p.b, q.r);
            return segmentsTouch(p, q);
        }

        bool samePoint(gfx::Point p, gfx::Point q) {
            return std::abs(p.x - q.x) < 0.5f && std::abs(p.y - q.y) < 0.5f;
        }

        // Une liaison de p (cote etape) a q, en equerre : verticale, horizontale
        // a mi-hauteur, verticale. `intoStep` : on arrive sur l'etape (q -> p).
        void elbow(std::vector<DiagramSegment>& out, const DiagramSegment& model,
                   gfx::Point from, gfx::Point to) {
            auto add = [&](gfx::Point a, gfx::Point b) {
                DiagramSegment s = model;
                s.a = a;
                s.b = b;
                s.manual = true;
                s.arrowDown = false;
                s.arrowUp = false;
                out.push_back(std::move(s));
            };
            if (std::abs(from.x - to.x) < 0.5f) { add(from, to); }
            else {
                const float mid = (from.y + to.y) / 2.f;
                add(from, { from.x, mid });
                add({ from.x, mid }, { to.x, mid });
                add({ to.x, mid }, to);
            }
            // une liaison qui remonte porte sa fleche (IEC 60848)
            if (!out.empty() && to.y < from.y - 0.5f) out.back().arrowUp = true;
            if (!out.empty() && model.arrowDown) out.back().arrowDown = to.y > from.y;
        }

        // Les positions choisies a la main (GrafcetLayoutFile) : l'etape et ses
        // actions bougent, les arrivees de renvois avec elle ; ses liaisons suivent
        // en equerre et sont marquees `manual` (problems() ne les juge pas).
        void applyPlacement(Diagram& d, const project::ChartLayout& placement, const DiagramMetrics& m) {
            for (auto& s : d.steps) {
                const auto at = placement.steps.find(s.id);
                if (at == placement.steps.end()) continue;
                const float dx = at->second.x - s.box.x, dy = at->second.y - s.box.y;
                if (std::abs(dx) < 0.5f && std::abs(dy) < 0.5f) continue;
                const gfx::Point oldTop{ s.box.x + s.box.w / 2.f, s.box.y };
                const gfx::Point oldBottom{ oldTop.x, s.box.bottom() };
                s.box.x += dx;
                s.box.y += dy;
                s.moved = true;
                const gfx::Point newTop{ s.box.x + s.box.w / 2.f, s.box.y };
                const gfx::Point newBottom{ newTop.x, s.box.bottom() };
                for (auto& a : d.actions) {
                    if (a.step != s.id) continue;
                    a.box.x += dx; a.box.y += dy;
                    a.qualifierBox.x += dx; a.qualifierBox.y += dy;
                }
                for (auto& r : d.refs) {
                    if (r.outgoing || r.step != s.id) continue;
                    r.box.x += dx; r.box.y += dy;
                }
                const std::string in = "in:" + std::to_string(s.id);
                std::vector<DiagramSegment> next;
                next.reserve(d.segments.size() + 8);
                for (const auto& g : d.segments) {
                    if (g.step == s.id || g.group == in) {
                        DiagramSegment moved = g;
                        moved.a.x += dx; moved.a.y += dy;
                        moved.b.x += dx; moved.b.y += dy;
                        moved.manual = true;
                        next.push_back(std::move(moved));
                    }
                    else if (samePoint(g.a, oldBottom)) elbow(next, g, newBottom, g.b);
                    else if (samePoint(g.b, oldTop)) elbow(next, g, g.a, newTop);
                    else next.push_back(g);
                }
                d.segments = std::move(next);
            }
            for (const auto& s : d.steps) {
                d.size.w = std::max(d.size.w, s.box.right() + m.margin);
                d.size.h = std::max(d.size.h, s.box.bottom() + m.margin);
            }
            for (const auto& a : d.actions) {
                d.size.w = std::max(d.size.w, a.box.right() + m.margin);
                d.size.h = std::max(d.size.h, a.box.bottom() + m.margin);
            }
        }

        void shiftX(Diagram& d, float dx) {
            if (std::abs(dx) < 0.01f) return;
            for (auto& s : d.steps) s.box.x += dx;
            for (auto& a : d.actions) { a.box.x += dx; a.qualifierBox.x += dx; }
            for (auto& t : d.transitions) {
                t.at.x += dx; t.bar.x += dx; t.hit.x += dx; t.label.x += dx; t.idBox.x += dx;
            }
            for (auto& g : d.segments) { g.a.x += dx; g.b.x += dx; }
            for (auto& j : d.junctions) { j.x0 += dx; j.x1 += dx; }
            for (auto& r : d.refs) r.box.x += dx;
            d.size.w += dx;
        }

        // LES REMONTEES PAR LES COTES (comme la scene 11 de la maquette). Une
        // transition qui remonte est d'abord un renvoi ; ici, toutes celles qui
        // remontent vers une meme etape deviennent, ensemble, des liaisons :
        // sous le trait, vers un couloir a gauche (si elle part de la colonne de
        // l'etape ou d'avant) ou a droite, une fleche vers le haut, puis au-dessus
        // de l'etape. Si une seule croise quelque chose, toutes restent des
        // renvois (problems() refait le controle a chaque essai).
        void sideReturns(Diagram& d, const DiagramMetrics& m) {
            const float gap = 14.f;
            struct Up { int t{ -1 }; int dd{ -1 }; bool left{ true }; };
            std::map<int, std::vector<Up>> byStep;
            for (const auto& t : d.transitions) {
                if (!t.renvoi || t.destinations.size() != 1 || t.sources.empty()) continue;
                const auto* st = d.step(t.destinations.front());
                if (!st || st->box.y >= t.at.y) continue;     // seulement ce qui remonte
                byStep[st->id].push_back({ t.id, st->id, t.at.x <= st->box.x + st->box.w / 2.f + 0.5f });
            }
            if (byStep.empty()) return;
            // toutes les arrivees de renvoi d'une etape doivent remonter (sinon on garde le renvoi)
            for (auto it = byStep.begin(); it != byStep.end();) {
                bool all = true;
                for (const auto& r : d.refs)
                    if (!r.outgoing && r.step == it->first)
                        for (const int f : r.from) {
                            bool found = false;
                            for (const auto& u : it->second) if (u.t == f) found = true;
                            if (!found) all = false;
                        }
                if (all) ++it; else it = byStep.erase(it);
            }
            int nLeft = 0;
            for (const auto& [dd, ups] : byStep) for (const auto& u : ups) if (u.left) ++nLeft;
            shiftX(d, static_cast<float>(nLeft) * gap);
            float right = 0.f;
            for (const auto& s : d.steps) right = std::max(right, s.box.right());
            for (const auto& a : d.actions) right = std::max(right, a.box.right());
            for (const auto& t : d.transitions) right = std::max(right, std::max(t.label.right(), t.bar.right()));
            for (const auto& r : d.refs) right = std::max(right, r.box.right());
            for (const auto& g : d.segments) right = std::max(right, std::max(g.a.x, g.b.x));
            int laneLeft = 0, laneRight = 0;
            float usedRight = 0.f;
            for (const auto& [dd, ups] : byStep) {
                const Diagram before = d;
                const auto* st = d.step(dd);
                if (!st) continue;
                const float dx = st->box.x + st->box.w / 2.f, top = st->box.y;
                const DiagramRef* arrival = nullptr;
                for (const auto& r : d.refs) if (!r.outgoing && r.step == dd) arrival = &r;
                if (!arrival) continue;
                const float yHigh = arrival->box.y + arrival->box.h / 2.f;
                const std::string group = "ret:" + std::to_string(dd);
                const std::string in = "in:" + std::to_string(dd);
                bool hasEntry = false;
                for (auto& g : d.segments)
                    if (std::abs(g.b.x - dx) < 0.5f && std::abs(g.b.y - top) < 0.5f && g.group != in) {
                        hasEntry = true;
                        if (g.a.y <= yHigh) g.group = group;
                        else hasEntry = false;
                    }
                std::vector<DiagramSegment> keep;
                std::map<int, float> lowOf;
                for (const auto& g : d.segments) {
                    bool drop = g.group == in;
                    for (const auto& u : ups)
                        if (g.transition == u.t && g.arrowDown) { drop = true; lowOf[u.t] = g.b.y; }
                    if (!drop) keep.push_back(g);
                }
                d.segments = std::move(keep);
                std::vector<DiagramRef> refs;
                for (const auto& r : d.refs) {
                    bool drop = !r.outgoing && r.step == dd;
                    for (const auto& u : ups) if (r.outgoing && r.transition == u.t) drop = true;
                    if (!drop) refs.push_back(r);
                }
                d.refs = std::move(refs);
                int kl = laneLeft, kr = laneRight;
                for (const auto& u : ups) {
                    auto* t = const_cast<DiagramTransition*>(d.transition(u.t));
                    if (!t || !lowOf.count(u.t)) continue;
                    const float xb = t->at.x, yb = t->at.y, yLow = lowOf[u.t];
                    const float cx = u.left ? m.margin + static_cast<float>(nLeft - 1 - kl++) * gap + gap / 2.f
                                            : right + static_cast<float>(++kr) * gap;
                    if (!u.left) usedRight = std::max(usedRight, cx);
                    auto add = [&](gfx::Point a, gfx::Point b, bool up) {
                        DiagramSegment g = segment(a, b, u.t, group);
                        g.arrowUp = up;
                        d.segments.push_back(std::move(g));
                    };
                    add({ xb, yb + 2.f }, { xb, yLow }, false);
                    add({ xb, yLow }, { cx, yLow }, false);
                    add({ cx, yLow }, { cx, yHigh }, true);
                    add({ cx, yHigh }, { dx, yHigh }, false);
                    t->renvoi = false;
                }
                if (!hasEntry) d.segments.push_back(segment({ dx, yHigh }, { dx, top }, -1, group));
                if (!problems(d).empty()) { d = before; continue; }
                laneLeft = kl;
                laneRight = kr;
            }
            if (usedRight > 0.f) d.size.w = std::max(d.size.w, usedRight + m.margin);
            d.renvoiCount = 0;
            for (const auto& t : d.transitions) if (t.renvoi) ++d.renvoiCount;
        }

        // LES RANGEES AU PLUS LONG CHEMIN. computeLayout range une etape a la
        // distance la plus COURTE de l'etape initiale : X3 de DetoxalA (atteinte
        // par X0 -> X3 et par X1 -> X3) se retrouvait a cote de X1, et X1 -> X3
        // devenait un renvoi. Ici, les liaisons qui remontent (trouvees par un
        // parcours en profondeur depuis les etapes initiales) mises a part, une
        // etape va SOUS toutes celles qui y menent : le dessin descend, comme la
        // norme le veut, et les remontees restent les seules a monter.
        // Les colonnes sont refaites comme computeLayout les fait.
        Layout refineLevels(const Chart& chart, const Layout& grid) {
            std::map<int, std::vector<int>> next;
            std::set<int> known;
            for (const auto& s : chart.steps) known.insert(s.id);
            for (const auto& t : chart.transitions)
                for (const int a : t.sources)
                    for (const int b : t.destinations)
                        if (known.count(a) && known.count(b) && a != b) next[a].push_back(b);

            std::map<int, int> state;                 // 0 jamais vu, 1 sur la pile, 2 fini
            std::set<std::pair<int, int>> back;       // les liaisons qui remontent
            std::vector<int> post;                    // l'ordre de fin du parcours
            auto visit = [&](int root) {
                if (state[root]) return;
                std::vector<std::pair<int, std::size_t>> stack{ { root, 0 } };
                state[root] = 1;
                while (!stack.empty()) {
                    auto& [node, k] = stack.back();
                    const auto& succ = next[node];
                    if (k < succ.size()) {
                        const int v = succ[k++];
                        if (state[v] == 1) back.insert({ node, v });
                        else if (state[v] == 0) { state[v] = 1; stack.push_back({ v, 0 }); }
                        continue;
                    }
                    state[node] = 2;
                    post.push_back(node);
                    stack.pop_back();
                }
            };
            std::vector<int> roots;
            for (const auto& s : chart.steps) if (s.initial) roots.push_back(s.id);
            if (roots.empty() && !chart.steps.empty()) roots.push_back(chart.steps.front().id);
            for (const int r : roots) visit(r);

            std::map<int, int> level;
            for (const int r : roots) level[r] = 0;
            for (auto it = post.rbegin(); it != post.rend(); ++it) {
                const int u = *it;
                if (!level.count(u)) level[u] = 0;
                for (const int v : next[u])
                    if (!back.count({ u, v })) level[v] = std::max(level.count(v) ? level[v] : 0, level[u] + 1);
            }
            int maxLevel = 0;
            for (const auto& [id, lv] : level) maxLevel = std::max(maxLevel, lv);
            // ce que rien n'atteint : apres, dans l'ordre de computeLayout
            std::vector<std::pair<int, int>> rest;
            for (const auto& s : chart.steps)
                if (!level.count(s.id)) {
                    const auto* p = grid.step(s.id);
                    rest.push_back({ p ? p->level : 0, s.id });
                }
            std::stable_sort(rest.begin(), rest.end());
            for (const auto& [old, id] : rest) level[id] = ++maxLevel;

            std::map<int, int> column;
            std::map<int, std::vector<int>> byLevel;
            for (const auto& s : chart.steps) byLevel[level[s.id]].push_back(s.id);
            auto predecessorColumn = [&](int stepId) {
                int best = -1;
                for (const auto& t : chart.transitions) {
                    if (std::find(t.destinations.begin(), t.destinations.end(), stepId) == t.destinations.end()) continue;
                    for (const int src : t.sources) {
                        const auto at = column.find(src);
                        if (at == column.end() || level[src] >= level[stepId]) continue;
                        if (best < 0 || at->second < best) best = at->second;
                    }
                }
                return best;
            };
            for (auto& [lv, ids] : byLevel) {
                std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
                    const int ca = predecessorColumn(a), cb = predecessorColumn(b);
                    if (ca != cb) return ca < cb;
                    return a < b;
                });
                std::set<int> taken;
                for (const int id : ids) {
                    int want = std::max(0, predecessorColumn(id));
                    while (taken.count(want)) ++want;
                    taken.insert(want);
                    column[id] = want;
                }
                (void)lv;
            }

            Layout out = grid;
            for (auto& p : out.steps) {
                if (!level.count(p.id)) continue;
                p.level = level[p.id];
                p.column = column[p.id];
            }
            out.levelCount = 0;
            out.columnCount = 0;
            for (const auto& p : out.steps) {
                out.levelCount = std::max(out.levelCount, p.level + 1);
                out.columnCount = std::max(out.columnCount, p.column + 1);
            }
            return out;
        }

    } // namespace

    Diagram buildDiagram(const Chart& chart, const project::ChartLayout* placement,
        const MeasureText& measure, const DiagramMetrics& metrics) {
        const Layout grid = refineLevels(chart, computeLayout(chart));
        std::set<int> renvoi;
        Placed placed;
        for (int pass = 1; pass <= static_cast<int>(chart.transitions.size()) + 1; ++pass) {
            placed = place(chart, grid, measure, metrics, renvoi);
            placed.diagram.passes = pass;
            const auto items = itemsOf(placed.diagram, &placed.groupBlame);
            int culprit = -1;
            for (std::size_t i = 0; i < items.size() && culprit < 0; ++i)
                for (std::size_t j = i + 1; j < items.size(); ++j) {
                    if (!conflict(items[i], items[j])) continue;
                    const int a = items[i].blame, b = items[j].blame;
                    if (a < 0 && b < 0) continue;   // rien a rendre en renvoi : problems() le dira
                    if (a < 0) culprit = b;
                    else if (b < 0) culprit = a;
                    else {
                        const float sa = placed.span[a], sb = placed.span[b];
                        culprit = sa > sb ? a : sb > sa ? b : std::max(a, b);
                    }
                    break;
                }
            if (culprit < 0) break;
            renvoi.insert(culprit);
        }
        sideReturns(placed.diagram, metrics);
        if (placement && !placement->steps.empty()) applyPlacement(placed.diagram, *placement, metrics);
        return std::move(placed.diagram);
    }

    std::vector<std::string> problems(const Diagram& d) {
        std::vector<std::string> out;
        const auto items = itemsOf(d, nullptr);
        for (std::size_t i = 0; i < items.size(); ++i)
            for (std::size_t j = i + 1; j < items.size(); ++j)
                if (conflict(items[i], items[j]))
                    out.push_back(items[i].what + " touche " + items[j].what);
        return out;
    }

} // namespace grafcet
