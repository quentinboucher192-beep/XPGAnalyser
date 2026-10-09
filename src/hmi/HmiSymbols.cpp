// =============================================================================
//  hmi/HmiSymbols.cpp - voir HmiSymbols.hpp
// =============================================================================
#include "HmiSymbols.hpp"
#include "HmiLiveGeometry.hpp"   // 1.11.4 : la geometrie en marche (les notes de l'expansion)
#include "HmiMarkers.hpp"        // 1.11.4 : un repere dans une constante texte ('$Nom$')

#include "HmiEdit.hpp"
#include "HmiEnums.hpp"            // 1.11.3 : Auto -> T_MODE#Auto (un parametre de type enumeration)
#include "HmiOperators.hpp"
#include "HmiExpr.hpp"
#include "HmiRuntime.hpp"
#include "HmiScript.hpp"        // 1.11.10 : splitDeclarations (les noms qu'une fonction de symbole cache)
#include "HmiTemplates.hpp"
#include "HmiWidgets.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>

namespace hmi {

namespace {

unsigned char uc(char c) { return static_cast<unsigned char>(c); }
bool identStart(char c) { return std::isalpha(uc(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(uc(c)) || c == '_'; }

std::string upperCopy(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(uc(c)));
    return out;
}

std::string trimmedCopy(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(uc(s[a]))) ++a;
    while (b > a && std::isspace(uc(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

bool sameName(std::string_view a, std::string_view b) { return upperCopy(a) == upperCopy(b); }

// Ce qu'un gestionnaire fait d'une tete de chemin : rien, ou (fin du morceau
// remplace, texte mis a la place).
using Replacement = std::optional<std::pair<std::size_t, std::string>>;
using RootHandler = std::function<Replacement(std::string_view text, std::size_t begin, std::size_t end)>;

// Le texte, avec chaque TETE DE CHEMIN proposee a `at` : un nom qui n'est ni
// un membre (apres un '.'), ni le prefixe d'un litteral type (T#5s, INT#3),
// ni le nom d'un argument d'appel (f(Nom := 1), f(x => y)). Les chaines et les
// commentaires sont recopies tels quels. `code` : du ST, ou "Nom :=" hors de
// toute parenthese est une affectation (le nom se remplace).
std::string rewriteRoots(std::string_view s, bool code, const RootHandler& at) {
    std::string out;
    out.reserve(s.size() + 16);
    std::size_t i = 0;
    int depth = 0;       // les parentheses ouvertes
    char prev = 0;       // le dernier caractere significatif (hors espaces)
    while (i < s.size()) {
        const char c = s[i];
        if (c == '(' && i + 1 < s.size() && s[i + 1] == '*') {             // (* commentaire *)
            const auto end = s.find("*)", i + 2);
            const std::size_t stop = end == std::string_view::npos ? s.size() : end + 2;
            out.append(s.substr(i, stop - i));
            i = stop;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {             // // jusqu'a la fin de la ligne
            const auto end = s.find('\n', i);
            const std::size_t stop = end == std::string_view::npos ? s.size() : end;
            out.append(s.substr(i, stop - i));
            i = stop;
            continue;
        }
        if (c == '\'' || c == '"') {                                        // une chaine ($' : l'apostrophe)
            std::size_t j = i + 1;
            while (j < s.size() && s[j] != c) j += (s[j] == '$' && j + 1 < s.size()) ? 2 : 1;
            const std::size_t stop = std::min(s.size(), j + 1);
            out.append(s.substr(i, stop - i));
            i = stop;
            prev = c;
            continue;
        }
        if (std::isdigit(uc(c))) {                                          // un nombre (16#FF, 1.5)
            std::size_t j = i;
            while (j < s.size() && (identChar(s[j]) || s[j] == '#'
                                    || (s[j] == '.' && j + 1 < s.size() && std::isdigit(uc(s[j + 1])))))
                ++j;
            out.append(s.substr(i, j - i));
            i = j;
            prev = '0';
            continue;
        }
        if (identStart(c)) {
            std::size_t j = i;
            while (j < s.size() && identChar(s[j])) ++j;
            if (j < s.size() && s[j] == '#') {                              // T#5s, DT#2026-09-24-10:00:00
                std::size_t m = j + 1;
                while (m < s.size() && (identChar(s[m]) || s[m] == '.' || s[m] == ':' || s[m] == '-' || s[m] == '+')) ++m;
                out.append(s.substr(i, m - i));
                i = m;
                prev = '0';
                continue;
            }
            std::size_t k = j;
            while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
            const bool assign = k + 1 < s.size() && s[k] == ':' && s[k + 1] == '=';
            const bool output = k + 1 < s.size() && s[k] == '=' && s[k + 1] == '>';
            const bool named = (assign && (depth > 0 || !code)) || output;
            if (prev != '.' && !named)
                if (auto r = at(s, i, j)) {
                    out += r->second;
                    i = r->first;
                    prev = 'a';
                    continue;
                }
            out.append(s.substr(i, j - i));
            i = j;
            prev = 'a';
            continue;
        }
        if (c == '(') ++depth;
        else if (c == ')' && depth > 0) --depth;
        out += c;
        if (!std::isspace(uc(c))) prev = c;
        ++i;
    }
    return out;
}

// Un argument qui se colle tel quel a la place du nom : un chemin, un litteral.
bool standsAlone(const std::string& t) {
    if (t.empty() || isVariablePath(t)) return true;
    double n = 0;
    if (parseNumber(t, n)) return true;
    const std::string u = upperCopy(t);
    if (u == "TRUE" || u == "FALSE") return true;
    if (t.size() >= 2 && (t.front() == '\'' || t.front() == '"') && t.back() == t.front()) {
        for (std::size_t i = 1; i + 1 < t.size(); ++i) {
            if (t[i] == '$') { ++i; continue; }
            if (t[i] == t.front()) return false;
        }
        return true;
    }
    // T#5s, INT#3 : un litteral type.
    std::size_t i = 0;
    while (i < t.size() && identChar(t[i])) ++i;
    if (i > 0 && identStart(t[0]) && i < t.size() && t[i] == '#') {
        for (std::size_t k = i + 1; k < t.size(); ++k)
            if (!(identChar(t[k]) || t[k] == '.' || t[k] == ':' || t[k] == '-' || t[k] == '+')) return false;
        return true;
    }
    return false;
}

// Les trous d'un texte a trous, chacun passe par `f` (son expression, sans le
// format qui la suit). Memes regles que TextTemplate::compile.
std::string rewriteTemplate(std::string_view text, const std::function<std::string(std::string_view)>& f) {
    std::string out;
    out.reserve(text.size() + 16);
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
            if (looksLikeFormat(candidate)) {
                fmt = ":" + candidate;
                expr = inside.substr(0, colon);
            }
        }
        out += "{" + f(expr) + fmt + "}";
        i = close + 1;
    }
    return out;
}

// Une chaine ST ('B', 'L''armoire', 'A$'B') -> son texte ; faux : pas une chaine seule.
// 1.11.4 : les reperes d'un texte ($Nom$, la regle des textes) : leurs `$` ne s'echappent
// pas dans une constante ('$Nom$' et non '$$Nom$$') - Dupliquer les trouve, le moteur les
// retire. `at` : la position de chaque `$` d'un repere dans `text`.
std::set<std::size_t> markerDollars(std::string_view text) {
    std::set<std::size_t> at;
    if (text.find('$') == std::string_view::npos) return at;
    for (const auto& sp : markers::find(text, markers::Mode::Text)) {
        at.insert(sp.at);
        at.insert(sp.end() - 1);
    }
    return at;
}

bool stringLiteral(const std::string& t, std::string& text) {
    if (t.size() < 2 || t.front() != '\'' || t.back() != '\'') return false;
    text.clear();
    // 1.11.4 : un repere ('$Nom$') se lit tel quel, ses `$` ne sont pas des echappements.
    const auto kept = markerDollars(std::string_view(t).substr(1, t.size() - 2));
    for (std::size_t i = 1; i + 1 < t.size(); ++i) {
        if (t[i] == '$' && kept.count(i - 1)) {
            const std::size_t close = t.find('$', i + 1);
            text += t.substr(i, close - i + 1);
            i = close;
            continue;
        }
        if (t[i] == '$' && i + 2 < t.size()) {
            const char n = t[++i];
            text += n == 'N' || n == 'n' ? '\n' : n == 'T' || n == 't' ? '\t' : n;
            continue;
        }
        if (t[i] == '\'') return false;
        text += t[i];
    }
    return true;
}

// ---- le repere (les memes regles que HmiEdit.cpp) ---------------------------
edit::Pt pivotLocal(const Object& o) {
    const Box b = o.box();
    return {b.w * o.number("pivotX", 0.5), b.h * o.number("pivotY", 0.5)};
}
edit::Pt pivotView(const Object& o) {
    const Box b = o.box();
    const edit::Pt p = pivotLocal(o);
    return {b.x + p.x, b.y + p.y};
}
edit::Pt rotateAround(edit::Pt p, edit::Pt c, double deg) {
    const double a = deg * 3.14159265358979323846 / 180.0, cs = std::cos(a), sn = std::sin(a);
    const double dx = p.x - c.x, dy = p.y - c.y;
    return {c.x + dx * cs - dy * sn, c.y + dx * sn + dy * cs};
}
void putPivotAt(Object& o, edit::Pt target) {
    const edit::Pt now = pivotView(o);
    o.setNumber("x", o.number("x") + target.x - now.x);
    o.setNumber("y", o.number("y") + target.y - now.y);
}

// Un objet du symbole (repere du symbole) pose dans l'instance : mis a
// l'echelle (sx, sy), retourne puis tourne comme elle.
void place(Object& c, const Object& inst, double sx, double sy) {
    const Box ib = inst.box();
    const edit::Pt pv = pivotView(c);
    const double rel = edit::normalizeAngle(c.rotation());
    const bool quarter = std::fabs(std::fmod(rel, 180.0) - 90.0) < 1.0;
    const double kx = quarter ? sy : sx, ky = quarter ? sx : sy;
    if (kx != 1.0 || ky != 1.0) {
        const Box b = c.box();
        c.setNumber("w", b.w * kx);
        c.setNumber("h", b.h * ky);
        if (c.kind == Kind::Line || c.kind == Kind::Polygon) {
            auto pts = edit::points(c);
            for (auto& p : pts) { p.x *= kx; p.y *= ky; }
            edit::setPoints(c, pts);
        }
    }
    putPivotAt(c, {ib.x + pv.x * sx, ib.y + pv.y * sy});
    if (inst.flipH()) {
        const edit::Pt now = pivotView(c);
        c.setFlag("flipH", !c.flipH());
        c.setNumber("rot", edit::normalizeAngle(-c.rotation()));
        putPivotAt(c, {2 * ib.cx() - now.x, now.y});
    }
    if (inst.flipV()) {
        const edit::Pt now = pivotView(c);
        c.setFlag("flipV", !c.flipV());
        c.setNumber("rot", edit::normalizeAngle(-c.rotation()));
        putPivotAt(c, {now.x, 2 * ib.cy() - now.y});
    }
    if (const double rot = inst.rotation(); std::fabs(rot) > 1e-9) {
        putPivotAt(c, rotateAround(pivotView(c), pivotView(inst), rot));
        c.setNumber("rot", edit::normalizeAngle(c.rotation() + rot));
    }
    // Les tailles de trait et de texte suivent l'echelle (la plus petite des deux).
    const double k = std::min(std::fabs(sx), std::fabs(sy));
    if (std::fabs(k - 1.0) > 1e-9 && k > 0)
        for (const char* key : {"fontSize", "strokeWidth", "radius", "thickness"})
            if (auto* p = c.find(key); p && p->expr.empty()) {
                double v = 0;
                if (parseNumber(p->value, v)) p->value = formatNumber(std::round(v * k * 100.0) / 100.0);
            }
}

// Ce que l'objet d'une instance tient d'elle : sa visibilite, son opacite, sa
// securite (niveau d'acces, autorisation).
void inherit(Object& c, const Object& inst) {
    const Prop* iv = inst.find("visible");
    const std::string visExpr = iv ? trimmedCopy(iv->expr) : std::string{};
    if (visExpr.empty() && !inst.flag("visible", true)) {
        c.setFlag("visible", false);
        c.setExpr("visible", {});
    } else if (!visExpr.empty()) {
        const Prop* cv = c.find("visible");
        const std::string mine = cv ? trimmedCopy(cv->expr) : std::string{};
        if (!mine.empty()) c.setExpr("visible", "(" + visExpr + ") AND (" + mine + ")");
        else if (c.flag("visible", true)) c.setExpr("visible", visExpr);
    }
    const Prop* io = inst.find("opacity");
    const std::string opExpr = io ? trimmedCopy(io->expr) : std::string{};
    const double op = inst.number("opacity", 100);
    if (!opExpr.empty()) {
        const Prop* co = c.find("opacity");
        const std::string mine = co && !co->expr.empty() ? "(" + co->expr + ")" : formatNumber(c.number("opacity", 100));
        c.setExpr("opacity", "(" + opExpr + ") * " + mine + " / 100");
    } else if (std::fabs(op - 100.0) > 1e-9) {
        if (Prop* co = c.find("opacity"); co && !co->expr.empty()) co->expr = "(" + co->expr + ") * " + formatNumber(op) + " / 100";
        else c.setNumber("opacity", c.number("opacity", 100) * op / 100.0);
    }
    if (const double access = inst.number("access", 0); access > c.number("access", 0)) c.setNumber("access", access);
    if (const std::string auth = trimmedCopy(inst.text("auth")); !auth.empty()) {
        const std::string mine = trimmedCopy(c.text("auth"));
        c.set("auth", mine.empty() ? auth : "(" + auth + ") AND (" + mine + ")");
    }
}

bool containsImpl(const Project& p, const View& s, std::string_view other, std::set<std::string>& seen) {
    for (const auto& o : s.objects) {
        if (o.kind != Kind::SymbolInstance) continue;
        const std::string name = o.text("symbol");
        if (sameName(name, other)) return true;
        if (!seen.insert(upperCopy(name)).second) continue;
        if (const View* inner = p.viewByName(name); inner && isSymbolView(*inner) && containsImpl(p, *inner, other, seen))
            return true;
    }
    return false;
}

} // namespace

// ============================================================== symboles ====
bool isSymbolView(const View& v) noexcept { return v.role == kSymbolRole; }

const View* symbolOf(const Project& p, const Object& instance) {
    const std::string name = trimmedCopy(instance.text("symbol"));
    if (name.empty()) return nullptr;
    const View* v = p.viewByName(name);
    return v && isSymbolView(*v) ? v : nullptr;
}

std::vector<const View*> symbolsOf(const Project& p) {
    std::vector<const View*> out;
    for (const auto& v : p.views) if (isSymbolView(v)) out.push_back(&v);
    return out;
}

std::vector<std::pair<const View*, const Object*>> instancesOf(const Project& p, std::string_view symbol) {
    std::vector<std::pair<const View*, const Object*>> out;
    for (const auto& v : p.views)
        for (const auto& o : v.objects)
            if (o.kind == Kind::SymbolInstance && trimmedCopy(o.text("symbol")) == symbol) out.emplace_back(&v, &o);
    return out;
}

bool hasInstances(const View& v) noexcept {
    for (const auto& o : v.objects) if (o.kind == Kind::SymbolInstance) return true;
    return false;
}

bool usesSymbols(const Project& p, const View& v) {
    if (hasInstances(v)) return true;
    for (const View* t : templateChain(p, v)) if (hasInstances(*t)) return true;
    if (const View* h = headerOf(p, v); h && hasInstances(*h)) return true;
    if (const View* f = footerOf(p, v); f && hasInstances(*f)) return true;
    return false;
}

bool symbolContains(const Project& p, const View& symbol, std::string_view other) {
    std::set<std::string> seen{upperCopy(symbol.name)};
    return containsImpl(p, symbol, other, seen);
}

// ---- 1.11.2 (SYM, decision 240) : la section « Parametres du symbole » -----------------
// ---- et les arguments positionnels (Voiture;50) lus par le moteur (decision 248) --------
namespace {
// Les morceaux de `params`, coupes aux `;` hors chaines et hors crochets / parentheses
// (comme parseArguments), sans les blancs autour ; les morceaux vides sautes, sauf
// `keepEmpty` (le rang des positionnels : "; 50" donne 50 au deuxieme parametre).
std::vector<std::string> argumentPieces(std::string_view text, bool keepEmpty = false) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool quoted = false;
    const auto flush = [&] {
        std::string t = trimmedCopy(cur);
        if (!t.empty() || keepEmpty) out.push_back(std::move(t));
        cur.clear();
    };
    for (const char c : text) {
        if (quoted) {
            cur += c;
            if (c == '\'') quoted = false;
            continue;
        }
        if (c == '\'') { quoted = true; cur += c; continue; }
        if (c == '[' || c == '(') ++depth;
        if ((c == ']' || c == ')') && depth > 0) --depth;
        if (c == ';' && depth == 0) { flush(); continue; }
        cur += c;
    }
    flush();
    return out;
}
// Un morceau : nomme ("Nom := valeur", ou "Nom = valeur", comme parseArguments), positionnel
// (sans := : "Voiture", "50", "X >= 3"), ou autre ("T[1] := 2" : un := sans nom ; garde tel quel).
enum class PieceKind { Named, Positional, Other };
struct ArgPiece {
    PieceKind kind = PieceKind::Positional;
    std::string name, value;
};
ArgPiece argumentPiece(const std::string& piece) {
    // Le premier ":=" hors des chaines, sinon le premier "=" ("'a:=b'" est un positionnel).
    std::size_t at = std::string::npos, eq = std::string::npos;
    bool quoted = false;
    for (std::size_t i = 0; i < piece.size() && at == std::string::npos; ++i) {
        const char c = piece[i];
        if (c == '\'') { quoted = !quoted; continue; }
        if (quoted) continue;
        if (c == ':' && i + 1 < piece.size() && piece[i + 1] == '=') at = i;
        else if (c == '=' && eq == std::string::npos) eq = i;
    }
    const std::size_t len = at != std::string::npos ? 2 : 1;
    if (at == std::string::npos) at = eq;
    if (at == std::string::npos) return {PieceKind::Positional, {}, piece};
    std::string name = trimmedCopy(std::string_view(piece).substr(0, at));
    bool ident = !name.empty() && identStart(name.front());
    for (const char c : name) ident = ident && identChar(c);
    if (ident) return {PieceKind::Named, std::move(name), trimmedCopy(std::string_view(piece).substr(at + len))};
    return {len == 1 ? PieceKind::Positional : PieceKind::Other, {}, piece};
}
// Les arguments sans := de `params`, dans leur ordre ; le k-ieme (vide compris) est celui
// du k-ieme parametre du symbole.
std::vector<std::string> positionalArguments(std::string_view params) {
    std::vector<std::string> out;
    for (const auto& piece : argumentPieces(params, true)) {
        if (piece.empty()) { out.emplace_back(); continue; }
        ArgPiece a = argumentPiece(piece);
        if (a.kind == PieceKind::Positional) out.push_back(std::move(a.value));
    }
    return out;
}
// 1.11.3 : la racine d'un chemin (Armoires de Armoires[1].Nom) ; vide : le texte ne
// commence pas par un nom.
std::string rootOf(std::string_view t) {
    std::size_t i = 0;
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) ++i;
    const std::size_t b = i;
    if (i >= t.size() || !identStart(t[i])) return {};
    while (i < t.size() && identChar(t[i])) ++i;
    return std::string(t.substr(b, i - b));
}
// 1.11.3 : l'argument tel que le moteur le colle. Une constante, une formule valide qui ne
// lit que des variables connues : tel quel. Sinon, le parametre etant type, la conversion
// (Voiture -> 'Voiture', 1,5 -> 1.5, Auto -> T_MODE#Auto) quand elle reussit.
std::string constantArgument(const Project* project, std::string_view type, std::string text) {
    // ANY (ou sans type) : rien vers quoi convertir - l'argument tel quel, comme avant.
    if (text.empty() || trimmedCopy(type).empty() || sameName(trimmedCopy(type), "ANY") || isLiteralArgument(text)) return text;
    if (isVariablePath(text)) {
        if (isKnownName(project, rootOf(text))) return text;
    } else if (const Expression e = Expression::compile(text); e.valid()) {
        // Un appel (Pompe(1), TO_STRING(x)) : laisse ; un calcul sur des noms connus aussi.
        if (text.find('(') != std::string::npos) return text;
        const auto& roots = e.roots();
        if (std::all_of(roots.begin(), roots.end(), [project](const std::string& r) { return isKnownName(project, r); })) return text;
    }
    std::string lit = argumentLiteral(project, type, text);
    return lit.empty() ? text : lit;
}
} // namespace

// Les nommes comme avant (parseArguments) ; 1.11.2 (decision 248) : un parametre que rien
// ne nomme prend le positionnel de son rang (Voiture;50 : Name = Voiture, Value = 50) ;
// un positionnel en trop est ignore.
namespace {
std::string withoutMarkers(std::string text);   // 1.11.4 (plus bas)
} // namespace

SymbolArguments symbolArguments(const View& symbol, const Object& instance, const Project* project) {
    const std::string params = instance.text("params");
    const auto given = parseArguments(params);
    const auto positional = positionalArguments(params);
    SymbolArguments out;
    for (std::size_t i = 0; i < symbol.params.size(); ++i) {
        const auto& prm = symbol.params[i];
        std::string text = prm.defaultValue;
        bool named = false;
        for (const auto& [name, value] : given)
            if (sameName(name, prm.name)) { text = value; named = true; break; }
        if (!named && i < positional.size() && !positional[i].empty()) text = positional[i];
        text = withoutMarkers(constantArgument(project, prm.type, trimmedCopy(text)));
        if (!text.empty()) out.emplace_back(prm.name, std::move(text));
    }
    return out;
}

std::vector<std::string> givenArguments(const View& symbol, std::string_view params) {
    std::vector<std::string> out(symbol.params.size());
    std::vector<bool> named(symbol.params.size(), false);
    for (const auto& piece : argumentPieces(params)) {
        ArgPiece a = argumentPiece(piece);
        if (a.kind != PieceKind::Named) continue;
        for (std::size_t i = 0; i < symbol.params.size(); ++i)
            if (!named[i] && sameName(a.name, symbol.params[i].name)) {
                out[i] = std::move(a.value);
                named[i] = true;
                break;
            }
    }
    // Les positionnels, comme le moteur (symbolArguments) : le k-ieme au k-ieme parametre,
    // s'il n'est pas nomme ; ceux en trop ignores.
    const auto positional = positionalArguments(params);
    for (std::size_t i = 0; i < symbol.params.size() && i < positional.size(); ++i)
        if (!named[i]) out[i] = positional[i];
    return out;
}

std::string withArgument(const View& symbol, std::string_view params, std::size_t index, std::string_view value) {
    auto given = givenArguments(symbol, params);
    if (index < given.size()) given[index] = trimmedCopy(value);
    std::string out;
    const auto add = [&out](const std::string& name, const std::string& v) { out += (out.empty() ? "" : "; ") + name + " := " + v; };
    for (std::size_t i = 0; i < given.size(); ++i)
        if (!given[i].empty()) add(symbol.params[i].name, given[i]);
    // Un argument nomme qui n'est pas un parametre du symbole (un parametre retire depuis) : garde.
    // Un morceau ni nomme ni positionnel ("T[1] := 2") : garde tel quel.
    for (const auto& piece : argumentPieces(params)) {
        const ArgPiece a = argumentPiece(piece);
        if (a.kind == PieceKind::Other) out += (out.empty() ? "" : "; ") + piece;
        if (a.kind != PieceKind::Named) continue;
        if (std::none_of(symbol.params.begin(), symbol.params.end(), [&](const ViewParam& p) { return sameName(p.name, a.name); })) add(a.name, a.value);
    }
    return out;
}

// ---- 1.11.3 : la valeur d'un parametre, constante convertie ou formule ------------------
namespace {
std::mutex                                 gPlcNamesLock;
std::shared_ptr<const std::set<std::string, std::less<>>> gPlcNames;

std::string squeezedUpper(std::string_view s) {
    std::string out;
    for (const char c : s)
        if (!std::isspace(uc(c))) out += static_cast<char>(std::toupper(uc(c)));
    return out;
}
// Un entier de ce type : ses bornes ; faux : pas un type entier.
bool integerBounds(const std::string& u, long long& lo, unsigned long long& hi) {
    struct B { const char* name; long long lo; unsigned long long hi; };
    static const B kTypes[] = {
        {"SINT", -128, 127}, {"INT", -32768, 32767}, {"DINT", INT32_MIN, INT32_MAX}, {"LINT", LLONG_MIN, LLONG_MAX},
        {"USINT", 0, 255}, {"UINT", 0, 65535}, {"UDINT", 0, UINT32_MAX}, {"ULINT", 0, ULLONG_MAX},
        {"BYTE", 0, 255}, {"WORD", 0, 65535}, {"DWORD", 0, UINT32_MAX}, {"LWORD", 0, ULLONG_MAX},
    };
    for (const auto& t : kTypes)
        if (u == t.name) { lo = t.lo; hi = t.hi; return true; }
    return false;
}
// "5s", "1m30s", "250ms", "2h" : une duree sans T#.
bool durationText(std::string_view t) {
    if (t.empty()) return false;
    std::size_t i = 0;
    bool any = false;
    while (i < t.size()) {
        const std::size_t d = i;
        while (i < t.size() && (std::isdigit(uc(t[i])) || t[i] == '.' || t[i] == '_')) ++i;
        if (i == d) return false;
        const std::size_t u = i;
        while (i < t.size() && std::isalpha(uc(t[i]))) ++i;
        const std::string unit = upperCopy(t.substr(u, i - u));
        if (unit != "MS" && unit != "S" && unit != "M" && unit != "MIN" && unit != "H" && unit != "D") return false;
        any = true;
    }
    return any;
}
std::string quoted(std::string_view text) {
    std::string out = "'";
    const auto kept = markerDollars(text);   // 1.11.4 : les `$` d'un repere restent seuls
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\'' || (c == '$' && !kept.count(i))) out += '$';
        if (c == '\n') { out += "$N"; continue; }
        out += c;
    }
    return out + "'";
}

// 1.11.4 : ce que le moteur colle a la place du parametre - une constante texte a repere
// ('$Nom$') sans ses `$` ('Nom') : un repere est transparent pour le calcul, comme partout.
std::string withoutMarkers(std::string text) {
    std::string inner;
    if (text.find('$') == std::string::npos || !stringLiteral(text, inner)) return text;
    const auto kept = markerDollars(inner);
    if (kept.empty()) return text;
    std::string plain;
    for (std::size_t i = 0; i < inner.size(); ++i)
        if (!kept.count(i)) plain += inner[i];
    std::string out = "'";
    for (const char c : plain) {
        if (c == '\'' || c == '$') out += '$';
        if (c == '\n') { out += "$N"; continue; }
        out += c;
    }
    return out + "'";
}
} // namespace

bool isTextType(std::string_view type) noexcept {
    std::string u;
    for (const char c : type)
        if (!std::isspace(uc(c))) u += static_cast<char>(std::toupper(uc(c)));
    return u == "STRING" || u == "WSTRING" || u.rfind("STRING[", 0) == 0 || u.rfind("WSTRING[", 0) == 0 || u == "TEXTE";
}

bool isAggregateType(const Project* project, std::string_view type) {
    const std::string u = squeezedUpper(type);
    if (u.rfind("ARRAY", 0) == 0) return true;
    if (!project) return false;
    const HmiType* t = project->hmiTypeByName(trimmedCopy(type));
    return t && t->kind == HmiTypeKind::Structure;
}

std::string effectiveArgument(const Project* project, std::string_view type, std::string_view text) {
    return constantArgument(project, type, trimmedCopy(text));
}

bool isLiteralArgument(std::string_view text) {
    const std::string t = trimmedCopy(text);
    if (t.empty() || isVariablePath(t)) return false;
    // Un nombre ST : un point, jamais une virgule (1,5 se convertit : ce n'est pas encore un litteral).
    if (std::isdigit(uc(t.front())) || ((t.front() == '-' || t.front() == '+') && t.size() > 1)) {
        if (t.find('#') != std::string::npos) return standsAlone(t);   // 16#FF
        std::size_t i = (t.front() == '-' || t.front() == '+') ? 1 : 0;
        bool digits = false, dot = false, exp = false;
        for (; i < t.size(); ++i) {
            const char c = t[i];
            if (std::isdigit(uc(c)) || c == '_') { digits = digits || c != '_'; continue; }
            if (c == '.' && !dot && !exp) { dot = true; continue; }
            if ((c == 'e' || c == 'E') && digits && !exp) {
                exp = true;
                if (i + 1 < t.size() && (t[i + 1] == '-' || t[i + 1] == '+')) ++i;
                continue;
            }
            return false;
        }
        return digits;
    }
    return standsAlone(t);
}

std::string shownLiteral(std::string_view text) {
    const std::string t = trimmedCopy(text);
    std::string inner;
    if (stringLiteral(t, inner)) return inner;
    return t;
}

void setPlcNames(std::shared_ptr<const std::set<std::string, std::less<>>> upperRoots) {
    const std::lock_guard<std::mutex> lock(gPlcNamesLock);
    gPlcNames = std::move(upperRoots);
}

bool isKnownName(const Project* project, std::string_view root) {
    const std::string r = trimmedCopy(root);
    if (r.empty()) return false;
    const std::string u = upperCopy(r);
    if (u == "SYS" || u == "THIS" || u == "TRUE" || u == "FALSE") return true;
    if (project) {
        if (project->variable(r)) return true;
        if (project->viewByName(r)) return true;
        if (project->functionByName(r)) return true;
        if (project->hmiTypeByName(r)) return true;    // TO_T_MODE, T_MODE#... : un nom de type
        // Le parametre d'une vue ou d'un symbole (une instance dans un symbole lui passe le sien :
        // Name := Nom), un objet (ses variables publiques : STEST_1.Ouvert).
        const auto same = [&r](std::string_view n) {
            if (n.size() != r.size()) return false;
            for (std::size_t i = 0; i < n.size(); ++i)
                if (std::toupper(uc(n[i])) != std::toupper(uc(r[i]))) return false;
            return true;
        };
        for (const auto& v : project->views) {
            for (const auto& prm : v.params)
                if (same(prm.name)) return true;
            for (const auto& o : v.objects)
                if (same(o.name)) return true;
        }
    }
    if (isStandardFunction(r)) return true;
    std::shared_ptr<const std::set<std::string, std::less<>>> names;
    {
        const std::lock_guard<std::mutex> lock(gPlcNamesLock);
        names = gPlcNames;
    }
    return names && names->count(u) > 0;
}

std::string argumentLiteral(const Project* project, std::string_view type, std::string_view text, std::string* why) {
    const auto fail = [why](std::string m) { if (why) *why = std::move(m); return std::string{}; };
    const std::string t = trimmedCopy(text);
    if (t.empty()) return fail("vide");
    const std::string u = squeezedUpper(type);
    const std::string ut = upperCopy(t);
    double num = 0;
    // ANY : un nombre, un booleen, un litteral deja ecrit ; sinon un texte.
    if (u.empty() || u == "ANY") {
        if (isLiteralArgument(t)) return t;
        if (parseNumber(t, num)) {
            std::string n = t;
            std::replace(n.begin(), n.end(), ',', '.');
            return n;
        }
        if (ut == "VRAI" || ut == "OUI") return "TRUE";
        if (ut == "FAUX" || ut == "NON") return "FALSE";
        return quoted(t);
    }
    if (isTextType(type)) {
        std::string inner;
        if (stringLiteral(t, inner)) return t;
        if (t.size() >= 2 && t.front() == '"' && t.back() == '"') return quoted(std::string_view(t).substr(1, t.size() - 2));
        return quoted(t);
    }
    if (u.rfind("ARRAY", 0) == 0)
        return fail("Un tableau (" + std::string(trimmedCopy(type)) + ") se donne par une variable du m\xC3\xAAme type : utilisez fx.");
    if (u == "BOOL") {
        if (ut == "TRUE" || ut == "VRAI" || ut == "OUI" || ut == "ON" || ut == "1" || ut == "YES") return "TRUE";
        if (ut == "FALSE" || ut == "FAUX" || ut == "NON" || ut == "OFF" || ut == "0" || ut == "NO") return "FALSE";
        return fail("\xC2\xAB " + t + " \xC2\xBB n'est pas un bool\xC3\xA9" "en : TRUE ou FALSE.");
    }
    long long lo = 0;
    unsigned long long hi = 0;
    if (integerBounds(u, lo, hi)) {
        std::string body = t;
        if (const auto hash = body.find('#'); hash != std::string::npos && identStart(body[0])) body = body.substr(hash + 1);   // INT#12
        if (body.size() > 2 && std::isdigit(uc(body[0])) && body.find('#') != std::string::npos) return body;              // 16#FF
        std::string digits;
        for (const char c : body)
            if (c != '_' && c != ' ') digits += c;
        double v = 0;
        if (!parseNumber(digits, v) || std::floor(v) != v)
            return fail("\xC2\xAB " + t + " \xC2\xBB n'est pas un nombre entier (" + u + ").");
        if (v < static_cast<double>(lo) || v > static_cast<double>(hi))
            return fail(digits + " est hors des bornes de " + u + " (" + std::to_string(lo) + ".." + std::to_string(hi) + ").");
        const long long n = static_cast<long long>(v);
        return std::to_string(n);
    }
    if (u == "REAL" || u == "LREAL") {
        std::string body = t;
        if (const auto hash = body.find('#'); hash != std::string::npos && identStart(body[0])) body = body.substr(hash + 1);
        std::string n;
        for (const char c : body)
            if (c != '_' && c != ' ') n += c == ',' ? '.' : c;
        if (!parseNumber(n, num)) return fail("\xC2\xAB " + t + " \xC2\xBB n'est pas un nombre (" + u + ").");
        if (n.find_first_of(".eE") == std::string::npos) n += ".0";
        if (n.front() == '.') n.insert(n.begin(), '0');
        return n;
    }
    if (u == "TIME") {
        if (ut.rfind("T#", 0) == 0 || ut.rfind("TIME#", 0) == 0) return t;
        std::string compact;
        for (const char c : t)
            if (c != ' ') compact += c;
        if (durationText(compact)) return "T#" + compact;
        if (parseNumber(compact, num) && num >= 0) return "T#" + compact + "ms";
        return fail("\xC2\xAB " + t + " \xC2\xBB n'est pas une dur\xC3\xA9" "e : T#5s, 1m30s, 250ms.");
    }
    if (u == "DATE" || u == "TOD" || u == "TIME_OF_DAY" || u == "DT" || u == "DATE_AND_TIME") {
        if (t.find('#') != std::string::npos) return t;
        const char* prefix = u == "DATE" ? "D#" : (u == "TOD" || u == "TIME_OF_DAY") ? "TOD#" : "DT#";
        return prefix + t;
    }
    if (project)
        if (const HmiType* en = findEnumeration(*project, trimmedCopy(type))) {
            std::string v = t;
            if (const auto hash = v.find('#'); hash != std::string::npos && sameName(v.substr(0, hash), en->name)) v = v.substr(hash + 1);
            if (const HmiEnumValue* ev = enumValueByName(*en, v)) return enumLiteral(*en, *ev);
            std::int64_t number = 0;
            if (enumNumberOf(*en, v, number))
                if (const HmiEnumValue* ev = enumValueByNumber(*en, number)) return enumLiteral(*en, *ev);
            std::string list;
            for (const auto& ev : en->values) list += (list.empty() ? "" : ", ") + ev.name;
            return fail("\xC2\xAB " + t + " \xC2\xBB n'est pas une valeur de " + en->name + " (" + list + ").");
        }
    if (isAggregateType(project, type))
        return fail("Une structure (" + trimmedCopy(type) + ") se donne par une variable du m\xC3\xAAme type : utilisez fx.");
    // Un DDT de l'automate, un type inconnu : une constante deja ecrite passe, le reste non.
    if (isLiteralArgument(t)) return t;
    return fail("Une valeur de " + trimmedCopy(type) + " se donne par une variable : utilisez fx.");
}

// ========================================================= remplacements ====
std::string substituteParams(std::string_view text, const SymbolArguments& args, bool code) {
    if (args.empty() || text.empty()) return std::string(text);
    std::vector<std::pair<std::string, std::string>> table;
    for (const auto& [name, value] : args) {
        const std::string t = trimmedCopy(value);
        table.emplace_back(upperCopy(name), standsAlone(t) ? t : "(" + t + ")");
    }
    return rewriteRoots(text, code, [&](std::string_view s, std::size_t b, std::size_t e) -> Replacement {
        const std::string id = upperCopy(s.substr(b, e - b));
        for (const auto& [name, value] : table)
            if (name == id) return std::make_pair(e, value);
        return std::nullopt;
    });
}

std::string substituteInTemplate(std::string_view text, const SymbolArguments& args) {
    if (args.empty() || text.find('{') == std::string_view::npos) return std::string(text);
    return rewriteTemplate(text, [&](std::string_view e) { return substituteParams(e, args); });
}

std::string resolveLiteralHoles(std::string_view text) {
    if (text.find('{') == std::string_view::npos) return std::string(text);
    std::string out;
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
        std::string literal;
        if (stringLiteral(trimmedCopy(inside), literal)) {
            // Le texte tel quel ; ses accolades doublees (litterales dans un texte a trous).
            for (const char ch : literal) {
                out += ch;
                if (ch == '{' || ch == '}') out += ch;
            }
        } else {
            out += "{" + inside + "}";
        }
        i = close + 1;
    }
    return out;
}

std::string replacePath(std::string_view text, std::string_view path, std::string_view name, bool code) {
    std::string p;
    for (const char c : path) if (!std::isspace(uc(c))) p += c;
    std::size_t rootLen = 0;
    while (rootLen < p.size() && identChar(p[rootLen])) ++rootLen;
    if (rootLen == 0 || !identStart(p[0])) return std::string(text);
    const std::string root = upperCopy(p.substr(0, rootLen));
    const std::string suffix = p.substr(rootLen);
    return rewriteRoots(text, code, [&](std::string_view s, std::size_t b, std::size_t e) -> Replacement {
        if (upperCopy(s.substr(b, e - b)) != root) return std::nullopt;
        std::size_t k = e;
        for (const char want : suffix) {
            while (k < s.size() && std::isspace(uc(s[k]))) ++k;
            if (k >= s.size() || std::toupper(uc(s[k])) != std::toupper(uc(want))) return std::nullopt;
            ++k;
        }
        // Armoires[0] ne prend pas Armoires[0]x ; Moteur ne prend pas Moteur_2 (deja un seul nom).
        if (!suffix.empty() && identChar(suffix.back()) && k < s.size() && identChar(s[k])) return std::nullopt;
        return std::make_pair(k, std::string(name));
    });
}

void rewriteNames(Object& o, const std::function<std::string(std::string_view, bool code)>& f) {
    const auto expr = [&](std::string& t) {
        if (!trimmedCopy(t).empty()) t = f(t, false);
    };
    const auto tpl = [&](std::string& t) {
        if (t.find('{') != std::string::npos) t = rewriteTemplate(t, [&](std::string_view e) { return f(e, false); });
    };
    const auto args = [&](std::string& t) {
        const auto list = parseArguments(t);
        if (list.empty()) return;
        std::string rebuilt;
        for (const auto& [name, value] : list) rebuilt += (rebuilt.empty() ? "" : "; ") + name + " := " + f(value, false);
        t = rebuilt;
    };
    for (auto& p : o.props) {
        if (!p.expr.empty()) p.expr = f(p.expr, false);
        if (p.key == "text") {
            tpl(p.value);
        } else if (p.key == "variable" || p.key == "output" || p.key == "state" || p.key == "lamp" || p.key == "condition"
                   || p.key == "auth"
                   // lot 11 : l'abscisse d'une courbe XY, sa remise a zero, les compteurs de
                   // production, l'ouverture et le mouvement d'une vanne reglante
                   || p.key == "xVariable" || p.key == "reset" || p.key == "good" || p.key == "bad" || p.key == "running"
                   || p.key == "opening" || p.key == "moving") {
            expr(p.value);
        } else if (p.key == "variables" || p.key == "references") {   // les plumes d'une courbe, les elements d'un graphique : a;b;c
            std::string rebuilt;
            std::size_t from = 0;
            bool changed = false;
            while (from <= p.value.size()) {
                const auto at = p.value.find(';', from);
                const std::string pen = p.value.substr(from, at == std::string::npos ? std::string::npos : at - from);
                std::string next = pen;
                expr(next);
                changed = changed || next != pen;
                rebuilt += (from ? ";" : "") + next;
                if (at == std::string::npos) break;
                from = at + 1;
            }
            if (changed) p.value = rebuilt;
        } else if (p.key == "states" && o.kind == Kind::AnimatedImage && !p.value.empty()) {
            auto states = parseImageStates(p.value);
            bool changed = false;
            for (auto& s : states) {
                std::string next = s.condition;
                expr(next);
                changed = changed || next != s.condition;
                s.condition = std::move(next);
            }
            if (changed) p.value = formatImageStates(states);
        } else if (p.key == "cells" && o.kind == Kind::Table && !p.value.empty()) {
            auto rows = parseCells(p.value);
            bool changed = false;
            for (auto& row : rows)
                for (auto& cell : row) {
                    std::string next = cell;
                    if (cellIsExpression(cell)) next = "=" + f(std::string_view(cell).substr(1), false);
                    else if (cellIsTemplate(cell)) tpl(next);
                    changed = changed || next != cell;
                    cell = std::move(next);
                }
            if (changed) p.value = formatCells(rows);
        } else if (p.key == "params" && o.kind == Kind::SymbolInstance) {
            args(p.value);
        }
    }
    for (auto& a : o.actions) {
        expr(a.watch);
        expr(a.guard);
        if (operationWritesVariable(a.operation) || a.operation == Operation::RequestResource) expr(a.target);
        if (a.operation == Operation::Assign || a.operation == Operation::Increment || a.operation == Operation::Decrement)
            expr(a.value);
        else if (a.operation == Operation::Log) tpl(a.value);
        else if (a.operation == Operation::RunScript) { if (!trimmedCopy(a.value).empty()) a.value = f(a.value, true); }
        else if (operationTakesArguments(a.operation)) args(a.value);
        if (!a.params.empty()) args(a.params);               // 1.11.6 : les references de Maths, les reglages du clavier
    }
}

std::vector<std::string> indexedPaths(const std::vector<const Object*>& objects) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    const auto collect = [&](std::string_view text, bool code) {
        (void)rewriteRoots(text, code, [&](std::string_view s, std::size_t b, std::size_t e) -> Replacement {
            std::size_t k = e;
            while (k < s.size() && std::isspace(uc(s[k]))) ++k;
            if (k >= s.size() || s[k] != '[') return std::nullopt;
            int depth = 0;
            std::size_t m = k;
            for (; m < s.size(); ++m) {
                if (s[m] == '[') ++depth;
                else if (s[m] == ']' && --depth == 0) break;
            }
            if (m >= s.size()) return std::nullopt;
            std::string path(s.substr(b, e - b));
            path += "[" + trimmedCopy(s.substr(k + 1, m - k - 1)) + "]";
            if (seen.insert(upperCopy(path)).second) out.push_back(path);
            return std::nullopt;
        });
        return std::string(text);
    };
    for (const Object* o : objects) {
        if (!o) continue;
        Object copy = *o;
        rewriteNames(copy, collect);
    }
    return out;
}

std::string suggestSymbolParams(const View& v, const std::vector<Id>& selection) {
    std::vector<const Object*> objects;
    for (Id id : edit::units(v, selection)) {
        objects.push_back(v.object(id));
        for (Id d : v.descendantsOf(id)) objects.push_back(v.object(d));
    }
    std::string out;
    std::set<std::string> names;
    int shown = 0;
    for (const auto& path : indexedPaths(objects)) {
        if (++shown > 4) break;
        const std::string root = path.substr(0, path.find('['));
        std::string name = root;
        if (root.size() > 3 && (root.back() == 's' || root.back() == 'S')) name.pop_back();
        else name += "_1";
        std::string unique = name;
        for (int k = 2; names.count(upperCopy(unique)) || sameName(unique, root); ++k) unique = name + "_" + std::to_string(k);
        names.insert(upperCopy(unique));
        out += (out.empty() ? "" : "; ") + unique + " := " + path;
    }
    return out;
}

// ============================================================= expansion ====
Id expandedId(Id instance, Id child) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    for (const std::uint32_t v : {static_cast<std::uint32_t>(instance), static_cast<std::uint32_t>(child)})
        for (int k = 0; k < 4; ++k) {
            h ^= (v >> (8 * k)) & 0xFFu;
            h *= 1099511628211ull;
        }
    return static_cast<Id>(0x80000000u | static_cast<std::uint32_t>((h ^ (h >> 31)) & 0x7FFFFFFFu));
}

namespace {
// 1.11.10 : les actions qui ouvrent une popup du symbole lui passent ses parametres et l'instance.
void addOwnedPopupArguments(const Project& p, const View& sym, const std::string& qualified, std::vector<Action>& actions) {
    for (auto& a : actions)
        if ((a.operation == Operation::Popup || a.operation == Operation::ChangePopup) && ownedPopup(p, sym, trimmedCopy(a.target)))
            a.value = withSymbolArguments(a.value, sym, qualified);
}
} // namespace

Expansion expandInstance(const Project& p, const Object& inst, int depth, std::string_view qualified) {
    Expansion out;
    if (depth >= kMaxSymbolDepth) return out;
    const View* sym = symbolOf(p, inst);
    if (!sym) return out;
    const SymbolArguments args = symbolArguments(*sym, inst, &p);
    // 1.11.10 : les appels des fonctions du symbole (et de ses instances) visent CETTE instance.
    const std::string q = qualified.empty() ? inst.name : std::string(qualified);
    const bool calls = !sym->functions.empty() || hasInstances(*sym);
    const Box ib = inst.box();
    const double sx = sym->width > 0 ? ib.w / sym->width : 1.0;
    const double sy = sym->height > 0 ? ib.h / sym->height : 1.0;
    std::set<std::string> siblings;
    for (const auto& so : sym->objects) siblings.insert(so.name);
    const auto relink = [&](std::string_view t, bool code) {
        if (!calls) return substituteParams(t, args, code);
        return substituteParams(qualifySymbolCalls(t, p, *sym, q), args, code);
    };
    for (const Object* so : sym->paintOrder()) {
        Object c = *so;
        c.id = expandedId(inst.id, so->id);
        c.name = inst.name + "." + so->name;
        c.layer = inst.layer;
        c.parent = so->parent != kNoId && sym->object(so->parent) ? expandedId(inst.id, so->parent) : inst.id;
        c.locked = false;
        addOwnedPopupArguments(p, *sym, q, c.actions);          // 1.11.10 : ses popups recoivent l'instance
        if (!args.empty() || calls) rewriteNames(c, relink);
        if (auto* tp = c.find("text")) tp->value = resolveLiteralHoles(tp->value);
        // Lier un tableau vise un objet par son nom : celui de l'instance.
        for (auto& a : c.actions)
            if (a.operation == Operation::BindTable && siblings.count(a.target)) a.target = inst.name + "." + a.target;
        // 1.11.4 : sa geometrie dans le symbole, pour la reposer en marche (relayoutLive).
        c.set(std::string(kLocalGeometryKey), encodeLocalGeometry(c));
        place(c, inst, sx, sy);
        inherit(c, inst);
        if (c.kind == Kind::SymbolInstance)
            if (const View* inner = symbolOf(p, c))
                c.set(std::string(kSymbolSizeKey), formatNumber(inner->width) + ";" + formatNumber(inner->height));
        out.objects.push_back(c);
        if (c.kind == Kind::SymbolInstance) {
            auto inner = expandInstance(p, c, depth + 1, q + "." + so->name);
            for (auto& o : inner.objects) out.objects.push_back(std::move(o));
            for (auto& s : inner.scripts) out.scripts.push_back(std::move(s));
            for (auto& a : inner.actions) out.actions.push_back(std::move(a));
        }
    }
    for (const auto& sc : sym->scripts) {
        Script s = sc;
        s.id = expandedId(inst.id, sc.id);
        s.name = inst.name + "." + sc.name;
        if (s.lang == ScriptLang::ST && (!args.empty() || calls)) s.body = relink(s.body, true);
        out.scripts.push_back(std::move(s));
    }
    if (!sym->actions.empty()) {
        Object holder;
        holder.actions = sym->actions;
        addOwnedPopupArguments(p, *sym, q, holder.actions);
        if (!args.empty() || calls) rewriteNames(holder, relink);
        for (auto& a : holder.actions) out.actions.push_back(std::move(a));
    }
    return out;
}

View expandInstances(const Project& p, const View& v) {
    if (!hasInstances(v)) return v;
    View out = v;
    out.objects.clear();
    out.objects.reserve(v.objects.size() * 2);
    std::set<Id> used;
    for (const auto& o : v.objects) used.insert(o.id);
    for (const auto& sc : v.scripts) used.insert(sc.id);
    const auto fresh = [&](Id id) {
        while (used.count(id)) id = static_cast<Id>(0x80000000u | ((static_cast<std::uint32_t>(id) + 1u) & 0x7FFFFFFFu));
        used.insert(id);
        return id;
    };
    // 1.11.10 : dans le code de la vue, Vanne_3.Ouvrir( vise l'instance de CETTE vue ; une
    // popup Vanne_3.Pop_Detail est celle du symbole, ouverte avec les arguments de Vanne_3.
    const auto viewCalls = [&](std::string_view t, bool) { return qualifyViewCalls(t, p, v); };
    const auto instancePopups = [&](std::vector<Action>& list) {
        for (auto& a : list) {
            if (a.operation != Operation::Popup && a.operation != Operation::ChangePopup) continue;
            std::string popup, given;
            if (!popupOfInstance(p, &v, trimmedCopy(a.target), popup, given)) continue;
            a.target = popup;
            a.value = given + (trimmedCopy(a.value).empty() ? std::string{} : "; " + a.value);
        }
    };
    instancePopups(out.actions);
    for (auto& sc : out.scripts)
        if (sc.lang == ScriptLang::ST) sc.body = qualifyViewCalls(sc.body, p, v);
    if (!out.actions.empty()) {
        Object holder;
        holder.actions = std::move(out.actions);
        rewriteNames(holder, viewCalls);
        out.actions = std::move(holder.actions);
    }
    for (const auto& o : v.objects) {
        out.objects.push_back(o);
        instancePopups(out.objects.back().actions);
        rewriteNames(out.objects.back(), viewCalls);
        if (o.kind != Kind::SymbolInstance) continue;
        // 1.11.4 : la taille du symbole, pour reposer ses objets en marche (relayoutLive).
        if (const View* sym = symbolOf(p, o))
            out.objects.back().set(std::string(kSymbolSizeKey), formatNumber(sym->width) + ";" + formatNumber(sym->height));
        Expansion e = expandInstance(p, o, 0, v.name + "." + o.name);
        // Deux identifiants derives egaux (improbable) : le second avance.
        std::map<Id, Id> moved;
        for (auto& c : e.objects) {
            const Id id = fresh(c.id);
            if (id != c.id) moved[c.id] = id;
            c.id = id;
        }
        for (auto& c : e.objects) {
            if (const auto it = moved.find(c.parent); it != moved.end()) c.parent = it->second;
            out.objects.push_back(std::move(c));
        }
        for (auto& s : e.scripts) {
            s.id = fresh(s.id);
            out.scripts.push_back(std::move(s));
        }
        for (auto& a : e.actions) out.actions.push_back(std::move(a));
    }
    return out;
}

// 1.11.4 : la meme pose, en marche (HmiLiveGeometry : l'instance a une formule de geometrie).
void placeInInstance(Object& o, const Object& instance, double symbolWidth, double symbolHeight) {
    const Box ib = instance.box();
    place(o, instance, symbolWidth > 0 ? ib.w / symbolWidth : 1.0, symbolHeight > 0 ? ib.h / symbolHeight : 1.0);
}

// =============================================================== edition ====
Id placeSymbol(Project& p, View& view, std::string_view symbol, double x, double y) {
    const View* sym = p.viewByName(symbol);
    if (!sym || !isSymbolView(*sym)) return kNoId;
    const std::string name(symbol);
    Object o = makeObject(Kind::SymbolInstance, p.allocate(), uniqueObjectName(view, name), x, y, view.activeLayer);
    o.setNumber("w", std::max(1, sym->width));
    o.setNumber("h", std::max(1, sym->height));
    o.set("symbol", name);
    return edit::addObject(view, std::move(o));
}

bool createSymbol(Project& p, Id viewId, const std::vector<Id>& selection, const std::string& name,
                  const std::string& params, Id* instance, std::string* why) {
    const auto fail = [&](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    View* host = p.view(viewId);
    if (!host) return fail("vue introuvable");
    if (!isIdentifier(name)) return fail("nom de symbole invalide : '" + name + "' (lettres, chiffres, _ ; pas de chiffre en t\xC3\xAAte)");
    if (p.viewByName(name)) return fail("'" + name + "' existe d\xC3\xA9j\xC3\xA0 dans le projet");
    const auto units = edit::units(*host, selection);
    if (units.empty()) return fail("aucun objet choisi : s\xC3\xA9lectionne d'abord ce qui fera le symbole");
    std::vector<ViewParam> declared;
    for (const auto& [n, def] : parseArguments(params)) {
        if (!isIdentifier(n)) return fail("param\xC3\xA8tre : nom invalide '" + n + "'");
        for (const auto& d : declared)
            if (sameName(d.name, n)) return fail("param\xC3\xA8tre en double : " + n);
        ViewParam prm;
        prm.name = n;
        prm.defaultValue = def;
        declared.push_back(std::move(prm));
    }
    std::set<Id> taken;
    for (Id u : units) {
        taken.insert(u);
        for (Id d : host->descendantsOf(u)) taken.insert(d);
    }
    const Box b = edit::selectionBounds(*host, units);

    View sym = makeView(p, name);
    sym.role = std::string(kSymbolRole);
    sym.width = std::max(8, static_cast<int>(std::ceil(b.w - 1e-6)));
    sym.height = std::max(8, static_cast<int>(std::ceil(b.h - 1e-6)));
    sym.background = host->background;
    sym.grid = host->grid;
    sym.params = declared;
    const Id layer = sym.activeLayer;
    std::map<Id, Id> ids;
    for (const Object* o : host->paintOrder())
        if (taken.count(o->id)) ids[o->id] = p.allocate();
    int at = INT_MAX;
    for (const Object* o : host->paintOrder()) {
        if (!taken.count(o->id)) continue;
        at = std::min(at, host->indexOf(o->id));
        Object c = *o;
        c.id = ids[o->id];
        c.parent = ids.count(o->parent) ? ids[o->parent] : kNoId;
        c.layer = layer;
        c.locked = false;
        c.setNumber("x", c.number("x") - b.x);
        c.setNumber("y", c.number("y") - b.y);
        // Les chemins donnes en valeur par defaut deviennent le nom du parametre.
        for (const auto& prm : declared) {
            const std::string def = trimmedCopy(prm.defaultValue);
            if (!isVariablePath(def)) continue;
            rewriteNames(c, [&](std::string_view t, bool code) { return replacePath(t, def, prm.name, code); });
        }
        sym.objects.push_back(std::move(c));
    }
    const Id instLayer = host->object(units.front())->layer;
    edit::remove(*host, units);
    Object inst = makeObject(Kind::SymbolInstance, p.allocate(), uniqueObjectName(*host, name), b.x, b.y, instLayer);
    inst.setNumber("w", sym.width);
    inst.setNumber("h", sym.height);
    inst.set("symbol", name);
    const Id made = inst.id;
    if (host->layerRank(inst.layer) < 0) inst.layer = host->activeLayer;
    const auto pos = static_cast<std::size_t>(std::clamp(at, 0, static_cast<int>(host->objects.size())));
    host->objects.insert(host->objects.begin() + static_cast<std::ptrdiff_t>(pos), std::move(inst));
    if (instance) *instance = made;
    // En dernier : `host` designe un element de p.views.
    p.views.push_back(std::move(sym));
    return true;
}

std::size_t renameSymbol(Project& p, std::string_view from, std::string_view to) {
    std::size_t n = 0;
    (void)renameTypeInOperators(p, from, to);     // 1.10 : les operateurs qui le citent (et les siens) suivent
    for (auto& v : p.views)
        for (auto& o : v.objects)
            if (o.kind == Kind::SymbolInstance && trimmedCopy(o.text("symbol")) == from) {
                o.set("symbol", std::string(to));
                ++n;
            }
    // 1.9 : le filtre "symbole:<nom>" des objets d'alarmes et des historiques suit
    // (les autres elements du filtre ne changent pas ; `n` compte les instances).
    for (auto& v : p.views)
        for (auto& o : v.objects) {
            if (o.kind != Kind::History && !kindIsAlarmView(o.kind)) continue;
            Prop* g = o.find("group");
            if (!g || g->value.find("symbole:") == std::string::npos) continue;
            std::string next;
            bool changed = false;
            std::size_t start = 0;
            while (start <= g->value.size()) {
                const std::size_t semi = std::min(g->value.find(';', start), g->value.size());
                std::string item = trimmedCopy(std::string_view(g->value).substr(start, semi - start));
                if (item.rfind("symbole:", 0) == 0 && trimmedCopy(std::string_view(item).substr(8)) == from) {
                    item = "symbole:" + std::string(to);
                    changed = true;
                }
                if (!item.empty()) next += (next.empty() ? "" : "; ") + item;
                start = semi + 1;
            }
            if (changed) g->value = std::move(next);
        }
    return n;
}

// ============================================== 1.11.10 : les fonctions d'un symbole ====
const HmiFunction* symbolFunction(const View& symbol, std::string_view name) {
    for (const auto& f : symbol.functions)
        if (sameName(f.name, name)) return &f;
    return nullptr;
}

const FunctionOverride* functionOverride(const Object& instance, std::string_view function) {
    for (const auto& o : instance.functionOverrides)
        if (sameName(o.function, function)) return &o;
    return nullptr;
}

const std::string& effectiveFunctionBody(const HmiFunction& f, const Object& instance, bool* overridden) {
    const FunctionOverride* o = f.isVirtual ? functionOverride(instance, f.name) : nullptr;
    if (overridden) *overridden = o != nullptr;
    return o ? o->body : f.body;
}

namespace {
// L'objet instance `name` pose dans `v` (nul : aucun).
const Object* instanceNamed(const View& v, std::string_view name) {
    for (const auto& o : v.objects)
        if (o.kind == Kind::SymbolInstance && sameName(o.name, name)) return &o;
    return nullptr;
}
// La chaine qui suit une tete de chemin : ".Vanne_2.Ouvrir", puis "(" (des blancs permis
// avant la parenthese). Rend les segments apres la tete ; faux : pas un appel.
bool callChain(std::string_view s, std::size_t end, std::vector<std::string>& segments) {
    segments.clear();
    std::size_t i = end;
    while (i < s.size() && s[i] == '.') {
        std::size_t j = i + 1;
        if (j >= s.size() || !identStart(s[j])) return false;
        std::size_t k = j;
        while (k < s.size() && identChar(s[k])) ++k;
        segments.emplace_back(s.substr(j, k - j));
        i = k;
    }
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return i < s.size() && s[i] == '(';
}
// Depuis le symbole `sym`, les segments `segs` (des instances posees, puis la fonction)
// menent-ils a une fonction (ou a SUPER.fonction) ?
bool reachesFunction(const Project& p, const View* sym, const std::vector<std::string>& segs, std::size_t from) {
    for (std::size_t k = from; sym && k < segs.size(); ++k) {
        if (k + 1 == segs.size()) return symbolFunction(*sym, segs[k]) != nullptr;
        const Object* inner = instanceNamed(*sym, segs[k]);
        sym = inner ? symbolOf(p, *inner) : nullptr;
    }
    return false;
}
} // namespace

std::string qualifySymbolCalls(std::string_view code, const Project& p, const View& symbol, std::string_view qualified) {
    if (code.find('(') == std::string_view::npos) return std::string(code);
    std::vector<std::string> segs;
    return rewriteRoots(code, true, [&](std::string_view s, std::size_t b, std::size_t e) -> Replacement {
        if (!callChain(s, e, segs)) return std::nullopt;
        const std::string_view root = s.substr(b, e - b);
        bool ours = false;
        if (segs.empty()) ours = symbolFunction(symbol, root) != nullptr;                 // Ouvrir(
        else if (sameName(root, kSuperName)) ours = segs.size() == 1 && symbolFunction(symbol, segs[0]);   // SUPER.Ouvrir(
        else if (const Object* inner = instanceNamed(symbol, root)) ours = reachesFunction(p, symbolOf(p, *inner), segs, 0);
        if (!ours) return std::nullopt;
        return std::make_pair(e, std::string(qualified) + "." + std::string(root));
    });
}

std::string qualifyViewCalls(std::string_view code, const Project& p, const View& view) {
    if (code.find('(') == std::string_view::npos || !hasInstances(view)) return std::string(code);
    std::vector<std::string> segs;
    return rewriteRoots(code, true, [&](std::string_view s, std::size_t b, std::size_t e) -> Replacement {
        const std::string_view root = s.substr(b, e - b);
        const Object* inst = instanceNamed(view, root);
        if (!inst || !callChain(s, e, segs) || segs.empty()) return std::nullopt;
        if (!reachesFunction(p, symbolOf(p, *inst), segs, 0)) return std::nullopt;
        return std::make_pair(e, view.name + "." + std::string(root));
    });
}

namespace {
std::vector<std::string> dottedSegments(std::string_view path) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= path.size()) {
        const std::size_t dot = path.find('.', i);
        const std::size_t end = dot == std::string_view::npos ? path.size() : dot;
        out.push_back(trimmedCopy(path.substr(i, end - i)));
        if (dot == std::string_view::npos) break;
        i = dot + 1;
    }
    return out;
}
} // namespace

bool instanceAt(const Project& p, std::string_view path, InstanceAt& out) {
    const auto segs = dottedSegments(path);
    if (segs.size() < 2) return false;
    const View* v = p.viewByName(segs[0]);
    if (!v) return false;
    const Object* first = instanceNamed(*v, segs[1]);
    if (!first) return false;
    Object cur = *first;
    const View* sym = symbolOf(p, cur);
    for (std::size_t k = 2; sym && k < segs.size(); ++k) {
        const Object* inner = instanceNamed(*sym, segs[k]);
        if (!inner) return false;
        // Comme l'expansion : ses arguments citent les parametres de l'instance qui la contient.
        const SymbolArguments outer = symbolArguments(*sym, cur, &p);
        Object next = *inner;
        if (!outer.empty())
            rewriteNames(next, [&](std::string_view t, bool code) { return substituteParams(t, outer, code); });
        cur = std::move(next);
        sym = symbolOf(p, cur);
    }
    if (!sym) return false;
    out.view = v;
    out.symbol = sym;
    out.instance = std::move(cur);
    out.path = std::string(path);
    return true;
}

bool boundSymbolFunction(const Project& p, std::string_view call, BoundFunction& out) {
    auto segs = dottedSegments(call);
    if (segs.size() < 3) return false;
    const std::string name = segs.back();
    segs.pop_back();
    const bool base = sameName(segs.back(), kSuperName);
    if (base) segs.pop_back();
    std::string path;
    for (const auto& sgm : segs) path += (path.empty() ? "" : ".") + sgm;
    InstanceAt at;
    if (!instanceAt(p, path, at)) return false;
    const HmiFunction* f = symbolFunction(*at.symbol, name);
    if (!f) return false;
    bool overridden = false;
    const std::string& body = base ? f->body : effectiveFunctionBody(*f, at.instance, &overridden);
    // Les parametres du symbole, sauf ceux que la fonction cache (ses declarations, son nom).
    SymbolArguments args = symbolArguments(*at.symbol, at.instance, &p);
    const auto parts = splitDeclarations(body, true);
    args.erase(std::remove_if(args.begin(), args.end(),
                              [&](const auto& a) { return parts.local(a.first) != nullptr || sameName(a.first, f->name); }),
               args.end());
    out.function = *f;
    out.function.body = substituteParams(qualifySymbolCalls(body, p, *at.symbol, path), args, true);
    // Un identifiant a lui (les compteurs d'appels du moteur) : stable pour ce chemin.
    std::uint64_t h = 1469598103934665603ull;
    for (const char c : upperCopy(call)) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
    out.function.id = static_cast<Id>(0x80000000u | static_cast<std::uint32_t>((h ^ (h >> 31)) & 0x7FFFFFFFu));
    out.instance = path;
    out.symbol = at.symbol->name;
    out.overridden = overridden;
    return true;
}

namespace {
// La fin de la chaine ".a.b" qui suit une tete de chemin (l'index apres le dernier nom).
std::size_t chainEnd(std::string_view s, std::size_t end) {
    std::size_t i = end;
    while (i < s.size() && s[i] == '.' && i + 1 < s.size() && identStart(s[i + 1])) {
        std::size_t k = i + 1;
        while (k < s.size() && identChar(s[k])) ++k;
        i = k;
    }
    return i;
}
} // namespace

std::size_t renameSymbolFunction(Project& p, std::string_view symbol, std::string_view from, std::string_view to) {
    View* sym = p.viewByName(symbol);
    if (!sym || !isSymbolView(*sym) || from.empty() || to.empty()) return 0;
    std::size_t changed = 0;
    const std::string toName(to);
    // Dans le symbole : chaque tete `from` (ses appels, le resultat dans son propre corps).
    const SymbolArguments direct{{std::string(from), toName}};
    const auto inSymbol = [&](std::string_view t, bool code) { return substituteParams(t, direct, code); };
    const auto apply = [&](std::string& text, const std::string& next) {
        if (next == text) return;
        text = next;
        ++changed;
    };
    // SUPER.from( dans les corps (les redefinitions le citent).
    const auto superCalls = [&](std::string_view t) {
        return rewriteRoots(t, true, [&](std::string_view s2, std::size_t b, std::size_t e) -> Replacement {
            if (!sameName(s2.substr(b, e - b), kSuperName)) return std::nullopt;
            const std::size_t end = chainEnd(s2, e);
            if (end == e || !sameName(s2.substr(e + 1, end - e - 1), from)) return std::nullopt;
            return std::make_pair(end, std::string(s2.substr(b, e - b)) + "." + toName);
        });
    };
    // 1.11.17 : chaque texte lu dans le symbole (forEachCode) - son code, ses fonctions, les
    // redefinitions de ses instances et ses popups (avant oubliees : une popup du symbole
    // appelle ses fonctions par leur nom court, qualifiedOwnedPopup).
    forEachCode(p, [&](std::string& text, CodeForm form, const CodeSite& s) {
        if (s.scope != sym || text.empty()) return;
        apply(text, form == CodeForm::Template ? rewriteInText(text, [&](std::string_view e) { return inSymbol(e, false); })
                                               : superCalls(inSymbol(text, form == CodeForm::Code)));
    });
    // Les redefinitions des instances : leur nom (leur corps a suivi plus haut).
    for (auto& v : p.views)
        for (auto& o : v.objects) {
            if (o.kind != Kind::SymbolInstance || !sameName(trimmedCopy(o.text("symbol")), symbol)) continue;
            for (auto& fo : o.functionOverrides)
                if (sameName(fo.function, from)) { fo.function = toName; ++changed; }
        }
    // Les appels qualifies : Vanne_3.from( (dans une vue ou un symbole qui pose l'instance)
    // et Vue.Vanne_3.from( (partout).
    const auto qualified = [&](std::string_view t, const View* here) {
        if (t.find('(') == std::string_view::npos) return std::string(t);
        return rewriteRoots(t, true, [&](std::string_view s2, std::size_t b, std::size_t e) -> Replacement {
            const std::size_t end = chainEnd(s2, e);
            if (end == e) return std::nullopt;
            std::size_t k = end;
            while (k < s2.size() && (s2[k] == ' ' || s2[k] == '\t')) ++k;
            if (k >= s2.size() || s2[k] != '(') return std::nullopt;
            const std::size_t lastDot = s2.rfind('.', end - 1);
            if (lastDot == std::string_view::npos || lastDot < e || !sameName(s2.substr(lastDot + 1, end - lastDot - 1), from))
                return std::nullopt;
            const std::string path(s2.substr(b, lastDot - b));
            const View* target = nullptr;
            if (InstanceAt at; instanceAt(p, path, at)) target = at.symbol;
            if (!target && here) {                                          // relatif a la vue (ou au symbole)
                const auto segs = dottedSegments(path);
                const View* cur = here;
                for (const auto& sg : segs) {
                    const Object* inst = cur ? instanceNamed(*cur, sg) : nullptr;
                    cur = inst ? symbolOf(p, *inst) : nullptr;
                }
                target = cur;
            }
            if (!target || !sameName(target->name, symbol)) return std::nullopt;
            return std::make_pair(end, path + "." + toName);
        });
    };
    // 1.11.17 : partout (forEachCode) - avant, ni les operateurs des symboles, ni les
    // redefinitions des autres instances, ni les alarmes, recettes, autorisations...
    forEachCode(p, [&](std::string& text, CodeForm form, const CodeSite& s) {
        if (text.find('(') == std::string::npos) return;
        apply(text, form == CodeForm::Template ? rewriteInText(text, [&](std::string_view e) { return qualified(e, s.view); })
                                               : qualified(text, s.view));
    });
    return changed;
}

// ================================================= 1.11.10 : les popups d'un symbole ====
const View* ownedPopup(const Project& p, const View& symbol, std::string_view name) {
    const View* v = p.viewByName(trimmedCopy(name));
    return v && v->ownerSymbol == symbol.id && symbol.id != kNoId ? v : nullptr;
}

const View* popupOwner(const Project& p, const View& popup) {
    if (popup.ownerSymbol == kNoId) return nullptr;
    const View* s = p.view(popup.ownerSymbol);
    return s && isSymbolView(*s) ? s : nullptr;
}

std::string withSymbolArguments(std::string_view given, const View& symbol, std::string_view qualified, const SymbolArguments* values) {
    const auto named = parseArguments(given);
    const auto has = [&](std::string_view n) {
        return std::any_of(named.begin(), named.end(), [&](const auto& g) { return sameName(g.first, n); });
    };
    std::string out;
    const auto add = [&](const std::string& n, const std::string& t) { out += (out.empty() ? "" : "; ") + n + " := " + t; };
    for (const auto& prm : symbol.params) {
        if (has(prm.name)) continue;
        if (!values) { add(prm.name, prm.name); continue; }
        for (const auto& [n, t] : *values)
            if (sameName(n, prm.name)) add(prm.name, t);
    }
    if (!has(kInstanceAlias)) add(std::string(kInstanceAlias), std::string(qualified));
    const std::string rest = trimmedCopy(given);
    if (!rest.empty()) out += (out.empty() ? "" : "; ") + rest;
    return out;
}

bool popupOfInstance(const Project& p, const View* here, std::string_view target, std::string& popup, std::string& arguments) {
    const std::size_t dot = target.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return false;
    const std::string path = trimmedCopy(target.substr(0, dot));
    const std::string name = trimmedCopy(target.substr(dot + 1));
    InstanceAt at;
    bool found = false;
    if (here && !path.empty()) found = instanceAt(p, here->name + "." + path, at);
    if (!found) found = instanceAt(p, path, at);
    if (!found) return false;
    const View* pop = ownedPopup(p, *at.symbol, name);
    if (!pop) return false;
    const SymbolArguments args = symbolArguments(*at.symbol, at.instance, &p);
    popup = pop->name;
    arguments = withSymbolArguments({}, *at.symbol, at.path, &args);
    return true;
}

View qualifiedOwnedPopup(const Project& p, const View& popup) {
    const View* sym = popupOwner(p, popup);
    if (!sym || (sym->functions.empty() && !hasInstances(*sym))) return popup;
    View out = popup;
    const auto calls = [&](std::string_view t, bool) { return qualifySymbolCalls(t, p, *sym, kInstanceAlias); };
    for (auto& o : out.objects) rewriteNames(o, calls);
    for (auto& sc : out.scripts)
        if (sc.lang == ScriptLang::ST) sc.body = qualifySymbolCalls(sc.body, p, *sym, kInstanceAlias);
    Object holder;
    holder.actions = std::move(out.actions);
    rewriteNames(holder, calls);
    out.actions = std::move(holder.actions);
    return out;
}

} // namespace hmi
