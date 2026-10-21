// =============================================================================
//  hmi/HmiGenericEdit.cpp - voir HmiGenericEdit.hpp
// =============================================================================
#include "HmiGenericEdit.hpp"

#include "HmiPopupParams.hpp"
#include "HmiScript.hpp"
#include "HmiTypes.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>

namespace hmi::generics {

namespace {

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
std::string upper(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}
bool same(std::string_view a, std::string_view b) { return a.size() == b.size() && upper(a) == upper(b); }
std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Un mot suivi de « : » (pas de « := ») nomme quelque chose - le parametre d'un CALLBACK (t : T) -, ce n'est pas un type.
bool namesSomething(std::string_view t, std::size_t j) {
    while (j < t.size() && t[j] == ' ') ++j;
    return j < t.size() && t[j] == ':' && (j + 1 >= t.size() || t[j + 1] != '=');
}

// Chaque emploi `name<a, b>` de `t` (et `name` seul si `plain`) : `f` rend son remplacement, ou rien (tel quel).
using UseVisit = std::function<std::optional<std::string>(std::string_view written, const std::vector<std::string>* args)>;
std::string visitUses(std::string_view t, std::string_view name, bool plain, const UseVisit& f) {
    std::string out;
    std::size_t i = 0;
    while (i < t.size()) {
        if (!identStart(t[i]) || (i > 0 && identChar(t[i - 1]))) {
            out += t[i++];
            continue;
        }
        std::size_t j = i;
        while (j < t.size() && identChar(t[j])) ++j;
        const std::string_view w = t.substr(i, j - i);
        if (!same(w, name) || namesSomething(t, j)) {
            out.append(w);
            i = j;
            continue;
        }
        std::size_t k = j;
        while (k < t.size() && t[k] == ' ') ++k;
        if (k < t.size() && t[k] == '<') {
            std::vector<std::string> args;
            std::string cur;
            int depth = 0;
            std::size_t q = k + 1;
            bool closed = false;
            for (; q < t.size(); ++q) {
                const char c = t[q];
                if (depth == 0 && c == '>') {
                    closed = true;
                    break;
                }
                if (depth == 0 && c == ',') {
                    args.push_back(trim(cur));
                    cur.clear();
                    continue;
                }
                if (c == '<' || c == '(' || c == '[') ++depth;
                else if (c == '>' || c == ')' || c == ']') --depth;
                cur += c;
            }
            if (!closed) {                                  // illisible : tel quel
                out.append(t.substr(i));
                return out;
            }
            args.push_back(trim(cur));
            if (const auto with = f(w, &args)) out += *with;
            else out.append(t.substr(i, q + 1 - i));
            i = q + 1;
            continue;
        }
        if (plain)
            if (const auto with = f(w, nullptr)) {
                out += *with;
                i = j;
                continue;
            }
        out.append(w);
        i = j;
    }
    return out;
}

// Les mots `from` remplaces par `to` (les generiques d'un type : renommes, ou fixes a un type), sauf un nom (x :).
std::string substituteWords(std::string_view t, const std::vector<std::pair<std::string, std::string>>& words) {
    if (words.empty()) return std::string(t);
    std::string out;
    std::size_t i = 0;
    while (i < t.size()) {
        if (!identChar(t[i]) || (i > 0 && identChar(t[i - 1]))) {
            out += t[i++];
            continue;
        }
        std::size_t j = i;
        while (j < t.size() && identChar(t[j])) ++j;
        const std::string_view w = t.substr(i, j - i);
        std::string with(w);
        if (!namesSomething(t, j))
            for (const auto& [from, to] : words)
                if (same(from, w)) {
                    with = to;
                    break;
                }
        out += with;
        i = j;
    }
    return out;
}

bool mentions(std::string_view t, std::string_view word) {
    return substituteWords(t, {{std::string(word), std::string("\x01")}}) != t;
}

// Chaque texte de type du projet, et ou il est.
void forEachTypeText(Project& p, const std::function<void(std::string&, const std::string&)>& f) {
    for (auto& t : p.programs.types) {
        for (auto& m : t.members) f(m.type, "Types IHM / " + t.name + "." + m.name);
        if (!t.signature.empty()) f(t.signature, "Types IHM / " + t.name);
        for (auto& op : t.operators) {
            f(op.left, "Types IHM / " + t.name + " (op\xC3\xA9rateur " + op.op + ")");
            f(op.right, "Types IHM / " + t.name + " (op\xC3\xA9rateur " + op.op + ")");
            f(op.result, "Types IHM / " + t.name + " (op\xC3\xA9rateur " + op.op + ")");
            for (auto& d : op.decls) f(d.type, "Types IHM / " + t.name + " (op\xC3\xA9rateur " + op.op + ") / " + d.name);
        }
    }
    for (auto& v : p.programs.variables) f(v.type, "Variables IHM / " + v.name);
    for (auto& sc : p.programs.scripts)
        for (auto& d : sc.decls) f(d.type, "Script " + sc.name + " / " + d.name);
    for (auto& fn : p.programs.functions) {
        f(fn.returnType, "Fonction " + fn.name + " (retour)");
        for (auto& d : fn.decls) f(d.type, "Fonction " + fn.name + " / " + d.name);
    }
    for (auto& vw : p.views) {
        for (auto& prm : vw.params) f(prm.type, vw.name + " / param\xC3\xA8tre " + prm.name);
        for (auto& fn : vw.functions) f(fn.returnType, vw.name + " / fonction " + fn.name + " (retour)");
        forEachDeclarations(vw, [&](std::vector<Declaration>& list) {
            for (auto& d : list) f(d.type, vw.name + " / " + d.name);
        });
    }
}

// Le plus frequent (le premier a egalite) ; vide : aucun.
struct Tally {
    std::vector<std::pair<std::string, int>> seen;
    void add(const std::string& t) {
        for (auto& [x, n] : seen)
            if (same(x, t)) {
                ++n;
                return;
            }
        seen.emplace_back(t, 1);
    }
    [[nodiscard]] std::string best() const {
        const std::pair<std::string, int>* b = nullptr;
        for (const auto& s : seen)
            if (!b || s.second > b->second) b = &s;
        return b ? b->first : std::string{};
    }
    [[nodiscard]] bool agree() const { return seen.size() <= 1; }
};

// Ce que les emplois de `name` donnent a son generique de rang `o` (des types du projet : pas le T d'un autre).
Tally bindingsOf(Project& p, std::string_view name, std::size_t arity, std::size_t o) {
    Tally tally;
    const auto look = [&](const std::string& text) {
        if (text.find('<') == std::string::npos) return;
        (void)visitUses(text, name, false, [&](std::string_view, const std::vector<std::string>* args) -> std::optional<std::string> {
            if (args && args->size() == arity && o < args->size() && types::validType(p, (*args)[o])) tally.add((*args)[o]);
            return std::nullopt;
        });
    };
    forEachTypeText(p, [&](std::string& t, const std::string&) { look(t); });
    forEachCode(static_cast<const Project&>(p), [&](const std::string& t, CodeForm, const CodeSite&) { look(t); });
    return tally;
}

void unique(std::vector<std::string>& v) {
    std::vector<std::string> out;
    for (auto& s : v)
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(std::move(s));
    v = std::move(out);
}

} // namespace

bool Mapping::keepsUses() const noexcept {
    if (!removed.empty() || !added.empty()) return false;
    for (std::size_t n = 0; n < from.size(); ++n)
        if (from[n] != static_cast<int>(n)) return false;
    return true;
}

Mapping compare(const std::vector<GenericParam>& before, const std::vector<GenericParam>& after) {
    Mapping m;
    m.from.assign(after.size(), -1);
    std::vector<bool> taken(before.size(), false);
    for (std::size_t n = 0; n < after.size(); ++n)
        for (std::size_t o = 0; o < before.size(); ++o)
            if (!taken[o] && same(before[o].name, after[n].name)) {
                m.from[n] = static_cast<int>(o);
                taken[o] = true;
                break;
            }
    // Ceux qui ne se retrouvent pas : pris dans l'ordre pour des renommages ; le reste retire ou ajoute.
    std::vector<int> left, fresh;
    for (std::size_t o = 0; o < before.size(); ++o)
        if (!taken[o]) left.push_back(static_cast<int>(o));
    for (std::size_t n = 0; n < after.size(); ++n)
        if (m.from[n] < 0) fresh.push_back(static_cast<int>(n));
    const std::size_t pairs = std::min(left.size(), fresh.size());
    for (std::size_t k = 0; k < pairs; ++k) {
        m.from[static_cast<std::size_t>(fresh[k])] = left[k];
        m.renamed.emplace_back(before[static_cast<std::size_t>(left[k])].name, after[static_cast<std::size_t>(fresh[k])].name);
    }
    for (std::size_t k = pairs; k < left.size(); ++k) m.removed.push_back(left[k]);
    for (std::size_t k = pairs; k < fresh.size(); ++k) m.added.push_back(fresh[k]);
    return m;
}

std::string retype(std::string_view type, std::string_view name, const std::vector<GenericParam>& before,
                   const std::vector<GenericParam>& after, bool plain) {
    const Mapping m = compare(before, after);
    if (m.keepsUses()) return std::string(type);
    return visitUses(type, name, plain && before.empty(), [&](std::string_view written, const std::vector<std::string>* args) -> std::optional<std::string> {
        if (args ? args->size() != before.size() : !before.empty()) return std::nullopt;   // un emploi deja faux : tel quel
        std::string out(written);
        if (after.empty()) return out;
        out += '<';
        for (std::size_t n = 0; n < after.size(); ++n) {
            const int o = m.from[n];
            if (n) out += ", ";
            out += args && o >= 0 && static_cast<std::size_t>(o) < args->size() ? (*args)[static_cast<std::size_t>(o)]
                                                                                 : genericSample(after[n].constraint);
        }
        return out + ">";
    });
}

Report applyToType(Project& p, Id typeId, const std::vector<GenericParam>& after) {
    Report r;
    HmiType* t = p.hmiType(typeId);
    if (!t) return r;
    const std::vector<GenericParam> before = t->generics;
    const std::string name = t->name;
    const Mapping m = compare(before, after);
    // 1. Dans ses membres : un generique renomme suit ; un retire prend le type que lui donnaient ses emplois.
    std::vector<std::pair<std::string, std::string>> words(m.renamed.begin(), m.renamed.end());
    for (const int o : m.removed) {
        const GenericParam& g = before[static_cast<std::size_t>(o)];
        const Tally tally = bindingsOf(p, name, before.size(), static_cast<std::size_t>(o));
        std::string with = tally.best();
        const bool given = !with.empty();
        if (!given) with = genericSample(g.constraint);
        words.emplace_back(g.name, with);
        t = p.hmiType(typeId);
        const bool used = t && std::any_of(t->members.begin(), t->members.end(), [&](const TypeMember& mb) { return mentions(mb.type, g.name); });
        if (!used) continue;
        r.fixed.push_back(g.name + " devient " + with + " dans les membres de " + name
                          + (!given ? std::string(" (aucun emploi ne le donnait : un type de sa contrainte)")
                             : tally.agree() ? std::string(" (le type que lui donnaient ses emplois)")
                                             : std::string(" (le plus fr\xC3\xA9quent de ses emplois ; les autres ont maintenant ") + with + ")"));
    }
    t = p.hmiType(typeId);
    if (!t) return r;
    t->generics = after;
    for (auto& mb : t->members) mb.type = substituteWords(mb.type, words);
    // 2. Ses emplois : variables, membres, declarations, parametres, retours, signatures, code.
    if (!m.keepsUses()) {
        forEachTypeText(p, [&](std::string& text, const std::string& where) {
            if (text.empty()) return;
            std::string next = retype(text, name, before, after, true);
            if (next == text) return;
            text = std::move(next);
            r.where.push_back(where);
        });
        forEachCode(p, [&](std::string& text, CodeForm, const CodeSite& site) {
            if (text.find('<') == std::string::npos) return;
            std::string next = retype(text, name, before, after, false);
            if (next == text) return;
            text = std::move(next);
            r.where.push_back(site.where);
        });
    }
    unique(r.where);
    return r;
}

Report applyToView(Project& p, Id viewId, const std::vector<GenericParam>& after) {
    Report r;
    View* v = p.view(viewId);
    if (!v) return r;
    const std::vector<GenericParam> before = v->generics;
    const std::string name = v->name;
    const Mapping m = compare(before, after);
    // 1. Ce que les instances et les ouvertures donnent aux generiques retires (donnes ou deduits).
    std::vector<Tally> tallies(before.size());
    if (!m.removed.empty()) {
        const auto count = [&](const View& caller, const std::string& args) {
            const auto b = params::bindGenerics(p, &caller, *v, args);
            for (const int o : m.removed)
                if (static_cast<std::size_t>(o) < b.types.size() && !b.types[static_cast<std::size_t>(o)].empty())
                    tallies[static_cast<std::size_t>(o)].add(b.types[static_cast<std::size_t>(o)]);
        };
        for (const auto& other : p.views) {
            for (const auto& a : other.actions)
                if (operationTakesArguments(a.operation) && same(trim(a.target), name)) count(other, a.value);
            for (const auto& o : other.objects) {
                if (o.kind == Kind::SymbolInstance && same(trim(o.text("symbol")), name)) count(other, o.text("params"));
                for (const auto& a : o.actions)
                    if (operationTakesArguments(a.operation) && same(trim(a.target), name)) count(other, a.value);
            }
        }
    }
    // 2. Ses parametres, ses declarations, ses retours.
    std::vector<std::pair<std::string, std::string>> words(m.renamed.begin(), m.renamed.end());
    std::vector<std::string> alone;   // les retires que rien ne donne : seuls, ANY ; ailleurs, un type de leur contrainte
    for (const int o : m.removed) {
        const GenericParam& g = before[static_cast<std::size_t>(o)];
        std::string with = tallies[static_cast<std::size_t>(o)].best();
        const bool given = !with.empty();
        if (!given) {
            with = genericSample(g.constraint);
            alone.push_back(g.name);
        }
        words.emplace_back(g.name, with);
        const bool used = std::any_of(v->params.begin(), v->params.end(), [&](const ViewParam& prm) { return mentions(prm.type, g.name); });
        if (used)
            r.fixed.push_back(g.name + " devient " + (given ? with + " dans les param\xC3\xA8tres de " + name + " (le type que lui donnaient ses instances)"
                                                            : "ANY (seul) ou " + with + " dans les param\xC3\xA8tres de " + name + " (rien ne le donnait)"));
    }
    v->generics = after;
    if (!words.empty()) {
        for (auto& prm : v->params) {
            std::string next = prm.type;
            for (const auto& a : alone)
                if (same(trim(next), a)) next = "ANY";
            next = substituteWords(next, words);
            if (next == prm.type) continue;
            prm.type = std::move(next);
            r.where.push_back(name + " / param\xC3\xA8tre " + prm.name);
        }
        for (auto& fn : v->functions) fn.returnType = substituteWords(fn.returnType, words);
        forEachDeclarations(*v, [&](std::vector<Declaration>& list) {
            for (auto& d : list) d.type = substituteWords(d.type, words);
        });
    }
    // 3. Les arguments T := REAL des instances et de ceux qui l'ouvrent : retires, renommes.
    if (!m.removed.empty() || !m.renamed.empty()) {
        const auto rewrite = [&](const std::string& text) {
            std::string out = text;
            for (const int o : m.removed) {
                const std::string& g = before[static_cast<std::size_t>(o)].name;
                if (params::withoutArgument(out, g) != params::withoutArgument(out, "\x01")) out = params::withoutArgument(out, g);
            }
            for (const auto& [from, to] : m.renamed) out = params::renameArgument(out, from, to);
            return out;
        };
        const auto follow = [&](std::string& text, const std::string& where) {
            std::string next = rewrite(text);
            if (next == text) return;
            text = std::move(next);
            r.where.push_back(where);
        };
        for (auto& other : p.views) {
            for (auto& a : other.actions)
                if (operationTakesArguments(a.operation) && same(trim(a.target), name)) follow(a.value, other.name + " / actions de la vue");
            for (auto& o : other.objects) {
                if (o.kind == Kind::SymbolInstance && same(trim(o.text("symbol")), name)) {
                    std::string args = o.text("params");
                    const std::string was = args;
                    follow(args, other.name + " / " + o.name);
                    if (args != was) o.set("params", args);
                }
                for (auto& a : o.actions)
                    if (operationTakesArguments(a.operation) && same(trim(a.target), name)) follow(a.value, other.name + " / " + o.name + " (action)");
            }
        }
    }
    unique(r.where);
    return r;
}

} // namespace hmi::generics
