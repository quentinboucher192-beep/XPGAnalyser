#include "SfcEdit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sfc {

    namespace {

        std::string upper(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        std::string trimmed(std::string_view s) {
            const auto a = s.find_first_not_of(" \t\r\n");
            if (a == std::string_view::npos) return {};
            const auto b = s.find_last_not_of(" \t\r\n");
            return std::string(s.substr(a, b - a + 1));
        }

        bool validIndex(const Chart& c, int i) { return i >= 0 && static_cast<std::size_t>(i) < c.items.size(); }

        EditResult fail(std::string why) {
            EditResult r;
            r.message = std::move(why);
            return r;
        }

        EditResult done(std::string what, int select) {
            EditResult r;
            r.ok = true;
            r.message = std::move(what);
            r.select = select;
            return r;
        }

        Item makeStep(const std::string& name, int x, int y) {
            Item it;
            it.kind = ItemKind::Step;
            it.tag = "step";
            it.name = name;
            it.x = x;
            it.y = y;
            it.hasLiterals = true;
            it.dirty = true;
            return it;
        }

        Item makeTransition(int x, int y) {
            Item it;
            it.kind = ItemKind::Transition;
            it.tag = "transition";
            it.x = x;
            it.y = y;
            it.dirty = true;
            return it;
        }

        Item makeBar(ItemKind k, int x, int y, int width) {
            Item it;
            it.kind = k;
            it.tag = k == ItemKind::AltBranch ? "altBranch" : k == ItemKind::AltJoint ? "altJoint"
                   : k == ItemKind::ParBranch ? "parBranch" : "parJoint";
            it.x = x;
            it.y = y;
            it.width = width;
            it.relativePos = 0;
            it.dirty = true;
            return it;
        }

        Item makeJump(const std::string& target, int x, int y) {
            Item it;
            it.kind = ItemKind::Jump;
            it.tag = "jumpSFC";
            it.name = target;
            it.x = x;
            it.y = y;
            it.dirty = true;
            return it;
        }

        std::string linkType(const Item& it) {
            switch (it.kind) {
            case ItemKind::Step:       return "step";
            case ItemKind::Transition: return "transition";
            case ItemKind::Jump:       return "jumpSFC";
            default:                   return it.tag;
            }
        }

        Item makeLink(const Item& from, int sx, int sy, const Item& to, int dx, int dy) {
            Item it;
            it.kind = ItemKind::Link;
            it.tag = "linkSFC";
            it.srcType = linkType(from);
            it.dstType = linkType(to);
            it.sx = sx; it.sy = sy;
            it.dx = dx; it.dy = dy;
            it.x = sx; it.y = sy;
            it.dirty = true;
            return it;
        }

        std::string numberText(double v) {
            if (std::fabs(v - std::round(v)) < 1e-9) return std::to_string(static_cast<long long>(std::llround(v)));
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", v);
            return buf;
        }

        // Tout ce qui est sous la rangee `after` descend de `by` rangees (by < 0 : monte).
        void shiftRows(Chart& c, int after, int by) {
            for (auto& it : c.items) {
                if (it.kind == ItemKind::Link) {
                    bool changed = false;
                    if (it.sy > after) { it.sy += by; changed = true; }
                    if (it.dy > after) { it.dy += by; changed = true; }
                    for (auto& [bx, byText] : it.bends) {
                        const double gy = std::atof(byText.c_str());
                        if (gy >= after + 1) { byText = numberText(gy + by); changed = true; }
                    }
                    it.x = it.sx;
                    it.y = it.sy;
                    if (changed) it.dirty = true;
                    continue;
                }
                if (it.kind == ItemKind::Other) continue;
                if (it.y > after) { it.y += by; it.dirty = true; }
            }
            c.structureChanged = true;
        }

        bool cellFree(const Chart& c, int x, int y) {
            for (const auto& it : c.items) {
                if (it.isCellObject() && it.x == x && it.y == y) return false;
                if (it.kind == ItemKind::TextBox && x >= it.x && x < it.x + it.boxW && y >= it.y && y < it.y + it.boxH)
                    return false;
            }
            return true;
        }

        bool barCovers(const Chart& c, int x, int y, int except = -1) {
            for (std::size_t i = 0; i < c.items.size(); ++i) {
                const auto& it = c.items[i];
                if (static_cast<int>(i) == except || !it.isBar() || it.y != y) continue;
                if (x >= it.x && x < it.x + it.width) return true;
            }
            return false;
        }

        int convergenceAt(const Chart& c, int x, int y) {
            for (std::size_t i = 0; i < c.items.size(); ++i) {
                const auto& it = c.items[i];
                if (it.isConvergence() && it.y == y && x >= it.x && x < it.x + it.width) return static_cast<int>(i);
            }
            return -1;
        }

        int divergenceAt(const Chart& c, int x, int y, ItemKind kind) {
            for (std::size_t i = 0; i < c.items.size(); ++i) {
                const auto& it = c.items[i];
                if (it.kind == kind && it.y == y && x >= it.x && x < it.x + it.width && it.x + it.relativePos == x)
                    return static_cast<int>(i);
            }
            return -1;
        }

        int add(Chart& c, Item it) {
            c.items.push_back(std::move(it));
            c.structureChanged = true;
            return static_cast<int>(c.items.size()) - 1;
        }

        // Retire des elements (indices), les liens qui y menaient compris.
        void eraseItems(Chart& c, std::vector<int> which) {
            std::sort(which.begin(), which.end());
            which.erase(std::unique(which.begin(), which.end()), which.end());
            for (auto it = which.rbegin(); it != which.rend(); ++it)
                if (validIndex(c, *it)) c.items.erase(c.items.begin() + *it);
            c.structureChanged = true;
        }

        bool sourceUsed(const Chart& c, const std::string& tag, const std::string& name) {
            const auto key = upper(name);
            for (const auto& it : c.items) {
                if (tag == "transitionSource" && it.kind == ItemKind::Transition && it.cond == CondKind::Section
                    && upper(it.condText) == key)
                    return true;
                if (tag == "actionSource" && it.kind == ItemKind::Step)
                    for (const auto& a : it.actions)
                        if (a.isSection && upper(a.name) == key) return true;
            }
            return false;
        }

        void dropUnusedSource(Chart& c, const std::string& tag, const std::string& name) {
            if (name.empty() || sourceUsed(c, tag, name)) return;
            const auto key = upper(name);
            for (auto it = c.sources.begin(); it != c.sources.end(); ++it)
                if (it->tag == tag && upper(it->name) == key) {
                    c.sources.erase(it);
                    c.structureChanged = true;
                    return;
                }
        }

        // `Ancien.x` -> `Nouveau.x` dans un texte ST (les mots entiers suivis d'un point).
        std::string renameStepRefs(const std::string& text, const std::string& from, const std::string& to) {
            if (from.empty()) return text;
            const auto key = upper(from);
            std::string out;
            std::size_t i = 0;
            bool inString = false, inComment = false;
            auto identChar = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; };
            while (i < text.size()) {
                const char ch = text[i];
                if (inComment) {
                    out.push_back(ch);
                    if (ch == '*' && i + 1 < text.size() && text[i + 1] == ')') { out.push_back(')'); i += 2; inComment = false; continue; }
                    ++i;
                    continue;
                }
                if (inString) { out.push_back(ch); if (ch == '\'') inString = false; ++i; continue; }
                if (ch == '\'') { inString = true; out.push_back(ch); ++i; continue; }
                if (ch == '(' && i + 1 < text.size() && text[i + 1] == '*') { inComment = true; out += "(*"; i += 2; continue; }
                if (identChar(ch) && (i == 0 || (!identChar(text[i - 1]) && text[i - 1] != '.'))) {
                    std::size_t j = i;
                    while (j < text.size() && identChar(text[j])) ++j;
                    const std::string word = text.substr(i, j - i);
                    if (upper(word) == key && j < text.size() && text[j] == '.') out += to;
                    else out += word;
                    i = j;
                    continue;
                }
                out.push_back(ch);
                ++i;
            }
            return out;
        }

        std::string defaultQualifier(const Action& a) {
            return a.qualifier.empty() ? std::string("N") : a.qualifier;
        }

    } // namespace

    std::set<std::string> stepNamesInProject(const domain::Project& p, domain::Index except) {
        std::set<std::string> out;
        for (std::size_t i = 0; i < p.sections.size(); ++i) {
            const auto& s = p.sections[i];
            if (static_cast<domain::Index>(i) == except || s.language != domain::PouLanguage::SFC || !domain::keepsSourceXml(s))
                continue;
            const auto c = parse(s.body);
            for (const auto& it : c.items)
                if (it.kind == ItemKind::Step) out.insert(upper(it.name));
        }
        return out;
    }

    bool validName(std::string_view name) {
        if (name.empty()) return false;
        if (!std::isalpha(static_cast<unsigned char>(name.front())) && name.front() != '_') return false;
        for (const char ch : name)
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') return false;
        return true;
    }

    bool plainVariable(std::string_view text) {
        const auto t = trimmed(text);
        if (t.empty()) return false;
        if (!std::isalpha(static_cast<unsigned char>(t.front())) && t.front() != '_' && t.front() != '%') return false;
        for (const char ch : t)
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '.' && ch != '%' && ch != '['
                && ch != ']')
                return false;
        const auto u = upper(t);
        return u != "NOT" && u != "AND" && u != "OR" && u != "XOR";
    }

    std::string freeStepName(const Chart& c, const std::set<std::string>& taken, const std::string& prefix) {
        for (int n = 1; n < 100000; ++n) {
            const std::string name = prefix + std::to_string(n);
            if (c.findStep(name) < 0 && !taken.count(upper(name))) return name;
        }
        return prefix + "X";
    }

    std::string freeSourceName(const Chart& c, const std::string& base, const std::set<std::string>& taken) {
        for (int n = 1; n < 100000; ++n) {
            const std::string name = base + std::to_string(n);
            if (!c.source("transitionSource", name) && !c.source("actionSource", name) && !taken.count(upper(name))
                && c.findStep(name) < 0)
                return name;
        }
        return base + "X";
    }

    // ------------------------------------------------------------------ la receptivite
    namespace {
        void applyCondition(Chart& c, Item& t, const std::string& rawText, const std::string& sectionName) {
            std::string text = trimmed(rawText);
            bool invert = false;
            {
                const auto u = upper(text);
                if (u.rfind("NOT ", 0) == 0 && plainVariable(text.substr(4))) {
                    invert = true;
                    text = trimmed(text.substr(4));
                }
            }
            if (text.empty()) {
                t.cond = CondKind::None;
                t.condText.clear();
                t.invert = false;
            }
            else if (plainVariable(text)) {
                const std::string old = t.cond == CondKind::Section ? t.condText : std::string{};
                t.cond = CondKind::Variable;
                t.condText = text;
                t.invert = invert;
                t.dirty = true;
                if (!old.empty()) dropUnusedSource(c, "transitionSource", old);
                return;
            }
            else {
                // une expression : la section de transition ST de cette transition, ou une neuve
                Source* s = t.cond == CondKind::Section ? c.source("transitionSource", t.condText) : nullptr;
                if (s && s->language != domain::PouLanguage::ST) s = nullptr;
                if (!s) {
                    Source fresh;
                    fresh.tag = "transitionSource";
                    fresh.name = sectionName;
                    fresh.language = domain::PouLanguage::ST;
                    fresh.innerTag = "STSource";
                    fresh.dirty = true;
                    c.sources.push_back(std::move(fresh));
                    c.structureChanged = true;
                    s = &c.sources.back();
                }
                s->body = text;
                s->dirty = true;
                t.cond = CondKind::Section;
                t.condText = s->name;
                t.invert = false;
            }
            t.dirty = true;
        }
    } // namespace

    EditResult setCondition(Chart& c, int ti, const std::string& text, const std::string& sectionName) {
        if (!validIndex(c, ti) || c.items[static_cast<std::size_t>(ti)].kind != ItemKind::Transition)
            return fail("choisis une transition");
        auto& t = c.items[static_cast<std::size_t>(ti)];
        if (!plainVariable(text) && !trimmed(text).empty() && upper(trimmed(text)).rfind("NOT ", 0) != 0
            && sectionName.empty() && !(t.cond == CondKind::Section && c.source("transitionSource", t.condText)))
            return fail("une expression demande une section de transition");
        applyCondition(c, t, text, sectionName);
        if (t.cond == CondKind::Section)
            return done("r\xC3\xA9" "ceptivit\xC3\xA9 \xC3\xA9" "crite dans la section de transition " + t.condText, ti);
        return done(t.cond == CondKind::None ? std::string("r\xC3\xA9" "ceptivit\xC3\xA9 effac\xC3\xA9" "e")
                                             : "r\xC3\xA9" "ceptivit\xC3\xA9 : " + std::string(t.invert ? "NOT " : "") + t.condText,
                    ti);
    }

    EditResult useTransitionSection(Chart& c, int ti, const std::string& sectionName) {
        if (!validIndex(c, ti) || c.items[static_cast<std::size_t>(ti)].kind != ItemKind::Transition)
            return fail("choisis une transition");
        if (!c.source("transitionSource", sectionName)) return fail("pas de section de transition " + sectionName);
        auto& t = c.items[static_cast<std::size_t>(ti)];
        const std::string old = t.cond == CondKind::Section ? t.condText : std::string{};
        t.cond = CondKind::Section;
        t.condText = sectionName;
        t.invert = false;
        t.dirty = true;
        if (!old.empty() && upper(old) != upper(sectionName)) dropUnusedSource(c, "transitionSource", old);
        return done("r\xC3\xA9" "ceptivit\xC3\xA9 : la section " + sectionName, ti);
    }

    // ------------------------------------------------------------------ ajouter
    EditResult addStepBelow(Chart& c, int item, const std::string& stepName, const std::string& condition,
                            const std::string& sectionForExpression) {
        if (!c.editable) return fail("ce GRAFCET ne se modifie pas ici (son XML n'a pas de r\xC3\xA9seau)");
        if (!validIndex(c, item)) return fail("choisis une \xC3\xA9tape ou une transition");
        const auto& sel = c.items[static_cast<std::size_t>(item)];
        if (sel.kind != ItemKind::Step && sel.kind != ItemKind::Transition)
            return fail("choisis une \xC3\xA9tape ou une transition");
        if (!validName(stepName)) return fail("nom d'\xC3\xA9tape invalide : " + stepName);
        if (c.findStep(stepName) >= 0) return fail("une \xC3\xA9tape s'appelle d\xC3\xA9j\xC3\xA0 " + stepName);
        const int x = sel.x, y0 = sel.y;
        const bool fromStep = sel.kind == ItemKind::Step;
        const int joint = convergenceAt(c, x, y0);

        shiftRows(c, y0, 2);
        if (joint >= 0) {
            // la convergence descend avec cette entree ; ses autres entrees l'y rejoignent par un lien
            auto& j = c.items[static_cast<std::size_t>(joint)];
            const Item jCopy = j;
            j.y = y0 + 2;
            j.dirty = true;
            std::vector<Item> links;
            for (int col = jCopy.x; col < jCopy.x + jCopy.width; ++col) {
                if (col == x) continue;
                const int o = c.itemAt(col, y0);
                if (o >= 0) links.push_back(makeLink(c.items[static_cast<std::size_t>(o)], col, y0, jCopy, col, y0 + 2));
            }
            for (auto& it : c.items)
                if (it.kind == ItemKind::Link && it.dstType == jCopy.tag && it.dy == y0 && it.dx >= jCopy.x
                    && it.dx < jCopy.x + jCopy.width) {
                    it.dy = y0 + 2;
                    it.dirty = true;
                }
            for (auto& l : links) add(c, std::move(l));
        }
        int stepIndex = -1, transIndex = -1;
        if (fromStep) {
            transIndex = add(c, makeTransition(x, y0 + 1));
            stepIndex = add(c, makeStep(stepName, x, y0 + 2));
        }
        else {
            stepIndex = add(c, makeStep(stepName, x, y0 + 1));
            transIndex = add(c, makeTransition(x, y0 + 2));
        }
        if (!trimmed(condition).empty())
            applyCondition(c, c.items[static_cast<std::size_t>(transIndex)], condition, sectionForExpression);
        return done("\xC3\xA9tape " + stepName + " et sa transition ajout\xC3\xA9" "es", fromStep ? transIndex : stepIndex);
    }

    EditResult addAlternative(Chart& c, int si, const std::string& stepName) {
        if (!c.editable) return fail("ce GRAFCET ne se modifie pas ici");
        if (!validIndex(c, si) || c.items[static_cast<std::size_t>(si)].kind != ItemKind::Step)
            return fail("choisis l'\xC3\xA9tape d'o\xC3\xB9 part la divergence en OU");
        if (!validName(stepName) || c.findStep(stepName) >= 0) return fail("nom d'\xC3\xA9tape invalide ou pris : " + stepName);
        const Item s = c.items[static_cast<std::size_t>(si)];
        const int x = s.x, y = s.y;
        int branch = divergenceAt(c, x, y + 1, ItemKind::AltBranch);
        const int below = c.itemAt(x, y + 1);
        if (branch < 0 && (below < 0 || c.items[static_cast<std::size_t>(below)].kind != ItemKind::Transition))
            return fail("l'\xC3\xA9tape " + s.name + " n'a pas de transition dessous : ajoute d'abord \xC3\xA9tape + transition");
        if (convergenceAt(c, x, y) >= 0) return fail("cette \xC3\xA9tape entre dans une convergence");
        // l'etape ou mene la branche principale : la cible du saut
        std::string target = s.name;
        {
            const auto topo = resolve(c);
            const int first = branch >= 0 ? -1 : below;
            if (first >= 0)
                if (const auto* tl = topo.of(first); tl && !tl->targets.empty())
                    target = c.items[static_cast<std::size_t>(tl->targets.front())].name;
        }
        int col = branch >= 0 ? c.items[static_cast<std::size_t>(branch)].x + c.items[static_cast<std::size_t>(branch)].width
                              : x + 1;
        for (;; ++col) {
            bool ok = true;
            for (int r = y + 1; r <= y + 4 && ok; ++r)
                ok = cellFree(c, col, r) && !barCovers(c, col, r, branch);
            if (ok && branch < 0)
                for (int k = x + 1; k < col && ok; ++k) ok = cellFree(c, k, y + 1) && !barCovers(c, k, y + 1);
            if (ok) break;
            if (col > x + 200) return fail("pas de colonne libre \xC3\xA0 droite");
        }
        if (branch >= 0) {
            auto& b = c.items[static_cast<std::size_t>(branch)];
            b.width = col - b.x + 1;
            b.dirty = true;
        }
        else {
            add(c, makeBar(ItemKind::AltBranch, x, y + 1, col - x + 1));
        }
        const int t2 = add(c, makeTransition(col, y + 1));
        add(c, makeStep(stepName, col, y + 2));
        add(c, makeTransition(col, y + 3));
        add(c, makeJump(target, col, y + 4));
        return done("divergence en OU : une branche de plus (" + stepName + "), elle revient \xC3\xA0 " + target, t2);
    }

    EditResult addSimultaneous(Chart& c, int ti, const std::string& stepName) {
        if (!c.editable) return fail("ce GRAFCET ne se modifie pas ici");
        if (!validIndex(c, ti) || c.items[static_cast<std::size_t>(ti)].kind != ItemKind::Transition)
            return fail("choisis la transition d'o\xC3\xB9 part la divergence en ET");
        if (!validName(stepName) || c.findStep(stepName) >= 0) return fail("nom d'\xC3\xA9tape invalide ou pris : " + stepName);
        const Item t = c.items[static_cast<std::size_t>(ti)];
        const int x = t.x, y = t.y;
        int branch = divergenceAt(c, x, y + 1, ItemKind::ParBranch);
        int joint = -1;
        if (branch >= 0) {
            const auto& b = c.items[static_cast<std::size_t>(branch)];
            for (std::size_t i = 0; i < c.items.size(); ++i) {
                const auto& it = c.items[i];
                if (it.kind != ItemKind::ParJoint || it.y < b.y || it.x > x || x >= it.x + it.width) continue;
                if (joint < 0 || it.y < c.items[static_cast<std::size_t>(joint)].y) joint = static_cast<int>(i);
            }
            if (joint < 0) return fail("la divergence en ET n'a pas de convergence : ajoute-la dans Control Expert");
        }
        else {
            const int n = c.itemAt(x, y + 1);
            if (n < 0 || c.items[static_cast<std::size_t>(n)].kind != ItemKind::Step)
                return fail("la transition n'est pas suivie d'une \xC3\xA9tape");
            const int after = c.itemAt(x, y + 2);
            if (after < 0 || c.items[static_cast<std::size_t>(after)].kind != ItemKind::Transition
                || convergenceAt(c, x, y + 1) >= 0)
                return fail("l'\xC3\xA9tape qui suit doit avoir sa transition juste dessous");
        }
        const int top = branch >= 0 ? c.items[static_cast<std::size_t>(branch)].y : y + 1;
        const int bottom = joint >= 0 ? c.items[static_cast<std::size_t>(joint)].y : y + 1;
        int col = branch >= 0 ? c.items[static_cast<std::size_t>(branch)].x + c.items[static_cast<std::size_t>(branch)].width
                              : x + 1;
        for (;; ++col) {
            bool ok = true;
            for (int r = top; r <= bottom && ok; ++r)
                ok = cellFree(c, col, r) && !barCovers(c, col, r, branch) && (joint < 0 || !barCovers(c, col, r, joint) || r != bottom);
            if (ok && branch < 0)
                for (int k = x + 1; k < col && ok; ++k) ok = cellFree(c, k, y + 1) && !barCovers(c, k, y + 1);
            if (ok) break;
            if (col > x + 200) return fail("pas de colonne libre \xC3\xA0 droite");
        }
        int step = -1;
        if (branch >= 0) {
            auto& b = c.items[static_cast<std::size_t>(branch)];
            b.width = col - b.x + 1;
            b.dirty = true;
            auto& j = c.items[static_cast<std::size_t>(joint)];
            j.width = std::max(j.width, col - j.x + 1);
            j.dirty = true;
            const Item jCopy = j;
            step = add(c, makeStep(stepName, col, top));
            if (bottom != top)
                add(c, makeLink(c.items[static_cast<std::size_t>(step)], col, top, jCopy, col, bottom));
        }
        else {
            add(c, makeBar(ItemKind::ParBranch, x, y + 1, col - x + 1));
            add(c, makeBar(ItemKind::ParJoint, x, y + 1, col - x + 1));
            step = add(c, makeStep(stepName, col, y + 1));
        }
        return done("divergence en ET : l'\xC3\xA9tape " + stepName + " s'active en m\xC3\xAAme temps", step);
    }

    EditResult addJump(Chart& c, int item, const std::string& target) {
        if (!c.editable) return fail("ce GRAFCET ne se modifie pas ici");
        if (!validIndex(c, item)) return fail("choisis une transition (ou une \xC3\xA9tape)");
        if (c.findStep(target) < 0) return fail("pas d'\xC3\xA9tape " + target);
        const Item sel = c.items[static_cast<std::size_t>(item)];
        if (sel.kind == ItemKind::Transition) {
            if (!cellFree(c, sel.x, sel.y + 1) || convergenceAt(c, sel.x, sel.y) >= 0
                || barCovers(c, sel.x, sel.y + 1))
                return fail("la transition m\xC3\xA8ne d\xC3\xA9j\xC3\xA0 quelque part : la case dessous est prise");
            const int j = add(c, makeJump(target, sel.x, sel.y + 1));
            return done("saut vers " + target, j);
        }
        if (sel.kind == ItemKind::Step) {
            if (!cellFree(c, sel.x, sel.y + 1) || !cellFree(c, sel.x, sel.y + 2) || convergenceAt(c, sel.x, sel.y) >= 0
                || barCovers(c, sel.x, sel.y + 1) || barCovers(c, sel.x, sel.y + 2))
                return fail("l'\xC3\xA9tape a d\xC3\xA9j\xC3\xA0 une suite : choisis sa transition");
            const int t = add(c, makeTransition(sel.x, sel.y + 1));
            add(c, makeJump(target, sel.x, sel.y + 2));
            return done("transition et saut vers " + target, t);
        }
        return fail("choisis une transition (ou une \xC3\xA9tape)");
    }

    // ------------------------------------------------------------------ l'etape
    EditResult setActions(Chart& c, int si, std::vector<Action> actions) {
        if (!validIndex(c, si) || c.items[static_cast<std::size_t>(si)].kind != ItemKind::Step)
            return fail("choisis une \xC3\xA9tape");
        auto& s = c.items[static_cast<std::size_t>(si)];
        std::vector<std::string> oldSections;
        for (const auto& a : s.actions) if (a.isSection) oldSections.push_back(a.name);
        for (auto& a : actions) {
            a.qualifier = defaultQualifier(a);
            a.name = trimmed(a.name);
            a.time = trimmed(a.time);
            if (a.name.empty()) return fail("une action sans variable ni section");
            if (qualifierTimed(a.qualifier) && a.time.empty())
                return fail("le qualificatif " + normalQualifier(a.qualifier) + " demande une dur\xC3\xA9" "e (t#2s)");
            if (!qualifierTimed(a.qualifier)) a.time.clear();
            a.timeIsVariable = !a.time.empty() && upper(a.time).rfind("T#", 0) != 0 && upper(a.time).rfind("TIME#", 0) != 0;
            if (a.isSection && !c.source("actionSource", a.name)) {
                Source fresh;
                fresh.tag = "actionSource";
                fresh.name = a.name;
                fresh.language = domain::PouLanguage::ST;
                fresh.innerTag = "STSource";
                fresh.body = "(* action de l'\xC3\xA9tape " + s.name + " *)\n";
                fresh.dirty = true;
                c.sources.push_back(std::move(fresh));
                c.structureChanged = true;
            }
        }
        s.actions = std::move(actions);
        s.dirty = true;
        for (const auto& name : oldSections) dropUnusedSource(c, "actionSource", name);
        return done(std::to_string(s.actions.size()) + " action(s) sur " + s.name, si);
    }

    EditResult renameStep(Chart& c, int si, const std::string& rawName) {
        if (!validIndex(c, si) || c.items[static_cast<std::size_t>(si)].kind != ItemKind::Step)
            return fail("choisis une \xC3\xA9tape");
        const std::string name = trimmed(rawName);
        if (!validName(name)) return fail("nom invalide : " + name);
        const std::string old = c.items[static_cast<std::size_t>(si)].name;
        if (upper(name) == upper(old)) {
            if (name == old) return fail("c'est d\xC3\xA9j\xC3\xA0 son nom");
        }
        else if (c.findStep(name) >= 0) return fail("une \xC3\xA9tape s'appelle d\xC3\xA9j\xC3\xA0 " + name);
        c.items[static_cast<std::size_t>(si)].name = name;
        c.items[static_cast<std::size_t>(si)].dirty = true;
        int refs = 0;
        for (auto& it : c.items) {
            if (it.kind == ItemKind::Jump && upper(it.name) == upper(old)) { it.name = name; it.dirty = true; ++refs; }
            if (it.kind == ItemKind::Transition && it.cond == CondKind::Variable) {
                const auto next = renameStepRefs(it.condText, old, name);
                if (next != it.condText) { it.condText = next; it.dirty = true; ++refs; }
            }
        }
        for (auto& s : c.sources) {
            if (s.language != domain::PouLanguage::ST && s.language != domain::PouLanguage::IL) continue;
            const auto next = renameStepRefs(s.body, old, name);
            if (next != s.body) { s.body = next; s.dirty = true; ++refs; }
        }
        return done("\xC3\xA9tape " + old + " renomm\xC3\xA9" "e " + name
                        + (refs ? " (" + std::to_string(refs) + " r\xC3\xA9" "f\xC3\xA9rence(s) suivie(s))" : std::string{}),
                    si);
    }

    EditResult setStepProperties(Chart& c, int si, bool initial, const std::string& tmin, const std::string& tmax,
                                 const std::string& delay, const std::string& comment) {
        if (!validIndex(c, si) || c.items[static_cast<std::size_t>(si)].kind != ItemKind::Step)
            return fail("choisis une \xC3\xA9tape");
        auto& s = c.items[static_cast<std::size_t>(si)];
        if (s.stepKind == StepKind::Initial || s.stepKind == StepKind::Normal)
            s.stepKind = initial ? StepKind::Initial : StepKind::Normal;
        s.tmin = trimmed(tmin);
        s.tmax = trimmed(tmax);
        s.delay = trimmed(delay);
        s.hasLiterals = true;
        s.hasComment = !comment.empty() || s.hasComment;
        s.comment = comment;
        s.dirty = true;
        return done("propri\xC3\xA9t\xC3\xA9s de " + s.name, si);
    }

    // ------------------------------------------------------------------ retirer, deplacer
    EditResult removeItem(Chart& c, int item) {
        if (!c.editable) return fail("ce GRAFCET ne se modifie pas ici");
        if (!validIndex(c, item)) return fail("rien de choisi");
        const Item sel = c.items[static_cast<std::size_t>(item)];
        const auto topo = resolve(c);
        std::vector<int> gone{ item };
        std::string what;
        // la paire : une etape et la transition qui la suit (dont elle est la seule source),
        // une transition et l'etape (ou le saut) qui la suit (dont elle est la seule entree)
        int partner = -1;
        if (sel.kind == ItemKind::Step) {
            const int t = c.itemAt(sel.x, sel.y + 1);
            if (t >= 0 && c.items[static_cast<std::size_t>(t)].kind == ItemKind::Transition)
                if (const auto* tl = topo.of(t); tl && tl->sources.size() == 1 && tl->sources.front() == item
                                                && convergenceAt(c, sel.x, sel.y) < 0)
                    partner = t;
            what = "\xC3\xA9tape " + sel.name;
        }
        else if (sel.kind == ItemKind::Transition) {
            const int n = c.itemAt(sel.x, sel.y + 1);
            if (n >= 0 && convergenceAt(c, sel.x, sel.y) < 0) {
                const auto& o = c.items[static_cast<std::size_t>(n)];
                if (o.kind == ItemKind::Jump) partner = n;
                else if (o.kind == ItemKind::Step && !o.initial() && topo.into(n).size() == 1 && topo.into(n).front() == item
                         && !barCovers(c, sel.x, sel.y + 1))
                    partner = n;
            }
            what = "transition";
        }
        else if (sel.kind == ItemKind::Jump) what = "saut vers " + sel.name;
        else if (sel.isBar()) what = sel.isDivergence() ? "divergence" : "convergence";
        else if (sel.kind == ItemKind::Link) what = "lien";
        else what = "\xC3\xA9l\xC3\xA9ment";
        if (partner >= 0) gone.push_back(partner);

        // les liens qui touchaient ce qui part
        std::vector<std::pair<int, int>> spots;
        for (const int g : gone) {
            const auto& it = c.items[static_cast<std::size_t>(g)];
            if (it.isCellObject()) spots.emplace_back(it.x, it.y);
        }
        for (std::size_t i = 0; i < c.items.size(); ++i) {
            const auto& it = c.items[i];
            if (it.kind != ItemKind::Link) continue;
            for (const auto& [px, py] : spots)
                if ((it.sx == px && it.sy == py && it.srcType != "altBranch" && it.srcType != "parBranch")
                    || (it.dx == px && it.dy == py && it.dstType != "altJoint" && it.dstType != "parJoint"))
                    gone.push_back(static_cast<int>(i));
        }
        std::vector<std::pair<std::string, std::string>> sources;
        for (const int g : gone) {
            const auto& it = c.items[static_cast<std::size_t>(g)];
            if (it.kind == ItemKind::Transition && it.cond == CondKind::Section) sources.emplace_back("transitionSource", it.condText);
            if (it.kind == ItemKind::Step)
                for (const auto& a : it.actions) if (a.isSection) sources.emplace_back("actionSource", a.name);
        }
        int rowA = -1;
        if (partner >= 0) rowA = std::min(sel.y, c.items[static_cast<std::size_t>(partner)].y);
        eraseItems(c, gone);
        for (const auto& [tag, name] : sources) dropUnusedSource(c, tag, name);
        // deux rangees devenues vides : ce qui est dessous remonte
        if (rowA >= 0) {
            bool empty = true;
            for (const auto& it : c.items) {
                if (it.kind == ItemKind::Link) {
                    if (it.sy == rowA || it.sy == rowA + 1 || it.dy == rowA || it.dy == rowA + 1) empty = false;
                    continue;
                }
                const int h = it.kind == ItemKind::TextBox ? it.boxH : 1;
                if (it.y <= rowA + 1 && it.y + h - 1 >= rowA) empty = false;
            }
            if (empty) shiftRows(c, rowA + 1, -2);
        }
        return done(what + (partner >= 0 ? std::string(" et ce qui la suivait") : std::string{}) + " supprim\xC3\xA9" "(e)", -1);
    }

    EditResult moveItem(Chart& c, int item, int x, int y) {
        if (!validIndex(c, item)) return fail("rien de choisi");
        auto& it = c.items[static_cast<std::size_t>(item)];
        if (!it.isCellObject() && it.kind != ItemKind::TextBox) return fail("seuls une \xC3\xA9tape, une transition, un saut se d\xC3\xA9placent");
        if (x < 0 || y < 0) return fail("hors de la grille");
        if (it.x == x && it.y == y) return fail("d\xC3\xA9j\xC3\xA0 l\xC3\xA0");
        if (it.isCellObject() && !cellFree(c, x, y)) return fail("la case (" + std::to_string(x) + ", " + std::to_string(y) + ") est prise");
        const int ox = it.x, oy = it.y;
        const std::string type = linkType(it);
        it.x = x;
        it.y = y;
        it.dirty = true;
        for (auto& l : c.items) {
            if (l.kind != ItemKind::Link) continue;
            bool touched = false;
            if (l.sx == ox && l.sy == oy && l.srcType == type) { l.sx = x; l.sy = y; touched = true; }
            if (l.dx == ox && l.dy == oy && l.dstType == type) { l.dx = x; l.dy = y; touched = true; }
            if (touched) { l.bends.clear(); l.x = l.sx; l.y = l.sy; l.dirty = true; }
        }
        return done("d\xC3\xA9plac\xC3\xA9 en (" + std::to_string(x) + ", " + std::to_string(y) + ")", item);
    }

    EditResult setSourceBody(Chart& c, const std::string& tag, const std::string& name, const std::string& body) {
        auto* s = c.source(tag, name);
        if (!s) return fail("pas de section " + name);
        if (s->language != domain::PouLanguage::ST && s->language != domain::PouLanguage::IL)
            return fail("la section " + name + " est en " + std::string(domain::toString(s->language))
                        + " : elle se modifie dans son \xC3\xA9" "diteur");
        if (s->body == body) return fail("rien n'a chang\xC3\xA9");
        s->body = body;
        s->dirty = true;
        return done("section " + name + " r\xC3\xA9\xC3\xA9" "crite", -1);
    }

    std::size_t rewriteNames(std::string& xml, const std::function<std::size_t(std::string&)>& rename) {
        auto c = parse(xml);
        if (!c.ok || !c.editable) return 0;
        std::size_t total = 0;
        auto apply = [&](std::string& text, bool& dirty) {
            if (text.empty()) return;
            std::string copy = text;
            const auto n = rename(copy);
            total += n;
            if (n > 0 && copy != text) { text = std::move(copy); dirty = true; }
        };
        for (auto& it : c.items) {
            if (it.kind == ItemKind::Transition && (it.cond == CondKind::Variable || it.cond == CondKind::Expression))
                apply(it.condText, it.dirty);
            if (it.kind == ItemKind::Step)
                for (auto& a : it.actions) {
                    if (!a.isSection) apply(a.name, it.dirty);
                    if (a.timeIsVariable) apply(a.time, it.dirty);
                }
        }
        for (auto& src : c.sources)
            if (src.language == domain::PouLanguage::ST || src.language == domain::PouLanguage::IL
                || src.language == domain::PouLanguage::LD)
                apply(src.body, src.dirty);
        if (c.modified()) xml = write(c);
        return total;
    }

} // namespace sfc

namespace project {

    namespace {
        // Comme l'import (XpgParser, countGraphRows) : les "lignes" d'un schema sont ses
        // etapes, ses transitions et les rangees de ses ladders - les statistiques suivent.
        std::uint32_t graphRows(std::string_view xml) {
            std::uint32_t n = 0;
            for (const std::string_view needle : { "<typeLine>", "<step ", "<transition>", "<transition ", "<FFBBlock " })
                for (std::size_t at = xml.find(needle); at != std::string_view::npos; at = xml.find(needle, at + needle.size())) ++n;
            return n;
        }
    } // namespace

    SfcEditCommand::SfcEditCommand(SfcProjectPtr project, domain::Index section, std::string body, std::string label)
        : project_(std::move(project)), section_(section), newBody_(std::move(body)), label_(std::move(label)) {}

    core::Status SfcEditCommand::execute() {
        if (!project_) return core::fail(core::ErrorCode::InvalidArgument, "no project");
        if (section_ >= project_->sections.size())
            return core::fail(core::ErrorCode::OutOfRange, "the section does not exist");
        auto& s = project_->sections[section_];
        if (!captured_) { oldBody_ = s.body; captured_ = true; }
        s.body = newBody_;
        s.lineCount = graphRows(s.body);
        return core::ok();
    }

    core::Status SfcEditCommand::undo() {
        if (!project_ || section_ >= project_->sections.size() || !captured_) return core::ok();
        auto& s = project_->sections[section_];
        s.body = oldBody_;
        s.lineCount = graphRows(s.body);
        return core::ok();
    }

} // namespace project
