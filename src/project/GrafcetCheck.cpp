#include "GrafcetCheck.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <queue>
#include <set>

namespace grafcet {

    namespace {

        bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
        bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

        std::string upper(std::string_view s) {
            std::string out(s);
            for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        bool equalsNoCase(std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        bool startsNoCase(std::string_view s, std::string_view p) {
            return s.size() >= p.size() && equalsNoCase(s.substr(0, p.size()), p);
        }

        std::string trimmed(std::string_view s) {
            std::size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return std::string(s.substr(b, e - b));
        }

        // Le texte sans commentaires ni chaines (remplaces par des espaces), de
        // meme longueur : les positions trouvees dedans valent dans l'original.
        std::string codeOnly(std::string_view s) {
            std::string out(s);
            bool comment = false, str = false;
            for (std::size_t i = 0; i < out.size(); ++i) {
                if (comment) {
                    if (out[i] == '*' && i + 1 < out.size() && out[i + 1] == ')') { out[i] = out[i + 1] = ' '; ++i; comment = false; continue; }
                    if (out[i] != '\n') out[i] = ' ';
                    continue;
                }
                if (str) { if (out[i] == '\'') str = false; out[i] = ' '; continue; }
                if (out[i] == '(' && i + 1 < out.size() && out[i + 1] == '*') { out[i] = out[i + 1] = ' '; ++i; comment = true; continue; }
                if (out[i] == '\'') { str = true; out[i] = ' '; }
            }
            return out;
        }

        // Un acces "Tableau[i].Champ" a la position `at` de `code` : le nom du
        // tableau doit etre `array` (sans casse), l'indice un entier.
        struct ArrayRef { std::size_t begin{ 0 }, end{ 0 }; int index{ -1 }; std::string field; };

        bool arrayRefAt(std::string_view code, std::size_t at, std::string_view array, ArrayRef& ref) {
            if (at > 0 && (identChar(code[at - 1]) || code[at - 1] == '.')) return false;
            if (!startsNoCase(code.substr(at), array)) return false;
            std::size_t i = at + array.size();
            while (i < code.size() && code[i] == ' ') ++i;
            if (i >= code.size() || code[i] != '[') return false;
            ++i;
            while (i < code.size() && code[i] == ' ') ++i;
            const std::size_t digits = i;
            while (i < code.size() && std::isdigit(static_cast<unsigned char>(code[i]))) ++i;
            if (i == digits) return false;
            const int index = std::atoi(std::string(code.substr(digits, i - digits)).c_str());
            while (i < code.size() && code[i] == ' ') ++i;
            if (i >= code.size() || code[i] != ']') return false;
            ++i;
            if (i >= code.size() || code[i] != '.') return false;
            ++i;
            const std::size_t f = i;
            while (i < code.size() && identChar(code[i])) ++i;
            if (i == f) return false;
            ref.begin = at;
            ref.end = i;
            ref.index = index;
            ref.field = std::string(code.substr(f, i - f));
            return true;
        }

        std::string stepArray(const Chart& c) { return "Steps_" + c.arrayPrefix; }
        std::string actArray(const Chart& c) { return "Acts_" + c.arrayPrefix; }

        // Saute les espaces a partir de i.
        std::size_t skipSpaces(std::string_view s, std::size_t i) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
            return i;
        }

        bool wordAt(std::string_view code, std::size_t i, std::string_view word) {
            if (!startsNoCase(code.substr(i), word)) return false;
            if (i > 0 && identChar(code[i - 1])) return false;
            const std::size_t e = i + word.size();
            return e >= code.size() || !identChar(code[e]);
        }

        const Chart* chartByName(const std::vector<Chart>* all, std::string_view name) {
            if (!all) return nullptr;
            for (const auto& c : *all)
                if (equalsNoCase(c.name, name) || equalsNoCase(c.arrayPrefix, name)) return &c;
            return nullptr;
        }

        const Chart* chartByInstance(const std::vector<Chart>* all, std::string_view inst) {
            if (!all) return nullptr;
            for (const auto& c : *all)
                if (!c.instance.empty() && equalsNoCase(c.instance, inst)) return &c;
            return nullptr;
        }

    } // namespace

    // ---------------------------------------------------------------- vocabulaire
    std::string kindCode(ActionKind k) {
        if (k == ActionKind::Unknown) return "K?";
        return "K" + std::to_string(static_cast<int>(k));
    }

    std::string_view kindQualifier(ActionKind k) {
        switch (k) {
        case ActionKind::Continuous:      return "N";
        case ActionKind::Rising:          return "P1";
        case ActionKind::DelayOn:         return "D";
        case ActionKind::PulseOn:         return "P1";
        case ActionKind::LimitedOn:       return "L";
        case ActionKind::Falling:         return "P0";
        case ActionKind::PulseOff:        return "P0";
        case ActionKind::LimitedOff:      return "L0";
        case ActionKind::CondDelayEnd:    return "C D";
        case ActionKind::CondDelayPulse:  return "C DP";
        case ActionKind::CondLimitedOn:   return "C L";
        case ActionKind::CondLimitedOnTp: return "C LP";
        case ActionKind::Unknown:         break;
        }
        return "?";
    }

    std::string durationText(std::string_view literal) {
        std::string s = trimmed(literal);
        std::size_t hash = s.find('#');
        if (hash == std::string::npos) return {};
        const std::string head = upper(s.substr(0, hash));
        if (head != "T" && head != "TIME") return {};
        std::string body = s.substr(hash + 1);
        for (auto& c : body) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        double ms = 0.0;
        std::size_t i = 0;
        bool any = false;
        while (i < body.size()) {
            if (body[i] == '_') { ++i; continue; }
            std::size_t n = i;
            while (n < body.size() && (std::isdigit(static_cast<unsigned char>(body[n])) || body[n] == '.')) ++n;
            if (n == i) return {};
            const double v = std::atof(body.substr(i, n - i).c_str());
            std::size_t u = n;
            while (u < body.size() && std::isalpha(static_cast<unsigned char>(body[u]))) ++u;
            const std::string unit = body.substr(n, u - n);
            if (unit == "ms") ms += v;
            else if (unit == "s") ms += v * 1000.0;
            else if (unit == "m") ms += v * 60000.0;
            else if (unit == "h") ms += v * 3600000.0;
            else if (unit == "d") ms += v * 86400000.0;
            else return {};
            any = true;
            i = u;
        }
        if (!any) return {};
        char buf[64];
        if (ms < 1000.0) { std::snprintf(buf, sizeof buf, "%.0f ms", ms); return buf; }
        const double sec = ms / 1000.0;
        if (sec >= 60.0 && std::fmod(sec, 60.0) == 0.0) {
            std::snprintf(buf, sizeof buf, "%.0f min", sec / 60.0);
            return buf;
        }
        if (sec >= 60.0) {
            const int m = static_cast<int>(sec / 60.0);
            std::snprintf(buf, sizeof buf, "%d min %.0f s", m, sec - m * 60.0);
            return buf;
        }
        if (std::fmod(sec, 1.0) == 0.0) std::snprintf(buf, sizeof buf, "%.0f s", sec);
        else {
            std::snprintf(buf, sizeof buf, "%.1f s", sec);
            for (char* p = buf; *p; ++p) if (*p == '.') *p = ',';
        }
        return buf;
    }

    namespace {
        // La duree d'une action, pour son libelle : "5 s", ou le parametre qui la
        // donne (config.DLDETOX), ou rien quand l'expression est trop compliquee.
        std::string delayWords(std::string_view delay) {
            const std::string t = trimmed(delay);
            if (t.empty()) return {};
            if (const auto d = durationText(t); !d.empty()) return d;
            const auto paths = variablePaths(t);
            if (paths.size() == 1) return paths.front();
            return "(calcul\xC3\xA9" "e)";
        }
    } // namespace

    std::string kindLabel(ActionKind k, std::string_view delay) {
        const std::string d = delayWords(delay);
        const std::string de = d.empty() ? std::string{} : " de " + d;
        const std::string a = d.empty() ? std::string{} : " \xC3\xA0 " + d;
        switch (k) {
        case ActionKind::Continuous:      return "continue";
        case ActionKind::Rising:          return "\xC3\xA0 l'activation";
        case ActionKind::DelayOn:         return "retard\xC3\xA9" "e" + de;
        case ActionKind::PulseOn:         return "impulsion \xC3\xA0 l'activation";
        case ActionKind::LimitedOn:       return "limit\xC3\xA9" "e" + a;
        case ActionKind::Falling:         return "\xC3\xA0 la d\xC3\xA9sactivation";
        case ActionKind::PulseOff:        return "impulsion \xC3\xA0 la d\xC3\xA9sactivation";
        case ActionKind::LimitedOff:      return "limit\xC3\xA9" "e" + a + " apr\xC3\xA8s la d\xC3\xA9sactivation";
        case ActionKind::CondDelayEnd:    return "conditionnelle, retard\xC3\xA9" "e" + de;
        case ActionKind::CondDelayPulse:  return "conditionnelle, impulsion retard\xC3\xA9" "e" + de;
        case ActionKind::CondLimitedOn:   return "conditionnelle, limit\xC3\xA9" "e" + a;
        case ActionKind::CondLimitedOnTp: return "conditionnelle, impulsion limit\xC3\xA9" "e" + a;
        case ActionKind::Unknown:         break;
        }
        return "genre inconnu";
    }

    std::string_view kindMeaning(ActionKind k) {
        switch (k) {
        case ActionKind::Continuous:      return "Tant que l'\xC3\xA9tape est active.";
        case ActionKind::Rising:          return "Une fois, quand l'\xC3\xA9tape s'active.";
        case ActionKind::DelayOn:         return "Apr\xC3\xA8s un retard, puis tant que l'\xC3\xA9tape reste active.";
        case ActionKind::PulseOn:         return "Une impulsion (un cycle), quand l'\xC3\xA9tape s'active.";
        case ActionKind::LimitedOn:       return "Quand l'\xC3\xA9tape s'active, pendant une dur\xC3\xA9" "e limit\xC3\xA9" "e.";
        case ActionKind::Falling:         return "Une fois, quand l'\xC3\xA9tape se d\xC3\xA9sactive.";
        case ActionKind::PulseOff:        return "Une impulsion (un cycle), quand l'\xC3\xA9tape se d\xC3\xA9sactive.";
        case ActionKind::LimitedOff:      return "Quand l'\xC3\xA9tape se d\xC3\xA9sactive, pendant une dur\xC3\xA9" "e limit\xC3\xA9" "e.";
        case ActionKind::CondDelayEnd:    return "Sur sa condition, apr\xC3\xA8s un retard, jusqu'\xC3\xA0 ce qu'elle retombe.";
        case ActionKind::CondDelayPulse:  return "Sur sa condition, une impulsion apr\xC3\xA8s un retard.";
        case ActionKind::CondLimitedOn:   return "Sur sa condition, pendant une dur\xC3\xA9" "e limit\xC3\xA9" "e.";
        case ActionKind::CondLimitedOnTp: return "Sur sa condition, une impulsion de dur\xC3\xA9" "e limit\xC3\xA9" "e.";
        case ActionKind::Unknown:         break;
        }
        return "Un genre que le moteur ne conna\xC3\xAEt pas.";
    }

    std::vector<ActionKind> allKinds() {
        std::vector<ActionKind> out;
        for (int k = 0; k <= 11; ++k) out.push_back(static_cast<ActionKind>(k));
        return out;
    }

    // ------------------------------------------------------------------ structure
    std::vector<int> transitionsFrom(const Chart& c, int stepId) {
        std::vector<int> out;
        for (const auto& t : c.transitions)
            if (std::find(t.sources.begin(), t.sources.end(), stepId) != t.sources.end()) out.push_back(t.id);
        return out;
    }

    std::vector<int> transitionsInto(const Chart& c, int stepId) {
        std::vector<int> out;
        for (const auto& t : c.transitions)
            if (std::find(t.destinations.begin(), t.destinations.end(), stepId) != t.destinations.end()) out.push_back(t.id);
        return out;
    }

    const Transition* transitionById(const Chart& c, int id) {
        for (const auto& t : c.transitions) if (t.id == id) return &t;
        return nullptr;
    }

    const Action* actionById(const Chart& c, int id) {
        for (const auto& a : c.actions) if (a.id == id) return &a;
        return nullptr;
    }

    std::vector<Branch> branches(const Chart& c) {
        std::vector<Branch> out;
        for (const auto& s : c.steps) {
            auto from = transitionsFrom(c, s.id);
            if (from.size() > 1) out.push_back(Branch{ BranchKind::OrDivergence, s.id, from });
            auto into = transitionsInto(c, s.id);
            if (into.size() > 1) out.push_back(Branch{ BranchKind::OrConvergence, s.id, into });
        }
        for (const auto& t : c.transitions) {
            if (t.destinations.size() > 1) out.push_back(Branch{ BranchKind::AndDivergence, t.id, t.destinations });
            if (t.sources.size() > 1) out.push_back(Branch{ BranchKind::AndConvergence, t.id, t.sources });
        }
        return out;
    }

    std::string_view branchName(BranchKind k) {
        switch (k) {
        case BranchKind::OrDivergence:   return "divergence en OU";
        case BranchKind::OrConvergence:  return "convergence en OU";
        case BranchKind::AndDivergence:  return "divergence en ET";
        case BranchKind::AndConvergence: return "convergence en ET";
        }
        return "";
    }

    // ------------------------------------------------------------------ raccourcis
    std::string readableCondition(const Chart& c, std::string_view expr) {
        if (c.arrayPrefix.empty()) return std::string(expr);
        const std::string code = codeOnly(expr);
        const std::string steps = stepArray(c), acts = actArray(c);
        std::string out;
        out.reserve(expr.size());
        std::size_t i = 0;
        while (i < expr.size()) {
            ArrayRef r;
            // FIN(An) : "Acts_P[n].Started AND Acts_P[n].Finished", la ou rien ne
            // lie l'un des deux a autre chose (NOT ou une comparaison juste avant,
            // une comparaison juste apres). Les parentheses ecrites autour restent
            // ecrites : "(FIN(A2))" se redeveloppe exactement en ce qu'il etait.
            {
                ArrayRef a, b;
                if (arrayRefAt(code, i, acts, a) && equalsNoCase(a.field, "Started")) {
                    std::size_t k = skipSpaces(code, a.end);
                    if (wordAt(code, k, "AND")) {
                        k = skipSpaces(code, k + 3);
                        if (arrayRefAt(code, k, acts, b) && equalsNoCase(b.field, "Finished") && b.index == a.index) {
                            bool ok = true;
                            std::size_t back = i;
                            while (back > 0 && (code[back - 1] == ' ' || code[back - 1] == '\t')) --back;
                            if (back >= 3 && wordAt(code, back - 3, "NOT")) ok = false;
                            if (back > 0 && (code[back - 1] == '=' || code[back - 1] == '<' || code[back - 1] == '>')) ok = false;
                            const std::size_t after = skipSpaces(code, b.end);
                            if (after < code.size() && (code[after] == '=' || code[after] == '<' || code[after] == '>')) ok = false;
                            // Seulement la forme exacte, pour que le developpement la
                            // redonne octet pour octet : "and" en minuscules s'ecrit
                            // "fin(A2)", autre chose (espaces doubles, casse melangee)
                            // reste tel quel.
                            const std::string n = std::to_string(a.index);
                            const std::string original(expr.substr(i, b.end - i));
                            const std::string up = acts + "[" + n + "].Started AND " + acts + "[" + n + "].Finished";
                            const std::string low = acts + "[" + n + "].Started and " + acts + "[" + n + "].Finished";
                            if (ok && (original == up || original == low)) {
                                out += (original == up ? "FIN(A" : "fin(A") + n + ")";
                                i = b.end;
                                continue;
                            }
                        }
                    }
                }
            }
            // FINI(Autre) : "(Gc_Autre.Finished AND NOT Ctrl_Autre.Cmd.InitReq)"
            if (code[i] == '(' && !(i > 0 && identChar(code[i - 1]))) {
                std::size_t k = skipSpaces(code, i + 1);
                if (startsNoCase(code.substr(k), "GC_")) {
                    std::size_t e = k;
                    while (e < code.size() && identChar(code[e])) ++e;
                    const std::string inst(expr.substr(k, e - k));
                    const std::string name = inst.substr(3);
                    const std::string want1 = ".Finished";
                    if (startsNoCase(code.substr(e), want1)) {
                        std::size_t m = skipSpaces(code, e + want1.size());
                        if (wordAt(code, m, "AND")) {
                            m = skipSpaces(code, m + 3);
                            if (wordAt(code, m, "NOT")) {
                                m = skipSpaces(code, m + 3);
                                const std::string ctrl = "Ctrl_" + name + ".Cmd.InitReq";
                                if (startsNoCase(code.substr(m), ctrl)) {
                                    std::size_t close = skipSpaces(code, m + ctrl.size());
                                    if (close < code.size() && code[close] == ')') {
                                        const std::string original(expr.substr(i, close + 1 - i));
                                        if (original == "(" + inst + ".Finished AND NOT " + ctrl + ")"
                                            || original == "(" + inst + ".Finished and not " + ctrl + ")") {
                                            out += (original[inst.size() + 11] == 'A' ? "FINI(" : "fini(") + name + ")";
                                            i = close + 1;
                                            continue;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (arrayRefAt(code, i, steps, r)) {
                // la forme exacte seulement (voir FIN) : Steps_P[3].ActiveTime
                const std::string original(expr.substr(i, r.end - i));
                const std::string base = steps + "[" + std::to_string(r.index) + "].";
                if (original == base + "ActiveTime") { out += "DUREE(X" + std::to_string(r.index) + ")"; i = r.end; continue; }
                if (original == base + "Active")     { out += "ACTIF(X" + std::to_string(r.index) + ")"; i = r.end; continue; }
            }
            // un identifiant entier d'un coup, pour ne jamais commencer un motif au
            // milieu d'un nom (MySteps_P[1] n'est pas Steps_P[1])
            if (identStart(code[i]) && !(i > 0 && identChar(code[i - 1]))) {
                std::size_t e = i;
                while (e < code.size() && identChar(code[e])) ++e;
                out.append(expr.substr(i, e - i));
                i = e;
                continue;
            }
            out.push_back(expr[i]);
            ++i;
        }
        return out;
    }

    std::vector<Shortcut> shortcutsIn(std::string_view text) {
        std::vector<Shortcut> out;
        const std::string code = codeOnly(text);
        std::size_t i = 0;
        while (i < code.size()) {
            if (!identStart(code[i]) || (i > 0 && (identChar(code[i - 1]) || code[i - 1] == '.'))) { ++i; continue; }
            std::size_t e = i;
            while (e < code.size() && identChar(code[e])) ++e;
            const std::string word = upper(std::string_view(code).substr(i, e - i));
            if ((word == "FIN" || word == "ACTIF" || word == "DUREE" || word == "FINI" || word == "INIT")
                && e < code.size() && code[e] == '(') {
                const auto close = code.find(')', e);
                if (close != std::string::npos) {
                    Shortcut s;
                    s.word = word;
                    s.target = trimmed(std::string_view(text).substr(e + 1, close - e - 1));
                    s.offset = i;
                    s.length = close + 1 - i;
                    out.push_back(std::move(s));
                    i = close + 1;
                    continue;
                }
            }
            i = e;
        }
        return out;
    }

    std::string expandShortcuts(const Chart& c, std::string_view text, const std::vector<Chart>* others) {
        std::string out;
        std::size_t at = 0;
        for (const auto& s : shortcutsIn(text)) {
            out.append(text.substr(at, s.offset - at));
            std::string t = s.target;
            if (s.word == "FINI") {
                const Chart* other = chartByName(others, t);
                std::string inst = other && !other->instance.empty() ? other->instance : "Gc_" + t;
                const std::string name = other ? (other->instance.size() > 3 ? other->instance.substr(3) : t) : t;
                const bool lower = text[s.offset] == 'f';   // "fini(Autre)" : le programme ecrit "and not"
                out += "(" + inst + (lower ? ".Finished and not Ctrl_" : ".Finished AND NOT Ctrl_") + name + ".Cmd.InitReq)";
            }
            else if (s.word == "INIT") {
                out.append(text.substr(s.offset, s.length));   // une cible d'action, pas une receptivite
            }
            else {
                if (!t.empty() && (t[0] == 'A' || t[0] == 'a' || t[0] == 'X' || t[0] == 'x')) t = t.substr(1);
                const std::string n = std::to_string(std::atoi(t.c_str()));
                if (s.word == "FIN") {
                    // Sans parentheses quand rien autour ne lie plus fort que AND :
                    // le debut, une parenthese, AND / OR / XOR. C'est la forme du
                    // programme (et de ImporterGrafcet) ; ailleurs (NOT FIN(A2),
                    // FIN(A2) = FALSE) les parentheses gardent le sens.
                    const std::string code = codeOnly(text);
                    auto bindsLoosely = [&](std::size_t from, bool before) {
                        if (before) {
                            std::size_t k = from;
                            while (k > 0 && (code[k - 1] == ' ' || code[k - 1] == '\t' || code[k - 1] == '\n' || code[k - 1] == '\r')) --k;
                            if (k == 0 || code[k - 1] == '(') return true;
                            std::size_t w = k;
                            while (w > 0 && identChar(code[w - 1])) --w;
                            const std::string word = upper(std::string_view(code).substr(w, k - w));
                            return word == "AND" || word == "OR" || word == "XOR";
                        }
                        std::size_t k = from;
                        while (k < code.size() && (code[k] == ' ' || code[k] == '\t' || code[k] == '\n' || code[k] == '\r')) ++k;
                        if (k >= code.size() || code[k] == ')' || code[k] == ';') return true;
                        return wordAt(code, k, "AND") || wordAt(code, k, "OR") || wordAt(code, k, "XOR");
                    };
                    const bool bare = bindsLoosely(s.offset, true) && bindsLoosely(s.offset + s.length, false);
                    const bool lower = text[s.offset] == 'f';   // "fin(A2)" : le programme ecrit "and"
                    const std::string inner = actArray(c) + "[" + n + (lower ? "].Started and " : "].Started AND ") + actArray(c) + "[" + n + "].Finished";
                    out += bare ? inner : "(" + inner + ")";
                }
                else if (s.word == "ACTIF") out += stepArray(c) + "[" + n + "].Active";
                else                        out += stepArray(c) + "[" + n + "].ActiveTime";
            }
            at = s.offset + s.length;
        }
        out.append(text.substr(at));
        return out;
    }

    std::vector<ChartLink> chartLinks(const Chart& c, const std::vector<Chart>& all) {
        std::vector<ChartLink> out;
        auto addUnique = [&](ChartLink l) {
            for (const auto& o : out)
                if (o.other == l.other && o.launches == l.launches && o.part == l.part && o.id == l.id) return;
            out.push_back(std::move(l));
        };
        for (const auto& t : c.transitions) {
            const std::string code = codeOnly(t.conditionExpr);
            for (std::size_t i = 0; i + 3 < code.size(); ++i) {
                if (!startsNoCase(std::string_view(code).substr(i), "GC_") || (i > 0 && identChar(code[i - 1]))) continue;
                std::size_t e = i;
                while (e < code.size() && identChar(code[e])) ++e;
                if (const Chart* o = chartByInstance(&all, std::string_view(t.conditionExpr).substr(i, e - i)))
                    if (o != &c && o->name != c.name) addUnique({ o->name, false, 'T', t.id });
                i = e;
            }
        }
        for (const auto& a : c.actions) {
            const std::string code = codeOnly(a.body);
            std::size_t i = 0;
            while ((i = code.find("Ctrl_", i)) != std::string::npos) {
                if (i > 0 && identChar(code[i - 1])) { i += 5; continue; }
                std::size_t e = i + 5;
                while (e < code.size() && identChar(code[e])) ++e;
                const std::string name = code.substr(i + 5, e - i - 5);
                const std::string rest = code.substr(e, 32);
                if (startsNoCase(rest, ".Cmd.InitReq")) {
                    std::size_t k = skipSpaces(code, e + 12);
                    if (k + 1 < code.size() && code[k] == ':' && code[k + 1] == '=') {
                        if (const Chart* o = chartByName(&all, name))
                            if (o->name != c.name) addUnique({ o->name, true, 'A', a.id });
                    }
                }
                i = e;
            }
            for (const auto& s : shortcutsIn(a.body))
                if (s.word == "INIT")
                    if (const Chart* o = chartByName(&all, s.target)) addUnique({ o->name, true, 'A', a.id });
        }
        return out;
    }

    // ---------------------------------------------------------------- variables
    std::vector<std::string> variablePaths(std::string_view expr) {
        std::vector<std::string> out;
        const std::string code = codeOnly(expr);
        static const std::set<std::string> keywords = { "AND", "OR", "NOT", "XOR", "MOD", "TRUE", "FALSE",
                                                        "IF", "THEN", "ELSE", "ELSIF", "END_IF" };
        std::size_t i = 0;
        while (i < code.size()) {
            const char ch = code[i];
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                while (i < code.size() && (identChar(code[i]) || code[i] == '.' || code[i] == '#')) ++i;
                continue;
            }
            if (!identStart(ch)) { ++i; continue; }
            std::size_t e = i;
            while (e < code.size() && identChar(code[e])) ++e;
            if (i > 0 && code[i - 1] == '.') { i = e; continue; }   // un membre, deja compte
            const std::string word = upper(std::string_view(code).substr(i, e - i));
            std::size_t n = skipSpaces(code, e);
            if (n < code.size() && code[n] == '#') {           // t#5s, INT#3 : un litteral
                ++n;
                while (n < code.size() && (identChar(code[n]) || code[n] == '.')) ++n;
                i = n;
                continue;
            }
            if (keywords.count(word)) { i = e; continue; }
            if (n < code.size() && code[n] == '(') { i = e; continue; }   // une fonction, un raccourci
            // le chemin : .membre et [indice] a la suite
            std::size_t p = e;
            for (;;) {
                if (p < code.size() && code[p] == '.' && p + 1 < code.size() && identStart(code[p + 1])) {
                    ++p;
                    while (p < code.size() && identChar(code[p])) ++p;
                    continue;
                }
                if (p < code.size() && code[p] == '[') {
                    int depth = 0;
                    while (p < code.size()) {
                        if (code[p] == '[') ++depth;
                        else if (code[p] == ']' && --depth == 0) { ++p; break; }
                        ++p;
                    }
                    continue;
                }
                break;
            }
            std::string path(expr.substr(i, p - i));
            if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(std::move(path));
            // les indices peuvent eux-memes citer des variables : on y repasse
            const auto bracket = code.find('[', e);
            i = (bracket != std::string::npos && bracket < p) ? bracket + 1 : p;
        }
        return out;
    }

    std::vector<std::string> variableRoots(std::string_view expr) {
        std::vector<std::string> out;
        for (const auto& p : variablePaths(expr)) {
            std::size_t e = 0;
            while (e < p.size() && identChar(p[e])) ++e;
            std::string root = p.substr(0, e);
            if (std::find(out.begin(), out.end(), root) == out.end()) out.push_back(std::move(root));
        }
        return out;
    }

    // ------------------------------------------------------------------ controles
    std::string_view checkTitle(CheckKind k) {
        switch (k) {
        case CheckKind::NoExitNotFinal:    return "\xC3\x89tape sans sortie";
        case CheckKind::Unreachable:       return "\xC3\x89tape jamais atteinte";
        case CheckKind::NoInitial:         return "Pas d'\xC3\xA9tape initiale";
        case CheckKind::DuplicateId:       return "Num\xC3\xA9ro en double";
        case CheckKind::TooManyEnds:       return "Plus de 3 sources ou destinations";
        case CheckKind::FinOfContinuous:   return "FIN d'une action continue";
        case CheckKind::ShortcutToMissing: return "Renvoi vers ce qui n'existe pas";
        case CheckKind::UnknownVariable:   return "Variable inexistante";
        case CheckKind::NoCondition:       return "R\xC3\xA9" "ceptivit\xC3\xA9 vide";
        case CheckKind::DanglingLink:      return "Liaison vers une \xC3\xA9tape absente";
        }
        return "";
    }

    std::vector<Check> runChecks(const Chart& c, const domain::Project* project, const std::vector<Chart>* all) {
        std::vector<Check> out;
        auto add = [&](CheckKind k, bool severe, char part, int id, std::string msg) {
            out.push_back(Check{ k, severe, part, id, std::move(msg) });
        };
        auto X = [](int id) { return "X" + std::to_string(id); };
        auto T = [](int id) { return "T" + std::to_string(id); };
        auto A = [](int id) { return "A" + std::to_string(id); };

        // pas d'etape initiale
        if (!c.steps.empty() && std::none_of(c.steps.begin(), c.steps.end(), [](const Step& s) { return s.initial; }))
            add(CheckKind::NoInitial, true, 0, -1,
                "Aucune \xC3\xA9tape initiale : le grafcet ne peut pas d\xC3\xA9marrer.");

        // numeros en double
        for (std::size_t i = 1; i < c.steps.size(); ++i)
            if (c.steps[i].id == c.steps[i - 1].id)
                add(CheckKind::DuplicateId, true, 'X', c.steps[i].id,
                    "Deux \xC3\xA9tapes portent le num\xC3\xA9ro " + std::to_string(c.steps[i].id) + ".");

        // liaisons et extremites
        for (const auto& t : c.transitions) {
            if (t.sources.size() > 3 || t.destinations.size() > 3)
                add(CheckKind::TooManyEnds, true, 'T', t.id,
                    T(t.id) + " a " + std::to_string(std::max(t.sources.size(), t.destinations.size()))
                    + " sources ou destinations : le moteur en garde 3 et ne v\xC3\xA9rifie pas.");
            for (int s : t.sources)
                if (!c.stepById(s))
                    add(CheckKind::DanglingLink, true, 'T', t.id, T(t.id) + " part de " + X(s) + ", qui n'existe pas.");
            for (int d : t.destinations)
                if (!c.stepById(d))
                    add(CheckKind::DanglingLink, true, 'T', t.id, T(t.id) + " m\xC3\xA8ne \xC3\xA0 " + X(d) + ", qui n'existe pas.");
            if (trimmed(t.conditionExpr).empty())
                add(CheckKind::NoCondition, true, 'T', t.id,
                    T(t.id) + " n'a pas de r\xC3\xA9" "ceptivit\xC3\xA9 : elle ne sera jamais franchie.");
        }
        for (const auto& a : c.actions)
            if (a.boundStep >= 0 && !c.stepById(a.boundStep))
                add(CheckKind::DanglingLink, true, 'A', a.id, A(a.id) + " appartient \xC3\xA0 " + X(a.boundStep) + ", qui n'existe pas.");

        // etape sans sortie qui n'est pas finale
        for (const auto& s : c.steps)
            if (!s.isFinal && transitionsFrom(c, s.id).empty())
                add(CheckKind::NoExitNotFinal, true, 'X', s.id,
                    X(s.id) + " n'a pas de transition de sortie et n'est pas finale : le grafcet s'y arr\xC3\xAAte.");

        // etapes jamais atteintes
        if (std::any_of(c.steps.begin(), c.steps.end(), [](const Step& s) { return s.initial; })) {
            std::set<int> seen;
            std::queue<int> q;
            for (const auto& s : c.steps) if (s.initial) { seen.insert(s.id); q.push(s.id); }
            while (!q.empty()) {
                const int at = q.front();
                q.pop();
                for (const auto& t : c.transitions) {
                    if (std::find(t.sources.begin(), t.sources.end(), at) == t.sources.end()) continue;
                    for (int d : t.destinations) if (seen.insert(d).second) q.push(d);
                }
            }
            for (const auto& s : c.steps)
                if (!seen.count(s.id))
                    add(CheckKind::Unreachable, true, 'X', s.id,
                        X(s.id) + " n'est jamais atteinte depuis une \xC3\xA9tape initiale.");
        }

        // les references des receptivites et des conditions d'action
        const std::string steps = stepArray(c), acts = actArray(c);
        auto scanRefs = [&](const std::string& expr, char part, int id, const Transition* t) {
            const std::string code = codeOnly(expr);
            for (std::size_t i = 0; i < code.size(); ++i) {
                ArrayRef r;
                if (arrayRefAt(code, i, steps, r)) {
                    if (!c.stepById(r.index))
                        add(CheckKind::ShortcutToMissing, true, part, id,
                            std::string(part == 'T' ? T(id) : A(id)) + " cite " + X(r.index) + " ("
                            + (equalsNoCase(r.field, "ActiveTime") ? "DUREE" : "ACTIF") + "), qui n'existe pas.");
                    i = r.end - 1;
                    continue;
                }
                if (arrayRefAt(code, i, acts, r)) {
                    const Action* a = actionById(c, r.index);
                    if (!a)
                        add(CheckKind::ShortcutToMissing, true, part, id,
                            std::string(part == 'T' ? T(id) : A(id)) + " cite " + A(r.index) + ", qui n'existe pas.");
                    else if (t && equalsNoCase(r.field, "Finished")
                             && (a->kind == ActionKind::Continuous || a->kind == ActionKind::CondDelayEnd)) {
                        const bool own = std::find(t->sources.begin(), t->sources.end(), a->boundStep) != t->sources.end();
                        add(CheckKind::FinOfContinuous, own, 'T', t->id,
                            "FIN(" + A(a->id) + ") dans " + T(t->id) + " : " + A(a->id) + " est une action continue ("
                            + kindCode(a->kind) + "), elle ne finit qu'une fois son \xC3\xA9tape quitt\xC3\xA9" "e"
                            + (own ? " : le grafcet restera bloqu\xC3\xA9 en " + X(a->boundStep) + "." : "."));
                    }
                    i = r.end - 1;
                    continue;
                }
                if (all && startsNoCase(std::string_view(code).substr(i), "GC_") && !(i > 0 && identChar(code[i - 1]))) {
                    std::size_t e = i;
                    while (e < code.size() && identChar(code[e])) ++e;
                    const std::string inst(std::string_view(expr).substr(i, e - i));
                    if (!chartByInstance(all, inst) && !(project && [&] {
                            for (const auto& v : project->variables)
                                if (equalsNoCase(project->strings.text(v.name), inst)) return true;
                            return false; }()))
                        add(CheckKind::ShortcutToMissing, true, part, id,
                            std::string(part == 'T' ? T(id) : A(id)) + " attend " + inst
                            + ", qui n'est pas un grafcet du programme.");
                    i = e - 1;
                }
            }
        };
        for (const auto& t : c.transitions) scanRefs(t.conditionExpr, 'T', t.id, &t);
        for (const auto& a : c.actions) scanRefs(a.enableExpr, 'A', a.id, nullptr);

        // variables inexistantes
        if (project) {
            std::set<std::string> names;
            for (const auto& v : project->variables) names.insert(upper(project->strings.text(v.name)));
            auto scanVars = [&](const std::string& expr, char part, int id) {
                for (const auto& root : variableRoots(expr))
                    if (!names.count(upper(root)))
                        add(CheckKind::UnknownVariable, true, part, id,
                            std::string(part == 'T' ? T(id) : A(id)) + " : la variable " + root
                            + " n'existe pas dans le programme.");
            };
            for (const auto& t : c.transitions) scanVars(t.conditionExpr, 'T', t.id);
            for (const auto& a : c.actions) scanVars(a.enableExpr, 'A', a.id);
        }
        return out;
    }

} // namespace grafcet
