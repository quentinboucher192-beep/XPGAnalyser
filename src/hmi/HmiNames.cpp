// =============================================================================
//  hmi/HmiNames.cpp - 1.13.0 (lot J8) : les espaces de noms (voir HmiNames.hpp)
// =============================================================================
#include "HmiNames.hpp"
#include "HmiScript.hpp"   // splitDeclarations, forEachCode
#include "HmiExpr.hpp"     // isStandardFunction

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>

namespace hmi::names {

namespace {

unsigned char uc(char c) { return static_cast<unsigned char>(c); }
bool identStart(char c) { return std::isalpha(uc(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(uc(c)) || c == '_'; }

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(uc(c)));
    return out;
}
bool same(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(uc(a[i])) != std::toupper(uc(b[i]))) return false;
    return true;
}
std::string parentOf(std::string_view ns) {
    const auto dot = ns.rfind('.');
    return dot == std::string_view::npos ? std::string{} : std::string(ns.substr(0, dot));
}

// Une distance d'edition courte (le nom le plus proche, pour " veux-tu dire ").
std::size_t distance(std::string_view a, std::string_view b) {
    const std::string x = upperOf(a), y = upperOf(b);
    std::vector<std::size_t> prev(y.size() + 1), cur(y.size() + 1);
    for (std::size_t j = 0; j <= y.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= x.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= y.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (x[i - 1] == y[j - 1] ? 0u : 1u)});
        std::swap(prev, cur);
    }
    return prev[y.size()];
}
std::string closestOf(std::string_view name, const std::vector<std::string>& pool) {
    std::string best;
    std::size_t bestD = 0;
    const std::size_t limit = std::max<std::size_t>(2, name.size() / 3);
    for (const auto& c : pool) {
        const std::size_t d = distance(name, c);
        if (d == 0 || d > limit) continue;
        if (best.empty() || d < bestD) { best = c; bestD = d; }
    }
    return best;
}

// ---- le balayage du code : jetons utiles, chaines et commentaires sautes ----------------
struct Word {
    std::size_t at{0}, end{0};
    char prev{0};          // le dernier caractere significatif avant le mot (0 : aucun)
    std::string prevWord;  // le dernier mot avant (MAJ), si le caractere significatif est une lettre
};

// Visite chaque identifiant du code (hors chaines, commentaires, litteraux types T#5s, 16#FF).
// `f` rend la position ou reprendre (>= end du mot) : un remplacement saute ce qu'il a pris.
void scanWords(std::string_view s, const std::function<std::size_t(const Word&)>& f) {
    std::size_t i = 0;
    char prev = 0;
    std::string prevWord;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto e = s.find("*)", i + 2);
            i = e == std::string_view::npos ? s.size() : e + 2;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto e = s.find('\n', i);
            i = e == std::string_view::npos ? s.size() : e;
            continue;
        }
        if (c == '\'' || c == '"') {
            std::size_t j = i + 1;
            while (j < s.size() && s[j] != c) j += (s[j] == '$' && j + 1 < s.size()) ? 2 : 1;
            i = std::min(s.size(), j + 1);
            prev = c;
            prevWord.clear();
            continue;
        }
        if (std::isdigit(uc(c))) {
            std::size_t j = i;
            while (j < s.size() && (identChar(s[j]) || s[j] == '#' || (s[j] == '.' && j + 1 < s.size() && std::isdigit(uc(s[j + 1]))))) ++j;
            i = j;
            prev = '0';
            prevWord.clear();
            continue;
        }
        if (identStart(c)) {
            std::size_t j = i;
            while (j < s.size() && identChar(s[j])) ++j;
            if (j < s.size() && s[j] == '#') {                     // T#5s, T_MODE#Auto : un litteral type
                std::size_t m = j + 1;
                while (m < s.size() && (identChar(s[m]) || s[m] == '.' || s[m] == ':' || s[m] == '-' || s[m] == '+')) ++m;
                i = m;
                prev = '0';
                prevWord.clear();
                continue;
            }
            Word w{i, j, prev, prev == 'a' ? prevWord : std::string{}};
            const std::size_t next = f(w);
            prevWord = upperOf(s.substr(i, j - i));
            prev = 'a';
            if (next > j) {
                // un remplacement a pris plus que le mot (une directive USING, un chemin) : son dernier caractere
                std::size_t k = std::min(next, s.size());
                while (k > j && std::isspace(uc(s[k - 1]))) --k;
                if (k > j && !identChar(s[k - 1])) {
                    prev = s[k - 1];
                    prevWord.clear();
                }
            }
            i = std::max(next, j);
            continue;
        }
        if (!std::isspace(uc(c))) {
            prev = c;
            prevWord.clear();
        }
        ++i;
    }
}

// La chaine d'identifiants qui part de `at` : Gaz . Ligne . T_X (les espaces autour des points toleres).
// Rend les segments et, pour chacun, sa fin dans le texte.
struct Chain {
    std::vector<std::string> segs;
    std::vector<std::size_t> ends;
};
Chain chainAt(std::string_view s, std::size_t at, std::size_t end) {
    Chain c;
    c.segs.emplace_back(s.substr(at, end - at));
    c.ends.push_back(end);
    std::size_t i = end;
    for (;;) {
        std::size_t k = i;
        while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
        if (k >= s.size() || s[k] != '.' || (k + 1 < s.size() && s[k + 1] == '.')) break;
        ++k;
        while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
        if (k >= s.size() || !identStart(s[k])) break;
        std::size_t e = k;
        while (e < s.size() && identChar(s[e])) ++e;
        c.segs.emplace_back(s.substr(k, e - k));
        c.ends.push_back(e);
        i = e;
    }
    return c;
}

// Le mot est-il le nom d'un argument d'appel (f(a := 1), f(x => y)) - et pas une affectation ?
bool namedArgument(std::string_view s, std::size_t end, int depth) {
    std::size_t k = end;
    while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
    if (k + 1 < s.size() && s[k] == '=' && s[k + 1] == '>') return true;
    return depth > 0 && k + 1 < s.size() && s[k] == ':' && s[k + 1] == '=';
}

// La profondeur des parentheses ouvertes avant `at` (hors chaines et commentaires) : recalculee
// a chaque mot - les codes sont courts.
int depthAt(std::string_view s, std::size_t at) {
    int depth = 0;
    std::size_t i = 0;
    while (i < at && i < s.size()) {
        const char c = s[i];
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto e = s.find("*)", i + 2);
            i = e == std::string_view::npos ? s.size() : e + 2;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            const auto e = s.find('\n', i);
            i = e == std::string_view::npos ? s.size() : e;
            continue;
        }
        if (c == '\'' || c == '"') {
            std::size_t j = i + 1;
            while (j < s.size() && s[j] != c) j += (s[j] == '$' && j + 1 < s.size()) ? 2 : 1;
            i = std::min(s.size(), j + 1);
            continue;
        }
        if (c == '(') ++depth;
        else if (c == ')' && depth > 0) --depth;
        ++i;
    }
    return depth;
}

void lineColumn(std::string_view s, std::size_t at, int& line, int& column) {
    line = 1;
    std::size_t start = 0;
    for (std::size_t i = 0; i < at && i < s.size(); ++i)
        if (s[i] == '\n') { ++line; start = i + 1; }
    column = static_cast<int>(at - start) + 1;
}

} // namespace

// ------------------------------------------------------------------ les USING ---
bool mentionsUsing(std::string_view code) noexcept {
    for (std::size_t i = 0; i + 5 <= code.size(); ++i) {
        if (std::toupper(uc(code[i])) != 'U') continue;
        if (same(code.substr(i, 5), "USING") && (i == 0 || !identChar(code[i - 1])) && (i + 5 >= code.size() || !identChar(code[i + 5])))
            return true;
    }
    return false;
}

std::vector<Using> usings(std::string_view s) {
    std::vector<Using> out;
    if (!mentionsUsing(s)) return out;
    scanWords(s, [&](const Word& w) -> std::size_t {
        if (!same(s.substr(w.at, w.end - w.at), "USING")) return w.end;
        // la ou une instruction commence : rien avant, un ';', ou la fin d'un bloc END_VAR.
        if (!(w.prev == 0 || w.prev == ';' || (w.prev == 'a' && (w.prevWord == "END_VAR" || w.prevWord == "THEN" || w.prevWord == "DO"
                                                                  || w.prevWord == "ELSE"))))
            return w.end;
        std::size_t k = w.end;
        while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
        if (k >= s.size() || !identStart(s[k])) return w.end;
        const Chain c = chainAt(s, k, [&] { std::size_t e = k; while (e < s.size() && identChar(s[e])) ++e; return e; }());
        std::string ns;
        for (const auto& seg : c.segs) ns += (ns.empty() ? "" : ".") + seg;
        std::size_t e = c.ends.back();
        std::size_t m = e;
        while (m < s.size() && (s[m] == ' ' || s[m] == '\t')) ++m;
        if (m < s.size() && s[m] == ';') e = m + 1;
        else if (!(m >= s.size() || s[m] == '\n' || s[m] == '\r')) return w.end;   // USING Gaz suivi d'autre chose : pas une directive
        Using u;
        u.ns = ns;
        u.at = w.at;
        u.length = e - w.at;
        lineColumn(s, w.at, u.line, u.column);
        out.push_back(std::move(u));
        return e;
    });
    return out;
}

std::string blankUsings(std::string_view code) {
    if (!mentionsUsing(code)) return std::string(code);
    std::string out(code);
    for (const auto& u : usings(code))
        for (std::size_t i = u.at; i < u.at + u.length && i < out.size(); ++i)
            if (out[i] != '\n' && out[i] != '\r') out[i] = ' ';
    return out;
}

// --------------------------------------------------------------------- l'index ---
std::string_view itemKindLabel(ItemKind k) noexcept {
    switch (k) {
        case ItemKind::Type: return "le type";
        case ItemKind::Function: return "la fonction";
        case ItemKind::Script: return "le script";
        case ItemKind::None: break;
    }
    return "le nom";
}

Index::Index(const Project& p) : project_(&p) {
    for (const auto& ns : p.namespaces()) {
        spaces_[upperOf(ns)] = ns;
        spaceList_.push_back(ns);
    }
    const auto add = [&](const std::string& ns, const std::string& name, ItemKind k, const std::string& lib) {
        if (ns.empty()) {
            root_.insert(upperOf(name));
            return;
        }
        auto& slot = items_[upperOf(ns)][upperOf(name)];
        if (slot.kind == ItemKind::None) slot = Item{name, k, lib};
    };
    for (const auto& t : p.programs.types) add(t.ns, t.name, ItemKind::Type, {});
    for (const auto& f : p.programs.functions) add(f.ns, f.name, ItemKind::Function, {});
    for (const auto& s : p.programs.scripts) add(s.ns, s.name, ItemKind::Script, {});
    p.forEachLibraryType([&](const HmiType& t, const LoadedLibrary& l) { add(t.ns, t.name, ItemKind::Type, l.name); });
    p.forEachLibraryFunction([&](const HmiFunction& f, const LoadedLibrary& l) { add(f.ns, f.name, ItemKind::Function, l.name); });
    p.forEachLibraryScript([&](const Script& s, const LoadedLibrary& l) { add(s.ns, s.name, ItemKind::Script, l.name); });
}

bool Index::isNamespace(std::string_view ns) const { return spaces_.count(upperOf(ns)) != 0; }

std::string Index::spelled(std::string_view ns) const {
    const auto it = spaces_.find(upperOf(ns));
    return it == spaces_.end() ? std::string(ns) : it->second;
}

ItemKind Index::kindIn(std::string_view ns, std::string_view name) const {
    const auto it = items_.find(upperOf(ns));
    if (it == items_.end()) return ItemKind::None;
    const auto jt = it->second.find(upperOf(name));
    return jt == it->second.end() ? ItemKind::None : jt->second.kind;
}

std::vector<std::string> Index::itemsOf(std::string_view ns) const {
    std::vector<std::string> out;
    const auto it = items_.find(upperOf(ns));
    if (it != items_.end())
        for (const auto& [k, item] : it->second) out.push_back(item.name);
    return out;
}

std::vector<std::string> Index::childSpaces(std::string_view ns) const {
    std::vector<std::string> out;
    const std::string pre = upperOf(ns) + ".";
    for (const auto& [k, spelledNs] : spaces_)
        if (k.size() > pre.size() && k.compare(0, pre.size(), pre) == 0 && k.find('.', pre.size()) == std::string::npos)
            out.push_back(spelledNs.substr(pre.size()));
    return out;
}

bool Index::rootHas(std::string_view name) const { return root_.count(upperOf(name)) != 0; }

std::vector<std::string> Index::spacesDefining(std::string_view name) const {
    std::vector<std::string> out;
    const std::string u = upperOf(name);
    for (const auto& [k, items] : items_)
        if (items.count(u)) out.push_back(spelled(k));
    return out;
}

std::string Index::libraryOf(std::string_view full) const {
    const auto [ns, name] = splitQualified(full);
    const auto it = items_.find(upperOf(ns));
    if (it == items_.end()) return {};
    const auto jt = it->second.find(upperOf(name));
    return jt == it->second.end() ? std::string{} : jt->second.library;
}

std::size_t qualifiedPrefix(const Index& ix, const std::vector<std::string>& segs, std::string* full, ItemKind* kind) {
    if (segs.size() < 2 || ix.empty()) return 0;
    for (std::size_t n = segs.size() - 1; n >= 1; --n) {
        std::string ns;
        for (std::size_t k = 0; k < n; ++k) ns += (k ? "." : "") + segs[k];
        if (!ix.isNamespace(ns)) continue;
        const ItemKind k = ix.kindIn(ns, segs[n]);
        if (k == ItemKind::None) continue;
        if (full) *full = qualifiedName(ix.spelled(ns), segs[n]);
        if (kind) *kind = k;
        return n + 1;
    }
    return 0;
}

// ------------------------------------------------------------------ un nom court ---
Resolved resolveBare(const Index& ix, const Scope& scope, std::string_view name) {
    Resolved r;
    if (ix.empty() || name.empty()) return r;
    const std::string u = upperOf(name);
    if (scope.hidden.count(u)) return r;
    // 1) son espace, puis ses parents (Gaz.Ligne voit Gaz).
    for (std::string o = scope.own; !o.empty(); o = parentOf(o))
        if (const ItemKind k = ix.kindIn(o, name); k != ItemKind::None) {
            r.full = qualifiedName(ix.spelled(o), name);
            r.kind = k;
            return r;
        }
    // 2) la racine : un item sans espace, une variable IHM.
    if (ix.rootHas(name) || ix.project().variable(name)) return r;
    // 3) les espaces importes.
    std::vector<std::string> hits;
    std::string via;
    ItemKind kind = ItemKind::None;
    for (const auto& imp : scope.imports) {
        const ItemKind k = ix.kindIn(imp, name);
        if (k == ItemKind::None) continue;
        const std::string full = qualifiedName(ix.spelled(imp), name);
        if (std::none_of(hits.begin(), hits.end(), [&](const std::string& h) { return same(h, full); })) {
            hits.push_back(full);
            if (via.empty()) { via = ix.spelled(imp); kind = k; }
        }
    }
    if (hits.size() == 1) {
        r.full = hits.front();
        r.kind = kind;
        r.via = via;
        return r;
    }
    if (hits.size() > 1) {
        r.ambiguous = std::move(hits);
        return r;
    }
    // 4) un autre espace le definit : le nom complet a ecrire.
    const auto defining = ix.spacesDefining(name);
    if (!defining.empty()) {
        r.hint = qualifiedName(defining.front(), name);
        r.kind = ix.kindIn(defining.front(), name);
        if (defining.size() > 1)
            for (const auto& d : defining) r.ambiguous.push_back(qualifiedName(d, name));
    }
    return r;
}

std::set<std::string> declaredNames(std::string_view code, bool function) {
    std::set<std::string> out;
    const auto parts = splitDeclarations(code, function, [](std::string_view) { return true; });
    for (const auto& l : parts.locals) out.insert(upperOf(l.name));
    return out;
}

bool active(const Project& p) noexcept {
    if (!p.libraries.empty()) return true;
    const auto any = [](const auto& v) { return std::any_of(v.begin(), v.end(), [](const auto& x) { return !x.ns.empty(); }); };
    if (any(p.programs.types) || any(p.programs.functions) || any(p.programs.scripts)) return true;
    const auto it = p.listFolders.find("espaces");
    return it != p.listFolders.end() && !it->second.empty();
}

std::string canonicalType(const Project& p, std::string_view type, std::string_view own) {
    if (type.empty() || !active(p)) return std::string(type);
    std::string out;
    out.reserve(type.size() + 8);
    std::size_t i = 0;
    const Index* ix = nullptr;
    std::optional<Index> made;
    while (i < type.size()) {
        if (!identStart(type[i]) || (i > 0 && (identChar(type[i - 1]) || type[i - 1] == '.' || type[i - 1] == '#'))) { out += type[i++]; continue; }
        std::size_t j = i;
        while (j < type.size() && (identChar(type[j]) || (type[j] == '.' && j + 1 < type.size() && identStart(type[j + 1])))) ++j;
        const std::string word(type.substr(i, j - i));
        i = j;
        // Un mot suivi de " : " nomme le parametre d'un rappel (CALLBACK (t : T) : BOOL) : tel quel.
        std::size_t c = j;
        while (c < type.size() && type[c] == ' ') ++c;
        if (c < type.size() && type[c] == ':' && (c + 1 >= type.size() || type[c + 1] != '=')) { out += word; continue; }
        if (word.find('.') != std::string::npos) {               // deja complet : son ecriture du projet
            const HmiType* t = p.hmiTypeByName(word);
            out += t ? fullName(*t) : word;
            continue;
        }
        if (!own.empty()) {
            if (!ix) { made.emplace(p); ix = &*made; }
            Scope scope;
            scope.own = std::string(own);
            const Resolved r = resolveBare(*ix, scope, word);
            if (!r.full.empty() && r.kind == ItemKind::Type) { out += r.full; continue; }
        }
        const HmiType* t = p.hmiTypeByName(word);
        out += t && !t->ns.empty() ? fullName(*t) : word;
    }
    return out;
}

// --------------------------------------------------------------- la reecriture ---
std::string qualify(const Project& p, std::string_view code, std::string_view own, const std::vector<std::string>& hidden, bool function) {
    if (!active(p)) return blankUsings(code);
    const Index ix(p);
    return qualify(ix, code, own, hidden, function);
}

std::string qualify(const Index& ix, std::string_view code, std::string_view own, const std::vector<std::string>& hidden, bool function) {
    const auto us = usings(code);
    const std::string text = blankUsings(code);
    if (ix.empty()) return text;
    Scope scope;
    scope.own = std::string(own);
    for (const auto& u : us) scope.imports.push_back(u.ns);
    scope.hidden = declaredNames(text, function);
    for (const auto& h : hidden) scope.hidden.insert(upperOf(h));
    if (scope.own.empty() && scope.imports.empty()) return text;   // rien a qualifier : la racine voit la racine
    std::string out;
    out.reserve(text.size() + 32);
    std::size_t copied = 0;
    scanWords(text, [&](const Word& w) -> std::size_t {
        if (w.prev == '.') return w.end;
        const std::string_view word = std::string_view(text).substr(w.at, w.end - w.at);
        // A.B : un espace en tete - deja un nom complet (ou un membre) : on saute la chaine.
        const Chain c = chainAt(text, w.at, w.end);
        if (c.segs.size() > 1 && ix.isNamespace(c.segs.front())) return c.ends.back();
        if (namedArgument(text, w.end, depthAt(text, w.at))) return w.end;
        const Resolved r = resolveBare(ix, scope, word);
        if (r.full.empty() || same(r.full, word)) return w.end;
        out.append(text, copied, w.at - copied);
        out += r.full;
        copied = w.end;
        return w.end;
    });
    out.append(text, copied, std::string::npos);
    return out;
}

// ------------------------------------------------------------------ les constats ---
std::vector<Finding> check(const Index& ix, std::string_view code, std::string_view own, const std::vector<std::string>& hidden,
                           bool function, bool allowUsing, const std::function<bool(std::string_view)>& knownElsewhere) {
    std::vector<Finding> out;
    const auto us = allowUsing ? usings(code) : std::vector<Using>{};
    if (ix.empty() && us.empty()) return out;
    const std::string text = blankUsings(code);
    Scope scope;
    scope.own = std::string(own);
    scope.hidden = declaredNames(text, function);
    for (const auto& h : hidden) scope.hidden.insert(upperOf(h));
    std::vector<bool> used(us.size(), false);
    for (std::size_t k = 0; k < us.size(); ++k) {
        const auto& u = us[k];
        if (!ix.isNamespace(u.ns)) {
            Finding f;
            f.line = u.line;
            f.column = u.column;
            f.length = static_cast<int>(u.length);
            f.name = u.ns;
            f.suggestion = closestOf(u.ns, ix.spaces());
            f.message = "USING " + u.ns + " : aucun espace de noms " + u.ns
                      + (f.suggestion.empty() ? std::string(" (ni dans le projet, ni dans une biblioth\xC3\xA8que reli\xC3\xA9" "e)")
                                              : " : veux-tu dire " + f.suggestion + " ?");
            if (!f.suggestion.empty()) {
                f.fixFrom = std::string(code.substr(u.at, u.length));
                f.fixTo = "USING " + f.suggestion + ";";
            }
            out.push_back(std::move(f));
            used[k] = true;          // dit une fois
            continue;
        }
        scope.imports.push_back(u.ns);
    }
    scanWords(text, [&](const Word& w) -> std::size_t {
        if (w.prev == '.') return w.end;
        const std::string_view word = std::string_view(text).substr(w.at, w.end - w.at);
        const Chain c = chainAt(text, w.at, w.end);
        // Gaz.X : l'espace doit avoir X (sinon le plus proche de l'espace).
        if (c.segs.size() > 1 && ix.isNamespace(c.segs.front()) && !scope.hidden.count(upperOf(c.segs.front()))) {
            std::string full;
            if (qualifiedPrefix(ix, c.segs, &full) == 0) {
                // le plus long prefixe espace : celui qui manque son item
                std::size_t n = 1;
                std::string ns = c.segs.front();
                while (n + 1 < c.segs.size() && ix.isNamespace(ns + "." + c.segs[n])) ns += "." + c.segs[n++];
                const std::string missing = c.segs[n];
                std::vector<std::string> pool = ix.itemsOf(ns);
                for (const auto& sub : ix.childSpaces(ns)) pool.push_back(sub);
                Finding f;
                lineColumn(text, w.at, f.line, f.column);
                f.length = static_cast<int>(c.ends[n] - w.at);
                f.name = ix.spelled(ns) + "." + missing;
                f.suggestion = closestOf(missing, pool);
                f.message = ix.spelled(ns) + " est un espace de noms : il n'a pas de " + missing
                          + (f.suggestion.empty() ? std::string(" (ni type, ni fonction, ni script)")
                                                  : " : veux-tu dire " + ix.spelled(ns) + "." + f.suggestion + " ?");
                if (!f.suggestion.empty()) {
                    f.fixFrom = std::string(text.substr(w.at, c.ends[n] - w.at));
                    f.fixTo = ix.spelled(ns) + "." + f.suggestion;
                }
                out.push_back(std::move(f));
            }
            return c.ends.back();
        }
        if (namedArgument(text, w.end, depthAt(text, w.at))) return w.end;
        const Resolved r = resolveBare(ix, scope, word);
        if (!r.via.empty())
            for (std::size_t k = 0; k < us.size(); ++k)
                if (same(us[k].ns, r.via)) used[k] = true;
        if (r.full.empty() && r.hint.empty() && r.ambiguous.size() > 1) {
            // Ambigu sous USING : chaque espace importe le definit.
            for (const auto& a : r.ambiguous)
                for (std::size_t k = 0; k < us.size(); ++k)
                    if (same(us[k].ns, splitQualified(a).first)) used[k] = true;
            Finding f;
            lineColumn(text, w.at, f.line, f.column);
            f.length = static_cast<int>(w.end - w.at);
            f.name = std::string(word);
            f.suggestion = r.ambiguous.front();
            std::string list;
            for (std::size_t k = 0; k < r.ambiguous.size(); ++k)
                list += (k == 0 ? "" : k + 1 == r.ambiguous.size() ? " et " : ", ") + r.ambiguous[k];
            f.message = std::string(word) + " est ambigu entre " + list + " - qualifie-le (" + r.ambiguous.front() + ")";
            f.fixFrom = std::string(word);
            f.fixTo = r.ambiguous.front();
            out.push_back(std::move(f));
        } else if (r.full.empty() && !r.hint.empty() && knownElsewhere && !knownElsewhere(word)) {
            // Un nom d'un autre espace, ni le sien ni importe : on l'ecrit en entier (ou on importe l'espace).
            const std::string ns = splitQualified(r.hint).first;
            Finding f;
            lineColumn(text, w.at, f.line, f.column);
            f.length = static_cast<int>(w.end - w.at);
            f.name = std::string(word);
            f.suggestion = r.hint;
            if (r.ambiguous.size() > 1) {
                std::string list;
                for (std::size_t k = 0; k < r.ambiguous.size(); ++k)
                    list += (k == 0 ? "" : k + 1 == r.ambiguous.size() ? " et " : ", ") + r.ambiguous[k];
                f.message = std::string(word) + " n'existe pas ici : " + list + " existent - \xC3\xA9" "cris le nom complet";
            } else {
                f.message = std::string(word) + " est dans l'espace " + ns + " : \xC3\xA9" "cris " + r.hint
                          + (allowUsing ? " (ou mets USING " + ns + "; en t\xC3\xAAte)" : std::string{});
            }
            f.fixFrom = std::string(word);
            f.fixTo = r.hint;
            f.warning = r.ambiguous.size() <= 1;     // il tourne (le seul de ce nom) : un avertissement ; plusieurs : une faute
            out.push_back(std::move(f));
        }
        return w.end;
    });
    for (std::size_t k = 0; k < us.size(); ++k) {
        if (used[k]) continue;
        // Un USING qu'aucun nom court n'emploie : il ne sert pas (un avertissement).
        Finding f;
        f.warning = true;
        f.line = us[k].line;
        f.column = us[k].column;
        f.length = static_cast<int>(us[k].length);
        f.name = us[k].ns;
        f.message = "USING " + us[k].ns + " ne sert pas : aucun nom court de " + us[k].ns + " n'est employ\xC3\xA9 ici - retire-le";
        f.fixFrom = std::string(code.substr(us[k].at, us[k].length));
        f.fixTo = "";
        out.push_back(std::move(f));
    }
    return out;
}

// --------------------------------------------------------- la fusion des jetons ---
std::vector<Run> qualifiedRuns(const Index& ix, const std::vector<TokenView>& toks, const Scope* bare) {
    std::vector<Run> out;
    if (ix.empty()) return out;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        if (!toks[i].ident || (i > 0 && toks[i - 1].dot)) continue;
        std::vector<std::string> segs{std::string(toks[i].text)};
        std::size_t j = i;
        while (j + 2 < toks.size() && toks[j + 1].dot && toks[j + 2].ident) {
            segs.emplace_back(toks[j + 2].text);
            j += 2;
        }
        if (segs.size() > 1 && ix.isNamespace(segs.front())) {
            std::string full;
            const std::size_t n = qualifiedPrefix(ix, segs, &full);
            if (n >= 2) {
                out.push_back(Run{i, 2 * n - 1, full});
                i += 2 * n - 2;
            }
            continue;
        }
        if (!bare) continue;
        // un nom suivi de := dans une liste d'arguments, ou de => : le nom d'un argument
        if (i + 1 < toks.size() && (toks[i + 1].text == "=>")) continue;
        const Resolved r = resolveBare(ix, *bare, toks[i].text);
        if (!r.full.empty() && !same(r.full, toks[i].text)) out.push_back(Run{i, 1, r.full});
    }
    return out;
}

// --------------------------------------------------------------- le renommage ---
namespace {
// Le nom `seg` commence-t-il par l'espace `from` (Gaz, Gaz.Ligne) ? Rend le nombre de segments de `from`.
std::size_t prefixSegments(const std::vector<std::string>& segs, std::string_view from) {
    std::vector<std::string> fs;
    std::size_t start = 0;
    while (start <= from.size()) {
        const auto dot = from.find('.', start);
        fs.emplace_back(from.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    if (segs.size() <= fs.size()) return 0;
    for (std::size_t k = 0; k < fs.size(); ++k)
        if (!same(segs[k], fs[k])) return 0;
    return fs.size();
}
std::string renamedNs(std::string_view ns, std::string_view from, std::string_view to) {
    if (same(ns, from)) return std::string(to);
    if (ns.size() > from.size() && same(ns.substr(0, from.size()), from) && ns[from.size()] == '.')
        return std::string(to) + std::string(ns.substr(from.size()));
    return std::string(ns);
}
} // namespace

std::string renameInCode(std::string_view code, std::string_view from, std::string_view to) {
    if (from.empty() || to.empty() || same(from, to)) return std::string(code);
    std::string out;
    out.reserve(code.size() + 16);
    std::size_t copied = 0;
    const auto put = [&](std::size_t at, std::size_t end, const std::string& with) {
        out.append(code, copied, at - copied);
        out += with;
        copied = end;
    };
    // Les directives USING (a leur place), puis les noms - en un seul passage.
    const auto directives = usings(code);
    const std::string text(code);
    scanWords(text, [&](const Word& w) -> std::size_t {
        for (const auto& u : directives)
            if (u.at == w.at) {
                const std::string next = renamedNs(u.ns, from, to);
                if (next != u.ns) put(u.at, u.at + u.length, "USING " + next + ";");
                return u.at + u.length;
            }
        if (w.prev == '.') return w.end;
        const Chain c = chainAt(text, w.at, w.end);
        const std::size_t n = prefixSegments(c.segs, from);
        if (n == 0) return c.ends.back();
        std::string rest;
        for (std::size_t k = n; k < c.segs.size(); ++k) rest += "." + c.segs[k];
        put(w.at, c.ends.back(), std::string(to) + rest);
        return c.ends.back();
    });
    out.append(code, copied, std::string::npos);
    // Les chaines qui nomment un item de l'espace : IHM_APPELER('Gaz.Horloge').
    std::string fixed;
    fixed.reserve(out.size());
    for (std::size_t i = 0; i < out.size();) {
        if (out[i] != '\'' ) { fixed += out[i++]; continue; }
        std::size_t j = i + 1;
        while (j < out.size() && out[j] != '\'' && out[j] != '\n') ++j;
        const std::string inside = out.substr(i + 1, j - i - 1);
        if (j < out.size() && out[j] == '\'' && isNamespaceName(inside)) {
            const auto [ns, name] = splitQualified(inside);
            const std::string next = renamedNs(ns, from, to);
            fixed += "'" + (next == ns ? inside : qualifiedName(next, name)) + "'";
            i = j + 1;
            continue;
        }
        fixed.append(out, i, j - i);
        i = j;
    }
    return fixed;
}

std::string renameItemInCode(const Index& ix, std::string_view code, std::string_view own, std::string_view targetFull,
                             std::string_view newShort, bool bare) {
    const auto [targetNs, targetName] = splitQualified(targetFull);
    if (targetName.empty() || newShort.empty() || same(targetName, newShort)) return std::string(code);
    const auto us = usings(code);
    const std::string text = blankUsings(code);
    Scope scope;
    scope.own = std::string(own);
    for (const auto& u : us) scope.imports.push_back(u.ns);
    scope.hidden = declaredNames(text, true);
    // Un nom court vise-t-il la cible ? Son espace (ou un parent), la racine, un import, le seul de ce nom.
    const auto targets = [&](std::string_view word) {
        if (scope.hidden.count(upperOf(word))) return false;
        if (ix.empty()) return targetNs.empty();
        const Resolved r = resolveBare(ix, scope, word);
        if (!r.full.empty()) return same(r.full, targetFull);
        if (!r.ambiguous.empty() && r.hint.empty()) return false;               // ambigu sous USING : ni l'un ni l'autre
        if (ix.rootHas(word)) return targetNs.empty();                          // la racine le definit : c'est lui
        if (ix.project().variable(word)) return false;
        return !targetNs.empty() && r.ambiguous.size() <= 1 && same(r.hint, targetFull);   // le seul de ce nom, d'un espace
    };
    std::string out;
    out.reserve(code.size() + 16);
    std::size_t copied = 0;
    const auto put = [&](std::size_t at, std::size_t end) {
        out.append(code, copied, at - copied);
        out += newShort;
        copied = end;
    };
    scanWords(text, [&](const Word& w) -> std::size_t {
        if (w.prev == '.') return w.end;
        const std::string_view word = std::string_view(text).substr(w.at, w.end - w.at);
        const Chain c = chainAt(text, w.at, w.end);
        if (c.segs.size() > 1 && !ix.empty() && ix.isNamespace(c.segs.front()) && !scope.hidden.count(upperOf(c.segs.front()))) {
            // Util.Moyenne(...) : la forme qualifiee - le dernier segment de l'item.
            std::string full;
            const std::size_t n = qualifiedPrefix(ix, c.segs, &full);
            if (n >= 2 && same(full, targetFull)) {
                const std::size_t end = c.ends[n - 1];
                put(end - c.segs[n - 1].size(), end);
            }
            return c.ends.back();
        }
        if (!same(word, targetName)) return w.end;
        if (namedArgument(text, w.end, depthAt(text, w.at))) return w.end;
        if (!targets(word)) return w.end;
        if (!bare) {
            // un appel (le nom suivi de '('), ou le nom passe en valeur (LIST_FILTER(L, Est_Valide), f := Valider)
            std::size_t k = w.end;
            while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) ++k;
            const bool call = k < text.size() && text[k] == '(';
            bool value = false;
            if (!call) {
                std::size_t b = w.at;
                while (b > 0 && std::isspace(uc(text[b - 1]))) --b;
                const bool after = b > 0 && (text[b - 1] == '(' || text[b - 1] == ',' || (text[b - 1] == '=' && b > 1 && text[b - 2] == ':'));
                std::size_t e = k;
                while (e < text.size() && std::isspace(uc(text[e]))) ++e;
                value = after && (e >= text.size() || text[e] == ';' || text[e] == ')' || text[e] == ',');
            }
            if (!call && !value) return w.end;
        }
        put(w.at, w.end);
        return w.end;
    });
    out.append(code, copied, std::string::npos);
    return out;
}

std::string renameInType(std::string_view type, std::string_view from, std::string_view to) {
    if (from.empty() || to.empty() || same(from, to) || type.find('.') == std::string_view::npos) return std::string(type);
    std::string out;
    std::size_t i = 0;
    while (i < type.size()) {
        if (!identStart(type[i]) || (i > 0 && (identChar(type[i - 1]) || type[i - 1] == '.'))) { out += type[i++]; continue; }
        std::size_t j = i;
        while (j < type.size() && (identChar(type[j]) || (type[j] == '.' && j + 1 < type.size() && identStart(type[j + 1])))) ++j;
        const std::string word(type.substr(i, j - i));
        const auto [ns, name] = splitQualified(word);
        out += ns.empty() ? word : qualifiedName(renamedNs(ns, from, to), name);
        i = j;
    }
    return out;
}

std::size_t renameNamespace(Project& p, std::string_view from, std::string_view to) {
    if (from.empty() || to.empty() || same(from, to)) return 0;
    std::size_t changed = 0;
    const auto ns = [&](std::string& s) {
        const std::string next = renamedNs(s, from, to);
        if (next != s) { s = next; ++changed; }
    };
    const auto type = [&](std::string& s) {
        const std::string next = renameInType(s, from, to);
        if (next != s) { s = next; ++changed; }
    };
    for (auto& t : p.programs.types) {
        ns(t.ns);
        type(t.base);
        for (auto& i : t.interfaces) type(i);
        for (auto& m : t.members) type(m.type);
        for (auto& m : t.methods) {
            type(m.returnType);
            for (auto& d : m.decls) type(d.type);
        }
    }
    for (auto& f : p.programs.functions) {
        ns(f.ns);
        type(f.returnType);
    }
    for (auto& s : p.programs.scripts) ns(s.ns);
    for (auto& v : p.programs.variables) type(v.type);
    for (auto& v : p.views) {
        for (auto& prm : v.params) type(prm.type);
        for (auto& f : v.functions) type(f.returnType);
    }
    forEachDeclarations(p, [&](std::vector<Declaration>& list) {
        for (auto& d : list) type(d.type);
    });
    if (auto it = p.listFolders.find("espaces"); it != p.listFolders.end())
        for (auto& e : it->second) ns(e);
    forEachCode(p, [&](std::string& text, CodeForm, const CodeSite&) {
        if (text.empty()) return;
        const std::string next = renameInCode(text, from, to);
        if (next != text) { text = next; ++changed; }
    });
    return changed;
}

std::string reservedClash(const Project& p, std::string_view ns) {
    if (ns.empty()) return {};
    const std::string head(ns.substr(0, ns.find('.')));
    const std::string intro = "le nom " + head + " est \xC3\xA0 la fois un espace de noms et ";
    if (p.variable(head)) return intro + "une variable IHM";
    if (p.viewByName(head)) return intro + "une vue";
    for (const auto& t : p.programs.types) if (t.ns.empty() && same(t.name, head)) return intro + "un type IHM";
    for (const auto& f : p.programs.functions) if (f.ns.empty() && same(f.name, head)) return intro + "une fonction IHM";
    for (const auto& s : p.programs.scripts) if (s.ns.empty() && same(s.name, head)) return intro + "un script g\xC3\xA9n\xC3\xA9ral";
    return {};
}

bool validNewNamespace(const Project& p, std::string_view name, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (name.empty()) return fail("donne un nom d'espace (Gaz, Util, Gaz.Ligne)");
    if (!isNamespaceName(name)) return fail("un espace de noms s'\xC3\xA9" "crit en lettres, chiffres et _ (Gaz, Gaz.Ligne) : pas d'espace ni d'accent");
    if (p.isNamespace(name)) return fail("l'espace " + std::string(name) + " existe d\xC3\xA9j\xC3\xA0");
    if (const std::string clash = reservedClash(p, name); !clash.empty()) return fail(clash);
    if (isStandardFunction(name.substr(0, name.find('.')))) return fail(std::string(name.substr(0, name.find('.'))) + " est une fonction standard");
    return true;
}

bool setItemNamespace(Project& p, ItemKind kind, Id id, std::string_view nsIn, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    std::string ns(nsIn);
    while (!ns.empty() && std::isspace(uc(ns.front()))) ns.erase(ns.begin());
    while (!ns.empty() && std::isspace(uc(ns.back()))) ns.pop_back();
    if (ns == "(racine)") ns.clear();
    if (!ns.empty() && !isNamespaceName(ns))
        return fail("\xC2\xAB " + ns + " \xC2\xBB : un espace de noms s'\xC3\xA9" "crit en lettres, chiffres et _ (Gaz, Gaz.Ligne)");
    // Une bibliotheque reliee (et ses sous-espaces) est en lecture seule : on n'y ajoute rien.
    for (const auto& lib : p.libraries)
        if (!ns.empty() && (same(ns, lib.name) || (ns.size() > lib.name.size() && same(ns.substr(0, lib.name.size()), lib.name) && ns[lib.name.size()] == '.')))
            return fail("l'espace " + lib.name + " est une biblioth\xC3\xA8que reli\xC3\xA9" "e : elle est en lecture seule (choisis un autre espace)");
    if (!ns.empty())
        if (const std::string clash = reservedClash(p, ns); !clash.empty()) return fail(clash);
    std::string name;
    switch (kind) {
        case ItemKind::Type: if (const HmiType* t = p.hmiType(id); t && !isLibraryId(id)) name = t->name; break;
        case ItemKind::Function: if (const HmiFunction* f = p.function(id); f && !isLibraryId(id)) name = f->name; break;
        case ItemKind::Script: if (const Script* s = p.script(id); s && !isLibraryId(id) && p.viewOfScript(id) == kNoId) name = s->name; break;
        case ItemKind::None: break;
    }
    if (name.empty()) return fail("cet \xC3\xA9l\xC3\xA9ment ne prend pas d'espace de noms (une biblioth\xC3\xA8que, un script de vue)");
    const std::string full = qualifiedName(ns, name);
    // Le nom complet doit rester libre parmi les items du meme genre (une fonction : ses surcharges la suivent).
    const auto taken = [&](const auto& list) {
        return std::any_of(list.begin(), list.end(), [&](const auto& x) { return x.id != id && same(qualifiedName(x.ns, x.name), full); });
    };
    if (kind == ItemKind::Type && taken(p.programs.types)) return fail("le type " + full + " existe d\xC3\xA9j\xC3\xA0");
    if (kind == ItemKind::Script && taken(p.programs.scripts)) return fail("le script " + full + " existe d\xC3\xA9j\xC3\xA0");
    bool libraryClash = false;
    if (kind == ItemKind::Type) p.forEachLibraryType([&](const HmiType& t, const LoadedLibrary&) { libraryClash = libraryClash || same(fullName(t), full); });
    if (libraryClash) return fail(full + " existe d\xC3\xA9j\xC3\xA0 dans une biblioth\xC3\xA8que reli\xC3\xA9" "e");
    // Le nom complet d'avant (Gaz.EnDefaut) : le code qui l'ecrivait en entier suit (plus bas).
    std::string beforeFull;
    switch (kind) {
        case ItemKind::Type: if (const HmiType* t = p.hmiType(id)) beforeFull = fullName(*t); break;
        case ItemKind::Function: if (const HmiFunction* f = p.function(id)) beforeFull = qualifiedName(f->ns, f->name); break;
        case ItemKind::Script: if (const Script* s = p.script(id)) beforeFull = qualifiedName(s->ns, s->name); break;
        case ItemKind::None: break;
    }
    switch (kind) {
        case ItemKind::Type:
            if (HmiType* t = p.hmiType(id)) {
                // Les types qui le nommaient par son nom complet suivent (les variables, les membres, les declarations).
                const std::string before = fullName(*t);
                t->ns = ns;
                if (!same(before, full)) {
                    const auto follow = [&](std::string& text) {
                        std::string out;
                        std::size_t i = 0;
                        while (i < text.size()) {
                            if (!identStart(text[i]) || (i > 0 && (identChar(text[i - 1]) || text[i - 1] == '.'))) { out += text[i++]; continue; }
                            std::size_t j = i;
                            while (j < text.size() && (identChar(text[j]) || (text[j] == '.' && j + 1 < text.size() && identStart(text[j + 1])))) ++j;
                            const std::string word = text.substr(i, j - i);
                            out += same(word, before) ? full : word;
                            i = j;
                        }
                        text = out;
                    };
                    for (auto& v : p.programs.variables) follow(v.type);
                    for (auto& ty : p.programs.types) {
                        for (auto& m : ty.members) follow(m.type);
                        // 1.13.0 : le parent, les interfaces et les signatures des methodes qui le nommaient en entier.
                        follow(ty.base);
                        for (auto& i : ty.interfaces) follow(i);
                        for (auto& m : ty.methods) {
                            follow(m.returnType);
                            for (auto& d : m.decls) follow(d.type);
                        }
                    }
                    for (auto& f : p.programs.functions) follow(f.returnType);
                    // 1.13.0 : les parametres des popups et des symboles (le selecteur donne le nom complet), les retours de
                    // leurs fonctions.
                    for (auto& v : p.views) {
                        for (auto& prm : v.params) follow(prm.type);
                        for (auto& fn : v.functions) follow(fn.returnType);
                    }
                    forEachDeclarations(p, [&](std::vector<Declaration>& list) { for (auto& d : list) follow(d.type); });
                }
            }
            break;
        case ItemKind::Function:
            // Ses surcharges (meme nom, meme espace) la suivent : une fonction et ses surcharges restent ensemble.
            if (HmiFunction* f = p.function(id)) {
                const std::string oldNs = f->ns, fname = f->name;
                for (auto& g : p.programs.functions)
                    if (same(g.name, fname) && same(g.ns, oldNs)) g.ns = ns;
            }
            break;
        case ItemKind::Script:
            if (Script* s = p.script(id)) s->ns = ns;
            break;
        case ItemKind::None: break;
    }
    // Le code qui l'ecrivait par son nom complet suit : Gaz.EnDefaut(v) devient Util.EnDefaut(v) (ou EnDefaut(v) a la
    // racine), IHM_APPELER('Gaz.Horloge') aussi. Un nom court ecrit ailleurs reste : Compiler le signale et le corrige.
    if (beforeFull.find('.') != std::string::npos && !same(beforeFull, full)) {
        forEachCode(p, [&](std::string& text, CodeForm, const CodeSite&) {
            if (text.empty()) return;
            std::string out;
            out.reserve(text.size());
            std::size_t i = 0;
            while (i < text.size()) {
                if (!identStart(text[i]) || (i > 0 && (identChar(text[i - 1]) || text[i - 1] == '.'))) { out += text[i++]; continue; }
                std::size_t j = i;
                while (j < text.size() && (identChar(text[j]) || (text[j] == '.' && j + 1 < text.size() && identStart(text[j + 1])))) ++j;
                const std::string word = text.substr(i, j - i);
                // Gaz.EnDefaut, ou Gaz.T_Vanne.Position (un membre derriere) : la tete seule change.
                if (same(word, beforeFull)) out += full;
                else if (word.size() > beforeFull.size() && word[beforeFull.size()] == '.' && same(word.substr(0, beforeFull.size()), beforeFull))
                    out += full + word.substr(beforeFull.size());
                else out += word;
                i = j;
            }
            text = std::move(out);
        });
    }
    return true;
}

std::vector<SpaceCount> projectSpaces(const Project& p) {
    std::vector<SpaceCount> out;
    const auto slot = [&](const std::string& ns) -> SpaceCount& {
        for (auto& s : out) if (same(s.ns, ns)) return s;
        out.push_back(SpaceCount{ns});
        return out.back();
    };
    for (const auto& t : p.programs.types) if (!t.ns.empty()) ++slot(t.ns).types;
    for (const auto& f : p.programs.functions) if (!f.ns.empty()) ++slot(f.ns).functions;
    for (const auto& s : p.programs.scripts) if (!s.ns.empty()) ++slot(s.ns).scripts;
    if (const auto it = p.listFolders.find("espaces"); it != p.listFolders.end())
        for (const auto& e : it->second) slot(e).kept = true;
    std::sort(out.begin(), out.end(), [](const SpaceCount& a, const SpaceCount& b) { return upperOf(a.ns) < upperOf(b.ns); });
    return out;
}

bool addEmptyNamespace(Project& p, std::string_view ns, std::string* why) {
    if (!validNewNamespace(p, ns, why)) return false;
    p.listFolders["espaces"].emplace_back(ns);
    return true;
}

bool removeEmptyNamespace(Project& p, std::string_view ns) {
    auto it = p.listFolders.find("espaces");
    if (it == p.listFolders.end()) return false;
    auto& list = it->second;
    const auto before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const std::string& e) { return same(e, ns); }), list.end());
    const bool changed = list.size() != before;
    if (list.empty()) p.listFolders.erase(it);
    return changed;
}

} // namespace hmi::names
