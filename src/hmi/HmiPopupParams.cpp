// =============================================================================
//  hmi/HmiPopupParams.cpp - 1.9 : les parametres des popups (voir le .hpp)
// =============================================================================
#include "HmiPopupParams.hpp"

#include "HmiMarkers.hpp"   // 1.11.1 (REP) : un repere ecrit comme son morceau ($Vit$ := ...)
#include "HmiRuntime.hpp"   // parseArguments, isVariablePath
#include "HmiSymbols.hpp"   // 1.11.10 : les instances d'un symbole (withArgument)
#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <functional>

namespace hmi::params {

namespace {

std::string up(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool same(std::string_view a, std::string_view b) { return up(a) == up(b); }
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// Les entiers : (signe, bits). WORD et DWORD sont des mots de bits (non signes).
struct IntInfo { bool isInt{false}; bool isSigned{false}; int bits{0}; bool bitString{false}; };
IntInfo intInfo(std::string_view t) {
    const std::string u = up(t);
    if (u == "SINT") return {true, true, 8, false};
    if (u == "INT") return {true, true, 16, false};
    if (u == "DINT") return {true, true, 32, false};
    if (u == "LINT") return {true, true, 64, false};
    if (u == "USINT") return {true, false, 8, false};
    if (u == "UINT") return {true, false, 16, false};
    if (u == "UDINT") return {true, false, 32, false};
    if (u == "ULINT") return {true, false, 64, false};
    if (u == "BYTE") return {true, false, 8, true};
    if (u == "WORD") return {true, false, 16, true};
    if (u == "DWORD") return {true, false, 32, true};
    if (u == "LWORD") return {true, false, 64, true};
    return {};
}

// Les noms lus (racines) d'un texte, hors chaines et commentaires.
template <class F>
void forEachRoot(std::string_view s, F&& f) {
    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            i = end == std::string_view::npos ? s.size() : end + 1;
            continue;
        }
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto end = s.find("*)", i + 2);
            i = end == std::string_view::npos ? s.size() : end + 2;
            continue;
        }
        if (identStart(c) && (i == 0 || (s[i - 1] != '.' && s[i - 1] != '#' && !identChar(s[i - 1])))) {
            std::size_t j = i;
            while (j < s.size() && identChar(s[j])) ++j;
            f(i, j);
            i = j;
            continue;
        }
        ++i;
    }
}

} // namespace

// ---------------------------------------------------------------- modes -----
std::string_view paramModeLabel(ParamMode m) noexcept {
    switch (m) {
        case ParamMode::Reference: return "R\xC3\xA9" "f\xC3\xA9rence";
        case ParamMode::Copy: return "Copie";
        case ParamMode::Both: return "Les deux";
    }
    return "R\xC3\xA9" "f\xC3\xA9rence";
}
std::string_view paramModeBadge(ParamMode m) noexcept {
    switch (m) {
        case ParamMode::Reference: return "REF";
        case ParamMode::Copy: return "COPIE";
        case ParamMode::Both: return "LES DEUX";
    }
    return "REF";
}
std::string_view paramModeKey(ParamMode m) noexcept {
    switch (m) {
        case ParamMode::Reference: return "reference";
        case ParamMode::Copy: return "copie";
        case ParamMode::Both: return "les_deux";
    }
    return "reference";
}
ParamMode paramModeFrom(std::string_view s) noexcept {
    for (const auto m : kParamModes)
        if (same(s, paramModeKey(m)) || same(s, paramModeLabel(m)) || same(s, paramModeBadge(m))) return m;
    return ParamMode::Reference;
}
std::string_view paramModeHelp(ParamMode m) noexcept {
    switch (m) {
        case ParamMode::Reference:
            return "Le nom d\xC3\xA9signe la variable de l'appelant : la popup la lit et l'\xC3\xA9" "crit en direct.";
        case ParamMode::Copy:
            return "Une capture prise \xC3\xA0 l'ouverture : la popup la lit et la modifie, l'original ne change jamais.";
        case ParamMode::Both:
            return "La capture, puis \xC2\xAB Appliquer copie sur r\xC3\xA9" "f\xC3\xA9rence \xC2\xBB \xC3\xA9" "crit les membres modifi\xC3\xA9s dans la variable de l'appelant.";
    }
    return {};
}

// ---------------------------------------------------------------- types -----
const std::vector<std::string>& baseTypes() {
    static const std::vector<std::string> k{"BOOL", "INT", "UINT", "WORD", "DINT", "UDINT", "DWORD",
                                            "REAL", "LREAL", "STRING", "TIME", "ANY"};
    return k;
}

std::vector<TypeChoice> proposedTypes(const Project& p, const PlcTypes& plc) {
    std::vector<TypeChoice> out;
    for (const auto& t : baseTypes()) out.push_back({t, "Base"});
    for (const auto& t : p.programs.types) out.push_back({t.name, "Types IHM"});
    if (plc.names)
        for (auto& n : plc.names()) {
            const bool dup = std::any_of(out.begin(), out.end(), [&](const TypeChoice& c) { return same(c.name, n); });
            if (!dup) out.push_back({std::move(n), "DDT de l'API"});
        }
    return out;
}

std::string normalizedType(std::string_view type) {
    std::string out;
    for (const char c : type)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (out.empty()) return "ANY";
    if (out.rfind("STRING[", 0) == 0) return "STRING";
    // ARRAY[0..9]OFREAL : on garde la forme sans espaces (comparee telle quelle)
    return out;
}

bool typeKnown(const Project& p, std::string_view type, const PlcTypes& plc) {
    const std::string n = normalizedType(type);
    if (n == "ANY") return true;
    if (std::find(baseTypes().begin(), baseTypes().end(), n) != baseTypes().end()) return true;
    if (intInfo(n).isInt || n == "LREAL" || n == "REAL" || n == "STRING" || n == "TIME" || n == "BOOL") return true;
    if (types::validType(p, type)) return true;
    const auto isDdt = [&](std::string_view t) {
        if (!plc.names) return false;
        for (const auto& d : plc.names()) if (same(d, t)) return true;
        return false;
    };
    if (isDdt(n) || (plc.isType && plc.isType(type))) return true;
    // ARRAY[a..b] OF <DDT>
    types::Spec spec;
    if (types::parseSpec(type, spec) && spec.array()) return typeKnown(p, spec.element, plc);
    return false;
}

bool typeAccepts(std::string_view declared, std::string_view given) {
    const std::string d = normalizedType(declared);
    if (trim(given).empty()) return true;           // inconnu : pas d'erreur sure
    const std::string g = normalizedType(given);
    if (d == "ANY" || g == "ANY") return true;
    if (d == g) return true;
    const IntInfo di = intInfo(d), gi = intInfo(g);
    if (di.isInt && gi.isInt) {
        if (di.bitString) return !gi.isSigned && gi.bits <= di.bits;     // un mot de bits : un non signe pas plus large
        if (di.isSigned) return gi.isSigned ? gi.bits <= di.bits : gi.bits < di.bits;
        return !gi.isSigned && gi.bits <= di.bits;
    }
    if (d == "REAL") return gi.isInt && gi.bits <= 16;
    if (d == "LREAL") return gi.isInt || g == "REAL";
    // ARRAY : memes bornes, element exactement le meme (deja compare : d == g)
    return false;
}

bool typeExact(std::string_view declared, std::string_view given) {
    if (trim(given).empty()) return true;           // inconnu : pas d'erreur sure
    const std::string d = normalizedType(declared), g = normalizedType(given);
    return d == "ANY" || g == "ANY" || d == g;
}

bool typeAcceptsFor(ParamMode mode, std::string_view declared, std::string_view given, bool variable) {
    if (mode == ParamMode::Copy || !variable) return typeAccepts(declared, given);
    return typeExact(declared, given);
}

std::vector<std::pair<std::string, std::string>> typeMembers(const Project& p, std::string_view type, const PlcTypes& plc) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& m : types::membersOf(p, type)) out.emplace_back(m.name, m.type);
    if (out.empty() && plc.members) out = plc.members(type);
    return out;
}

// ----------------------------------------------------- type d'une expression -
std::string expressionType(const Project& p, const View* caller, std::string_view expression, const PlcTypes& plc) {
    const std::string t = trim(markers::strip(expression));   // 1.11.7 : $V[0]$ se lit V[0] (un repere est transparent)
    if (t.empty()) return {};
    const std::string u = up(t);
    if (u == "TRUE" || u == "FALSE") return "BOOL";
    if (t.front() == '\'' || t.front() == '"') return "STRING";
    if (u.rfind("T#", 0) == 0 || u.rfind("TIME#", 0) == 0) return "TIME";
    {
        // un nombre (avec un signe)
        std::size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
        bool digits = i < t.size(), dot = false;
        for (std::size_t j = i; j < t.size(); ++j) {
            if (t[j] == '.' || t[j] == 'e' || t[j] == 'E') { dot = true; continue; }
            if (!std::isdigit(static_cast<unsigned char>(t[j])) && t[j] != '_') { digits = false; break; }
        }
        if (digits) return dot ? "REAL" : "INT";
        if (u.rfind("16#", 0) == 0 || u.rfind("2#", 0) == 0 || u.rfind("8#", 0) == 0) return "INT";
    }
    if (!isVariablePath(t)) return {};   // un calcul : son type n'est pas sur ici
    // la racine du chemin
    std::size_t r = 0;
    while (r < t.size() && identChar(t[r])) ++r;
    const std::string root = t.substr(0, r);
    const std::string rest = t.substr(r);
    // un parametre de la vue appelante : son type declare, puis les membres
    const auto memberWalk = [&](std::string type, std::string_view tail) -> std::string {
        std::size_t i = 0;
        while (i < tail.size()) {
            if (tail[i] == '[') {
                int depth = 0;
                std::size_t j = i;
                for (; j < tail.size(); ++j) {
                    if (tail[j] == '[') ++depth;
                    else if (tail[j] == ']' && --depth == 0) break;
                }
                types::Spec spec;
                if (!types::parseSpec(type, spec) || !spec.array()) return {};
                type = spec.element;
                i = j + 1;
                continue;
            }
            if (tail[i] == '.') {
                std::size_t j = i + 1;
                while (j < tail.size() && identChar(tail[j])) ++j;
                const std::string member(tail.substr(i + 1, j - i - 1));
                std::string next;
                for (const auto& [n, mt] : typeMembers(p, type, plc)) if (same(n, member)) { next = mt; break; }
                if (next.empty() && plc.memberType) next = plc.memberType(type, member);
                if (next.empty()) return {};
                type = next;
                i = j;
                continue;
            }
            return {};
        }
        return type;
    };
    if (caller)
        if (const auto* prm = caller->param(root)) {
            if (trim(prm->type).empty()) return {};
            return memberWalk(prm->type, rest);
        }
    // une variable IHM (ses membres, ses cases)
    if (const std::string ihm = types::typeOfPath(p, t); !ihm.empty()) return ihm;
    for (const auto& v : p.programs.variables)
        if (same(v.name, root)) return memberWalk(v.type, rest);
    // une variable de l'automate
    if (plc.rootType)
        if (const std::string rt = plc.rootType(root); !rt.empty()) return memberWalk(rt, rest);
    return {};
}

// ------------------------------------------------------------- arguments -----
bool mentionsName(std::string_view text, std::string_view name) {
    bool found = false;
    forEachRoot(text, [&](std::size_t a, std::size_t b) {
        if (!found && same(text.substr(a, b - a), name)) found = true;
    });
    return found;
}

std::string renameRoot(std::string_view text, std::string_view from, std::string_view to) {
    std::string out;
    std::size_t last = 0;
    forEachRoot(text, [&](std::size_t a, std::size_t b) {
        if (!same(text.substr(a, b - a), from)) return;
        out.append(text.substr(last, a - last));
        out.append(to);
        last = b;
    });
    out.append(text.substr(last));
    return out;
}

std::string renameArgument(std::string_view text, std::string_view from, std::string_view to) {
    // Le nom au debut d'un argument (au debut, ou apres un ';' hors chaine et
    // hors crochets), suivi de ":=" ou "=".
    std::string out;
    std::size_t i = 0;
    bool start = true;
    int depth = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '\'' || c == '"') {
            const auto end = text.find(c, i + 1);
            const std::size_t stop = end == std::string_view::npos ? text.size() : end + 1;
            out.append(text.substr(i, stop - i));
            i = stop;
            start = false;
            continue;
        }
        if (c == '[') ++depth;
        if (c == ']') --depth;
        if (c == ';' && depth == 0) { out += c; ++i; start = true; continue; }
        if (start && std::isspace(static_cast<unsigned char>(c))) { out += c; ++i; continue; }
        if (start && identStart(c)) {
            std::size_t j = i;
            while (j < text.size() && identChar(text[j])) ++j;
            std::size_t k = j;
            while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
            const bool assign = k < text.size() && (text[k] == '=' || (text[k] == ':' && k + 1 < text.size() && text[k + 1] == '='));
            out.append(assign && same(text.substr(i, j - i), from) ? std::string(to) : std::string(text.substr(i, j - i)));
            i = j;
            start = false;
            continue;
        }
        start = false;
        out += c;
        ++i;
    }
    return out;
}

namespace {
// "A := Moteur.X; B := 'Moteur'" : renomme la racine dans les valeurs seulement
// (les noms a gauche sont ceux des parametres de l'autre vue).
std::string renameArgumentValues(std::string_view text, std::string_view from, std::string_view to) {
    std::string out;
    std::size_t i = 0;
    while (i <= text.size()) {
        // le segment jusqu'au ';' de premier niveau
        std::size_t j = i;
        int depth = 0;
        char quote = 0;
        for (; j < text.size(); ++j) {
            const char c = text[j];
            if (quote) { if (c == quote) quote = 0; continue; }
            if (c == '\'' || c == '"') { quote = c; continue; }
            if (c == '[') ++depth;
            else if (c == ']') --depth;
            else if (c == ';' && depth == 0) break;
        }
        const std::string_view seg = text.substr(i, j - i);
        auto eq = seg.find(":=");
        std::size_t len = 2;
        if (eq == std::string_view::npos) { eq = seg.find('='); len = 1; }
        if (eq == std::string_view::npos) out.append(seg);
        else {
            out.append(seg.substr(0, eq + len));
            out += renameRoot(seg.substr(eq + len), from, to);
        }
        if (j >= text.size()) break;
        out += ';';
        i = j + 1;
    }
    return out;
}
} // namespace

std::size_t renameParam(Project& p, std::string_view viewName, std::string_view from, std::string_view to,
                        std::vector<std::string>* where) {
    std::size_t count = 0;
    const auto note = [&](std::string w) {
        ++count;
        if (where) where->push_back(std::move(w));
    };
    View* v = p.viewByName(viewName);
    if (!v || !v->param(from) || trim(to).empty()) return 0;
    for (auto& prm : v->params)
        if (same(prm.name, from)) { prm.name = std::string(to); note(v->name + " / param\xC3\xA8tre " + std::string(to)); }
    // Un texte a trous : les expressions entre accolades.
    const auto inTemplate = [&](const std::string& text) {
        std::string out;
        std::size_t i = 0;
        while (i < text.size()) {
            const auto open = text.find('{', i);
            if (open == std::string::npos) { out.append(text, i, std::string::npos); break; }
            const auto close = text.find('}', open);
            if (close == std::string::npos) { out.append(text, i, std::string::npos); break; }
            out.append(text, i, open + 1 - i);
            out += renameRoot(std::string_view(text).substr(open + 1, close - open - 1), from, to);
            out += '}';
            i = close + 1;
        }
        return out;
    };
    const auto actions = [&](std::vector<Action>& list, const std::string& label) {
        bool changed = false;
        for (auto& a : list) {
            const Action before = a;
            if (a.operation == Operation::ApplyCopy) {
                if (same(trim(a.target), from)) a.target = std::string(to);
            } else {
                a.target = renameRoot(a.target, from, to);
            }
            if (a.operation == Operation::Log) a.value = inTemplate(a.value);
            else if (!operationTakesArguments(a.operation)) a.value = renameRoot(a.value, from, to);
            else a.value = renameArgumentValues(a.value, from, to);   // "Moteur := Moteur" : la valeur seulement
            a.guard = renameRoot(a.guard, from, to);
            a.watch = renameRoot(a.watch, from, to);
            a.params = renameArgumentValues(a.params, from, to);     // 1.11.6 : les valeurs seulement
            changed = changed || !(before == a);
        }
        if (changed) note(label);
    };
    actions(v->actions, v->name + " / actions de la vue");
    for (auto& o : v->objects) {
        bool changed = false;
        for (auto& pr : o.props) {
            const Prop before = pr;
            pr.expr = renameRoot(pr.expr, from, to);
            if (pr.key == "variable" || pr.key == "target" || pr.key == "var") pr.value = renameRoot(pr.value, from, to);
            else if (pr.value.find('{') != std::string::npos) pr.value = inTemplate(pr.value);
            changed = changed || !(before == pr);
        }
        if (changed) note(v->name + " / " + o.name);
        actions(o.actions, v->name + " / " + o.name + " (actions)");
    }
    for (auto& sc : v->scripts) {
        if (sc.lang != ScriptLang::ST) continue;
        const std::string next = renameRoot(sc.body, from, to);
        if (next == sc.body) continue;
        sc.body = next;
        note(v->name + " / script " + (sc.name.empty() ? sc.event : sc.name));
    }
    // Les arguments donnes par ceux qui ouvrent cette vue.
    const std::string target = v->name;
    for (auto& other : p.views) {
        const auto args = [&](std::vector<Action>& list, const std::string& label) {
            bool changed = false;
            for (auto& a : list) {
                if (!operationTakesArguments(a.operation) || !same(trim(a.target), target)) continue;
                const std::string next = renameArgument(a.value, from, to);
                if (next != a.value) { a.value = next; changed = true; }
            }
            if (changed) note(label);
        };
        args(other.actions, other.name + " / actions de la vue");
        for (auto& o : other.objects) args(o.actions, other.name + " / " + o.name);
    }
    // 1.11.10 : un symbole - l'argument nomme de chaque instance suit (les positionnels
    // gardent leur rang : rien a faire).
    if (isSymbolView(*v)) {
        for (auto& other : p.views)
            for (auto& o : other.objects) {
                if (o.kind != Kind::SymbolInstance || !same(trim(o.text("symbol")), target)) continue;
                const std::string before = o.text("params");
                const std::string next = renameArgument(before, from, to);
                if (next == before) continue;
                o.set("params", next);
                note(other.name + " / " + o.name);
            }
    }
    return count;
}

// ---- 1.11.10 : supprimer, deplacer un parametre - les instances et les appelants suivent ----
namespace {
// Les morceaux de premier niveau d'un texte d'arguments : coupes aux ';' hors des
// chaines, des crochets et des parentheses, sans les blancs autour.
std::vector<std::string> argumentSegments(std::string_view text) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    char quote = 0;
    for (const char c : text) {
        if (quote) {
            cur += c;
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; cur += c; continue; }
        if (c == '[' || c == '(') ++depth;
        else if ((c == ']' || c == ')') && depth > 0) --depth;
        else if (c == ';' && depth == 0) { out.push_back(trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    out.push_back(trim(cur));
    return out;
}
// Le nom d'un morceau nomme ("Four := F1", "Four = F1") ; vide : positionnel ou autre.
std::string segmentName(std::string_view seg) {
    std::size_t i = 0;
    if (seg.empty() || !identStart(seg[0])) return {};
    while (i < seg.size() && identChar(seg[i])) ++i;
    std::size_t k = i;
    while (k < seg.size() && std::isspace(static_cast<unsigned char>(seg[k]))) ++k;
    const bool assign = k < seg.size() && (seg[k] == '=' || (seg[k] == ':' && k + 1 < seg.size() && seg[k + 1] == '='));
    return assign ? std::string(seg.substr(0, i)) : std::string{};
}
// Chaque instance du symbole `symbol`, dans toutes les vues (les symboles compris).
template <class F>
void forEachInstance(Project& p, std::string_view symbol, F&& f) {
    for (auto& other : p.views)
        for (auto& o : other.objects)
            if (o.kind == Kind::SymbolInstance && same(trim(o.text("symbol")), symbol)) f(other, o);
}
// Les instances en positionnels passent en nommes (le sens d'aujourd'hui garde).
void nameInstanceArguments(Project& p, const View& symbol, const std::function<void(std::string)>& note) {
    forEachInstance(p, symbol.name, [&](View& owner, Object& o) {
        const std::string before = o.text("params");
        if (trim(before).empty()) return;
        const std::string next = hmi::withArgument(symbol, before, symbol.params.size(), {});
        if (next == before) return;
        o.set("params", next);
        note(owner.name + " / " + o.name);
    });
}
} // namespace

std::string withoutArgument(std::string_view text, std::string_view name) {
    std::string out;
    for (const auto& seg : argumentSegments(text)) {
        if (seg.empty() || same(segmentName(seg), name)) continue;
        out += (out.empty() ? "" : "; ") + seg;
    }
    return out;
}

bool removeParam(Project& p, std::string_view viewName, std::size_t index, std::vector<std::string>* where) {
    View* v = p.viewByName(viewName);
    if (!v || index >= v->params.size()) return false;
    const auto note = [&](std::string w) { if (where) where->push_back(std::move(w)); };
    const std::string name = v->params[index].name, target = v->name;
    if (isSymbolView(*v)) {
        nameInstanceArguments(p, *v, note);
        forEachInstance(p, target, [&](View& owner, Object& o) {
            const std::string before = o.text("params");
            const std::string next = withoutArgument(before, name);
            if (next == before) return;
            o.set("params", next);
            note(owner.name + " / " + o.name);
        });
    }
    // Les actions qui ouvrent la vue (Ouvrir une popup, Changer de popup, Naviguer).
    for (auto& other : p.views) {
        const auto args = [&](std::vector<Action>& list, const std::string& label) {
            bool changed = false;
            for (auto& a : list) {
                if (!operationTakesArguments(a.operation) || !same(trim(a.target), target)) continue;
                const std::string next = withoutArgument(a.value, name);
                if (next != a.value) { a.value = next; changed = true; }
            }
            if (changed) note(label);
        };
        args(other.actions, other.name + " / actions de la vue");
        for (auto& o : other.objects) args(o.actions, other.name + " / " + o.name);
    }
    v = p.viewByName(viewName);
    v->params.erase(v->params.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool moveParam(Project& p, std::string_view viewName, std::size_t index, std::size_t to, std::vector<std::string>* where) {
    View* v = p.viewByName(viewName);
    if (!v || index >= v->params.size() || to >= v->params.size() || to == index) return false;
    const auto note = [&](std::string w) { if (where) where->push_back(std::move(w)); };
    if (isSymbolView(*v)) nameInstanceArguments(p, *v, note);
    v = p.viewByName(viewName);
    ViewParam moved = std::move(v->params[index]);
    v->params.erase(v->params.begin() + static_cast<std::ptrdiff_t>(index));
    v->params.insert(v->params.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    return true;
}

namespace {
// Le texte ecrit-il `name` (une affectation "name... :=") ?
bool assigns(std::string_view source, std::string_view name) {
    const std::string stripped = markers::strip(source);   // 1.11.1 (REP) : $Vit$ := ... ecrit Vit
    const std::string_view code = stripped;
    bool found = false;
    forEachRoot(code, [&](std::size_t a, std::size_t b) {
        if (found || !same(code.substr(a, b - a), name)) return;
        // le chemin (membres, indices), puis ":="
        std::size_t i = b;
        int depth = 0;
        while (i < code.size()) {
            const char c = code[i];
            if (c == '[') ++depth;
            else if (c == ']') --depth;
            else if (depth == 0 && !(c == '.' || identChar(c) || std::isspace(static_cast<unsigned char>(c)))) break;
            ++i;
        }
        if (i + 1 < code.size() && code[i] == ':' && code[i + 1] == '=') found = true;
    });
    return found;
}
} // namespace

bool popupWritesParam(const View& popup, std::string_view param) {
    const auto actionWrites = [&](const Action& a) {
        if (operationWritesVariable(a.operation) && mentionsName(a.target, param)) {
            // la racine de la cible
            std::size_t r = 0;
            const std::string t = trim(markers::strip(a.target));   // 1.11.1 (REP) : $Vit$ -> Vit
            while (r < t.size() && identChar(t[r])) ++r;
            if (same(t.substr(0, r), param)) return true;
        }
        return a.operation == Operation::RunScript && assigns(a.value, param);
    };
    for (const auto& a : popup.actions) if (actionWrites(a)) return true;
    for (const auto& s : popup.scripts) if (assigns(s.body, param)) return true;
    for (const auto& o : popup.objects) {
        for (const auto& a : o.actions) if (actionWrites(a)) return true;
        // un champ de saisie (ou une commande) relie : sa propriete "variable"
        for (const auto& pr : o.props)
            if ((pr.key == "variable" || pr.key == "target" || pr.key == "var") && !pr.value.empty()) {
                std::size_t r = 0;
                const std::string t = trim(pr.value);
                while (r < t.size() && identChar(t[r])) ++r;
                if (same(t.substr(0, r), param)) return true;
            }
    }
    return false;
}

std::vector<ArgumentProblem> checkArguments(const Project& p, const View* caller, const View& popup,
                                            std::string_view arguments, const PlcTypes& plc) {
    std::vector<ArgumentProblem> out;
    const auto given = parseArguments(arguments);
    for (const auto& [name, text] : given)
        if (!popup.param(name))
            out.push_back({ArgumentProblem::Kind::UnknownParam, true, name,
                           "la popup " + popup.name + " n'a pas de param\xC3\xA8tre " + name
                               + " : retire-le ou d\xC3\xA9" "clare-le dans ses param\xC3\xA8tres"});
    for (const auto& prm : popup.params) {
        const auto it = std::find_if(given.begin(), given.end(), [&](const auto& g) { return same(g.first, prm.name); });
        if (it == given.end() || trim(it->second).empty()) {
            if (trim(prm.defaultValue).empty())
                out.push_back({ArgumentProblem::Kind::Missing, true, prm.name,
                               "le param\xC3\xA8tre " + prm.name + " de " + popup.name
                                   + " n'est pas donn\xC3\xA9 et n'a pas de valeur par d\xC3\xA9" "faut : donne-le"});
            continue;
        }
        const std::string arg = trim(markers::strip(it->second));   // 1.11.7 : IN_V := $V[0]$ passe V[0] (une reference)
        const std::string given_t = expressionType(p, caller, arg, plc);
        const bool variable = isVariablePath(arg);
        if (!typeAcceptsFor(prm.mode, prm.type, given_t, variable)) {
            // Refuse en Reference / Les deux mais accepte en Copie : dire pourquoi.
            const bool widening = typeAccepts(prm.type, given_t);
            out.push_back({ArgumentProblem::Kind::TypeMismatch, true, prm.name,
                           "le param\xC3\xA8tre " + prm.name + " attend " + normalizedType(prm.type) + ", " + arg
                               + " est " + normalizedType(given_t)
                               + (widening ? " : en " + std::string(paramModeLabel(prm.mode))
                                                 + ", la variable de l'appelant est \xC3\xA9" "crite et doit avoir exactement ce type "
                                                   "(donne une variable " + normalizedType(prm.type) + ", ou passe le param\xC3\xA8tre en Copie)"
                                           : std::string(" : donne une valeur du bon type"))});
        }
        if (prm.mode != ParamMode::Copy && !isVariablePath(arg) && popupWritesParam(popup, prm.name))
            out.push_back({ArgumentProblem::Kind::NotWritable, false, prm.name,
                           "le param\xC3\xA8tre " + prm.name + " (" + std::string(paramModeLabel(prm.mode))
                               + ") est \xC3\xA9" "crit par la popup, mais " + arg
                               + " n'est pas une variable : on ne pourra pas l'\xC3\xA9" "crire"});
    }
    return out;
}

// -------------------------------------------------------- aide a la saisie -----
std::vector<Suggestion> paramSuggestions(const Project& p, const View& v, std::string_view typed, const PlcTypes& plc) {
    std::vector<Suggestion> out;
    const std::string t = trim(typed);
    const auto starts = [](std::string_view s, std::string_view prefix) { return up(s).rfind(up(prefix), 0) == 0; };
    const auto dot = t.rfind('.');
    if (dot == std::string::npos) {
        for (const auto& prm : v.params) {
            if (!starts(prm.name, t)) continue;
            std::string detail = "param\xC3\xA8tre \xC2\xB7 " + std::string(paramModeBadge(prm.mode));
            if (!prm.description.empty()) detail += " \xC2\xB7 " + prm.description;
            out.push_back({prm.name, prm.type, detail, 0});
        }
        return out;
    }
    // "Moteur.Vi" : le type du chemin avant le dernier point, puis ses membres.
    const std::string head = t.substr(0, dot);
    const std::string prefix = t.substr(dot + 1);
    std::size_t r = 0;
    while (r < head.size() && identChar(head[r])) ++r;
    const auto* prm = v.param(head.substr(0, r));
    if (!prm) return out;
    std::string type;
    if (r == head.size()) type = prm->type;
    else {
        // un membre de membre : le meme calcul que pour un argument, depuis le parametre
        type = expressionType(p, &v, head, plc);
    }
    if (trim(type).empty()) return out;
    for (const auto& [name, mt] : typeMembers(p, type, plc))
        if (starts(name, prefix)) out.push_back({name, mt, "membre de " + type, 0});
    return out;
}

std::vector<Suggestion> argumentSuggestions(const Project& p, const View* caller, const ViewParam& prm,
                                            const std::vector<std::string>& candidates, const PlcTypes& plc) {
    std::vector<Suggestion> out;
    const std::string want = normalizedType(prm.type);
    for (const auto& c : candidates) {
        const std::string type = expressionType(p, caller, c, plc);
        int rank = 2;
        if (!type.empty()) {
            if (normalizedType(type) == want) rank = 0;
            else if (typeAcceptsFor(prm.mode, prm.type, type, isVariablePath(c))) rank = want == "ANY" ? 0 : 1;
            else rank = 3;
        }
        out.push_back({c, type, type.empty() ? std::string("type inconnu") : type, rank});
    }
    std::stable_sort(out.begin(), out.end(), [](const Suggestion& a, const Suggestion& b) { return a.rank < b.rank; });
    return out;
}

// ------------------------------------------------------------------ infos -----
std::vector<Opener> popupOpeners(const Project& p, const View& popup) {
    std::vector<Opener> out;
    const auto look = [&](const View& v, const Object* o, const Action& a) {
        if (a.operation != Operation::Popup && a.operation != Operation::ChangePopup) return;
        if (!same(trim(a.target), popup.name)) return;
        out.push_back({v.name, o ? o->name : std::string{}, std::string(triggerLabel(a.trigger)),
                       std::string(operationLabel(a.operation)), a.value});
    };
    for (const auto& v : p.views) {
        for (const auto& a : v.actions) look(v, nullptr, a);
        for (const auto& o : v.objects)
            for (const auto& a : o.actions) look(v, &o, a);
    }
    return out;
}

std::vector<ParamUse> paramUses(const View& v, std::string_view param) {
    std::vector<ParamUse> out;
    const auto action = [&](const Object* o, const Action& a) {
        const std::string where = "action " + std::string(triggerLabel(a.trigger)) + " \xE2\x86\x92 " + std::string(operationLabel(a.operation));
        if (mentionsName(a.target, param) || mentionsName(a.value, param) || mentionsName(a.guard, param)
            || mentionsName(a.watch, param) || mentionsName(a.params, param))
            out.push_back({o ? o->name : std::string{}, where, describeAction(a)});
        else if (a.operation == Operation::ApplyCopy && same(trim(a.target), param))
            out.push_back({o ? o->name : std::string{}, where, describeAction(a)});
    };
    for (const auto& a : v.actions) action(nullptr, a);
    for (const auto& o : v.objects) {
        for (const auto& pr : o.props) {
            const bool inExpr = mentionsName(pr.expr, param);
            // un texte a trous : "{Moteur.Vitesse} tr/min"
            bool inTemplate = false;
            if (!inExpr && pr.value.find('{') != std::string::npos) {
                std::size_t i = 0;
                while ((i = pr.value.find('{', i)) != std::string::npos) {
                    const auto end = pr.value.find('}', i);
                    if (end == std::string::npos) break;
                    if (mentionsName(std::string_view(pr.value).substr(i + 1, end - i - 1), param)) { inTemplate = true; break; }
                    i = end + 1;
                }
            }
            if (inExpr) out.push_back({o.name, "propri\xC3\xA9t\xC3\xA9 " + pr.key, pr.expr});
            else if (inTemplate) out.push_back({o.name, "texte " + pr.key, pr.value});
        }
        for (const auto& a : o.actions) action(&o, a);
    }
    for (const auto& s : v.scripts)
        if (mentionsName(s.body, param)) {
            // la premiere ligne qui l'emploie
            std::size_t start = 0;
            std::string line;
            while (start <= s.body.size()) {
                const auto end = s.body.find('\n', start);
                const std::string_view l = std::string_view(s.body).substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (mentionsName(l, param)) { line = trim(l); break; }
                if (end == std::string::npos) break;
                start = end + 1;
            }
            out.push_back({{}, "script " + (s.name.empty() ? s.event : s.name), line});
        }
    return out;
}

} // namespace hmi::params
