// =============================================================================
//  hmi/HmiCalc.cpp - 1.12.7 : les valeurs calculees au demarrage (voir HmiCalc.hpp)
// =============================================================================
#include "HmiCalc.hpp"

#include "HmiDecl.hpp"
#include "HmiDeclEdit.hpp"   // usesIn : le code qui lit une declaration
#include "HmiScript.hpp"
#include "HmiSymbols.hpp"   // 1.12.16 : les constantes des vues (viewmembers)

#include <algorithm>
#include <cctype>
#include <deque>
#include <functional>
#include <map>
#include <set>

namespace hmi::calc {

namespace {

std::string upper(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

bool sameWord(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Les mots qui ouvrent ou ferment un bloc de declarations : plus dans le code d'un calcul.
bool varBlockWord(const std::string& u) {
    return u == "VAR" || u == "VAR_TEMP" || u == "VAR_INPUT" || u == "VAR_OUTPUT" || u == "VAR_IN_OUT" || u == "VAR_GLOBAL"
        || u == "VAR_EXTERNAL" || u == "END_VAR";
}

// Une native qui ecrit dans son premier argument (LIST_ADD(L, x), MAP_SET(m, k, v)...).
bool mutating(const std::string& u) {
    static const std::set<std::string> kNames = {
        "LIST_ADD",    "LIST_INSERT",   "LIST_REMOVE_AT", "LIST_REMOVE",   "LIST_CLEAR",   "LIST_SORT",   "LIST_REVERSE",
        "VECTOR_PUSH", "VECTOR_POP",    "VECTOR_INSERT",  "VECTOR_ERASE",  "VECTOR_CLEAR", "VECTOR_RESIZE", "MAP_REMOVE",
        "MAP_CLEAR",   "MAP_SET",       "MAP_INSERT",     "MAP_INCREMENT", "LIST_SET",     "TUPLE_SET",   "IOTA",
        "SWAP",        "EXCHANGE"};                                                                    // 1.12.16 : <utility>
    if (kNames.count(u)) return true;
    // Les familles des conteneurs (1.12.7 et apres) : LIST_REMOVE_IF, LIST_SORT_BY, LIST_ROTATE...
    static const char* kFamilies[] = {"LIST_", "VECTOR_", "MAP_", "TUPLE_", "SET_", "RING_", "ARRAY_"};
    bool container = false;
    for (const char* f : kFamilies) container = container || u.rfind(f, 0) == 0;
    if (!container) return false;
    static const char* kVerbs[] = {"_SET", "_INSERT", "_CLEAR", "_ADD", "_PUSH", "_POP", "_ERASE", "_RESIZE", "_ROTATE", "_MOVE",
                                   "_SWAP", "_FILL", "_INCREMENT", "_SHUFFLE", "_APPEND", "_EXTEND", "_REVERSE", "_REMOVE", "_SORT"};
    for (const char* v : kVerbs) {
        const std::string verb(v);
        if (u.find(verb) != std::string::npos) return true;
    }
    return false;
}

using Tokens = std::vector<const decl::Token*>;

Tokens meaningful(const std::vector<decl::Token>& all) {
    Tokens t;
    for (const auto& k : all)
        if (k.kind != decl::TokenKind::Comment) t.push_back(&k);
    return t;
}

// Les variables des boucles FOR i := ... et FOR EACH x[, y] IN ..., dans l'ordre, une fois chacune.
struct LoopVar {
    std::string name;
    bool        each{false};
};
std::vector<LoopVar> loopVariables(std::string_view code, const Tokens& t) {
    std::vector<LoopVar> out;
    const auto add = [&](std::string name, bool each) {
        for (const auto& v : out)
            if (sameWord(v.name, name)) return;
        out.push_back({std::move(name), each});
    };
    for (std::size_t i = 0; i + 1 < t.size(); ++i) {
        if (t[i]->kind != decl::TokenKind::Word || upper(t[i]->text(code)) != "FOR") continue;
        if (t[i + 1]->kind != decl::TokenKind::Word) continue;
        if (upper(t[i + 1]->text(code)) == "EACH") {
            for (std::size_t j = i + 2; j < t.size(); ++j) {
                if (t[j]->kind == decl::TokenKind::Word) {
                    if (upper(t[j]->text(code)) == "IN") break;
                    add(std::string(t[j]->text(code)), true);
                } else if (t[j]->text(code) != ",") {
                    break;
                }
            }
            continue;
        }
        add(std::string(t[i + 1]->text(code)), false);
    }
    return out;
}

bool declared(const std::vector<Declaration>& list, std::string_view name) {
    return std::any_of(list.begin(), list.end(), [&](const Declaration& d) { return sameWord(d.name, name); });
}

std::size_t distance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0u : 1u)});
            diag = up;
        }
    }
    return row[b.size()];
}

} // namespace

bool Analysis::hasError() const noexcept {
    return std::any_of(issues.begin(), issues.end(), [](const Issue& i) { return i.error; });
}

std::string closest(std::string_view name, const std::vector<std::string>& names) {
    const std::string u = upper(name);
    std::string best;
    std::size_t bestD = 3;
    for (const auto& n : names) {
        const std::string v = upper(n);
        if (v == u) continue;
        const std::size_t d = distance(u, v);
        // Un nom d'une lettre ne se propose pas pour un autre tres different (x pour Seuil).
        if (d < bestD && d < std::min(u.size(), v.size())) {
            bestD = d;
            best = n;
        }
    }
    return best;
}

std::string pathOf(const Script& sc, const Declaration& d, const View* view) {
    return (view ? view->name + "." : std::string{}) + sc.name + "." + d.name;
}

std::vector<Declaration> declarationsFor(const Script& owner, const Declaration& d) {
    std::vector<Declaration> out;
    Declaration me = d;
    me.kind = DeclKind::Variable;
    me.storage = Storage::Execution;
    me.computed = false;
    me.value.clear();
    me.calc.clear();
    me.locals.clear();
    out.push_back(std::move(me));
    for (const auto& l : d.locals) {
        if (declared(out, l.name)) continue;
        Declaration x = l;
        x.kind = DeclKind::Variable;
        x.storage = Storage::Execution;
        x.computed = false;
        x.calc.clear();
        x.locals.clear();
        out.push_back(std::move(x));
    }
    // Les constantes du script - et ses variables, pour le calcul d'une variable (la valeur ecrite,
    // ou celle deja calculee : le moteur la donne). Une locale du meme nom passe avant.
    std::vector<Declaration> siblings;
    for (const auto& s : owner.decls) {
        if (&s == &d || sameWord(s.name, d.name) || declared(out, s.name)) continue;
        if (s.kind == DeclKind::Constant || (d.kind == DeclKind::Variable && s.kind == DeclKind::Variable)) {
            Declaration x = s;
            x.calc.clear();
            x.locals.clear();
            if (x.kind == DeclKind::Variable) x.storage = Storage::Execution;
            siblings.push_back(std::move(x));
        }
    }
    // Une boucle FOR i := ... sans i declare : un DINT (FOR EACH declare les siennes).
    const auto tokens = decl::lex(d.calc);
    for (const auto& v : loopVariables(d.calc, meaningful(tokens))) {
        if (v.each || declared(out, v.name) || declared(siblings, v.name)) continue;
        Declaration x;
        x.kind = DeclKind::Variable;
        x.name = v.name;
        x.type = "DINT";
        out.push_back(std::move(x));
    }
    for (auto& s : siblings) out.push_back(std::move(s));
    return out;
}

std::string runnableCode(const Script& owner, const Declaration& d) {
    return decl::composeCode(d.calc, declarationsFor(owner, d), decl::Role::Script).text;
}

const Declaration* qualified(const Project& p, std::string_view path, const Script** script, std::string* why, Id from) {
    const auto dot = path.find('.');
    if (dot == std::string_view::npos) {
        if (why) *why = std::string(path) + " : pas un nom qualifi\xC3\xA9 (Script.Constante)";
        return nullptr;
    }
    const std::string root(path.substr(0, dot));
    std::string member(path.substr(dot + 1));
    if (const auto cut = member.find_first_of(".["); cut != std::string::npos) member.resize(cut);
    const Script* sc = p.generalScript(root);
    if (!sc) {
        if (why) *why = root + " : script g\xC3\xA9n\xC3\xA9ral inconnu";
        return nullptr;
    }
    for (const auto& d : sc->decls) {
        if (!sameWord(d.name, member)) continue;
        if (d.kind != DeclKind::Constant) {
            if (why)
                *why = sc->name + "." + d.name + " est une variable de " + sc->name
                     + " : seules ses constantes se lisent d'ailleurs";
            return nullptr;
        }
        if (d.visibility == Visibility::Private && sc->id != from) {
            if (why) *why = sc->name + "." + d.name + " est priv\xC3\xA9" "e : seul " + sc->name + " la lit (rends-la publique)";
            return nullptr;
        }
        if (script) *script = sc;
        return &d;
    }
    if (why) {
        std::vector<std::string> names;
        for (const auto& d : sc->decls)
            if (d.kind == DeclKind::Constant) names.push_back(d.name);
        const std::string near = closest(member, names);
        *why = sc->name + "." + member + " n'existe pas" + (near.empty() ? std::string{} : " (veux-tu dire " + sc->name + "." + near + " ?)");
    }
    return nullptr;
}

Analysis analyse(const Project& p, const Script& owner, const Declaration& d, const View* view) {
    Analysis a;
    const std::string_view code = d.calc;
    const auto all = decl::lex(code);
    const Tokens t = meaningful(all);
    const auto word = [&](std::size_t i) { return i < t.size() && t[i]->kind == decl::TokenKind::Word; };
    const auto punct = [&](std::size_t i, std::string_view s) {
        return i < t.size() && t[i]->kind == decl::TokenKind::Punct && t[i]->text(code) == s;
    };
    const auto text = [&](std::size_t i) { return std::string(t[i]->text(code)); };
    const auto issue = [&](bool error, std::size_t i, std::string message) {
        Issue x;
        x.error = error;
        if (i < t.size()) {
            x.line = t[i]->line;
            x.column = t[i]->column;
        }
        x.message = std::move(message);
        // Une meme faute (un nom ecrit deux fois) : dite une fois.
        for (const auto& y : a.issues)
            if (y.message == x.message) return;
        a.issues.push_back(std::move(x));
    };
    const std::string self = upper(d.name);
    std::set<std::string> locals;
    for (const auto& l : d.locals) locals.insert(upper(l.name));
    for (const auto& v : loopVariables(code, t)) locals.insert(upper(v.name));
    std::map<std::string, const Declaration*> siblings;
    for (const auto& s : owner.decls)
        if (&s != &d && !sameWord(s.name, d.name)) siblings.emplace(upper(s.name), &s);
    for (const auto& l : d.locals)
        if (siblings.count(upper(l.name)))
            issue(false, t.size(), "la locale " + l.name + " cache la d\xC3\xA9" "claration " + l.name + " du script");
    bool blockSaid = false;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (!word(i)) continue;
        const std::string w = text(i);
        const std::string u = upper(w);
        if (varBlockWord(u)) {
            if (!blockSaid)
                issue(true, i,
                      "VAR \xE2\x80\xA6 END_VAR : les locales ne s'\xC3\xA9" "crivent plus dans le code, elles vont dans l'onglet Locales");
            blockSaid = true;
            continue;
        }
        if (i > 0 && punct(i - 1, ".")) continue;                      // un membre : vu avec sa racine
        // Le chemin pointe : Reglages.Seuils, Recette.Etapes.
        std::size_t j = i;
        std::string path = w;
        while (punct(j + 1, ".") && word(j + 2)) {
            path += "." + text(j + 2);
            j += 2;
        }
        const bool call = punct(j + 1, "(");
        // Ecrit ? Le chemin, ses indices et ses membres, puis ':='.
        std::size_t k = j + 1;
        while (k < t.size()) {
            if (punct(k, "[")) {
                int depth = 0;
                for (; k < t.size(); ++k) {
                    if (punct(k, "[")) ++depth;
                    else if (punct(k, "]") && --depth == 0) break;
                }
                ++k;
                continue;
            }
            if (punct(k, ".") && word(k + 1)) {
                k += 2;
                continue;
            }
            break;
        }
        const bool assigned = punct(k, ":=");
        // Le premier argument d'un appel : LIST_ADD(Seuils, x).
        const bool firstArg = i >= 2 && punct(i - 1, "(") && word(i - 2) && !isReservedWord(text(i - 2));
        const std::string fn = firstArg ? upper(text(i - 2)) : std::string{};
        // 1.12.16 : le second argument, quand le premier est un nom - SWAP(A, B), LIST_TRANSFORM(S, D, OP) et LIST_COPY(S, D) l'ecrivent.
        const bool secondArg = i >= 4 && punct(i - 1, ",") && word(i - 2) && punct(i - 3, "(") && word(i - 4) && !isReservedWord(text(i - 4));
        const std::string fn2 = secondArg ? upper(text(i - 4)) : std::string{};
        const bool secondWritten = fn2 == "SWAP" || fn2 == "LIST_TRANSFORM" || fn2 == "LIST_COPY";
        const bool written = assigned || (firstArg && mutating(fn)) || secondWritten;
        if (u == self) {
            if (assigned || firstArg || secondWritten) a.assigns = true;
            i = j;
            continue;
        }
        if (call || isReservedWord(w) || locals.count(u)) {
            i = j;
            continue;
        }
        if (const auto it = siblings.find(u); it != siblings.end()) {
            const Declaration& s = *it->second;
            if (s.kind == DeclKind::Constant) {
                if (written) issue(true, i, s.name + " est une constante : elle ne s'\xC3\xA9" "crit pas");
                else if (s.computed) a.reads.push_back(s.name);
                if (!written) a.uses.push_back(s.name);
            } else if (s.kind == DeclKind::Variable) {
                if (d.kind == DeclKind::Constant)
                    issue(true, i, s.name + " est une variable du script : elle se calcule apr\xC3\xA8s les constantes (une constante ne la lit pas)");
                else if (written)
                    issue(false, i, s.name + " : seule " + d.name + " s'\xC3\xA9" "crit ici (l'\xC3\xA9" "criture de " + s.name + " serait perdue)");
                else if (s.computed)
                    a.reads.push_back(s.name);
                if (!written) a.uses.push_back(s.name);
            }
            i = j;
            continue;
        }
        // Reglages.Seuils : la constante publique d'un script general (une variable IHM du meme nom passe avant).
        if (path.find('.') != std::string::npos && !p.variable(w) && p.generalScript(w)) {
            const Script* gs = nullptr;
            std::string why;
            const Declaration* q = qualified(p, path, &gs, &why, owner.id);
            if (!q) issue(true, i, why);
            else if (written) issue(true, i, gs->name + "." + q->name + " est une constante : elle ne s'\xC3\xA9" "crit pas");
            else if (gs->id == owner.id && sameWord(q->name, d.name)) a.assigns = a.assigns || assigned;
            else if (q->computed) a.reads.push_back(gs->name + "." + q->name);
            if (q && !written && !(gs->id == owner.id && sameWord(q->name, d.name))) a.uses.push_back(gs->name + "." + q->name);
            i = j;
            continue;
        }
        // 1.12.16 : Vue_Four.Seuil_Haut - la constante d'une vue (publique, ou de la vue qui calcule).
        if (path.find('.') != std::string::npos && !p.variable(w)) {
            const View* from = view ? view : p.view(owner.id);        // le porteur des constantes d'une vue : la vue
            viewmembers::Member m;
            std::string why;
            if (viewmembers::resolve(p, path, m, &why, from) && m.constant) {
                const std::string q = m.view->name + "." + m.constant->name;
                const bool me = m.view->id == owner.id && sameWord(m.constant->name, d.name);
                if (written) issue(true, i, q + " est une constante : elle ne s'\xC3\xA9" "crit pas");
                else if (me) a.assigns = a.assigns || assigned;
                else if (m.constant->computed) a.reads.push_back(q);
                if (!written && !me) a.uses.push_back(q);
                i = j;
                continue;
            }
            if (!why.empty()) {
                issue(true, i, why);
                i = j;
                continue;
            }
        }
        if (p.variable(w)) a.uses.push_back(w);
        if (view && view->param(w) && !p.view(owner.id))     // 1.12.16 : une constante de vue qui le lit se calcule a l'ouverture
            issue(true, i, w + " est un param\xC3\xA8tre de la vue : il n'existe pas encore au d\xC3\xA9marrage");
        if (const Variable* v = p.variable(w); v && !v->equipment.empty())
            issue(false, i, w + " est li\xC3\xA9" "e \xC3\xA0 l'\xC3\xA9quipement " + v->equipment
                                + " : au d\xC3\xA9marrage, elle vaut sa valeur initiale (l'\xC3\xA9quipement n'est pas encore lu)");
        i = j;
    }
    if (!a.assigns)
        issue(false, t.size(),
              d.name + " ne re\xC3\xA7oit aucune valeur : " + (d.kind == DeclKind::Constant ? "elle garde" : "sa valeur initiale est")
                  + " la valeur vide de son type (\xC3\xA9" "cris " + d.name + " := ...;)");
    // Une meme lecture : une fois.
    std::sort(a.reads.begin(), a.reads.end());
    a.reads.erase(std::unique(a.reads.begin(), a.reads.end()), a.reads.end());
    std::vector<std::string> once;
    for (auto& u : a.uses)
        if (std::none_of(once.begin(), once.end(), [&](const std::string& o) { return sameWord(o, u); })) once.push_back(std::move(u));
    a.uses = std::move(once);
    return a;
}

namespace {
// Le nom qualifie `upath` (en majuscules : REGLAGES.SEUILS) ecrit dans un code, entre deux
// non-identifiants (sans casse ; un commentaire compte aussi : une lecture de plus a montrer).
bool mentions(std::string_view code, const std::string& upath) {
    const std::string u = upper(code);
    const auto ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (std::size_t at = u.find(upath); at != std::string::npos; at = u.find(upath, at + 1)) {
        const std::size_t end = at + upath.size();
        if ((at == 0 || (!ident(u[at - 1]) && u[at - 1] != '.')) && (end >= u.size() || !ident(u[end]))) return true;
    }
    return false;
}
} // namespace

std::vector<std::string> readers(const Project& p, const Script& owner, const Declaration& d, const View* view) {
    std::vector<std::string> out;
    const auto add = [&](std::string s) {
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(std::move(s));
    };
    const std::string qualifiedName = owner.name + "." + d.name;
    const auto calcs = [&](const Script& sc, const View* v) {
        for (const auto& x : sc.decls) {
            if (!x.computed || x.id == d.id) continue;
            for (const auto& r : analyse(p, sc, x, v).uses)
                if ((&sc == &owner && sameWord(r, d.name)) || (!view && sameWord(r, qualifiedName))) {
                    add("calcul de " + pathOf(sc, x, v));
                    break;
                }
        }
    };
    for (const auto& sc : p.programs.scripts) calcs(sc, nullptr);
    for (const auto& v : p.views)
        for (const auto& sc : v.scripts) calcs(sc, &v);
    if (!decledit::usesIn(owner.body, d.name).empty()) add("le code de " + (view ? view->name + "." : std::string{}) + owner.name);
    if (!view && d.kind == DeclKind::Constant && d.visibility == Visibility::Public) {
        const std::string u = upper(qualifiedName);
        for (const auto& sc : p.programs.scripts)
            if (sc.id != owner.id && mentions(sc.body, u)) add("le script " + sc.name);
        for (const auto& f : p.programs.functions)
            if (mentions(f.body, u)) add("la fonction " + f.name);
        for (const auto& v : p.views)
            for (const auto& sc : v.scripts)
                if (mentions(sc.body, u)) add("le script " + v.name + "." + sc.name);
    }
    return out;
}

bool hasComputed(const Project& p) noexcept {
    const auto any = [](const Script& sc) {
        return std::any_of(sc.decls.begin(), sc.decls.end(), [](const Declaration& d) { return d.computed; });
    };
    if (std::any_of(p.programs.scripts.begin(), p.programs.scripts.end(), any)) return true;
    for (const auto& v : p.views) {
        if (std::any_of(v.scripts.begin(), v.scripts.end(), any)) return true;
        if (std::any_of(v.constants.begin(), v.constants.end(), [](const Declaration& d) { return d.computed; })) return true;   // 1.12.16
    }
    return false;
}

Plan plan(const Project& p) {
    struct Node {
        const Script*            sc{nullptr};
        const View*              view{nullptr};
        const Declaration*       d{nullptr};
        std::string              path;
        std::vector<std::string> deps;     // les cles des noeuds lus
    };
    const auto keyOf = [](Id script, std::string_view name) { return std::to_string(script) + ":" + upper(name); };
    std::map<std::string, Node> nodes;
    std::vector<std::string>    order;     // l'ordre du projet
    const auto collect = [&](const Script& sc, const View* view, const View* holderOf = nullptr) {
        if (sc.lang != ScriptLang::ST) return;
        for (const auto& d : sc.decls) {
            if (!d.computed || d.kind == DeclKind::Parameter) continue;
            if (holderOf && viewmembers::computedAtOpen(p, *holderOf, d)) continue;   // 1.12.16 : a l'ouverture de la vue
            const std::string key = keyOf(sc.id, d.name);
            if (nodes.count(key)) continue;
            Node n;
            n.sc = &sc;
            n.view = view;
            n.d = &d;
            n.path = pathOf(sc, d, view);
            nodes.emplace(key, std::move(n));
            order.push_back(key);
        }
    };
    for (const auto& sc : p.programs.scripts) collect(sc, nullptr);
    for (const auto& v : p.views)
        for (const auto& sc : v.scripts) collect(sc, &v);
    // 1.12.16 : les constantes des vues calculees au demarrage - leur porteur, un script du nom de la vue
    // (son identifiant : celui de la vue), garde le temps du plan.
    std::deque<Script> holders;
    for (const auto& v : p.views)
        if (std::any_of(v.constants.begin(), v.constants.end(), [](const Declaration& d) { return d.computed; })) {
            holders.push_back(viewmembers::constantsScript(v));
            collect(holders.back(), nullptr, &v);
        }
    for (auto& [key, n] : nodes) {
        const Analysis a = analyse(p, *n.sc, *n.d, n.view);
        for (const auto& r : a.reads) {
            const auto dot = r.find('.');
            if (dot == std::string::npos) {
                n.deps.push_back(keyOf(n.sc->id, r));
            } else if (const Script* gs = p.generalScript(std::string_view(r).substr(0, dot))) {
                n.deps.push_back(keyOf(gs->id, std::string_view(r).substr(dot + 1)));
            } else if (const View* vv = p.viewByName(std::string_view(r).substr(0, dot))) {   // 1.12.16 : Vue.Constante
                n.deps.push_back(keyOf(vv->id, std::string_view(r).substr(dot + 1)));
            }
        }
    }
    Plan out;
    std::set<std::string> done;
    std::vector<std::string> stack;
    std::function<bool(const std::string&)> visit = [&](const std::string& key) -> bool {
        if (done.count(key)) return true;
        if (const auto at = std::find(stack.begin(), stack.end(), key); at != stack.end()) {
            std::string text;
            for (auto it = at; it != stack.end(); ++it) text += nodes.at(*it).path + " \xE2\x86\x92 ";
            out.cycle = text + nodes.at(key).path;
            return false;
        }
        const auto it = nodes.find(key);
        if (it == nodes.end()) return true;            // une valeur ecrite, ou inconnue (l'analyse le dit)
        stack.push_back(key);
        for (const auto& dep : it->second.deps)
            if (!visit(dep)) return false;
        stack.pop_back();
        done.insert(key);
        Step s;
        s.script = it->second.sc->id;
        s.view = it->second.view ? it->second.view->id : kNoId;
        s.declaration = it->second.d->id;
        s.name = it->second.d->name;
        s.path = it->second.path;
        out.order.push_back(std::move(s));
        return true;
    };
    for (const bool constants : {true, false})
        for (const auto& key : order) {
            if ((nodes.at(key).d->kind == DeclKind::Constant) != constants) continue;
            if (!visit(key)) {
                out.order.clear();
                return out;
            }
        }
    return out;
}

} // namespace hmi::calc
