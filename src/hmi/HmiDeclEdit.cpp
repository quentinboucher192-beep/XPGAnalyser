// =============================================================================
//  hmi/HmiDeclEdit.cpp - 1.11.18 (refonte des scripts, lot 5) : editer les
//  declarations d'un code (voir HmiDeclEdit.hpp)
// =============================================================================
#include "HmiDeclEdit.hpp"
#include "HmiTypeForms.hpp"     // 1.12.14 : la valeur d'une structure (le moteur la range)

#include "HmiEnums.hpp"
#include "HmiGuide.hpp"       // guide::fold : sans casse ni accents
#include "HmiOperators.hpp"
#include "HmiScript.hpp"
#include "HmiSymbols.hpp"     // replacePath, symbolOf, symbolFunction
#include "HmiTypeRegistry.hpp" // 1.11.19 (refonte, lot 6) : les types proposes, lus, remis en forme
#include "HmiTypes.hpp"
#include "HmiUnits.hpp"       // 1.13.0 (lot J6b) : la case Unite (le catalogue, un type numerique)

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>

namespace hmi::decledit {

namespace {

constexpr std::size_t npos = static_cast<std::size_t>(-1);

bool same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool identStart(char c) noexcept { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool identChar(char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
std::string quoted(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// Un libelle relu : sans casse ni accents, sans blancs ni _ - / . (VAR_TEMP = vartemp, E/S = es).
std::string squeezed(std::string_view s) {
    std::string out;
    for (const char c : guide::fold(trimmed(s)))
        if (c != ' ' && c != '_' && c != '-' && c != '/' && c != '.' && c != '\t') out += c;
    return out;
}
bool oneOf(const std::string& s, std::initializer_list<std::string_view> words) {
    return std::any_of(words.begin(), words.end(), [&](std::string_view w) { return s == w; });
}

void bindFunction(Code& c, HmiFunction& f) {
    c.body = &f.body;
    c.decls = &f.decls;
    c.role = decl::Role::Function;
    c.reserved.push_back(f.name);      // son resultat : Nom := ... (une declaration ne le prend pas)
}
void bindOperator(Code& c, HmiOperator& o) {
    c.body = &o.body;
    c.decls = &o.decls;
    c.role = decl::Role::Operator;
    if (auto fn = operatorFunctionName(o); !fn.empty()) c.reserved.push_back(std::move(fn));   // TO_REAL, ADD : son resultat aussi
}
void bindScript(Code& c, Script& s) {
    c.body = &s.body;
    c.decls = &s.decls;
    c.role = decl::Role::Script;
    c.declares = s.lang == ScriptLang::ST;
}

std::string_view baseName(Tab t, decl::Role role) noexcept {
    switch (t) {
        case Tab::Constants:  return "Constante";
        case Tab::Variables:  return role == decl::Role::Script ? std::string_view("Variable") : std::string_view("Locale");
        case Tab::Parameters: return "Parametre";
    }
    return "Declaration";
}

// La ligne `row` de l'onglet : son indice dans decls (npos : hors de l'onglet).
std::size_t indexOf(const Code& c, Tab t, std::size_t row) {
    const auto rows = rowsOf(*c.decls, t);
    return row < rows.size() ? rows[row] : npos;
}

// Le code et l'onglet, verifies (un script C ou C++, un onglet que ce code n'a pas).
bool ready(const Code& c, Tab t, std::string* why) {
    if (!c.valid()) return fail(why, "code introuvable (supprim\xC3\xA9 entre-temps ?)");
    if (!c.declares) return fail(why, "un script C ou C++ garde ses d\xC3\xA9" "clarations dans son code (le langage les veut l\xC3\xA0)");
    const auto tabs = tabsOf(c);
    if (std::find(tabs.begin(), tabs.end(), t) == tabs.end())
        return fail(why, c.role == decl::Role::Script     ? "un script n'a pas de param\xC3\xA8tre (seule une fonction en a)"
                       : c.role == decl::Role::Operator ? "un op\xC3\xA9rateur n'a pas d'autre param\xC3\xA8tre que a et b"
                                                        : "une red\xC3\xA9" "finition garde les param\xC3\xA8tres de sa fonction");
    return true;
}

// LES TROUS D'IHM_JOURNAL ET D'IHM_LOG : leurs chaines ('mini {Mini:0.0}', "{Tours} mesures")
// lisent les variables du code. `f` recoit chaque expression de trou : ses octets [debut, fin)
// dans `body` (sans le format :0.0), et le jeton de sa chaine.
template <class F>
void forEachHole(std::string_view body, F&& f) {
    const auto toks = decl::lex(body);
    int depth = 0;
    std::vector<int> calls;                 // la profondeur des arguments de chaque appel ouvert
    const decl::Token* previous = nullptr;
    for (const auto& t : toks) {
        if (t.kind == decl::TokenKind::Comment) continue;
        const auto text = t.text(body);
        if (t.kind == decl::TokenKind::Punct && text == "(") {
            ++depth;
            if (previous && previous->kind == decl::TokenKind::Word
                && (same(previous->text(body), "IHM_JOURNAL") || same(previous->text(body), "IHM_LOG")))
                calls.push_back(depth);
        } else if (t.kind == decl::TokenKind::Punct && text == ")") {
            if (!calls.empty() && calls.back() == depth) calls.pop_back();
            if (depth > 0) --depth;
        } else if (t.kind == decl::TokenKind::String && !calls.empty() && text.size() >= 2) {
            for (std::size_t i = 1; i + 1 < text.size(); ++i) {
                if (text[i] == '{' && i + 2 < text.size() && text[i + 1] == '{') { ++i; continue; }   // {{ : une accolade
                if (text[i] != '{') continue;
                const auto close = text.find('}', i + 1);
                if (close == std::string_view::npos || close + 1 >= text.size() + 1) break;
                std::string_view inside = text.substr(i + 1, close - i - 1);
                if (const auto colon = inside.rfind(':'); colon != std::string_view::npos && colon + 1 < inside.size() && inside[colon + 1] != '=')
                    inside = inside.substr(0, colon);
                f(t.begin + i + 1, t.begin + i + 1 + inside.size(), t);
                i = close;
            }
        }
        previous = &t;
    }
}

// Le code ou `from` devient `to` : les trous d'abord (replacePath ne lit pas les chaines),
// du dernier au premier (les octets d'avant ne bougent pas), puis le code lui-meme.
std::string renamedCode(std::string_view body, const std::string& from, const std::string& to) {
    std::vector<std::pair<std::size_t, std::size_t>> holes;
    forEachHole(body, [&](std::size_t b, std::size_t e, const decl::Token&) { holes.emplace_back(b, e); });
    std::string out(body);
    for (auto it = holes.rbegin(); it != holes.rend(); ++it)
        out.replace(it->first, it->second - it->first, replacePath(out.substr(it->first, it->second - it->first), from, to, false));
    return replacePath(out, from, to, true);
}

// Les utilisations de `from`, renommees en `to`, dans tout ce que lit ce code.
void renameUses(Project& p, const Place& at, Code& c, std::size_t self, const std::string& from, const std::string& to) {
    *c.body = renamedCode(*c.body, from, to);
    for (std::size_t j = 0; j < c.decls->size(); ++j)
        if (j != self && !(*c.decls)[j].value.empty()) (*c.decls)[j].value = replacePath((*c.decls)[j].value, from, to, false);
    // 1.12.7 : les calculs au demarrage du code (le sien aussi : il s'ecrit par son nom), sauf ou une
    // locale du meme nom le cache ; et Script.Nom, la constante d'un script general, partout ailleurs.
    for (auto& d : *c.decls) {
        if (!d.computed && d.calc.empty()) continue;
        if (std::any_of(d.locals.begin(), d.locals.end(), [&](const Declaration& l) { return same(l.name, from); })) continue;
        d.calc = renamedCode(d.calc, from, to);
    }
    if (at.kind == Place::Kind::Script && (*c.decls)[self].kind == DeclKind::Constant) {
        const Script* owner = nullptr;
        for (const auto& s : p.programs.scripts)
            if (s.id == at.id) owner = &s;
        if (owner) {
            const std::string qFrom = owner->name + "." + from, qTo = owner->name + "." + to;
            const auto inCode = [&](std::string& code) {
                if (code.find('.') != std::string::npos) code = replacePath(code, qFrom, qTo, true);
            };
            const auto inDecls = [&](std::vector<Declaration>& list) {
                for (auto& d : list) {
                    if (!d.calc.empty()) inCode(d.calc);
                    if (!d.value.empty() && d.value.find('.') != std::string::npos) d.value = replacePath(d.value, qFrom, qTo, false);
                }
            };
            for (auto& s : p.programs.scripts) {
                inCode(s.body);
                inDecls(s.decls);
            }
            for (auto& v : p.views)
                for (auto& s : v.scripts) {
                    inCode(s.body);
                    inDecls(s.decls);
                }
            for (auto& f : p.programs.functions) {
                inCode(f.body);
                inDecls(f.decls);
            }
        }
    }
    // Un parametre d'une fonction de symbole : ses redefinitions le lisent aussi.
    if (at.kind != Place::Kind::SymbolFunction || (*c.decls)[self].kind != DeclKind::Parameter) return;
    const View* symbol = p.view(at.view);
    const HmiFunction* f = nullptr;
    if (symbol)
        for (const auto& g : symbol->functions)
            if (g.id == at.id) f = &g;
    if (!f) return;
    const std::string function = f->name;
    for (auto& v : p.views)
        for (auto& o : v.objects)
            for (auto& fo : o.functionOverrides) {
                if (!same(fo.function, function) || symbolOf(p, o) != symbol) continue;
                fo.body = renamedCode(fo.body, from, to);
                for (auto& d : fo.decls)
                    if (!d.value.empty()) d.value = replacePath(d.value, from, to, false);
            }
}

} // namespace

// ------------------------------------------------------------------ le code ----
Code locate(Project& p, const Place& at) {
    Code c;
    using K = Place::Kind;
    switch (at.kind) {
        case K::Script:
            for (auto& s : p.programs.scripts)
                if (s.id == at.id) bindScript(c, s);
            break;
        case K::ViewScript:
            if (auto* v = p.view(at.view))
                for (auto& s : v->scripts)
                    if (s.id == at.id) bindScript(c, s);
            break;
        case K::Function:
            for (auto& f : p.programs.functions)
                if (f.id == at.id) bindFunction(c, f);
            break;
        case K::SymbolFunction:
            if (auto* v = p.view(at.view))
                for (auto& f : v->functions)
                    if (f.id == at.id) bindFunction(c, f);
            break;
        case K::Override:
            if (auto* v = p.view(at.view))
                if (auto* o = v->object(at.object))
                    for (auto& fo : o->functionOverrides) {
                        if (!same(fo.function, at.function)) continue;
                        c.body = &fo.body;
                        c.decls = &fo.decls;
                        c.role = decl::Role::Function;
                        c.reserved.push_back(fo.function);
                        if (const auto* sym = symbolOf(p, *o))
                            if (const auto* f = symbolFunction(*sym, fo.function)) c.inherited = &f->decls;
                    }
            break;
        case K::TypeOperator:
            if (auto* t = p.hmiType(at.type))
                for (auto& o : t->operators)
                    if (o.id == at.id) bindOperator(c, o);
            break;
        case K::SymbolOperator:
            if (auto* v = p.view(at.view))
                for (auto& o : v->operators)
                    if (o.id == at.id) bindOperator(c, o);
            break;
        case K::ViewConstants:
            // 1.12.16 : les constantes d'une vue ou d'une popup - comme celles d'un script (calculees au
            // demarrage, ou a chaque ouverture), sans corps : le renommage suit dans tout le projet
            // (viewmembers::renameMember, par l'appelant).
            if (auto* v = p.view(at.view)) {
                static thread_local std::string none;
                none.clear();
                c.body = &none;
                c.decls = &v->constants;
                c.role = decl::Role::Script;
                c.constantsOnly = true;
                for (const auto& f : v->functions) c.reserved.push_back(f.name);
            }
            break;
    }
    return c;
}

Code locate(const Project& p, const Place& at) {
    // En lecture : les pointeurs rendus ne servent qu'a lire (les gestes prennent un Project&).
    return locate(const_cast<Project&>(p), at);
}

// ---------------------------------------------------------------- les onglets ----
DeclKind kindOf(Tab t) noexcept {
    switch (t) {
        case Tab::Constants:  return DeclKind::Constant;
        case Tab::Variables:  return DeclKind::Variable;
        case Tab::Parameters: return DeclKind::Parameter;
    }
    return DeclKind::Variable;
}

std::vector<Tab> tabsOf(const Code& c) {
    if (!c.valid() || !c.declares) return {};
    if (c.constantsOnly) return {Tab::Constants};   // 1.12.16 : les constantes d'une vue
    switch (c.role) {
        case decl::Role::Script:   return {Tab::Constants, Tab::Variables};
        case decl::Role::Operator: return {Tab::Variables, Tab::Constants};
        case decl::Role::Function:
            if (c.inherited) return {Tab::Variables, Tab::Constants};     // une redefinition : les parametres de sa fonction
            return {Tab::Parameters, Tab::Variables, Tab::Constants};
    }
    return {};
}

std::string tabLabel(Tab t, decl::Role role) {
    switch (t) {
        case Tab::Constants:  return "Constantes";
        case Tab::Variables:  return role == decl::Role::Script ? "Variables" : "Locales";
        case Tab::Parameters: return "Param\xC3\xA8tres";
    }
    return {};
}

std::vector<std::size_t> rowsOf(const std::vector<Declaration>& decls, Tab t) {
    std::vector<std::size_t> out;
    const DeclKind k = kindOf(t);
    for (std::size_t i = 0; i < decls.size(); ++i)
        if (decls[i].kind == k) out.push_back(i);
    return out;
}

std::vector<Column> columnsOf(Tab t, decl::Role role) {
    const bool script = role == decl::Role::Script;
    // 1.12.2 : plus de colonne Public / Prive - elle ne faisait rien (une declaration n'est vue
    // que de son code tant que les noms qualifies Script.Variable n'existent pas) et laissait
    // croire le contraire. La visibilite reste dans le modele et le fichier, pour ce jour-la.
    // 1.13.0 (lot J6b) : l'Unite suit le Type (REAL | bar).
    switch (t) {
        case Tab::Constants:
            return {Column::Name, Column::Type, Column::Unit, Column::Value, Column::Description};
        case Tab::Variables:
            if (script) return {Column::Name, Column::Type, Column::Unit, Column::Value, Column::Storage, Column::Description};
            return {Column::Name, Column::Type, Column::Unit, Column::Value, Column::Description};
        case Tab::Parameters:
            return {Column::Name, Column::Type, Column::Unit, Column::Mode, Column::Value, Column::Description};
    }
    return {};
}

std::string columnTitle(Column c, Tab t) {
    switch (c) {
        case Column::Name: return "Nom";
        case Column::Type: return "Type";
        case Column::Value:
            return t == Tab::Constants ? std::string("Valeur") : t == Tab::Variables ? std::string("Initiale") : std::string("D\xC3\xA9" "faut");
        case Column::Storage:     return "Stockage";
        case Column::Mode:        return "Mode";
        case Column::Visibility:  return "Visibilit\xC3\xA9";
        case Column::Description: return "Documentation";
        case Column::Unit:        return "Unit\xC3\xA9";   // 1.13.0 (lot J6b)
    }
    return {};
}

std::string cellText(const Declaration& d, Column c) {
    switch (c) {
        case Column::Name:        return d.name;
        case Column::Type:        return d.type;
        case Column::Value:       return d.computed ? "\xC6\x92x calcul\xC3\xA9" "e au d\xC3\xA9marrage" : d.value;   // 1.12.7
        case Column::Storage:     return std::string(storageLabel(d.storage));
        case Column::Mode:        return std::string(modeLabel(d.mode));
        case Column::Visibility:  return std::string(visibilityLabel(d.visibility));
        case Column::Description: return d.description;
        case Column::Unit:        return d.unit;           // 1.13.0 (lot J6b)
    }
    return {};
}

// --------------------------------------------------------------- les libelles ----
std::string_view storageLabel(Storage s) noexcept {
    switch (s) {
        case Storage::Execution:  return "Ex\xC3\xA9" "cution";
        case Storage::Kept:       return "Conserv\xC3\xA9" "e";
        case Storage::Persistent: return "Persistante";
    }
    return {};
}

std::string_view storageHelp(Storage s) noexcept {
    switch (s) {
        case Storage::Execution:
            return "remise \xC3\xA0 sa valeur initiale \xC3\xA0 chaque ex\xC3\xA9" "cution (l'ancien VAR_TEMP)";
        case Storage::Kept:
            return "gard\xC3\xA9" "e d'une ex\xC3\xA9" "cution \xC3\xA0 l'autre, repart de sa valeur initiale au lancement (l'ancien VAR)";
        case Storage::Persistent:
            return "gard\xC3\xA9" "e d'une ex\xC3\xA9" "cution \xC3\xA0 l'autre ET d'un lancement \xC3\xA0 l'autre (simulation, poste d'exploitation)";
    }
    return {};
}

std::string_view modeLabel(PassMode m) noexcept {
    switch (m) {
        case PassMode::In:    return "Entr\xC3\xA9" "e";
        case PassMode::InOut: return "Entr\xC3\xA9" "e/sortie";
        case PassMode::Out:   return "Sortie";
    }
    return {};
}

std::string_view visibilityLabel(Visibility v) noexcept {
    return v == Visibility::Public ? std::string_view("Public") : std::string_view("Priv\xC3\xA9");
}

std::optional<Storage> storageFromText(std::string_view text) noexcept {
    const std::string s = squeezed(text);
    if (oneOf(s, {"execution", "exec", "temp", "vartemp", "temporaire", "temporary", "local", "locale", "run"})) return Storage::Execution;
    if (oneOf(s, {"conservee", "conserve", "keep", "kept", "var", "retain", "varretain", "static", "statique", "garde", "gardee"}))
        return Storage::Kept;
    if (oneOf(s, {"persistante", "persistant", "persistent", "persistance", "persistence", "remanente", "remanent", "permanent",
                  "permanente", "varpersistent"}))
        return Storage::Persistent;
    return std::nullopt;
}

std::optional<PassMode> modeFromText(std::string_view text) noexcept {
    const std::string s = squeezed(text);
    if (oneOf(s, {"entree", "in", "input", "varinput", "e", "lue", "lecture"})) return PassMode::In;
    if (oneOf(s, {"entreesortie", "entreeetsortie", "inout", "varinout", "es", "inputoutput", "reference", "lueetecrite"}))
        return PassMode::InOut;
    if (oneOf(s, {"sortie", "out", "output", "varoutput", "s", "ecrite", "ecriture"})) return PassMode::Out;
    return std::nullopt;
}

std::optional<Visibility> visibilityFromText(std::string_view text) noexcept {
    const std::string s = squeezed(text);
    if (oneOf(s, {"public", "publique", "publ", "expose", "exposee", "visible"})) return Visibility::Public;
    if (oneOf(s, {"prive", "privee", "private", "priv", "locale", "local", "interne", "internal", "cachee", "cache"})) return Visibility::Private;
    return std::nullopt;
}

std::vector<Storage> storagesFor(decl::Role role) noexcept {
    if (role == decl::Role::Script) return {Storage::Execution, Storage::Kept, Storage::Persistent};
    return {Storage::Execution};
}

// 1.11.19 (refonte, lot 6) : le registre des types - les types de base qu'une declaration propose,
// puis les structures et les enumerations du projet ; un type lu par lui (un tableau dont
// l'element est inconnu n'est plus accepte sur sa seule forme).
std::vector<std::string> typeChoices(const Project& p) { return typereg::Registry::build(p)->names(typereg::UseDeclaration); }

bool typeAllowed(const Project& p, std::string_view type) {
    return typereg::Registry::build(p)->resolve(type, typereg::UseDeclaration).ok;
}

// ------------------------------------------------------- ce que la grille montre ----
std::vector<Use> usesIn(std::string_view body, std::string_view name) {
    std::vector<Use> out;
    if (name.empty() || body.empty()) return out;
    const auto toks = decl::lex(body);
    std::vector<const decl::Token*> sig;
    sig.reserve(toks.size());
    for (const auto& t : toks)
        if (t.kind != decl::TokenKind::Comment) sig.push_back(&t);
    int depth = 0;                    // les parentheses ouvertes : f(Nom := 1) nomme un argument
    for (std::size_t i = 0; i < sig.size(); ++i) {
        const auto& t = *sig[i];
        const auto text = t.text(body);
        if (t.kind == decl::TokenKind::Punct) {
            if (text == "(") ++depth;
            else if (text == ")" && depth > 0) --depth;
            continue;
        }
        if (t.kind != decl::TokenKind::Word || !same(text, name)) continue;
        if (i > 0 && sig[i - 1]->kind == decl::TokenKind::Punct && sig[i - 1]->text(body) == ".") continue;   // un membre x.Nom
        if (i + 1 < sig.size() && sig[i + 1]->kind == decl::TokenKind::Punct) {
            const auto next = sig[i + 1]->text(body);
            if (next == "=>" || (next == ":=" && depth > 0)) continue;                                        // un argument nomme
        }
        out.push_back({t.line, t.column, static_cast<int>(t.end - t.begin)});
    }
    // Les trous d'IHM_JOURNAL et d'IHM_LOG ({Nom:0.0}) : des utilisations aussi.
    forEachHole(body, [&](std::size_t b, std::size_t e, const decl::Token& str) {
        for (const auto& u : usesIn(body.substr(b, e - b), name))
            if (u.line == 1) out.push_back({str.line, str.column + static_cast<int>(b - str.begin) + u.column - 1, u.length});
    });
    std::sort(out.begin(), out.end(), [](const Use& a, const Use& b) { return a.line != b.line ? a.line < b.line : a.column < b.column; });
    return out;
}

std::size_t usageCount(const Code& c, std::size_t index) {
    if (!c.valid() || index >= c.decls->size()) return 0;
    const std::string& name = (*c.decls)[index].name;
    std::size_t n = usesIn(*c.body, name).size();
    for (std::size_t j = 0; j < c.decls->size(); ++j)
        if (j != index) n += usesIn((*c.decls)[j].value, name).size();
    return n;
}

std::vector<std::string> faults(const Project& p, const Code& c) {
    std::vector<std::string> out;
    if (!c.valid()) return out;
    const TypeKnown known = [&p](std::string_view n) { return types::knownTypeName(p, n); };   // 1.12.20
    const auto found = decl::declarationFaults(*c.decls, c.role, *c.body, known, c.inherited);
    out.resize(c.decls->size());
    for (std::size_t i = 0; i < c.decls->size(); ++i) {
        std::vector<std::string> why = i < found.size() ? found[i] : std::vector<std::string>{};
        const auto& d = (*c.decls)[i];
        for (const auto& r : c.reserved)
            if (!d.name.empty() && same(d.name, r))
                why.push_back(c.role == decl::Role::Function ? "le nom de la fonction (son r\xC3\xA9sultat s'\xC3\xA9" "crit " + r + " := ...)"
                                                             : "nom pris par l'op\xC3\xA9rateur (" + r + ")");
        if (!c.declares) why.push_back("un script C ou C++ garde ses d\xC3\xA9" "clarations dans son code");
        for (const auto& w : why) out[i] += (out[i].empty() ? "" : " ; ") + w;
    }
    return out;
}

// ------------------------------------------------------------------ les noms ----
bool nameAllowed(const Code& c, std::string_view name, std::size_t self, std::string* why) {
    if (!c.valid()) return fail(why, "code introuvable");
    if (name.empty()) return fail(why, "un nom est obligatoire");
    bool ident = identStart(name[0]);
    for (const char ch : name) ident = ident && identChar(ch);
    if (!ident) return fail(why, "nom " + quoted(name) + " illisible : des lettres sans accent, des chiffres et _ (pas un chiffre en t\xC3\xAAte)");
    if (isReservedWord(name)) return fail(why, quoted(name) + " est un mot r\xC3\xA9serv\xC3\xA9 du langage");
    for (std::size_t j = 0; j < c.decls->size(); ++j)
        if (j != self && same((*c.decls)[j].name, name))
            return fail(why, quoted(name) + " est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "clar\xC3\xA9 dans ce code (onglet "
                                 + tabLabel((*c.decls)[j].kind == DeclKind::Constant ? Tab::Constants
                                            : (*c.decls)[j].kind == DeclKind::Variable ? Tab::Variables : Tab::Parameters, c.role)
                                 + ")");
    if (c.inherited)
        for (const auto& d : *c.inherited)
            if (d.kind == DeclKind::Parameter && same(d.name, name))
                return fail(why, quoted(name) + " est un param\xC3\xA8tre de la fonction red\xC3\xA9" "finie");
    for (const auto& r : c.reserved)
        if (same(r, name))
            return fail(why, c.role == decl::Role::Function
                                 ? quoted(name) + " est le nom de la fonction (son r\xC3\xA9sultat s'\xC3\xA9" "crit " + r + " := ...)"
                                 : quoted(name) + " est pris par l'op\xC3\xA9rateur");
    if (c.role == decl::Role::Operator && (same(name, "A") || same(name, "B") || same(name, kResultName)))
        return fail(why, quoted(name) + " est pris par l'op\xC3\xA9rateur : a et b sont ses op\xC3\xA9randes, Resultat son r\xC3\xA9sultat");
    if (const auto* d = decl::extract(*c.body).find(name))
        return fail(why, quoted(name) + " est encore d\xC3\xA9" "clar\xC3\xA9 dans un bloc " + std::string(decl::keyword(d->section))
                             + " du code : migrez d'abord ce bloc (bandeau de l'onglet Code)");
    return true;
}

std::string freeName(const Code& c, std::string_view base) {
    std::string candidate(base);
    for (int k = 2; k < 10000; ++k) {
        if (nameAllowed(c, candidate, npos, nullptr)) return candidate;
        candidate = std::string(base) + std::to_string(k);
    }
    return candidate;
}

// ------------------------------------------------------------------ les gestes ----
bool add(Project& p, const Place& at, Tab t, int after, std::string_view name, Id* made, std::string* why) {
    Code c = locate(p, at);
    if (!ready(c, t, why)) return false;
    const std::string n = name.empty() ? freeName(c, baseName(t, c.role)) : trimmed(name);
    if (!nameAllowed(c, n, npos, why)) return false;
    Declaration d;
    d.id = p.allocate();
    d.kind = kindOf(t);
    d.name = n;
    switch (t) {
        case Tab::Constants:  d.type = "REAL"; d.value = "0.0"; break;
        case Tab::Variables:  d.type = "INT"; d.value = "0"; d.storage = Storage::Execution; break;
        case Tab::Parameters: d.type = "REAL"; d.mode = PassMode::In; break;
    }
    // Une declaration d'un script est publique (la demande : exposable, par son nom qualifie au
    // lot 7) ; la locale d'une fonction, d'un operateur, reste locale.
    d.visibility = c.role == decl::Role::Script || t == Tab::Parameters ? Visibility::Public : Visibility::Private;
    const auto rows = rowsOf(*c.decls, t);
    std::size_t pos = c.decls->size();
    if (!rows.empty()) {
        const std::size_t r = after < 0 || static_cast<std::size_t>(after) >= rows.size() ? rows.size() - 1 : static_cast<std::size_t>(after);
        pos = rows[r] + 1;
    }
    if (made) *made = d.id;
    c.decls->insert(c.decls->begin() + static_cast<std::ptrdiff_t>(pos), std::move(d));
    return true;
}

bool remove(Project& p, const Place& at, Tab t, const std::vector<std::size_t>& rows, std::string* why) {
    Code c = locate(p, at);
    if (!ready(c, t, why)) return false;
    std::vector<std::size_t> idx;
    for (const auto r : rows) {
        const std::size_t i = indexOf(c, t, r);
        if (i == npos) return fail(why, "ligne " + std::to_string(r + 1) + " introuvable");
        idx.push_back(i);
    }
    if (idx.empty()) return fail(why, "aucune ligne choisie");
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    for (auto it = idx.rbegin(); it != idx.rend(); ++it) c.decls->erase(c.decls->begin() + static_cast<std::ptrdiff_t>(*it));
    return true;
}

bool duplicate(Project& p, const Place& at, Tab t, const std::vector<std::size_t>& rows, std::vector<Id>* made, std::string* why) {
    Code c = locate(p, at);
    if (!ready(c, t, why)) return false;
    std::vector<std::size_t> idx;
    for (const auto r : rows) {
        const std::size_t i = indexOf(c, t, r);
        if (i == npos) return fail(why, "ligne " + std::to_string(r + 1) + " introuvable");
        idx.push_back(i);
    }
    if (idx.empty()) return fail(why, "aucune ligne choisie");
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    // De la derniere a la premiere : chaque copie juste apres son original, les indices
    // des lignes d'avant ne bougent pas.
    std::vector<Id> ids;
    for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
        Declaration copy = (*c.decls)[*it];
        copy.id = p.allocate();
        const std::string original = copy.name;
        copy.name = freeName(c, copy.name + "_copie");
        // 1.12.7 : une valeur calculee - la copie ecrit son propre nom (Seuils_copie := ...), pas l'original.
        if (copy.computed && !copy.calc.empty()) copy.calc = renamedCode(copy.calc, original, copy.name);
        ids.insert(ids.begin(), copy.id);
        c.decls->insert(c.decls->begin() + static_cast<std::ptrdiff_t>(*it + 1), std::move(copy));
    }
    if (made) *made = std::move(ids);
    return true;
}

bool move(Project& p, const Place& at, Tab t, std::size_t row, int delta, std::string* why) {
    Code c = locate(p, at);
    if (!ready(c, t, why)) return false;
    const auto rows = rowsOf(*c.decls, t);
    if (row >= rows.size()) return fail(why, "ligne " + std::to_string(row + 1) + " introuvable");
    const long target = static_cast<long>(row) + delta;
    if (delta == 0) return true;
    if (target < 0) return fail(why, "d\xC3\xA9j\xC3\xA0 en t\xC3\xAAte");
    if (target >= static_cast<long>(rows.size())) return fail(why, "d\xC3\xA9j\xC3\xA0 en bas");
    std::swap((*c.decls)[rows[row]], (*c.decls)[rows[static_cast<std::size_t>(target)]]);
    return true;
}

namespace {

std::string upperOf(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

// Le type naturel d'un entier ecrit : INT s'il y tient, sinon DINT, LINT.
std::string integerTypeOf(long long v) {
    if (v >= -32768 && v <= 32767) return "INT";
    if (v >= -2147483648LL && v <= 2147483647LL) return "DINT";
    return "LINT";
}

// Le type d'un litteral ecrit ; vide : pas un litteral (un nom, une expression). `integer` :
// un entier ecrit (sa valeur dans `value`).
std::string literalTypeOf(std::string_view text, bool& integer, long long& value) {
    integer = false;
    const std::string v = trimmed(text);
    if (v.empty()) return {};
    const std::string u = upperOf(v);
    if (u == "TRUE" || u == "FALSE") return "BOOL";
    if (v.front() == '\'') return "STRING";
    if (v.front() == '"') return "WSTRING";
    if (const auto hash = u.find('#'); hash != std::string::npos && hash > 0) {
        const std::string head = u.substr(0, hash);
        std::string rest = u.substr(hash + 1);
        if (std::all_of(head.begin(), head.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
            // 16#FF, 2#1010, 8#17 : un entier dans une base.
            const int base = std::atoi(head.c_str());
            std::erase(rest, '_');
            if ((base != 2 && base != 8 && base != 16) || rest.empty()) return {};
            char* end = nullptr;
            const long long n = std::strtoll(rest.c_str(), &end, base);
            if (!end || *end) return {};
            integer = true;
            value = n;
            return integerTypeOf(n);
        }
        if (head == "T" || head == "TIME" || head == "LT" || head == "LTIME") return "TIME";
        if (head == "D" || head == "DATE") return "DATE";
        if (head == "TOD" || head == "TIME_OF_DAY") return "TIME_OF_DAY";
        if (head == "DT" || head == "DATE_AND_TIME") return "DATE_AND_TIME";
        return v.substr(0, hash);                    // INT#3, T_MODE#Auto : un litteral type
    }
    // Un nombre : signe, chiffres (et _), un point ou un exposant pour un reel.
    std::string n;
    for (const char c : v)
        if (c != '_') n += c;
    std::size_t i = (n[0] == '-' || n[0] == '+') ? 1 : 0;
    if (i >= n.size() || !std::isdigit(static_cast<unsigned char>(n[i]))) return {};
    bool real = false;
    for (std::size_t k = i; k < n.size(); ++k) {
        const char c = n[k];
        if (std::isdigit(static_cast<unsigned char>(c))) continue;
        if (c == '.' || c == 'e' || c == 'E' || ((c == '-' || c == '+') && (n[k - 1] == 'e' || n[k - 1] == 'E'))) {
            real = true;
            continue;
        }
        return {};                                   // 3 + x, 2s... : une expression
    }
    if (real) return "REAL";
    errno = 0;
    const long long x = std::strtoll(n.c_str(), nullptr, 10);
    if (errno == ERANGE) return "LINT";
    integer = true;
    value = x;
    return integerTypeOf(x);
}

// La valeur nulle d'un type de base (une constante qui change de type) ; vide : aucune.
std::string zeroOf(std::string_view type) {
    // 1.12.7 : une MAP, une liste, un vecteur - vides.
    const std::string u = upperOf(trimmed(type));
    if (u.rfind("MAP", 0) == 0 || u.rfind("LIST", 0) == 0 || u.rfind("VECTOR", 0) == 0 || u.rfind("SET ", 0) == 0 || u.rfind("RING[", 0) == 0
        || u.rfind("RING [", 0) == 0)      // 1.12.7 : SET, RING
        return "[]";
    const auto n = typereg::numericOf(type);
    switch (n.family) {
        case typereg::Family::Bool:    return "FALSE";
        case typereg::Family::Integer: return "0";
        case typereg::Family::Real:    return "0.0";
        case typereg::Family::String:  return "''";
        case typereg::Family::Time:    return "T#0s";
        case typereg::Family::Date:                                       // 1.12.1
            return n.low == 1 ? "D#1970-01-01" : n.low == 2 ? "TOD#00:00:00" : "DT#1970-01-01-00:00:00";
        case typereg::Family::None:    break;
    }
    return {};
}

} // namespace

std::string valueMisfit(const Project& p, std::string_view type, std::string_view valueText) {
    const std::string value = trimmed(valueText);
    if (value.empty()) return {};
    const auto r = typereg::Registry::build(p)->resolve(type, typereg::UseAll);
    if (!r.ok) return {};                                         // un type inconnu : on ne sait pas
    // 1.12.14 : une structure ecrite (Nom := 'A', Niveau := 2.5) - seule, dans un tableau, une liste, une
    // MAP, a toute profondeur : le moteur la range dans le type ; ce qui ne va pas est dit.
    if ((value.front() == '(' || value.front() == '[') && r.entry && r.entry->category == typereg::Category::Structure) {
        std::string why;
        return hmi::typeform::valueFits(p, r.text, value, &why) ? std::string{} : why;
    }
    auto category = r.category;
    std::string target = r.text;
    const bool array = category == typereg::Category::Collection;
    if (array) {
        // Un tableau : son element (une valeur seule remplit toutes les cases). 1.12.7 : une MAP, une
        // liste, un vecteur, un tuple - pas une valeur simple (avant : une constante MAP gardait le 0.0
        // de la constante neuve) ; leurs elements entre crochets (un nom, une expression : on ne sait pas).
        if (!r.entry || upperOf(r.text).rfind("ARRAY", 0) != 0) {
            bool integer = false;
            long long n = 0;
            if (value.front() != '[' && value.front() != '(' && !literalTypeOf(value, integer, n).empty())
                return "un " + r.text + " prend ses \xC3\xA9l\xC3\xA9ments entre crochets ([1, 2] ; une MAP : ['cl\xC3\xA9' := 1] ; vide : [])"
                     + ", pas " + value;
            return {};
        }
        category = r.entry->category;
        target = r.entry->name;
    }
    switch (category) {
        case typereg::Category::Structure:
        case typereg::Category::PlcType:
            return array ? "les cases d'un tableau de " + target + " prennent les valeurs initiales de leur type"
                         : "une structure (" + target + ") prend les valeurs initiales de son type";
        case typereg::Category::Enumeration: {
            if (const auto* e = findEnumeration(p, target)) {
                std::int64_t n = 0;
                if (enumNumberOf(*e, value, n)) return {};
                std::string names;
                for (std::size_t i = 0; i < e->values.size() && i < 4; ++i) names += (names.empty() ? "" : ", ") + e->values[i].name;
                return value + " n'est pas une valeur de " + e->name + (names.empty() ? std::string{} : " (" + names + "...)");
            }
            return {};
        }
        case typereg::Category::Elementary:
        case typereg::Category::TextTime: {
            bool integer = false;
            long long n = 0;
            const std::string lit = literalTypeOf(value, integer, n);
            if (lit.empty()) return {};                           // un nom (une constante), une expression
            if (integer && typereg::isInteger(target)) {
                if (!typereg::valueFits(n, target)) return value + " est hors des bornes d'un " + target;
                return {};
            }
            if (typereg::conversion(lit, target).kind == typereg::Conversion::Forbidden)
                return "un " + lit + " (" + value + ") ne va pas dans un " + target;
            return {};
        }
        case typereg::Category::Collection:
        case typereg::Category::Reference:
        case typereg::Category::Callback:          // 1.12.19
        case typereg::Category::Generic:
        case typereg::Category::Void:
            break;
    }
    return {};
}

bool set(Project& p, const Place& at, Tab t, std::size_t row, Column col, std::string_view text, std::string* why, std::string* note) {
    Code c = locate(p, at);
    if (!ready(c, t, why)) return false;
    const std::size_t i = indexOf(c, t, row);
    if (i == npos) return fail(why, "ligne " + std::to_string(row + 1) + " introuvable");
    Declaration& d = (*c.decls)[i];
    const std::string v = trimmed(text);
    switch (col) {
        case Column::Name: {
            if (v == d.name) return true;
            if (!nameAllowed(c, v, i, why)) return false;
            const std::string old = d.name;
            // Une declaration employee, renommee d'un nom que le code emploie deja pour autre
            // chose (une variable IHM, une fonction...) : les deux se confondraient.
            if (!same(old, v) && usageCount(c, i) > 0 && !usesIn(*c.body, v).empty())
                return fail(why, quoted(v) + " est d\xC3\xA9j\xC3\xA0 employ\xC3\xA9 dans le code (une variable IHM, une fonction...) : "
                                 "renommer " + old + " ainsi les confondrait");
            renameUses(p, at, c, i, old, v);
            (*c.decls)[i].name = v;
            return true;
        }
        case Column::Type: {
            if (v.empty()) return fail(why, "un type est obligatoire");
            const auto r = typereg::Registry::build(p)->resolve(v, typereg::UseDeclaration);
            if (!r.ok)
                return fail(why, r.missing ? r.why + " - \xC2\xAB Choisir un type\xE2\x80\xA6 \xC2\xBB les montre"
                                           : "type " + quoted(v) + " non pris en charge : " + r.why);
            d.type = r.text;                 // la forme du registre : REAL, ARRAY[0..9] OF REAL, T_Four (son nom ecrit)
            // 1.11.21 : sa valeur ne convient plus au nouveau type (0 pour un ARRAY[1..4] OF T_Four) :
            // retiree - une constante prend la valeur nulle de son type. La meme commande : un Ctrl+Z.
            if (const std::string bad = valueMisfit(p, d.type, d.value); !bad.empty()) {
                const std::string old = d.value;
                d.value = d.kind == DeclKind::Constant ? zeroOf(d.type) : std::string{};
                if (note)
                    *note = "valeur " + old + " " + (d.value.empty() ? std::string("retir\xC3\xA9" "e") : "remplac\xC3\xA9" "e par " + d.value)
                          + " : " + bad;
            }
            // 1.13.0 (lot J6b) : un type qui ne prend pas d'unite (BOOL, STRING, une structure) la retire.
            if (!d.unit.empty() && !units::typeTakesUnit(d.type)) {
                const std::string old = d.unit;
                d.unit.clear();
                if (note) *note += (note->empty() ? std::string{} : " ; ") + "unit\xC3\xA9 " + old + " retir\xC3\xA9" "e : un " + d.type + " n'en prend pas";
            }
            return true;
        }
        case Column::Unit: {                 // 1.13.0 (lot J6b) : l'unite physique (vide : sans unite)
            if (v.empty()) {
                d.unit.clear();
                return true;
            }
            if (!units::typeTakesUnit(d.type))
                return fail(why, "une unit\xC3\xA9 ne va que sur un nombre (ici " + d.type + ")");
            const units::Catalog cat(p.config);
            const std::string sym = cat.canonical(v);
            if (sym.empty())
                return fail(why, "unit\xC3\xA9 inconnue " + quoted(v) + " : choisis-la dans la liste (Ctrl+Espace) ou ajoute-la dans "
                                 "Configuration \xE2\x80\xBA Unit\xC3\xA9s de mesure");
            d.unit = sym;
            return true;
        }
        case Column::Value: {
            if (d.kind == DeclKind::Constant && v.empty()) return fail(why, "une constante a une valeur (100.0, 3, T#5s, un texte...)");
            // 1.12.19 (la regle des cases) : lue comme toute case de valeur (hmi::cellValue) - =formule ;
            // un nom connu (une declaration voisine, une variable IHM) ; sinon une valeur fixe du type :
            // 2,5 -> 2.5, VRAI -> TRUE, 5s -> T#5s, Voiture -> 'Voiture', Auto -> T_MODE#Auto. Avant : la
            // virgule et VRAI seuls (ce qu'un tableur donne) ; =3 illisible ; Voiture un nom sans controle.
            const auto neighbour = [&](std::string_view n) {
                for (std::size_t k = 0; k < c.decls->size(); ++k)
                    if (k != i && same((*c.decls)[k].name, n)) return true;
                if (c.inherited)
                    for (const auto& o : *c.inherited)
                        if (same(o.name, n)) return true;
                return false;
            };
            const CellText cell = cellValue(&p, d.type, v, neighbour);
            if (!cell.ok()) return fail(why, cell.why.find(v) != std::string::npos ? cell.why : "valeur " + quoted(v) + " : " + cell.why);
            const std::string& value = cell.text;
            std::string bad;
            if (!value.empty() && !declarationValueReadable(d.type, value, &bad))
                return fail(why, "valeur " + quoted(v) + " illisible pour un " + d.type + (bad.empty() ? std::string{} : " : " + bad));
            if (const std::string misfit = valueMisfit(p, d.type, value); !misfit.empty())   // 1.11.21
                return fail(why, "valeur " + quoted(v) + " : " + misfit);
            // 1.12.7 : une valeur ecrite remplace la valeur calculee (le calcul reste, inactif : \xC6\x92x le reprend).
            if (d.computed && note) *note = d.name + " : la valeur \xC3\xA9" "crite remplace le calcul (\xC2\xAB \xC6\x92x Calcul\xE2\x80\xA6 \xC2\xBB le reprend)";
            d.computed = false;
            d.value = value;
            return true;
        }
        case Column::Storage: {
            if (d.kind != DeclKind::Variable) return fail(why, "seule une variable a un stockage");
            const auto s = storageFromText(v);
            if (!s) return fail(why, "stockage " + quoted(v) + " inconnu : Ex\xC3\xA9" "cution, Conserv\xC3\xA9" "e ou Persistante");
            if (c.role != decl::Role::Script && *s != Storage::Execution)
                return fail(why, "une fonction n'a pas de m\xC3\xA9moire : ses locales repartent \xC3\xA0 chaque appel (Ex\xC3\xA9" "cution)");
            d.storage = *s;
            return true;
        }
        case Column::Mode: {
            if (d.kind != DeclKind::Parameter) return fail(why, "seul un param\xC3\xA8tre a un mode");
            const auto m = modeFromText(v);
            if (!m) return fail(why, "mode " + quoted(v) + " inconnu : Entr\xC3\xA9" "e, Entr\xC3\xA9" "e/sortie ou Sortie");
            d.mode = *m;
            return true;
        }
        case Column::Visibility: {
            const auto vis = visibilityFromText(v);
            if (!vis) return fail(why, "visibilit\xC3\xA9 " + quoted(v) + " inconnue : Public ou Priv\xC3\xA9");
            if (c.role != decl::Role::Script && *vis != d.visibility)
                return fail(why, "une d\xC3\xA9" "claration de fonction ou d'op\xC3\xA9rateur reste locale (priv\xC3\xA9" "e)");
            if (d.kind == DeclKind::Parameter && *vis != d.visibility) return fail(why, "un param\xC3\xA8tre n'a pas de visibilit\xC3\xA9");
            d.visibility = *vis;
            return true;
        }
        case Column::Description: {
            std::string doc;
            for (const char ch : v) doc += (ch == '\r' || ch == '\n' || ch == '\t') ? ' ' : ch;
            d.description = trimmed(doc);
            return true;
        }
    }
    return fail(why, "colonne inconnue");
}

// ---- 1.12.7 : la valeur calculee au demarrage ----------------------------------------------
std::string localsText(const std::vector<Declaration>& locals) {
    std::string out;
    for (const auto& l : locals) {
        out += l.name + " : " + l.type;
        if (!l.value.empty()) out += " := " + l.value;
        out += ';';
        if (!l.description.empty()) out += "   // " + l.description;
        out += '\n';
    }
    return out;
}

bool parseLocals(const Project& p, std::string_view text, std::vector<Declaration>& out, std::string* why) {
    out.clear();
    const std::string blank = trimmed(text);
    if (blank.empty()) return true;
    for (const auto& t : decl::lex(text))
        if (t.kind == decl::TokenKind::Word) {
            const std::string u = upperOf(t.text(text));
            if (u == "VAR" || u == "END_VAR" || u == "VAR_TEMP" || u == "VAR_INPUT" || u == "VAR_OUTPUT" || u == "VAR_IN_OUT")
                return fail(why, "ligne " + std::to_string(t.line) + " : pas de " + u + " - une locale par ligne, Nom : TYPE := valeur;");
        }
    const std::string wrapped = "VAR\n" + std::string(text) + "\nEND_VAR\n";
    const auto x = decl::extract(wrapped);
    if (!x.errors.empty()) {
        const auto& e = x.errors.front();
        return fail(why, (e.line > 1 ? "ligne " + std::to_string(e.line - 1) + " : " : std::string{}) + e.message);
    }
    for (const auto& d : x.decls) {
        Declaration l;
        l.kind = DeclKind::Variable;
        l.storage = Storage::Execution;
        l.visibility = Visibility::Private;
        l.name = d.name;
        l.type = d.type;
        l.value = d.initial;
        l.description = d.comment;
        if (isReservedWord(l.name)) return fail(why, quoted(l.name) + " : nom r\xC3\xA9serv\xC3\xA9 du langage");
        for (const auto& o : out)
            if (same(o.name, l.name)) return fail(why, quoted(l.name) + " : d\xC3\xA9" "clar\xC3\xA9" "e deux fois");
        if (!typeAllowed(p, l.type)) return fail(why, quoted(l.name) + " : type " + quoted(l.type) + " non pris en charge");
        out.push_back(std::move(l));
    }
    return true;
}

bool moveVarBlocks(std::string& calc, std::string& locals, std::string* why) {
    const auto x = decl::extract(calc);
    if (x.blocks.empty()) return fail(why, "pas de bloc VAR \xE2\x80\xA6 END_VAR dans le code");
    if (!x.errors.empty()) {
        const auto& e = x.errors.front();
        return fail(why, (e.line > 0 ? "ligne " + std::to_string(e.line) + " : " : std::string{}) + e.message);
    }
    if (!x.functions.empty()) return fail(why, "une FUNCTION ne va pas dans un calcul");
    std::vector<Declaration> moved;
    for (const auto& b : x.decls) {
        if (b.section != decl::Section::Var && b.section != decl::Section::Temp)
            return fail(why, quoted(b.name) + " : seules les locales (VAR, VAR_TEMP) vont dans l'onglet Locales");
        Declaration l;
        l.name = b.name;
        l.type = b.type;
        l.value = b.initial;
        l.description = b.comment;
        moved.push_back(std::move(l));
    }
    // Le code sans ses blocs ; les lignes vides laissees en tete, retirees.
    std::string body = x.body;
    const auto first = body.find_first_not_of(" \t\r\n");
    body = first == std::string::npos ? std::string{} : body.substr(body.rfind('\n', first) == std::string::npos ? 0 : body.rfind('\n', first) + 1);
    calc = body;
    if (!locals.empty() && locals.back() != '\n') locals += '\n';
    locals += localsText(moved);
    return true;
}

bool setCalculation(Project& p, const Place& at, Tab t, std::size_t row, std::string_view calc, std::string_view locals,
                    std::string* why) {
    Code c = locate(p, at);
    if (!ready(c, t, why)) return false;
    if (c.role != decl::Role::Script) return fail(why, "une valeur calcul\xC3\xA9" "e au d\xC3\xA9marrage n'est permise qu'\xC3\xA0 un script");
    const std::size_t i = indexOf(c, t, row);
    if (i == npos) return fail(why, "ligne " + std::to_string(row + 1) + " introuvable");
    Declaration& d = (*c.decls)[i];
    if (d.kind == DeclKind::Parameter) return fail(why, "un param\xC3\xA8tre ne se calcule pas au d\xC3\xA9marrage");
    std::vector<Declaration> parsed;
    if (!parseLocals(p, locals, parsed, why)) return false;
    // Un bloc VAR ... END_VAR colle dans le code : ses declarations vont dans les locales (le code
    // garde ses lignes, le bloc blanchi) - « Deplacer dans Locales », fait au passage.
    std::string body(calc);
    if (const auto x = decl::extract(body); !x.blocks.empty() && x.errors.empty() && x.functions.empty()) {
        for (const auto& b : x.decls) {
            if (b.section != decl::Section::Var && b.section != decl::Section::Temp) continue;
            if (std::any_of(parsed.begin(), parsed.end(), [&](const Declaration& l) { return same(l.name, b.name); }))
                return fail(why, quoted(b.name) + " : d\xC3\xA9" "clar\xC3\xA9" "e deux fois (dans les locales et dans un bloc VAR du code)");
            Declaration l;
            l.kind = DeclKind::Variable;
            l.storage = Storage::Execution;
            l.visibility = Visibility::Private;
            l.name = b.name;
            l.type = b.type;
            l.value = b.initial;
            l.description = b.comment;
            if (!typeAllowed(p, l.type)) return fail(why, quoted(l.name) + " : type " + quoted(l.type) + " non pris en charge");
            parsed.push_back(std::move(l));
        }
        if (std::all_of(x.decls.begin(), x.decls.end(), [](const decl::Decl& b) {
                return b.section == decl::Section::Var || b.section == decl::Section::Temp;
            })) {
            // Les lignes vides laissees en tete par le bloc : retirees (le calcul commence a son code).
            body = x.body;
            const auto first = body.find_first_not_of(" \t\r\n");
            body = first == std::string::npos ? std::string{} : body.substr(body.rfind('\n', first) == std::string::npos ? 0 : body.rfind('\n', first) + 1);
        }
    }
    calc = body;
    for (const auto& l : parsed)
        if (same(l.name, d.name)) return fail(why, quoted(l.name) + " : c'est le nom de la d\xC3\xA9" "claration calcul\xC3\xA9" "e");
    std::string code(calc);
    while (!code.empty() && (code.back() == ' ' || code.back() == '\t' || code.back() == '\n' || code.back() == '\r')) code.pop_back();
    if (!code.empty()) code += '\n';
    // Une locale n'a pas d'identifiant : elle n'est vue que de son calcul (ni remanence, ni reference).
    d.computed = !code.empty();
    d.calc = code;
    d.locals = std::move(parsed);
    // Une constante redevenue ecrite sans valeur : la valeur nulle de son type.
    if (!d.computed && d.kind == DeclKind::Constant && trimmed(d.value).empty()) d.value = zeroOf(d.type);
    return true;
}

bool setReopen(Project& p, const Place& at, Tab t, std::size_t row, bool on, std::string* why) {
    Code c = locate(p, at);
    if (!ready(c, t, why)) return false;
    const std::size_t i = indexOf(c, t, row);
    if (i == npos) return fail(why, "ligne " + std::to_string(row + 1) + " introuvable");
    (*c.decls)[i].reopen = on;
    return true;
}

} // namespace hmi::decledit
