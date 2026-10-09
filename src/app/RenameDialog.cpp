// app/RenameDialog.cpp - renommer, en montrant d'abord tout ce qui change
// (voir RenameDialog.hpp) ; et la moitie IHM du plan.
#include "RenameDialog.hpp"
#include "TutorialsLot8.hpp"                // Lot API 8 : didacticiels et aide (F1)
#include "../menu/MenuManager.hpp"          // Lot API 8 : didacticiels et aide (F1 : l'aide par-dessus)

#include "../hmi/HmiCommands.hpp"
#include "../hmi/HmiDecl.hpp"   // 1.11.18 (refonte, lot 3) : les declarations du modele
#include "../hmi/HmiExpr.hpp"            // looksLikeFormat : le format d'un trou {X:0.0}
#include "../hmi/HmiModel.hpp"
#include "../hmi/HmiScenarios.hpp"       // stepKind : ce que vise un pas d'essai
#include "../hmi/HmiScript.hpp"          // splitDeclarations : les variables locales d'un script
#include "../hmi/HmiSymbols.hpp"         // replacePath, rewriteNames, isSymbolView
#include "../menu/MenuManager.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace app {

namespace pr = project::rename;

// =============================================================================
//  1. LA MOITIE IHM DU PLAN
// -----------------------------------------------------------------------------
//  Trois renommages touchent l'IHM : une variable de l'automate (globale) que
//  l'IHM lit, une variable IHM, une vue. Chacun passe sur tout ce qui cite un
//  nom - les objets (hmi::rewriteNames : expressions, textes a trous,
//  variables, actions, arguments), les actions et les scripts des vues, les
//  scripts generaux, les fonctions, les alarmes, les recettes, les
//  historiques, les utilisateurs, les unites et formats, la table des
//  adresses, les equipements, les essais, les rapports, les traductions.
//  LE MEME CODE montre (sur une copie) et fait (dans la commande).
// =============================================================================
namespace {

using Fn = std::function<std::string(std::string_view, bool code)>;

// Lot API 8 : Field - un champ de DDT (from, to : son nom ; type : le DDT ; plc :
// les types de l'automate, pour suivre les chemins que l'IHM lit).
enum class What : std::uint8_t { Plc, HmiVar, View, Field };
struct Ren {
    What        what{What::Plc};
    std::string from, to;
    std::string type{};
    std::shared_ptr<const pr::PlcTypes> plc{};
};

std::vector<Ren> hmiRenames(const std::vector<pr::Rename>& renames) {
    std::vector<Ren> out;
    for (const auto& r : renames) {
        if (r.target.kind == pr::Kind::Variable && r.target.global) out.push_back({What::Plc, r.target.name, r.to});
        else if (r.target.kind == pr::Kind::HmiVariable) out.push_back({What::HmiVar, r.target.name, r.to});
        else if (r.target.kind == pr::Kind::HmiView) out.push_back({What::View, r.target.name, r.to});
        else if (r.target.kind == pr::Kind::DdtField) out.push_back({What::Field, r.target.name, r.to, r.target.typeName, r.plc});
    }
    return out;
}

std::vector<std::string> joined(std::vector<std::string> path, std::string last) {
    path.push_back(std::move(last));
    return path;
}

std::vector<std::string_view> linesOf(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        auto nl = s.find('\n', start);
        if (nl == std::string_view::npos) nl = s.size();
        auto line = s.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        if (nl == s.size()) break;
        start = nl + 1;
    }
    return out;
}

// Ce que le passage a change (le plan) ; nul : la commande, qui ne fait que.
class Sink {
public:
    explicit Sink(std::vector<pr::Change>* out) : out_(out) {}
    void change(std::vector<std::string> path, std::string label, const std::string& before, const std::string& after, bool code) {
        if (!out_ || before == after) return;
        pr::Change c;
        c.tab = pr::Tab::Hmi;
        c.path = std::move(path);
        c.label = std::move(label);
        c.before = before;
        c.after = after;
        c.code = code;
        out_->push_back(std::move(c));
    }
    // Un code : ligne par ligne (renommer n'ajoute pas de ligne).
    void lines(const std::vector<std::string>& path, const std::string& before, const std::string& after) {
        if (!out_ || before == after) return;
        const auto a = linesOf(before), b = linesOf(after);
        if (a.size() != b.size()) {
            change(path, "le code", before, after, true);
            return;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] == b[i]) continue;
            pr::Change c;
            c.tab = pr::Tab::Hmi;
            c.path = path;
            c.label = "ligne " + std::to_string(i + 1);
            c.before = std::string(a[i]);
            c.after = std::string(b[i]);
            c.line = static_cast<int>(i + 1);
            c.code = true;
            out_->push_back(std::move(c));
        }
    }
    std::vector<std::string> conflicts;
private:
    std::vector<pr::Change>* out_;
};

// Les trous d'un texte a trous ({Expr}, {Expr:0.0}) passes par `f` ; les
// accolades doublees restent. Les memes regles que les textes de l'IHM.
std::string rewriteHoles(std::string_view text, const std::function<std::string(std::string_view)>& f) {
    if (text.find('{') == std::string_view::npos) return std::string(text);
    std::string out;
    out.reserve(text.size() + 8);
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) {
            out += c;
            out += c;
            i += 2;
            continue;
        }
        if (c != '{') {
            out += c;
            ++i;
            continue;
        }
        const auto close = text.find('}', i + 1);
        if (close == std::string_view::npos) {
            out.append(text.substr(i));
            break;
        }
        const std::string inside(text.substr(i + 1, close - i - 1));
        std::string expr = inside, fmt;
        const auto colon = inside.rfind(':');
        if (colon != std::string::npos && colon + 1 < inside.size() && inside[colon + 1] != '=') {
            const std::string candidate = inside.substr(colon + 1);
            if (hmi::looksLikeFormat(candidate)) {
                fmt = ":" + candidate;
                expr = inside.substr(0, colon);
            }
        }
        out += "{" + f(expr) + fmt + "}";
        i = close + 1;
    }
    return out;
}

// Parcourt un code ST : les commentaires sont recopies, chaque chaine ('...')
// passe par `onString` (son contenu, sans les apostrophes), chaque mot par
// `onWord` (qui peut consommer la suite : rend la fin de ce qu'il a pris).
std::string walkCode(std::string_view code, const std::function<std::string(std::string_view)>& onString,
                     const std::function<std::size_t(std::string_view, std::size_t, std::size_t, std::string&)>& onWord) {
    std::string out;
    out.reserve(code.size() + 8);
    std::size_t i = 0;
    while (i < code.size()) {
        const char c = code[i];
        if (c == '(' && i + 1 < code.size() && code[i + 1] == '*') {
            const auto end = code.find("*)", i + 2);
            const std::size_t stop = end == std::string_view::npos ? code.size() : end + 2;
            out.append(code.substr(i, stop - i));
            i = stop;
            continue;
        }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') {
            const auto end = code.find('\n', i);
            const std::size_t stop = end == std::string_view::npos ? code.size() : end;
            out.append(code.substr(i, stop - i));
            i = stop;
            continue;
        }
        if (c == '\'' || c == '"') {
            std::size_t j = i + 1;
            while (j < code.size() && code[j] != c) j += (code[j] == '$' && j + 1 < code.size()) ? 2 : 1;
            const std::size_t end = std::min(j, code.size());
            out += c;
            const auto inner = code.substr(i + 1, end - i - 1);
            out += c == '\'' && onString ? onString(inner) : std::string(inner);
            if (j < code.size()) out += c;
            i = j + 1;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i + 1;
            while (j < code.size() && (std::isalnum(static_cast<unsigned char>(code[j])) || code[j] == '_')) ++j;
            if (onWord) {
                const std::size_t taken = onWord(code, i, j, out);
                if (taken > i) {
                    i = taken;
                    continue;
                }
            }
            out.append(code.substr(i, j - i));
            i = j;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// Les trous des chaines d'un code ST : IHM_JOURNAL('Niveau {Cuve.niveau:0.0}').
std::string rewriteStringHoles(std::string_view code, const std::function<std::string(std::string_view)>& f) {
    if (code.find('{') == std::string_view::npos) return std::string(code);
    return walkCode(code, [&](std::string_view s) { return s.find('{') == std::string_view::npos ? std::string(s) : rewriteHoles(s, f); }, {});
}

// ---- Lot API 8 : LES CHAINES QUI DESIGNENT UNE VUE ------------------------------
//  Renommer une vue reecrit une chaine egale a son nom la ou elle designe une
//  vue, dans le ST et les expressions : le premier argument d'une fonction de
//  navigation (IHM_NAVIGUER('Vue_A'), IHM_POPUP...), et une chaine comparee (=,
//  <>) a une variable systeme qui contient un nom de vue (SYS.CurrentView,
//  PreviousView, StartView, TopView, TopPopup, HomeView) ou a IHM_VUE() ;
//  'Vue_A.Objet' comparee a SYS.FocusedObject / SYS.PressedObject. Ailleurs
//  (IHM_JOURNAL('Vue_A ouverte')), une chaine reste un texte.
struct StTok {
    enum class K : std::uint8_t { Ident, Str, Num, Op } k{K::Op};
    std::size_t b{0}, e{0};      // [b, e) ; une chaine : ses guillemets compris
};

std::vector<StTok> stTokens(std::string_view s) {
    std::vector<StTok> out;
    const std::size_t n = s.size();
    std::size_t i = 0;
    const auto uc = [](char c) { return static_cast<unsigned char>(c); };
    while (i < n) {
        const char c = s[i];
        const char nx = i + 1 < n ? s[i + 1] : '\0';
        if (std::isspace(uc(c))) { ++i; continue; }
        if (c == '(' && nx == '*') {
            const auto end = s.find("*)", i + 2);
            i = end == std::string_view::npos ? n : end + 2;
            continue;
        }
        if (c == '/' && nx == '/') {
            const auto end = s.find('\n', i);
            i = end == std::string_view::npos ? n : end;
            continue;
        }
        if (c == '\'' || c == '"') {
            std::size_t j = i + 1;
            while (j < n && s[j] != c) j += (s[j] == '$' && j + 1 < n) ? 2 : 1;
            const std::size_t e = std::min(n, j + 1);
            out.push_back({StTok::K::Str, i, e});
            i = e;
            continue;
        }
        if (std::isalnum(uc(c)) || c == '_') {
            std::size_t j = i + 1;
            while (j < n && (std::isalnum(uc(s[j])) || s[j] == '_' || s[j] == '#')) ++j;
            const bool num = std::isdigit(uc(c)) || s.substr(i, j - i).find('#') != std::string_view::npos;
            out.push_back({num ? StTok::K::Num : StTok::K::Ident, i, j});
            i = j;
            continue;
        }
        static const char* const kTwo[] = {":=", "<>", "<=", ">=", "=>", "**", ".."};
        std::size_t len = 1;
        for (const char* t : kTwo)
            if (c == t[0] && nx == t[1]) len = 2;
        out.push_back({StTok::K::Op, i, i + len});
        i += len;
    }
    return out;
}

// La variable systeme d'une vue : 1 un nom de vue, 2 "Vue.Objet", 0 : aucune.
int viewSysVar(std::string_view name) {
    static const char* const kViews[] = {"CurrentView", "PreviousView", "StartView", "TopView", "TopPopup", "HomeView"};
    for (const char* v : kViews)
        if (pr::sameName(name, v)) return 1;
    return pr::sameName(name, "FocusedObject") || pr::sameName(name, "PressedObject") ? 2 : 0;
}

std::string rewriteViewStrings(std::string_view s, std::string_view from, std::string_view to) {
    if (from.empty() || s.find(from) == std::string_view::npos) return std::string(s);
    static const char* const kFns[] = {"IHM_NAVIGUER", "IHM_POPUP", "IHM_CHANGER_POPUP", "IHM_CENTRER_POPUP",
                                       "IHM_POPUP_OUVERTE", "IHM_FERMER_POPUP"};
    const auto toks = stTokens(s);
    const auto word = [&](std::size_t k) { return s.substr(toks[k].b, toks[k].e - toks[k].b); };
    const auto is = [&](std::size_t k, StTok::K kind, std::string_view text) {
        return k < toks.size() && toks[k].k == kind && pr::sameName(word(k), text);
    };
    // Ce que designe l'operande qui finit en k (qui commence en k) : viewSysVar, ou 1 pour IHM_VUE().
    const auto endsAt = [&](std::size_t k) -> int {
        if (k >= 2 && toks[k].k == StTok::K::Ident && is(k - 1, StTok::K::Op, ".") && is(k - 2, StTok::K::Ident, "SYS")) return viewSysVar(word(k));
        if (k >= 2 && is(k, StTok::K::Op, ")") && is(k - 1, StTok::K::Op, "(") && is(k - 2, StTok::K::Ident, "IHM_VUE")) return 1;
        return 0;
    };
    const auto startsAt = [&](std::size_t k) -> int {
        if (is(k, StTok::K::Ident, "SYS") && is(k + 1, StTok::K::Op, ".") && k + 2 < toks.size() && toks[k + 2].k == StTok::K::Ident
            && !is(k + 3, StTok::K::Op, "."))
            return viewSysVar(word(k + 2));
        if (is(k, StTok::K::Ident, "IHM_VUE") && is(k + 1, StTok::K::Op, "(") && is(k + 2, StTok::K::Op, ")")) return 1;
        return 0;
    };
    const auto comparison = [&](std::size_t k) { return is(k, StTok::K::Op, "=") || is(k, StTok::K::Op, "<>"); };
    std::string out;
    std::size_t done = 0;
    for (std::size_t k = 0; k < toks.size(); ++k) {
        if (toks[k].k != StTok::K::Str || toks[k].e - toks[k].b < 2) continue;
        const auto inner = s.substr(toks[k].b + 1, toks[k].e - toks[k].b - 2);
        const bool whole = inner == from;
        const bool object = !whole && inner.size() > from.size() + 1 && inner.substr(0, from.size()) == from && inner[from.size()] == '.';
        if (!whole && !object) continue;
        // Ce qui est compare a cette chaine : a gauche (x = 'V'), ou a droite ('V' = x).
        int against = 0;
        if (k >= 2 && comparison(k - 1)) against = endsAt(k - 2);
        if (!against && comparison(k + 1)) against = startsAt(k + 2);
        bool hit = object ? against == 2 : against == 1;
        if (whole && !hit && k >= 2 && is(k - 1, StTok::K::Op, "(") && toks[k - 2].k == StTok::K::Ident)
            hit = std::any_of(std::begin(kFns), std::end(kFns), [&](const char* f) { return pr::sameName(word(k - 2), f); });
        if (!hit) continue;
        out.append(s.substr(done, toks[k].b + 1 - done));
        out.append(to);
        out.append(inner.substr(from.size()));
        done = toks[k].e - 1;
    }
    if (done == 0) return std::string(s);
    out.append(s.substr(done));
    return out;
}

// ---- Lot API 8 : LES SCRIPTS C ET C++ ------------------------------------------
//  Ni compiles ni executes : l'IHM ne leur donne pas d'acces aux variables par
//  un nom en chaine (pas de GetTag("...")), ils accompagnent le projet vers la
//  cible. On y reecrit ce qui se lit sans doute : les identifiants entiers (la
//  casse comptee, comme en C), hors chaines, commentaires, membres (a.x, p->x,
//  N::x) et appels (Nom(...)) ; et les chaines qui nomment tout entieres la
//  variable, la vue ou un chemin qui en part ("Vitesse", "Vue_A.Bouton",
//  "Cuve.Niveau") - la forme d'un acces par nom, s'il y en a un sur la cible.
struct CTok {
    enum class K : std::uint8_t { Ident, Str, Chr, Num, Op } k{K::Op};
    std::size_t b{0}, e{0};
};

std::vector<CTok> cTokens(std::string_view s) {
    std::vector<CTok> out;
    const std::size_t n = s.size();
    std::size_t i = 0;
    const auto uc = [](char c) { return static_cast<unsigned char>(c); };
    while (i < n) {
        const char c = s[i];
        const char nx = i + 1 < n ? s[i + 1] : '\0';
        if (std::isspace(uc(c))) { ++i; continue; }
        if (c == '/' && nx == '*') {
            const auto end = s.find("*/", i + 2);
            i = end == std::string_view::npos ? n : end + 2;
            continue;
        }
        if ((c == '/' && nx == '/') || c == '#') {       // un commentaire ; une ligne du preprocesseur
            const auto end = s.find('\n', i);
            i = end == std::string_view::npos ? n : end;
            continue;
        }
        if (c == '"' || c == '\'') {
            std::size_t j = i + 1;
            while (j < n && s[j] != c && s[j] != '\n') j += (s[j] == '\\' && j + 1 < n) ? 2 : 1;
            const std::size_t e = std::min(n, j + 1);
            out.push_back({c == '"' ? CTok::K::Str : CTok::K::Chr, i, e});
            i = e;
            continue;
        }
        if (std::isalnum(uc(c)) || c == '_') {
            std::size_t j = i + 1;
            while (j < n && (std::isalnum(uc(s[j])) || s[j] == '_' || (std::isdigit(uc(c)) && s[j] == '.'))) ++j;
            out.push_back({std::isdigit(uc(c)) ? CTok::K::Num : CTok::K::Ident, i, j});
            i = j;
            continue;
        }
        std::size_t len = 1;
        if ((c == '-' && nx == '>') || (c == ':' && nx == ':') || (c == '=' && nx == '=') || (c == '!' && nx == '=')) len = 2;
        out.push_back({CTok::K::Op, i, i + len});
        i += len;
    }
    return out;
}

// Les noms qu'un script C / C++ declare lui-meme (int Vitesse = 0; double x, y;
// bool Test(...)) : chez lui, ils sont les siens.
std::set<std::string> cDeclared(std::string_view s) {
    static const char* const kTypes[] = {"int", "long", "short", "char", "float", "double", "bool", "unsigned", "signed", "auto",
                                         "void", "size_t", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
                                         "uint32_t", "uint64_t", "string", "wstring", "BOOL", "INT", "DINT", "UINT", "UDINT",
                                         "SINT", "USINT", "REAL", "LREAL", "WORD", "DWORD", "BYTE", "STRING"};
    const auto toks = cTokens(s);
    const auto text = [&](std::size_t k) { return s.substr(toks[k].b, toks[k].e - toks[k].b); };
    std::set<std::string> out;
    for (std::size_t k = 0; k < toks.size(); ++k) {
        if (toks[k].k != CTok::K::Ident) continue;
        const auto w = text(k);
        if (std::none_of(std::begin(kTypes), std::end(kTypes), [&](const char* t) { return w == t; })) continue;
        // Le type, puis * & const, puis des noms separes de virgules.
        std::size_t j = k + 1;
        for (;;) {
            while (j < toks.size() && ((toks[j].k == CTok::K::Op && (text(j) == "*" || text(j) == "&"))
                                       || (toks[j].k == CTok::K::Ident && text(j) == "const")))
                ++j;
            if (j >= toks.size() || toks[j].k != CTok::K::Ident) break;
            const std::size_t name = j++;
            if (j >= toks.size() || toks[j].k != CTok::K::Op) break;
            const auto op = text(j);
            if (op != "=" && op != ";" && op != "," && op != "[" && op != ")" && op != "(") break;
            out.insert(std::string(text(name)));
            if (op != ",") break;
            ++j;
        }
    }
    return out;
}

// Les identifiants `from` (entiers, la casse comptee) devenus `to`, et chaque
// chaine passee par `onString` (son contenu, sans les guillemets) ; `from` vide :
// seulement les chaines.
std::string rewriteC(std::string_view s, std::string_view from, std::string_view to,
                     const std::function<std::string(std::string_view)>& onString) {
    const auto toks = cTokens(s);
    const auto text = [&](std::size_t k) { return s.substr(toks[k].b, toks[k].e - toks[k].b); };
    std::string out;
    std::size_t done = 0;
    const auto replace = [&](std::size_t b, std::size_t e, std::string_view by) {
        out.append(s.substr(done, b - done));
        out.append(by);
        done = e;
    };
    for (std::size_t k = 0; k < toks.size(); ++k) {
        const auto& t = toks[k];
        if (t.k == CTok::K::Str && onString && t.e - t.b >= 2) {
            const auto inner = s.substr(t.b + 1, t.e - t.b - 2);
            const std::string next = onString(inner);
            if (next != inner) replace(t.b + 1, t.e - 1, next);
            continue;
        }
        if (t.k != CTok::K::Ident || from.empty() || text(k) != from) continue;
        if (k > 0 && toks[k - 1].k == CTok::K::Op && (text(k - 1) == "." || text(k - 1) == "->" || text(k - 1) == "::")) continue;
        if (k + 1 < toks.size() && toks[k + 1].k == CTok::K::Op && text(k + 1) == "(") continue;
        replace(t.b, t.e, to);
    }
    if (done == 0) return std::string(s);
    out.append(s.substr(done));
    return out;
}

// Une chaine qui n'est qu'un chemin (Cuve.Niveau, Armoires[0].V3) : rien d'autre.
bool pathLike(std::string_view s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s.front())) || s.front() == '_')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '[' || c == ']' || c == ',';
    });
}

bool viewHasParam(const hmi::View& v, std::string_view name) {
    return std::any_of(v.params.begin(), v.params.end(), [&](const hmi::ViewParam& p) { return pr::sameName(p.name, name); });
}

bool hasHmiVariable(const hmi::Project& p, std::string_view name) {
    return std::any_of(p.programs.variables.begin(), p.programs.variables.end(),
                       [&](const hmi::Variable& v) { return pr::sameName(v.name, name); });
}

std::string actionLabel(std::size_t i, const hmi::Action& a) {
    return "action " + std::to_string(i + 1) + " (" + std::string(hmi::triggerLabel(a.trigger)) + ")";
}

void recordActions(Sink& s, const std::vector<std::string>& path, const std::vector<hmi::Action>& a, const std::vector<hmi::Action>& b) {
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const auto base = actionLabel(i, a[i]);
        s.change(path, base + " \xC2\xB7 surveille", a[i].watch, b[i].watch, true);
        s.change(path, base + " \xC2\xB7 condition", a[i].guard, b[i].guard, true);
        s.change(path, base + " \xC2\xB7 cible", a[i].target, b[i].target, true);
        s.change(path, base + " \xC2\xB7 valeur", a[i].value, b[i].value, true);
        s.change(path, base + " \xC2\xB7 param\xC3\xA8tres", a[i].params, b[i].params, true);   // 1.11.6
    }
}

void recordObject(Sink& s, const std::vector<std::string>& path, const hmi::Object& a, const hmi::Object& b) {
    for (std::size_t i = 0; i < a.props.size() && i < b.props.size(); ++i) {
        s.change(path, a.props[i].key + " (expression)", a.props[i].expr, b.props[i].expr, true);
        s.change(path, a.props[i].key, a.props[i].value, b.props[i].value, true);
    }
    recordActions(s, path, a.actions, b.actions);
}

// Une liste "A; B; C" : les elements egaux a `from` deviennent `to`, les
// espaces autour gardes.
std::string renameInList(std::string_view list, std::string_view from, std::string_view to) {
    std::string out;
    std::size_t start = 0;
    while (start <= list.size()) {
        auto end = list.find(';', start);
        if (end == std::string_view::npos) end = list.size();
        const auto part = list.substr(start, end - start);
        std::size_t a = 0, b = part.size();
        while (a < b && std::isspace(static_cast<unsigned char>(part[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(part[b - 1]))) --b;
        if (part.substr(a, b - a) == from) {
            out.append(part.substr(0, a));
            out.append(to);
            out.append(part.substr(b));
        } else {
            out.append(part);
        }
        if (end == list.size()) break;
        out += ';';
        start = end + 1;
    }
    return out;
}

// Le plan a zones : une zone par ligne, "Nom | points | groupe | vue | couleur".
std::string renameZoneViews(std::string_view zones, std::string_view from, std::string_view to) {
    std::string out;
    std::size_t start = 0;
    while (start <= zones.size()) {
        auto nl = zones.find('\n', start);
        if (nl == std::string_view::npos) nl = zones.size();
        const std::string line(zones.substr(start, nl - start));
        std::vector<std::string> fields;
        std::size_t f = 0;
        while (f <= line.size()) {
            auto bar = line.find('|', f);
            if (bar == std::string::npos) bar = line.size();
            fields.push_back(line.substr(f, bar - f));
            if (bar == line.size()) break;
            f = bar + 1;
        }
        if (fields.size() > 3) {
            std::string& v = fields[3];
            std::size_t a = 0, b = v.size();
            while (a < b && std::isspace(static_cast<unsigned char>(v[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(v[b - 1]))) --b;
            if (std::string_view(v).substr(a, b - a) == from) v = v.substr(0, a) + std::string(to) + v.substr(b);
        }
        for (std::size_t k = 0; k < fields.size(); ++k) out += (k ? "|" : "") + fields[k];
        if (nl == zones.size()) break;
        out += '\n';
        start = nl + 1;
    }
    return out;
}

std::string roleTitle(const hmi::View& v) {
    if (hmi::isSymbolView(v)) return "Symboles";
    if (v.role == "popup") return "Popups";
    if (v.role == "modele" || v.role == "entete" || v.role == "pied") return "Mod\xC3\xA8les";
    return "Vues";
}

class Rewriter {
public:
    Rewriter(hmi::Project& p, const Ren& r, Sink& s) : p_(p), r_(r), s_(s) {
        view_ = r.what == What::View;
        field_ = r.what == What::Field;
        // L'IHM lit d'abord ses variables : une variable IHM du nom d'une
        // globale la cache - les expressions ne parlent pas de l'automate. Une
        // vue du nom d'une variable IHM : Vue.X se confondrait, on n'y touche pas.
        paths_ = field_ || !((r.what == What::Plc || view_) && hasHmiVariable(p, r.from));
        // Une fonction IHM du meme nom : "Nom(...)" est un appel, que le texte
        // seul ne distingue pas d'une lecture - on ne touche a rien, et on le dit.
        if (r.what == What::Plc && paths_
            && std::any_of(p.programs.functions.begin(), p.programs.functions.end(),
                           [&](const hmi::HmiFunction& fn) { return pr::sameName(fn.name, r.from); })) {
            paths_ = false;
            s.conflicts.push_back("une fonction IHM s'appelle aussi " + r.from + " : ses appels et la variable se confondraient");
        }
        // Lot API 8 : une vue - ses chaines (IHM_NAVIGUER('Vue_A'), SYS.CurrentView
        // = 'Vue_A') la designent sans doute : elles suivent meme quand Vue.Objet
        // ne le peut pas.
        exprs_ = paths_ || view_;
        if (field_) {
            fa_.type = r.type;
            fa_.field = r.from;
            fa_.to = r.to;
            // Un chemin dont le type ne se lit pas suit le champ s'il est seul de
            // son nom (dans l'automate et parmi les types IHM).
            fa_.unique = (!r.plc || r.plc->owners(r.from).size() <= 1)
                      && std::none_of(p.programs.types.begin(), p.programs.types.end(), [&](const hmi::HmiType& t) {
                             return std::any_of(t.members.begin(), t.members.end(),
                                                [&](const hmi::TypeMember& m) { return pr::sameName(m.name, r.from); });
                         });
        }
        const std::string dotted = r.from + ".", dottedTo = r.to + ".";
        f_ = [this, dotted, dottedTo](std::string_view t, bool code) -> std::string {
            if (t.empty()) return std::string(t);
            if (field_) return fieldText(t, pr::Syntax::St, [this](std::string_view n) { return hmiRoot(n); });
            // Une vue : Vue.Objet.Propriete (le point dit que c'est elle), et les
            // chaines qui la designent (lot API 8).
            if (view_) return rewriteViewStrings(paths_ ? hmi::replacePath(t, dotted, dottedTo, code) : std::string(t), r_.from, r_.to);
            return hmi::replacePath(t, r_.from, r_.to, code);
        };
    }

    void run() {
        views();
        if (exprs_) {
            programs();
            supervision();
        }
        configuration();
        if (view_) viewReferences();
        if (exprs_) everywhere();                   // Lot API 8 : renommer partout (IHM)
        // Les noms en dernier : les endroits se disent sous leur nom d'avant (une
        // vue renommee reste "Vue_A" dans l'arbre du dialogue).
        names();
    }

private:
    std::string expr(const std::string& t) const { return f_(t, false); }
    std::string holes(const std::string& t) const {
        return rewriteHoles(t, [this](std::string_view e) { return f_(e, false); });
    }

    // ---- les noms eux-memes ----------------------------------------------------------
    void names() {
        if (r_.what == What::HmiVar) {
            for (auto& v : p_.programs.variables)
                if (v.name == r_.from) {
                    s_.change({"Variables IHM"}, "nom", v.name, r_.to, false);
                    v.name = r_.to;
                }
        } else if (r_.what == What::Plc) {
            // Liee par son nom (l'adresse est le nom de la globale) : elle suit.
            for (auto& v : p_.programs.variables)
                if (v.bound() && pr::sameName(v.address, r_.from)) {
                    s_.change({"Variables IHM", v.name}, "adresse (li\xC3\xA9" "e \xC3\xA0 " + v.equipment + ")", v.address, r_.to, false);
                    v.address = r_.to;
                }
        } else if (field_) {
            // Lot API 8 : liee par un chemin de l'automate qui passe par ce champ.
            for (auto& v : p_.programs.variables) {
                if (!v.bound() || v.address.find('.') == std::string::npos) continue;
                const std::string next = plcPath(v.address);
                s_.change({"Variables IHM", v.name}, "adresse (li\xC3\xA9" "e \xC3\xA0 " + v.equipment + ")", v.address, next, false);
                v.address = next;
            }
        } else {
            for (auto& v : p_.views)
                if (v.name == r_.from) {
                    s_.change({roleTitle(v)}, "nom", v.name, r_.to, false);
                    v.name = r_.to;
                }
        }
    }

    // ---- un script ST, le corps d'une fonction ----------------------------------------
    // 1.11.18 (refonte, lot 3) : `decls` - ses declarations du modele : des locales comme
    // celles de ses blocs ; leurs valeurs (initiales, par defaut) suivent le renommage.
    void script(std::string& body, bool function, const std::vector<std::string>& path,
                std::vector<hmi::Declaration>* decls = nullptr) {
        const bool modeled = decls && !decls->empty();
        if (body.empty() && !modeled) return;
        const std::string whole = modeled ? hmi::decl::composeCode(body, *decls, function ? hmi::decl::Role::Function
                                                                                          : hmi::decl::Role::Script).text
                                          : body;
        const auto parts = hmi::splitDeclarations(whole, function);
        // Une variable locale (un parametre) du meme nom : le script parle d'elle.
        // (Un champ : ses locales sont des racines, que hmiRoot connait.)
        const bool named = !view_ && !field_;
        if (named && parts.local(r_.from)) return;
        std::string next = body;
        bool values = false;
        if (exprs_) {
            parts_ = &parts;
            next = f_(next, true);
            next = rewriteStringHoles(next, [this](std::string_view e) { return f_(e, false); });
            if (modeled)
                for (auto& d : *decls) {
                    const std::string v = f_(d.value, false);
                    if (v == d.value) continue;
                    s_.change(path, "d\xC3\xA9" "claration " + d.name, d.value, v, true);
                    d.value = v;
                    values = true;
                }
            parts_ = nullptr;
        }
        if (next == body && !values) return;
        if (named && parts.local(r_.to))
            s_.conflicts.push_back(path.back() + " a une variable locale " + r_.to + " : il la lirait \xC3\xA0 la place");
        if (next == body) return;
        s_.lines(path, body, next);
        body = std::move(next);
    }

    // ---- Lot API 8 : un script C ou C++ (voir rewriteC) ---------------------------------
    void cscript(std::string& body, const std::vector<std::string>& path) {
        if (body.empty()) return;
        const auto declared = cDeclared(body);
        std::string next;
        if (field_) {
            cLocals_ = &declared;
            next = fieldText(body, pr::Syntax::C, [this](std::string_view n) { return hmiRoot(n); });
            cLocals_ = nullptr;
            // Un chemin de l'automate nomme dans une chaine ("ConfigGaz.Nom_gaz").
            next = rewriteC(next, {}, {}, [this](std::string_view s) {
                return pathLike(s) ? fieldText(s, pr::Syntax::St, [this](std::string_view n) { return hmiRoot(n); }) : std::string(s);
            });
        } else if (view_) {
            next = rewriteC(body, {}, {}, [this](std::string_view s) {
                if (s == r_.from) return r_.to;
                if (s.size() > r_.from.size() + 1 && s.substr(0, r_.from.size()) == r_.from && s[r_.from.size()] == '.')
                    return r_.to + std::string(s.substr(r_.from.size()));
                return std::string(s);
            });
        } else {
            // Sa variable a lui, du meme nom : il parle d'elle. Une variable IHM du
            // nom de la globale : l'IHM lit la sienne (paths_).
            if (!paths_ || declared.count(r_.from)) return;
            next = rewriteC(body, r_.from, r_.to, [this](std::string_view s) {
                return pathLike(s) ? hmi::replacePath(s, r_.from, r_.to, false) : std::string(s);
            });
            if (next != body && declared.count(r_.to))
                s_.conflicts.push_back(path.back() + " d\xC3\xA9" "clare " + r_.to + " : il le lirait \xC3\xA0 la place");
        }
        if (next == body) return;
        s_.lines(path, body, next);
        body = std::move(next);
    }

    // ---- Lot API 8 : un champ de DDT ------------------------------------------------
    // Ce qu'est une racine de l'IHM : une locale du script, une variable IHM, une
    // vue, SYS, une fonction - pas l'automate ; un parametre de la vue : ce que
    // l'appelant passe (inconnu) ; sinon une variable de l'automate (plcRoot).
    pr::RootType hmiRoot(std::string_view name) const {
        const pr::RootType other{pr::RootIs::Other, {}};
        if (parts_ && parts_->local(name)) return other;
        if (cLocals_ && cLocals_->count(std::string(name))) return other;
        if (scopeView_ && viewHasParam(*scopeView_, name)) return {};
        if (pr::sameName(name, "SYS") || hasHmiVariable(p_, name) || hmi::isHmiFunction(name)) return other;
        if (std::any_of(p_.views.begin(), p_.views.end(), [&](const hmi::View& v) { return pr::sameName(v.name, name); })) return other;
        if (std::any_of(p_.programs.functions.begin(), p_.programs.functions.end(),
                        [&](const hmi::HmiFunction& f) { return pr::sameName(f.name, name); }))
            return other;
        return plcRoot(name);
    }
    pr::RootType plcRoot(std::string_view name) const { return r_.plc ? r_.plc->root(name) : pr::RootType{}; }

    // Les acces au champ d'un texte ; un acces qui ne dit pas son type (voir
    // rewriteFieldAccesses) : le plan est refuse (Sink::conflicts), et dit lequel.
    std::string fieldText(std::string_view t, pr::Syntax syntax, const pr::RootTyper& root) const {
        static const pr::PlcTypes kNone;
        pr::FieldScan scan;
        std::string next = pr::rewriteFieldAccesses(t, fa_, r_.plc ? *r_.plc : kNone, root, syntax, &scan);
        if (!scan.ambiguous.empty()) {
            std::string shown = compactLine(t, scan.ambiguous.front());
            if (shown.size() > 60) shown = shown.substr(0, 57) + "...";
            s_.conflicts.push_back("l'IHM lit \xC2\xAB " + shown + " \xC2\xBB sans que son type se lise (" + fa_.field
                                   + " est aussi le membre d'un autre type) : renomme-le \xC3\xA0 la main");
        }
        return next;
    }
    // Un chemin de l'automate (une adresse, la table des adresses, un equipement).
    std::string plcPath(const std::string& t) const {
        return fieldText(t, pr::Syntax::St, [this](std::string_view n) { return plcRoot(n); });
    }
    static std::string compactLine(std::string_view t, int line) {
        const auto lines = linesOf(t);
        std::string_view l = line >= 1 && static_cast<std::size_t>(line) <= lines.size() ? lines[static_cast<std::size_t>(line - 1)] : t;
        while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.remove_prefix(1);
        while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.remove_suffix(1);
        return std::string(l);
    }

    // ---- les objets, les actions de vue, les scripts de vue ------------------------------
    bool object(const std::vector<std::string>& path, hmi::Object& o, bool content) {
        if (!content && !view_) return false;
        hmi::Object b = o;
        if (content) hmi::rewriteNames(b, f_);
        if (content) objectRest(b);                  // Lot API 8 : renommer partout (IHM)
        if (view_) {
            for (auto& a : b.actions)
                if (hmi::operationOpensView(a.operation) && a.target == r_.from) a.target = r_.to;
            if (b.kind == hmi::Kind::NavBar)
                if (auto* pv = b.find("views")) pv->value = renameInList(pv->value, r_.from, r_.to);
            if (b.kind == hmi::Kind::ZoneMap)
                if (auto* pz = b.find("mapZones")) pz->value = renameZoneViews(pz->value, r_.from, r_.to);
            if (b.kind == hmi::Kind::SymbolInstance)
                if (auto* ps = b.find("symbol"); ps && ps->value == r_.from) ps->value = r_.to;
        }
        if (b == o) return false;
        recordObject(s_, joined(path, o.name.empty() ? "objet " + std::to_string(o.id) : o.name), o, b);
        o = std::move(b);
        return true;
    }

    void views() {
        for (auto& v : p_.views) {
            const std::vector<std::string> path{roleTitle(v), v.name};
            // Un parametre de vue du meme nom la cache (popups, symboles). Un
            // champ : un parametre est une racine dont le type ne se lit pas (hmiRoot).
            const bool hidden = !view_ && !field_ && viewHasParam(v, r_.from);
            const bool content = exprs_ && !hidden;
            scopeView_ = &v;
            bool touched = false;
            for (auto& o : v.objects) touched = object(path, o, content) || touched;
            // Les actions de la vue : passees comme celles d'un objet.
            {
                hmi::Object holder;
                holder.actions = v.actions;
                const hmi::Object was = holder;
                if (content) hmi::rewriteNames(holder, f_);
                if (content) restActions(holder.actions);     // Lot API 8 : renommer partout (IHM)
                if (view_)
                    for (auto& a : holder.actions)
                        if (hmi::operationOpensView(a.operation) && a.target == r_.from) a.target = r_.to;
                if (!(holder == was)) {
                    recordActions(s_, joined(path, "actions de la vue"), was.actions, holder.actions);
                    v.actions = std::move(holder.actions);
                    touched = true;
                }
            }
            for (auto& sc : v.scripts)
                if (content || view_) {
                    const hmi::Script old = sc;
                    const auto where = joined(path, "script " + (sc.name.empty() ? sc.event : sc.name));
                    if (sc.lang == hmi::ScriptLang::ST) script(sc.body, false, where, &sc.decls);
                    else cscript(sc.body, where);                                   // lot API 8 : C, C++
                    touched = touched || !(old == sc);
                }
            scopeView_ = nullptr;
            // Les valeurs par defaut des parametres : lues chez l'appelant.
            if (!view_ && exprs_)
                for (auto& prm : v.params) {
                    const std::string next = expr(prm.defaultValue);
                    s_.change(path, "param\xC3\xA8tre " + prm.name, prm.defaultValue, next, true);
                    prm.defaultValue = next;
                }
            // Un parametre du NOUVEAU nom : ce que la vue citait, elle le
            // lirait dans son parametre.
            if (touched && !view_ && !field_ && viewHasParam(v, r_.to))
                s_.conflicts.push_back("la vue " + v.name + " a un param\xC3\xA8tre " + r_.to + " : ses objets le liraient \xC3\xA0 la place");
        }
    }

    // ---- la programmation generale -------------------------------------------------------
    void programs() {
        for (auto& sc : p_.programs.scripts) {
            // Lot API 8 : un script C ou C++ aussi (voir rewriteC).
            if (sc.lang != hmi::ScriptLang::ST) cscript(sc.body, {"Scripts g\xC3\xA9n\xC3\xA9raux", sc.name});
            else script(sc.body, false, {"Scripts g\xC3\xA9n\xC3\xA9raux", sc.name}, &sc.decls);
            const std::string w = expr(sc.watch);
            s_.change({"Scripts g\xC3\xA9n\xC3\xA9raux", sc.name}, "surveille", sc.watch, w, true);
            sc.watch = w;
        }
        for (auto& fn : p_.programs.functions) script(fn.body, true, {"Fonctions", fn.name}, &fn.decls);
    }

    // ---- alarmes, recettes, historiques, utilisateurs, essais, rapports --------------------
    void supervision() {
        for (auto& a : p_.alarms) {
            const std::string c = expr(a.condition), m = holes(a.message);
            s_.change({"Alarmes", a.name}, "condition", a.condition, c, true);
            s_.change({"Alarmes", a.name}, "message", a.message, m, true);
            a.condition = c;
            a.message = m;
        }
        for (auto& rc : p_.recipes)
            for (auto& fld : rc.fields) {
                const std::string v = expr(fld.variable);
                s_.change({"Recettes", rc.name}, "\xC3\xA9l\xC3\xA9ment " + fld.name, fld.variable, v, true);
                fld.variable = v;
            }
        for (std::size_t i = 0; i < p_.history.archived.size(); ++i) {
            auto& item = p_.history.archived[i];
            const std::string v = expr(item);
            s_.change({"Historiques"}, "variable archiv\xC3\xA9" "e " + std::to_string(i + 1), item, v, true);
            item = v;
        }
        for (auto& u : p_.security.users) {
            const std::string e = expr(u.expression);
            s_.change({"Utilisateurs", u.login}, "autorisation", u.expression, e, true);
            u.expression = e;
        }
        for (auto& sc : p_.scenarios)
            for (std::size_t i = 0; i < sc.steps.size(); ++i) {
                auto& st = sc.steps[i];
                const auto kind = hmi::stepKind(st.action);
                const std::string base = "pas " + std::to_string(i + 1);
                if (kind == hmi::StepKind::Write) {
                    const std::string t = expr(st.target), v = expr(st.value);
                    s_.change({"Essais", sc.name}, base + " \xC2\xB7 cible", st.target, t, true);
                    s_.change({"Essais", sc.name}, base + " \xC2\xB7 valeur", st.value, v, true);
                    st.target = t;
                    st.value = v;
                } else if (kind == hmi::StepKind::WaitUntil || kind == hmi::StepKind::Check) {
                    const std::string v = expr(st.value);
                    s_.change({"Essais", sc.name}, base + " \xC2\xB7 expression", st.value, v, true);
                    st.value = v;
                }
            }
        for (auto& rp : p_.reports) {
            const std::string m = expr(rp.measures);
            s_.change({"Rapports", rp.name}, "mesures", rp.measures, m, true);
            rp.measures = m;
        }
        // Les traductions sont rangees sous leur texte d'origine : un texte a
        // trous qui change de trou change de cle.
        std::map<std::string, std::map<std::string, std::string>> texts;
        bool moved = false;
        for (const auto& [key, tr] : p_.languages.texts) {
            const std::string k = holes(key);
            std::map<std::string, std::string> t2;
            for (const auto& [code, t] : tr) t2[code] = holes(t);
            if (k != key || t2 != tr) {
                moved = true;
                s_.change({"Langues"}, "texte traduit", key, k, true);
            }
            auto& slot = texts[k];
            for (auto& [code, t] : t2) slot.emplace(code, std::move(t));
        }
        if (moved) p_.languages.texts = std::move(texts);
        for (auto& st : p_.styles)
            for (auto& prop : st.props) {
                const std::string e = expr(prop.expr);
                s_.change({"Styles", st.name}, prop.key + " (expression)", prop.expr, e, true);
                prop.expr = e;
            }
    }

    // ---- ce que la configuration nomme ------------------------------------------------------
    //  Les unites et formats disent un nom comme une expression (l'IHM d'abord) ;
    //  la table des adresses et les equipements parlent de l'automate, toujours.
    void configuration() {
        if (r_.what == What::View) return;
        if (exprs_)
            for (auto& d : p_.displays) {
                const std::string v = expr(d.path);
                s_.change({"Unit\xC3\xA9s et formats"}, "variable", d.path, v, true);
                d.path = v;
            }
        if (r_.what != What::Plc && !field_) return;
        const auto plc = [this](const std::string& t) {
            if (t.empty()) return t;
            return field_ ? plcPath(t) : hmi::replacePath(t, r_.from, r_.to, false);     // lot API 8 : un champ
        };
        for (auto& a : p_.comm.addresses) {
            const std::string v = plc(a.variable);
            s_.change({"Communication", "Table des adresses"}, a.address.empty() ? std::string("variable") : a.address, a.variable, v, true);
            a.variable = v;
        }
        for (auto& e : p_.equipments)
            for (auto& b : e.behaviors) {
                if (b.kind != hmi::BehaviorKind::FollowPlc) continue;
                const std::string v = plc(b.source);
                s_.change({"\xC3\x89quipements", e.name}, "suit l'automate (" + b.address + ")", b.source, v, true);
                b.source = v;
            }
    }

    // ---- une vue : qui l'ouvre, la montre, l'essaie ------------------------------------------
    void viewReferences() {
        for (std::size_t i = 0; i < p_.station.screens.size(); ++i) {
            auto& sc = p_.station.screens[i];
            if (sc.view != r_.from) continue;
            s_.change({"Poste d'exploitation"}, "\xC3\xA9" "cran " + std::to_string(sc.display), sc.view, r_.to, false);
            sc.view = r_.to;
        }
        for (auto& sc : p_.scenarios)
            for (std::size_t i = 0; i < sc.steps.size(); ++i) {
                auto& st = sc.steps[i];
                const auto kind = hmi::stepKind(st.action);
                std::string next = st.target;
                if (kind == hmi::StepKind::OpenView && st.target == r_.from) next = r_.to;
                if (kind == hmi::StepKind::Click && st.target.rfind(r_.from + ".", 0) == 0) next = r_.to + st.target.substr(r_.from.size());
                s_.change({"Essais", sc.name}, "pas " + std::to_string(i + 1) + " \xC2\xB7 cible", st.target, next, false);
                st.target = next;
                if (kind == hmi::StepKind::Check && st.expected == "'" + r_.from + "'") {
                    const std::string e = "'" + r_.to + "'";
                    s_.change({"Essais", sc.name}, "pas " + std::to_string(i + 1) + " \xC2\xB7 attendu", st.expected, e, false);
                    st.expected = e;
                }
            }
    }

    // ---- Lot API 8 : renommer partout (IHM) ----
    // Ce que rewriteNames ne voit pas : le nom de fichier d'un bouton d'export
    // (texte a trous) ; les actions "mettre de cote" (30; raison {X} : une
    // expression, puis un texte a trous), "exporter" (le fichier, texte a
    // trous), "lier un tableau" (le fichier, ou une expression qui le donne) ;
    // le titre d'une popup et la consigne d'une alarme (textes a trous).
    void objectRest(hmi::Object& b) const {
        if (b.kind == hmi::Kind::ExportButton)
            if (auto* pf = b.find("fileName"); pf && !pf->value.empty()) pf->value = holes(pf->value);
        restActions(b.actions);
    }
    void restActions(std::vector<hmi::Action>& list) const {
        for (auto& a : list) {
            if (a.operation == hmi::Operation::ShelveAlarm && !a.value.empty()) {
                const auto semi = a.value.find(';');
                const std::string minutes = a.value.substr(0, semi);
                std::string next = trimmedText(minutes).empty() ? minutes : expr(minutes);
                if (semi != std::string::npos) next += ";" + holes(a.value.substr(semi + 1));
                a.value = next;
            } else if (a.operation == hmi::Operation::Export && !a.value.empty()) {
                a.value = holes(a.value);
            } else if (a.operation == hmi::Operation::BindTable && !trimmedText(a.value).empty() && !p_.externalByName(a.value)) {
                a.value = expr(a.value);
            } else if (a.operation == hmi::Operation::SetLanguage && !trimmedText(a.target).empty()) {
                // Tranche 2 : la cible de "changer de langue" (un code, ou une expression qui le donne) -
                // seul un nom qui est celui qu'on renomme bouge.
                a.target = expr(a.target);
            }
        }
    }
    static std::string trimmedText(std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return std::string(s);
    }
    void everywhere() {
        for (auto& v : p_.views) {
            if (v.popup.title.find('{') == std::string::npos) continue;
            if (!view_ && !field_ && viewHasParam(v, r_.from)) continue;     // son parametre la cache
            scopeView_ = &v;
            const std::string t = holes(v.popup.title);
            scopeView_ = nullptr;
            s_.change({roleTitle(v), v.name}, "titre de la popup", v.popup.title, t, true);
            v.popup.title = t;
        }
        for (auto& a : p_.alarms) {
            const std::string i = holes(a.instruction);
            s_.change({"Alarmes", a.name}, "consigne", a.instruction, i, true);
            a.instruction = i;
        }
    }

    hmi::Project& p_;
    const Ren&    r_;
    Sink&         s_;
    Fn            f_;
    bool          view_{false};
    bool          exprs_{true};
    // ---- Lot API 8
    bool          field_{false};            // un champ de DDT
    bool          paths_{true};             // les chemins (Nom, Vue.Objet) se reecrivent (exprs_ : aussi les chaines d'une vue)
    pr::FieldAccess fa_{};
    const hmi::ScriptParts* parts_{nullptr};       // le script ST en cours : ses locales
    const std::set<std::string>* cLocals_{nullptr}; // le script C en cours : ce qu'il declare
    const hmi::View* scopeView_{nullptr};           // la vue en cours : ses parametres
};

class DocumentHmiSide final : public pr::HmiSide {
public:
    explicit DocumentHmiSide(std::shared_ptr<hmi::Document> doc) : doc_(std::move(doc)) {}

    pr::HmiNames names() const override {
        pr::HmiNames n;
        const auto& p = doc_->project;
        for (const auto& v : p.programs.variables) n.variables.push_back({v.name, v.type, v.equipment, v.address});
        for (const auto& f : p.programs.functions) n.functions.push_back(f.name);
        for (const auto& v : p.views) n.views.push_back({v.name, hmi::isSymbolView(v) ? std::string("symbole") : v.role});
        return n;
    }

    void collect(const std::vector<pr::Rename>& renames, std::vector<pr::Change>& out) const override {
        hmi::Project copy = doc_->project;
        const auto first = out.size();
        Sink sink(&out);
        for (const auto& r : hmiRenames(renames)) Rewriter(copy, r, sink).run();
        // Les noms eux-memes (la variable IHM, la vue) en tete : c'est ce qu'on
        // cherche d'abord dans l'arbre.
        std::stable_partition(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(), [](const pr::Change& c) {
            return c.label == "nom" || (!c.path.empty() && c.path.front() == "Variables IHM");
        });
    }

    std::string conflict(const std::vector<pr::Rename>& renames) const override {
        hmi::Project copy = doc_->project;
        Sink sink(nullptr);
        for (const auto& r : hmiRenames(renames)) Rewriter(copy, r, sink).run();
        return sink.conflicts.empty() ? std::string{} : sink.conflicts.front();
    }

    core::CommandPtr command(const std::vector<pr::Rename>& renames, const std::string& label) const override {
        const auto list = hmiRenames(renames);
        if (list.empty()) return nullptr;
        return hmi::changeProject(doc_, label, [&list](hmi::Project& p) {
            Sink sink(nullptr);
            for (const auto& r : list) Rewriter(p, r, sink).run();
        });
    }

private:
    std::shared_ptr<hmi::Document> doc_;
};

} // namespace

std::unique_ptr<pr::HmiSide> makeHmiRenameSide(std::shared_ptr<hmi::Document> doc) {
    if (!doc) return nullptr;
    return std::make_unique<DocumentHmiSide>(std::move(doc));
}

// =============================================================================
//  2. LE DIALOGUE
// =============================================================================
namespace {

const gfx::FontId kSmall{13};

// Un morceau surligne d'une ligne : 1 l'ancien nom (barre, rouge), 2 le nouveau
// (vert), 3 l'ancien nom tant qu'aucun nouveau n'est permis (ambre).
struct Span {
    std::size_t start{0}, len{0};
    int         kind{0};
};

struct Row {
    std::vector<ui::NodeId> children;
    std::string             text, key;
    std::vector<Span>       spans;
    int                     change{-1};      // une ligne : son rang dans plan.changes
    int                     depth{0};
    std::size_t             count{0};        // un groupe : les changements dessous
    ui::Icon                icon{ui::Icon::None};
    bool                    group{false}, side{false}, info{false};
    bool                    doubt{false};            // lot API 8 : un acces a verifier (Change::doubt)
};

class RowsModel final : public ui::ITreeModel {
public:
    std::vector<Row> rows;   // rows[0] : la racine (le noeud 1)
    [[nodiscard]] const Row* at(ui::NodeId n) const {
        return n >= 1 && n <= rows.size() ? &rows[static_cast<std::size_t>(n - 1)] : nullptr;
    }
    [[nodiscard]] ui::NodeId root() const override { return 1; }
    [[nodiscard]] std::size_t childCount(ui::NodeId n) const override {
        const auto* r = at(n);
        return r ? r->children.size() : 0;
    }
    [[nodiscard]] ui::NodeId childAt(ui::NodeId n, std::size_t i) const override {
        const auto* r = at(n);
        return r && i < r->children.size() ? r->children[i] : ui::kInvalidNode;
    }
    [[nodiscard]] bool hasChildren(ui::NodeId n) const override { return childCount(n) > 0; }
    [[nodiscard]] std::string text(ui::NodeId n) const override {
        const auto* r = at(n);
        return r ? r->text : std::string{};
    }
    [[nodiscard]] ui::CellStyle style(ui::NodeId n) const override {
        ui::CellStyle st;
        const auto* r = at(n);
        if (!r) return st;
        if (r->group) {
            st.icon = r->icon;
            st.bold = r->side;
            if (r->count) st.badge = std::to_string(r->count);
        } else if (r->doubt) {
            st.fgTone = ui::Tone::Warning;           // lot API 8
        } else if (r->info) {
            st.fgTone = ui::Tone::Muted;
        }
        return st;
    }
};

bool isPreview(const pr::Change& c) { return c.after.find(pr::previewMark()) != std::string::npos; }

// Les blancs d'un morceau de code ramenes a un espace : une ligne tient dans
// une ligne de l'arbre.
std::string compact(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool blank = false;
    for (const char c : s) {
        const bool ws = c == ' ' || c == '\t' || c == '\r' || c == '\n';
        if (ws) {
            if (!blank) out += ' ';
            blank = true;
            continue;
        }
        blank = false;
        out += c;
    }
    return out;
}

// La ligne d'un changement : son endroit court ("12", "visible :"), puis le
// texte, l'ancien nom et le nouveau cote a cote ("Vitesse->Debit"), marques.
void formatLeaf(const pr::Change& c, Row& row) {
    const bool preview = isPreview(c);
    // Le separateur n'est pas ":" - le code en est plein (INT, :=).
    std::string text = c.line > 0 ? std::to_string(c.line) + "   " : c.label + " \xC2\xB7 ";
    if (c.doubt) text += "\xE2\x9A\xA0 ";        // lot API 8 : a verifier (le type ne se lit pas)
    const std::size_t head = text.size();
    std::vector<Span> spans;
    bool leading = true;
    for (const auto& pc : pr::inlineDiff(c.before, c.after)) {
        if (pc.kind == 2 && preview) continue;
        std::string t = compact(pc.text);
        if (leading && pc.kind == 0) {
            const auto first = t.find_first_not_of(' ');
            t = first == std::string::npos ? std::string{} : t.substr(first);
        }
        if (t.empty()) continue;
        leading = false;
        if (pc.kind == 0) {
            text += t;
            continue;
        }
        if (pc.kind == 2) t = "\xE2\x86\x92" + t;
        spans.push_back({text.size(), t.size(), pc.kind == 1 ? (preview ? 3 : 1) : 2});
        text += t;
    }
    // Un changement loin dans une longue ligne : le debut se replie en "..."
    // pour que le nom surligne reste a l'ecran.
    if (!spans.empty() && spans.front().start > head + 64) {
        std::size_t cut = spans.front().start - 28;
        while (cut < text.size() && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) ++cut;
        const std::string ellipsis = "\xE2\x80\xA6";
        text = text.substr(0, head) + ellipsis + text.substr(cut);
        const std::size_t removed = cut - head;
        for (auto& sp : spans) sp.start = sp.start - removed + ellipsis.size();
    }
    row.text = std::move(text);
    row.spans = std::move(spans);
}

std::string sideTitle(pr::Tab t) {
    switch (t) {
        case pr::Tab::Api:    return "Programme (API)";
        case pr::Tab::Tables: return "Tables d'animation";
        case pr::Tab::Hmi:    return "IHM";
    }
    return "?";
}

ui::Icon sideIcon(pr::Tab t) {
    switch (t) {
        case pr::Tab::Api:    return ui::Icon::Cpu;
        case pr::Tab::Tables: return ui::Icon::AnimationTable;
        case pr::Tab::Hmi:    return ui::Icon::Screen;
    }
    return ui::Icon::Folder;
}

// L'icone d'un groupe, par ce qu'il range (le premier niveau d'un chemin) ;
// les niveaux plus bas : une section, une vue, un script.
ui::Icon groupIcon(const pr::Change& c, std::size_t level) {
    const std::string& top = c.path.front();
    if (level == 0) {
        if (top == "Code" || top == "Appels") return ui::Icon::Section;
        if (top.rfind("Ordre", 0) == 0) return ui::Icon::Task;
        if (top.rfind("Biblioth", 0) == 0) return ui::Icon::Library;
        if (top == "Vues" || top == "Popups" || top == "Symboles" || top.rfind("Mod", 0) == 0) return ui::Icon::Screen;
        if (top.rfind("Scripts", 0) == 0 || top == "Fonctions") return ui::Icon::Code;
        if (top == "Alarmes") return ui::Icon::Warning;
        if (top == "Utilisateurs") return ui::Icon::User;
        if (top == "Langues") return ui::Icon::Globe;
        if (top == "Communication" || top.rfind("\xC3\x89quipements", 0) == 0) return ui::Icon::Network;
        if (top == "Historiques") return ui::Icon::History;
        if (c.tab == pr::Tab::Tables) return ui::Icon::AnimationTable;
        return ui::Icon::Variable;
    }
    if (top == "Code" || top == "Appels") return ui::Icon::Document;
    return ui::Icon::None;
}

std::shared_ptr<RowsModel> buildRows(const pr::Plan& plan, std::optional<pr::Tab> tab) {
    auto m = std::make_shared<RowsModel>();
    m->rows.emplace_back();
    std::map<std::string, ui::NodeId> groups;
    const auto add = [&](ui::NodeId parent, Row row) {
        m->rows.push_back(std::move(row));
        const auto id = static_cast<ui::NodeId>(m->rows.size());
        m->rows[static_cast<std::size_t>(parent - 1)].children.push_back(id);
        return id;
    };
    const auto group = [&](ui::NodeId parent, const std::string& key, const std::string& title, int depth, ui::Icon icon, bool side) {
        if (const auto it = groups.find(key); it != groups.end()) return it->second;
        Row r;
        r.text = title;
        r.key = key;
        r.depth = depth;
        r.icon = icon;
        r.group = true;
        r.side = side;
        const auto id = add(parent, std::move(r));
        groups.emplace(key, id);
        return id;
    };
    // Le programme, puis les tables, puis l'IHM : toujours dans cet ordre.
    for (const auto side : {pr::Tab::Api, pr::Tab::Tables, pr::Tab::Hmi}) {
        if (tab && *tab != side) continue;
        for (std::size_t i = 0; i < plan.changes.size(); ++i) {
            const auto& c = plan.changes[i];
            if (c.tab != side) continue;
            ui::NodeId parent = 1;
            int depth = 0;
            std::string key(1, static_cast<char>('0' + static_cast<int>(side)));
            if (!tab) parent = group(parent, key, sideTitle(side), depth++, sideIcon(side), true);
            for (std::size_t k = 0; k < c.path.size(); ++k) {
                key += "\n" + c.path[k];
                parent = group(parent, key, c.path[k], depth++, groupIcon(c, k), false);
            }
            Row leaf;
            formatLeaf(c, leaf);
            leaf.change = static_cast<int>(i);
            leaf.depth = depth;
            leaf.info = c.info;
            leaf.doubt = c.doubt;
            leaf.key = key + "\n#" + std::to_string(i);
            (void)add(parent, std::move(leaf));
        }
    }
    // Les nombres des groupes, du bas vers le haut (un enfant vient toujours
    // apres son parent).
    for (std::size_t n = m->rows.size(); n-- > 1;) {
        Row& r = m->rows[n];
        if (!r.group) {
            r.count = r.info ? 0 : 1;
            continue;
        }
        std::size_t total = 0;
        for (const auto c : r.children) total += m->rows[static_cast<std::size_t>(c - 1)].count;
        r.count = total;
    }
    return m;
}

std::string plural(std::size_t n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + (n > 1 ? many : one);
}

bool containsInsensitive(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    const auto low = [](std::string_view s) {
        std::string o(s);
        for (auto& ch : o) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return o;
    };
    return low(hay).find(low(needle)) != std::string::npos;
}

// Un texte en morceaux, sur une ligne : les morceaux marques sur leur fond.
// `keep` : les genres de morceaux montres (1 : l'ancien, 2 : le nouveau).
float drawPieces(const ui::PaintContext& ctx, gfx::Point at, float right, const std::vector<pr::Piece>& pieces, int keep,
                 bool preview, gfx::FontId font) {
    const auto& th = ctx.theme;
    float x = at.x;
    const float lh = ctx.r.lineHeight(font);
    bool leading = true;
    for (const auto& pc : pieces) {
        if (pc.kind != 0 && pc.kind != keep) continue;
        std::string t = compact(pc.text);
        if (leading && pc.kind == 0) {
            const auto first = t.find_first_not_of(' ');
            t = first == std::string::npos ? std::string{} : t.substr(first);
        }
        if (t.empty()) continue;
        leading = false;
        if (x >= right) break;
        const auto fits = ctx.r.fitCharacters(t, font, right - x);
        if (fits < t.size()) t = t.substr(0, fits);
        const float w = ctx.r.measure(t, font).width;
        if (pc.kind != 0) {
            const gfx::Color c = preview ? th.color.warning : pc.kind == 1 ? th.color.error : th.color.ok;
            ctx.r.fillRect({x - 1.f, at.y - 2.f, w + 2.f, lh + 4.f}, c.withAlpha(th.isDark() ? 80 : 60));
            if (pc.kind == 1 && !preview) ctx.r.line({x, at.y + lh * 0.55f}, {x + w, at.y + lh * 0.55f}, c, 1.f);
        }
        ctx.r.drawText({x, at.y}, t, font, th.color.text);
        x += w;
    }
    return x;
}

} // namespace

// ---------------------------------------------------------------- l'arbre ----
//  Un TreeView ordinaire (deplier, replier, le clavier, la molette) ; par-dessus
//  son dessin, les noms surlignes de chaque ligne visible.
class RenameDialog::Tree final : public ui::TreeView {
public:
    Tree(std::string id, std::optional<pr::Tab> tab) : ui::TreeView(std::move(id)), tab_(tab) {
        setShowRootNode(false);
        setSelectionMode(ui::SelectionMode::Single);
    }
    [[nodiscard]] std::optional<pr::Tab> tab() const noexcept { return tab_; }
    [[nodiscard]] const Row* rowOf(ui::NodeId n) const { return rows_ ? rows_->at(n) : nullptr; }

    // Le nouveau plan ; ce qu'on avait replie a la main le reste.
    void show(const pr::Plan& plan) {
        if (rows_)
            for (std::size_t i = 1; i < rows_->rows.size(); ++i) {
                const auto& r = rows_->rows[i];
                if (!r.group) continue;
                if (isExpanded(static_cast<ui::NodeId>(i + 1))) folded_.erase(r.key);
                else folded_.insert(r.key);
            }
        rows_ = buildRows(plan, tab_);
        setModel(rows_);
        std::size_t leaves = 0;
        for (const auto& r : rows_->rows) leaves += r.group ? 0 : 1;
        // Tout ouvert ; une tres longue liste : les groupes seulement.
        expandToDepth(leaves > 400 ? 2 : 64);
        for (std::size_t i = 1; i < rows_->rows.size(); ++i)
            if (rows_->rows[i].group && folded_.count(rows_->rows[i].key)) collapse(static_cast<ui::NodeId>(i + 1));
    }

    // La premiere ligne dont le texte contient `text` : ses parents ouverts, amenee
    // en vue, choisie comme d'un clic.
    bool selectText(std::string_view text) {
        if (!rows_) return false;
        for (std::size_t i = 1; i < rows_->rows.size(); ++i) {
            if (!containsInsensitive(rows_->rows[i].text, text)) continue;
            const auto node = static_cast<ui::NodeId>(i + 1);
            // Les parents : ceux dont la liste contient ce noeud.
            std::vector<ui::NodeId> chain;
            ui::NodeId cur = node;
            for (bool found = true; found;) {
                found = false;
                for (std::size_t k = 0; k < rows_->rows.size(); ++k) {
                    const auto& kids = rows_->rows[k].children;
                    if (std::find(kids.begin(), kids.end(), cur) == kids.end()) continue;
                    cur = static_cast<ui::NodeId>(k + 1);
                    if (cur != 1) chain.push_back(cur);
                    found = cur != 1;
                    break;
                }
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) expand(*it);
            ensureVisible(node);
            gfx::Rect r;
            if (!rowRect(node, r)) return false;
            const gfx::Point p{r.x + r.w * 0.5f, r.y + r.h * 0.5f};
            (void)dispatch(ui::MouseDown{p, ui::MouseButton::Left, 1, {}});
            (void)dispatch(ui::MouseUp{p, ui::MouseButton::Left, {}});
            return currentNode() == node;
        }
        return false;
    }

protected:
    void onPaint(const ui::PaintContext& ctx) override {
        ui::TreeView::onPaint(ctx);
        if (!rows_) return;
        const auto& th = ctx.theme;
        const float indent = th.metric.indentPerLevel;
        const auto font = th.font.ui;
        const bool dark = th.isDark();
        ctx.r.pushClip(contentRect());
        bool inside = false;
        for (const auto n : visibleNodes()) {
            gfx::Rect rr;
            if (!rowRect(n, rr)) {
                if (inside) break;            // passe la fenetre : le reste est dessous
                continue;
            }
            inside = true;
            const auto* r = rows_->at(n);
            if (!r || r->spans.empty()) continue;
            // La ou TreeView ecrit le texte : le retrait, la place de la fleche.
            const float textX = rr.x + 4.f + static_cast<float>(r->depth) * indent + 18.f;
            const float maxX = rr.right() - 6.f;
            const std::string_view text(r->text);
            for (const auto& sp : r->spans) {
                if (sp.start > text.size()) break;
                const float x0 = textX + ctx.r.measure(text.substr(0, sp.start), font).width;
                if (x0 >= maxX) break;
                const float x1 = std::min(maxX, x0 + ctx.r.measure(text.substr(sp.start, sp.len), font).width);
                const gfx::Color c = sp.kind == 1 ? th.color.error : sp.kind == 2 ? th.color.ok : th.color.warning;
                ctx.r.fillRect({x0 - 1.f, rr.y + 3.f, x1 - x0 + 2.f, rr.h - 6.f}, c.withAlpha(dark ? 78 : 58));
                if (sp.kind == 1) {
                    const float y = std::floor(rr.y + rr.h * 0.5f) + 0.5f;
                    // Le trait ne barre que le nom (pas la fleche qui suit).
                    ctx.r.line({x0, y}, {x1, y}, c.withAlpha(220), 1.f);
                }
            }
        }
        ctx.r.popClip();
    }

private:
    std::optional<pr::Tab>     tab_;
    std::shared_ptr<RowsModel> rows_;
    std::set<std::string>      folded_;
};

// ----------------------------------------------------------- le detail -------
//  La ligne choisie, en entier : l'endroit, puis avant (l'ancien nom barre) et
//  apres (le nouveau surligne). Un groupe : ce qu'il contient.
class RenameDialog::Detail final : public ui::Widget {
public:
    Detail() = default;
    void set(const pr::Change* change, std::string hint) {
        change_ = change ? std::optional<pr::Change>(*change) : std::nullopt;
        hint_ = std::move(hint);
        invalidate();
    }
protected:
    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto b = bounds();
        ctx.r.fillRect(b, th.color.inputBg);
        ctx.r.strokeRect(b, th.color.border, 1.f);
        const float x = b.x + 10.f, right = b.right() - 10.f;
        ctx.r.pushClip(b);
        if (!change_) {
            ctx.r.drawText({x, b.y + 10.f}, hint_, th.font.ui, th.color.textMuted);
            ctx.r.popClip();
            return;
        }
        const auto& c = *change_;
        std::string where;
        for (const auto& part : c.path) where += (where.empty() ? "" : " \xE2\x80\xBA ") + part;
        where += " \xE2\x80\xBA " + c.label;
        if (c.doubt) where += "  \xC2\xB7  le type de cette valeur ne se lit pas : rien n'y est \xC3\xA9" "crit, \xC3\xA0 renommer \xC3\xA0 la main si c'est ce champ";
        else if (c.info) where += "  \xC2\xB7  rien n'y est \xC3\xA9" "crit : c'est l\xC3\xA0 qu'il se lira sous le nouveau nom";
        ctx.r.drawText({x, b.y + 6.f}, where, kSmall, th.color.textMuted);
        const auto pieces = pr::inlineDiff(c.before, c.after);
        const bool preview = isPreview(c);
        const auto mono = th.font.mono;
        const float lh = ctx.r.lineHeight(mono) + 6.f;
        const float y1 = b.y + 26.f;
        const float labelW = 64.f;
        if (preview) {
            ctx.r.drawText({x, y1}, "Cit\xC3\xA9", kSmall, th.color.textMuted);
            (void)drawPieces(ctx, {x + labelW, y1}, right, pieces, 1, true, mono);
            ctx.r.drawText({x, y1 + lh}, "Tape un nouveau nom : ce texte le prendra \xC3\xA0 la place.", kSmall, th.color.textMuted);
        } else {
            ctx.r.drawText({x, y1}, "Avant", kSmall, th.color.textMuted);
            (void)drawPieces(ctx, {x + labelW, y1}, right, pieces, 1, false, mono);
            ctx.r.drawText({x, y1 + lh}, "Apr\xC3\xA8s", kSmall, th.color.textMuted);
            (void)drawPieces(ctx, {x + labelW, y1 + lh}, right, pieces, 2, false, mono);
        }
        ctx.r.popClip();
    }
private:
    std::optional<pr::Change> change_;
    std::string               hint_{"Choisis une ligne : elle s'affiche ici en entier, avant et apr\xC3\xA8s."};
};

// ------------------------------------------------------------ le corps -------
//  Tout le dialogue, place a la main : le panneau centre, son titre, le nom et
//  son verdict, la cible, la case des liees, les onglets, le detail, le pied.
class RenameDialog::Body final : public ui::Widget {
public:
    Body(std::string title, float w, float h) : title_(std::move(title)), width_(w), height_(h) {}
    ui::InputText* name{nullptr};
    ui::Checkbox*  links{nullptr};
    ui::TabControl* tabs{nullptr};
    Detail*        detail{nullptr};
    ui::Button*    cancel{nullptr};
    ui::Button*    ok{nullptr};
    std::string    status, info, linkText, summary, note;
    ui::Tone       statusTone{ui::Tone::Muted};

    void setTitle(std::string t) {
        title_ = std::move(t);
        invalidate();
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::clamp(width_, 640.f, std::max(640.f, r.w - 24.f));
        const float h = std::clamp(height_, 440.f, std::max(440.f, r.h - 24.f));
        panel_ = {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h};
        const float x = panel_.x + 16.f, inner = panel_.w - 32.f;
        float y = panel_.y + 42.f;
        nameAt_ = y;
        const float fieldX = x + 112.f;
        const float fieldW = std::min(340.f, inner - 112.f - 200.f);
        if (name) name->setBounds({fieldX, y, fieldW, 30.f});
        statusX_ = fieldX + fieldW + 12.f;
        y += 36.f;
        infoAt_ = y;
        y += 22.f;
        if (links) {
            const float cw = std::min(inner * 0.45f, ui::measureWidth(links->label(), gfx::FontId{16}) + 40.f);
            links->setBounds({x, y, cw, 26.f});
            linkX_ = x + cw + 8.f;
            linkAt_ = y;
            y += 32.f;
        }
        const float footer = 52.f, detailH = 74.f;
        const float bottom = panel_.bottom() - footer;
        if (detail) detail->setBounds({x, bottom - detailH, inner, detailH});
        if (tabs) tabs->setBounds({x, y + 2.f, inner, std::max(80.f, bottom - detailH - 8.f - (y + 2.f))});
        footerAt_ = bottom;
        float bx = panel_.right() - 16.f;
        for (auto* bt : {ok, cancel}) {
            if (!bt) continue;
            const float bw = std::max(110.f, ui::measureWidth(bt->text(), gfx::FontId{16}) + 36.f);
            bx -= bw;
            bt->setBounds({bx, panel_.bottom() - 42.f, bw, 30.f});
            bx -= 10.f;
        }
        buttonsX_ = bx;
    }

    void onPaint(const ui::PaintContext& ctx) override {
        const auto& th = ctx.theme;
        const auto& c = th.color;
        ctx.r.fillRect(panel_, c.panelBg);
        ctx.r.strokeRect(panel_, c.borderStrong, 1.f);
        const float titleH = th.metric.headerHeight + 4.f;
        ctx.r.fillRect({panel_.x, panel_.y, panel_.w, titleH}, c.headerBg);
        ctx.r.drawText({panel_.x + 14.f, panel_.y + (titleH - ctx.r.lineHeight(th.font.uiBold)) * 0.5f}, title_, th.font.uiBold, c.text);
        const float x = panel_.x + 16.f;
        const float lh = ctx.r.lineHeight(th.font.ui);
        ctx.r.drawText({x, nameAt_ + (30.f - lh) * 0.5f}, "Nouveau nom", th.font.ui, c.text);
        // Le verdict, a droite du champ : une pastille et sa phrase.
        if (!status.empty()) {
            const gfx::Color tone = th.onSurface(th.tone(statusTone, c.textMuted));
            const float cy = nameAt_ + 15.f;
            ctx.r.fillRoundedRect({statusX_, cy - 5.f, 10.f, 10.f}, tone, 5.f);
            const float room = panel_.right() - 16.f - (statusX_ + 18.f);
            std::string s = status;
            if (const auto fits = ctx.r.fitCharacters(s, th.font.ui, room); fits < s.size()) {
                std::size_t keep = fits > 3 ? fits - 3 : 0;
                while (keep > 0 && (static_cast<unsigned char>(s[keep]) & 0xC0) == 0x80) --keep;
                s = s.substr(0, keep) + "...";
            }
            ctx.r.drawText({statusX_ + 18.f, cy - lh * 0.5f}, s, th.font.ui, tone);
        }
        if (!info.empty()) ctx.r.drawText({x, infoAt_ + 2.f}, info, kSmall, c.textMuted);
        if (links && !linkText.empty()) {
            ctx.r.pushClip({linkX_, linkAt_, panel_.right() - 16.f - linkX_, 26.f});
            ctx.r.drawText({linkX_, linkAt_ + (26.f - lh) * 0.5f}, linkText, th.font.ui, links->enabled() ? c.text : c.textMuted);
            ctx.r.popClip();
        }
        // Le pied : ce qui va se passer, en une phrase ; la note dessous.
        const float fy = footerAt_ + 10.f;
        ctx.r.pushClip({x, footerAt_, std::max(0.f, buttonsX_ - x), panel_.bottom() - footerAt_});
        ctx.r.drawText({x, fy}, summary, th.font.ui, c.text);
        if (!note.empty()) ctx.r.drawText({x, fy + lh + 2.f}, note, kSmall, c.textMuted);
        ctx.r.popClip();
        // La poignee du coin : trois traits.
        const float gx = panel_.right() - 4.f, gy = panel_.bottom() - 4.f;
        for (int k = 1; k <= 3; ++k) {
            const float d = 4.f * static_cast<float>(k);
            ctx.r.line({gx - d, gy}, {gx, gy - d}, c.textMuted, 1.f);
        }
    }

    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev)) {
            const gfx::Rect grip{panel_.right() - 16.f, panel_.bottom() - 16.f, 16.f, 16.f};
            if (d->button == ui::MouseButton::Left && grip.contains(d->pos)) {
                resizing_ = true;
                grabAt_ = d->pos;
                grabW_ = panel_.w;
                grabH_ = panel_.h;
                return ui::EventResult::Consumed;
            }
            // Un clic hors du panneau ne ferme rien (un geste perdu ne doit pas
            // jeter ce qu'on a tape) ; il ne va pas non plus dessous.
            return ui::EventResult::Consumed;
        }
        if (const auto* m = std::get_if<ui::MouseMove>(&ev); m && resizing_) {
            // Le panneau reste centre : il grandit de deux fois le geste.
            width_ = grabW_ + 2.f * (m->pos.x - grabAt_.x);
            height_ = grabH_ + 2.f * (m->pos.y - grabAt_.y);
            invalidateLayout();
            invalidate();
            return ui::EventResult::Consumed;
        }
        if (std::holds_alternative<ui::MouseUp>(ev) && resizing_) {
            resizing_ = false;
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    std::string title_;
    float       width_, height_;
    gfx::Rect   panel_{};
    float       nameAt_{0}, statusX_{0}, infoAt_{0}, linkX_{0}, linkAt_{0}, footerAt_{0}, buttonsX_{0};
    bool        resizing_{false};
    gfx::Point  grabAt_{};
    float       grabW_{0}, grabH_{0};
};

// ------------------------------------------------------------ le dialogue ----
namespace {
struct TabInfo {
    const char*            title;
    std::optional<pr::Tab> tab;
    ui::Icon               icon;
};
const TabInfo kTabs[] = {
    {"Tout", std::nullopt, ui::Icon::Folder},
    {"API", pr::Tab::Api, ui::Icon::Cpu},
    {"IHM", pr::Tab::Hmi, ui::Icon::Screen},
    {"Tables", pr::Tab::Tables, ui::Icon::AnimationTable},
};
} // namespace

RenameDialog::RenameDialog(Spec spec) : menu::WidgetMenu(spec.id), spec_(std::move(spec)) {}
RenameDialog::~RenameDialog() = default;

menu::MenuTraits RenameDialog::traits() const {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

std::string RenameDialog::title() const {
    return "Renommer " + std::string(pr::kindLabel(spec_.kind)) + " " + (plan_.found() ? plan_.target.display : spec_.oldName);
}

std::string RenameDialog::newName() const { return name_ ? name_->text() : std::string{}; }

core::Status RenameDialog::buildUi() {
    // Le premier plan (sur l'ancien nom) : la cible, ses liees, ou elle est citee.
    plan_ = spec_.plan ? spec_.plan(std::string{}, false) : pr::Plan{};
    auto body = std::make_unique<Body>(title(), spec_.width, spec_.height);
    body_ = body.get();
    const std::string base = spec_.id;

    auto field = std::make_unique<ui::InputText>(base + ".name");
    field->setText(!spec_.initial.empty() ? spec_.initial : plan_.found() ? plan_.target.name : spec_.oldName);
    field->setPlaceholder("le nouveau nom");
    name_ = &static_cast<ui::InputText&>(body->addChild(std::move(field)));
    body->name = name_;
    wires_ += name_->textChanged->connect([this](const std::string&) {
        dueAt_ = now_ + 0.25;
        error_.clear();
        quickCheck();
    });

    if (!plan_.links.empty()) {
        const bool plc = plan_.links.front().kind == pr::Kind::Variable;
        const std::string what = plc ? "la variable de l'automate li\xC3\xA9" "e" : "la variable IHM li\xC3\xA9" "e";
        const std::string label = plan_.links.size() > 1 ? "Aussi renommer les " + std::to_string(plan_.links.size()) + (plc ? " variables de l'automate li\xC3\xA9" "es" : " variables IHM li\xC3\xA9" "es")
                                                          : "Aussi renommer " + what;
        auto box = std::make_unique<ui::Checkbox>(label, base + ".links");
        box->setState(ui::Checkbox::State::Checked);
        links_ = &static_cast<ui::Checkbox&>(body->addChild(std::move(box)));
        body->links = links_;
        wires_ += links_->stateChanged->connect([this](ui::Checkbox::State) { recompute(); });
    }

    auto tabs = std::make_unique<ui::TabControl>(base + ".tabs");
    for (const auto& t : kTabs) {
        auto tree = std::make_unique<Tree>(base + ".tree." + t.title, t.tab);
        trees_.push_back(tree.get());
        wires_ += tree->selectionChanged->connect([this](ui::NodeId) { syncDetail(); });
        tabs->addTab({t.title, t.icon}, std::move(tree));
    }
    tabs_ = &static_cast<ui::TabControl&>(body->addChild(std::move(tabs)));
    body->tabs = tabs_;
    wires_ += tabs_->currentChanged->connect([this](std::size_t) { syncDetail(); });

    detail_ = &static_cast<Detail&>(body->addChild(std::make_unique<Detail>()));
    body->detail = detail_;

    auto* cancel = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Annuler", base + ".cancel")));
    body->cancel = cancel;
    wires_ += cancel->clicked->connect([this] { finish(false); });
    ok_ = &static_cast<ui::Button&>(body->addChild(std::make_unique<ui::Button>("Confirmer", base + ".ok")));
    ok_->setStyle(ui::Button::Style::Primary);
    body->ok = ok_;
    wires_ += ok_->clicked->connect([this] { finish(true); });
    body->note = spec_.note;

    setRoot(std::move(body));
    recompute();
    quickCheck();
    return core::ok();
}

void RenameDialog::onEnter() {
    if (focused_ || !name_) return;
    focused_ = true;
    // Le champ a le clavier, son texte choisi : taper remplace l'ancien nom.
    focus().focus(name_);
    (void)name_->dispatch(ui::KeyDown{ui::Key::A, ui::KeyMods{true, false, false, false}, false});
}

RenameDialog::Tree* RenameDialog::currentTree() const {
    if (!tabs_) return nullptr;
    const auto i = tabs_->currentIndex();
    return i < trees_.size() ? trees_[i] : nullptr;
}

void RenameDialog::quickCheck() {
    if (!spec_.check) return;
    quick_ = pr::Verdict::Unchanged;
    quickWhy_ = spec_.check(newName(), &quick_);
    refreshTexts();
}

void RenameDialog::recompute() {
    dueAt_ = -1.0;
    const bool with = links_ && links_->isChecked();
    plan_ = spec_.plan ? spec_.plan(newName(), with) : pr::Plan{};
    quick_ = plan_.verdict;
    quickWhy_ = plan_.problem;
    refreshTabs();
    refreshTexts();
}

void RenameDialog::refreshTabs() {
    for (auto* t : trees_) t->show(plan_);
    if (!tabs_) return;
    for (std::size_t i = 0; i < trees_.size() && i < std::size(kTabs); ++i) {
        const auto n = plan_.count(kTabs[i].tab);
        tabs_->setTabBadge(i, std::to_string(n), n ? ui::Tone::Accent : ui::Tone::None);
    }
    syncDetail();
}

bool RenameDialog::canConfirm() const noexcept {
    if (!spec_.apply || done_) return false;
    // En cours de frappe : le verdict de la derniere lettre ; le plan suit.
    return dueAt_ >= 0.0 ? quick_ == pr::Verdict::Ok : plan_.ok();
}

void RenameDialog::refreshTexts() {
    if (!body_) return;
    using V = pr::Verdict;
    const V v = dueAt_ >= 0.0 ? quick_ : plan_.verdict;
    const std::string& why = dueAt_ >= 0.0 ? quickWhy_ : plan_.problem;
    std::string status;
    ui::Tone tone = ui::Tone::Muted;
    switch (v) {
        case V::Ok:          status = "libre"; tone = ui::Tone::Ok; break;
        case V::Taken:       status = "d\xC3\xA9j\xC3\xA0 pris : " + why; tone = ui::Tone::Error; break;
        case V::Invalid:     status = "nom invalide : " + why; tone = ui::Tone::Error; break;
        case V::Unchanged:   status = why; break;
        case V::NotFound:    status = "introuvable : " + why; tone = ui::Tone::Error; break;
        case V::Unsupported: status = "impossible : " + why; tone = ui::Tone::Error; break;
        case V::Ambiguous:   status = "\xC3\xA0 v\xC3\xA9rifier : " + why; tone = ui::Tone::Error; break;     // lot API 8
    }
    if (!error_.empty()) {
        status = "refus\xC3\xA9 : " + error_;
        tone = ui::Tone::Error;
    }
    body_->status = status;
    body_->statusTone = tone;
    body_->info = plan_.found() ? plan_.target.display + " \xC2\xB7 " + plan_.target.detail : std::string{};
    // Les liees : "Vitesse_IHM -> Debit_IHM (liee a la meme adresse %MW100)".
    std::string linkText;
    bool usable = false;
    for (const auto& l : plan_.links) {
        if (!linkText.empty()) linkText += " ; ";
        if (l.usable()) {
            linkText += l.name + " \xE2\x86\x92 " + l.proposed;
            usable = true;
        } else if (!l.problem.empty()) {
            linkText += l.name + " : " + l.problem;
        } else {
            linkText += l.name;
        }
        linkText += " (" + l.how + ")";
    }
    body_->linkText = linkText;
    if (links_) links_->setEnabled(usable || !plan_.ok());
    // Le pied.
    const auto n = plan_.count(), m = plan_.places();
    // Lot API 8 : les acces a verifier (un champ de DDT dont le type ne se lit pas).
    const auto doubts = static_cast<std::size_t>(std::count_if(plan_.changes.begin(), plan_.changes.end(), [](const pr::Change& c) { return c.doubt; }));
    if (plan_.verdict == pr::Verdict::Ambiguous) {
        body_->summary = plural(n, "changement", "changements") + " pr\xC3\xAA" "t" + (n > 1 ? "s" : "") + " ; "
                       + (doubts ? plural(doubts, "acc\xC3\xA8s", "acc\xC3\xA8s") + " \xC3\xA0 v\xC3\xA9rifier (\xE2\x9A\xA0)" : std::string("un chemin de l'IHM \xC3\xA0 v\xC3\xA9rifier"))
                       + " : Confirmer reste gris\xC3\xA9 tant qu'ils ne sont pas renomm\xC3\xA9s \xC3\xA0 la main.";
    } else if (plan_.ok()) {
        body_->summary = n == 0 ? std::string("Rien d'autre ne change.")
                                : plural(n, "changement", "changements") + " dans " + plural(m, "endroit", "endroits") + " \xE2\x80\x94 un seul Ctrl+Z pour tout d\xC3\xA9" "faire";
    } else if (plan_.found()) {
        body_->summary = n == 0 ? std::string("L'ancien nom n'est cit\xC3\xA9 nulle part ailleurs.")
                                : "L'ancien nom est cit\xC3\xA9 " + plural(n, "fois", "fois") + " dans " + plural(m, "endroit", "endroits") + " : tout suivra.";
    } else {
        body_->summary = {};
    }
    body_->setTitle(title());
    if (ok_) ok_->setEnabled(canConfirm());
    body_->invalidate();
}

void RenameDialog::syncDetail() {
    if (!detail_) return;
    auto* tree = currentTree();
    const Row* row = tree ? tree->rowOf(tree->currentNode()) : nullptr;
    if (row && row->change >= 0 && static_cast<std::size_t>(row->change) < plan_.changes.size()) {
        detail_->set(&plan_.changes[static_cast<std::size_t>(row->change)], {});
        return;
    }
    if (row && row->group) {
        detail_->set(nullptr, row->text + " : " + plural(row->count, "changement", "changements") + ". Choisis une ligne pour la voir en entier.");
        return;
    }
    detail_->set(nullptr, plan_.changes.empty() ? std::string("Rien \xC3\xA0 montrer ici.")
                                                : std::string("Choisis une ligne : elle s'affiche ici en entier, avant et apr\xC3\xA8s."));
}

void RenameDialog::Update(const menu::FrameContext& f) {
    now_ = f.totalSeconds;
    if (dueAt_ >= 0.0 && now_ >= dueAt_) recompute();
    menu::WidgetMenu::Update(f);
    if (ok_) {
        const bool can = canConfirm();
        if (can != ok_->enabled()) ok_->setEnabled(can);
    }
}

ui::EventResult RenameDialog::HandleEvent(const ui::InputEvent& ev) {
    if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
        // ---- Lot API 8 : didacticiels et aide ---- F1 : la page "Renommer partout" de l'aide.
        if (k->key == ui::Key::F1 && k->mods.none() && !k->repeat) {
            lot8::setPendingHelpAnchor(lot8::helpAnchorFor("renommer"));
            manager().PushMenu("help");
            return ui::EventResult::Consumed;
        }
        // ---- fin Lot API 8 : didacticiels et aide ----
        if (k->key == ui::Key::Escape) {
            finish(false);
            return ui::EventResult::Consumed;
        }
        if (k->key == ui::Key::Return) {
            finish(true);          // refuse en silence tant que le nom ne va pas
            return ui::EventResult::Consumed;
        }
    }
    return menu::WidgetMenu::HandleEvent(ev);
}

void RenameDialog::finish(bool ok) {
    if (done_) return;
    if (ok) {
        if (dueAt_ >= 0.0) recompute();
        if (!plan_.ok() || !spec_.apply) return;
        std::string why;
        if (!spec_.apply(plan_, &why)) {
            error_ = why.empty() ? std::string("la commande n'a pas \xC3\xA9t\xC3\xA9 faite") : why;
            refreshTexts();
            return;
        }
    }
    done_ = true;
    manager().CloseDialog(menu::DialogResult{ok ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                             ok ? plan_.newName : std::string{}});
}

// ---- pour les scripts et les essais ------------------------------------------------
void RenameDialog::typeName(const std::string& text) {
    if (!name_) return;
    name_->setText(text);
    recompute();
}

bool RenameDialog::showTab(std::string_view title) {
    if (!tabs_ || title.empty()) return false;
    for (std::size_t i = 0; i < std::size(kTabs); ++i) {
        const std::string_view name(kTabs[i].title);
        // Par le debut du titre, sans casse : "api", "Tab", "ihm".
        if (title.size() > name.size() || !pr::sameName(name.substr(0, title.size()), title)) continue;
        tabs_->setCurrentIndex(i);
        syncDetail();
        return true;
    }
    return false;
}

bool RenameDialog::confirm(std::string* why) {
    if (dueAt_ >= 0.0) recompute();
    if (!canConfirm()) {
        if (why) *why = plan_.problem.empty() ? std::string("Confirmer est gris\xC3\xA9") : plan_.problem;
        return false;
    }
    finish(true);
    if (!done_ && why) *why = error_;
    return done_;
}

void RenameDialog::cancel() { finish(false); }

bool RenameDialog::setWithLinks(bool on) {
    if (!links_ || !links_->enabled()) return false;
    links_->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    return true;
}

bool RenameDialog::selectRow(std::string_view text) {
    auto* tree = currentTree();
    if (!tree || !tree->selectText(text)) return false;
    syncDetail();
    return true;
}

} // namespace app
