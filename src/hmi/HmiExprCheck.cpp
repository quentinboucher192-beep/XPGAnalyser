// =============================================================================
//  hmi/HmiExprCheck.cpp - Lot API 8 : les expressions impossibles
//  (voir HmiExprCheck.hpp). Une lecture a part, tolerante : elle ne refait pas
//  le travail de sim::parse (la syntaxe), elle suit les noms, les appels et
//  les types, et ne s'arrete jamais sur ce qu'elle ne comprend pas.
// =============================================================================
#include "HmiExprCheck.hpp"
#include "HmiCalc.hpp"   // 1.12.7 : les constantes qualifiees (Reglages.Seuils)
#include "../sim/DialectLib.hpp"   // 1.12.7 : les natives de std::numeric, de <bit>, les templates
#include "../core/Edition.hpp"   // 1.12.0 : IHM Studio - pas d'espace API.
#include "HmiNatives.hpp"   // 1.12.0 : les couleurs, l'aleatoire
#include "HmiCallCheck.hpp"     // 1.11.20 : les appels, controles comme le moteur les fait
#include "HmiOverload.hpp"      // 1.11.20 : les signatures et les surcharges
#include "HmiDecl.hpp"   // 1.11.18 (refonte, lot 3) : les declarations du modele, reconstruites
#include "HmiApiVars.hpp"   // 1.11.1 (API-M) : API.<globale>, API.<Unite>.<variable>
#include "HmiPopupParams.hpp"
#include "HmiRoles.hpp"      // 1.12.6 (lot B) : le role de chaque propriete

#include "HmiExpr.hpp"
#include "HmiMarkers.hpp"   // 1.11 (REP) : les reperes $...$, transparents pour la verification
#include "HmiOperators.hpp"
#include "HmiPublicVars.hpp"
#include "HmiScript.hpp"
#include "HmiSymbols.hpp"   // 1.11.10 : les fonctions des symboles
#include "HmiTypes.hpp"
#include "../project/MemberTree.hpp"
#include "HmiTypeRegistry.hpp"   // 1.11.19 (refonte, lot 6) : un type est-il un nombre ?
#include "HmiUnits.hpp"   // 1.13.0 (lot J6) : les unites de mesure (familles, conversions, verdict)
#include "HmiLibrary.hpp" // 1.13.0 : biblio::unitConfig (les unites des bibliotheques reliees)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <set>

#include "HmiNames.hpp"   // 1.13.0 (lot J8) : les espaces de noms (Util.Moyenne, Gaz.T_Vanne)

namespace hmi::exprcheck {

namespace {

std::string up(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// Les types qu'on sait suivre ; Unknown ne declenche jamais d'erreur.
enum class T : std::uint8_t { Unknown, Bool, Num, Text, Time };

std::string typeWord(T t) {
    switch (t) {
        case T::Bool: return "un bool\xC3\xA9" "en";
        case T::Num:  return "un nombre";
        case T::Text: return "un texte";
        case T::Time: return "une dur\xC3\xA9" "e";
        case T::Unknown: break;
    }
    return "une valeur";
}

// Le type d'un type elementaire IHM ("REAL", "STRING"...).
T typeOfName(std::string_view type) {
    const std::string u = up(trimmed(type));
    if (u.empty()) return T::Unknown;
    if (u == "BOOL" || u == "EBOOL") return T::Bool;
    if (u == "STRING" || u.rfind("STRING[", 0) == 0 || u == "WSTRING") return T::Text;
    if (u == "TIME" || u == "LTIME") return T::Time;
    return typereg::isNumber(u) ? T::Num : T::Unknown;     // 1.11.19 (lot 6) : les nombres du registre
}

// ------------------------------------------------------------ les jetons ---
struct Tok {
    enum class K : std::uint8_t { End, Ident, Num, Str, Lit, Op } k{K::End};
    std::string text;
    double      num{0};
    bool        numKnown{false};
    T           litType{T::Unknown};    // un litteral type (T#5s, INT#3, BOOL#1)
    // 1.13.0 : sa place dans le texte lu [at, end) - la correction d'un clic remplace le texte exact d'un operande.
    std::size_t at{0}, end{0};
};

std::vector<Tok> lex(std::string_view s) {
    std::vector<Tok> out;
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '(' && i + 1 < n && s[i + 1] == '*') {
            const auto e = s.find("*)", i + 2);
            i = e == std::string_view::npos ? n : e + 2;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') ++i;
            continue;
        }
        if (c == '\'' || c == '"') {
            Tok t;
            t.k = Tok::K::Str;
            t.at = i;
            std::size_t j = i + 1;
            while (j < n && s[j] != c) {
                if (s[j] == '$' && j + 1 < n) { t.text += s[j + 1]; j += 2; continue; }
                t.text += s[j++];
            }
            i = j < n ? j + 1 : n;
            t.end = i;
            out.push_back(std::move(t));
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            bool based = false;
            while (j < n) {
                const char d = s[j];
                if (identChar(d) || d == '.') { ++j; continue; }
                if (d == '#') { based = true; ++j; continue; }
                if ((d == '+' || d == '-') && !based && j > i && (s[j - 1] == 'e' || s[j - 1] == 'E')) { ++j; continue; }
                break;
            }
            Tok t;
            t.k = Tok::K::Num;
            t.at = i;
            t.end = j;
            t.text = std::string(s.substr(i, j - i));
            std::string clean;
            for (char d : t.text) if (d != '_') clean += d;
            if (!based) {
                char* end = nullptr;
                const double v = std::strtod(clean.c_str(), &end);
                if (end && *end == '\0') { t.num = v; t.numKnown = true; }
            } else if (const auto h = clean.find('#'); h != std::string::npos) {
                const int base = std::atoi(clean.substr(0, h).c_str());
                if (base == 2 || base == 8 || base == 16) {
                    char* end = nullptr;
                    const std::string digits = clean.substr(h + 1);
                    const long long v = std::strtoll(digits.c_str(), &end, base);
                    if (!digits.empty() && end && *end == '\0') { t.num = static_cast<double>(v); t.numKnown = true; }
                }
            }
            out.push_back(std::move(t));
            i = j;
            continue;
        }
        if (identStart(c)) {
            std::size_t j = i;
            while (j < n && identChar(s[j])) ++j;
            std::string word(s.substr(i, j - i));
            if (j < n && s[j] == '#') {
                // T#5s, TIME#1h, INT#3, BOOL#1, D#2024-01-01, TOD#12:00:00
                const std::string w = up(word);
                const bool date = w == "D" || w == "DATE" || w == "DT" || w == "DATE_AND_TIME";
                std::size_t k = j + 1;
                while (k < n && (identChar(s[k]) || s[k] == '.' || s[k] == ':' || (date && s[k] == '-'))) ++k;
                Tok t;
                t.k = Tok::K::Lit;
                t.at = i;
                t.end = k;
                t.text = std::string(s.substr(i, k - i));
                t.litType = (w == "T" || w == "TIME" || w == "LTIME") ? T::Time
                          : w == "BOOL"                                ? T::Bool
                          : typeOfName(w) == T::Num                    ? T::Num
                          : w == "STRING"                              ? T::Text
                                                                       : T::Unknown;
                out.push_back(std::move(t));
                i = k;
                continue;
            }
            Tok t;
            t.k = Tok::K::Ident;
            t.at = i;
            t.end = j;
            t.text = std::move(word);
            out.push_back(std::move(t));
            i = j;
            continue;
        }
        Tok t;
        t.k = Tok::K::Op;
        static const char* const kTwo[] = {"<=", ">=", "<>", "**", ":="};
        for (const char* two : kTwo)
            if (s.substr(i, 2) == two) t.text = two;
        if (t.text.empty()) t.text = std::string(1, c);
        t.at = i;
        i += t.text.size();
        t.end = i;
        out.push_back(std::move(t));
    }
    Tok last;
    last.at = last.end = n;
    out.push_back(std::move(last));
    return out;
}

// ------------------------------------------------------------ les valeurs --
struct Val {
    T           type{T::Unknown};
    bool        numKnown{false};    // un nombre constant
    double      num{0};
    bool        strKnown{false};    // un texte constant
    std::string str;
    std::string show;               // ce qu'on montre dans un message
    std::vector<std::string> choices;   // les textes constants qu'elle peut valoir (un litteral, un SEL)
    // 1.12.17 : un tableau, une liste, une MAP, une structure lus entiers (TABLEAU, pas TABLEAU[0]) : ce que
    // c'est (types::Whole) et leur type ecrit ; vide : une valeur seule, ou rien de connu.
    std::string whole, entire, pick, wholeType;
    // 1.13.0 (lot J6) : l'unite physique de la valeur ("bar") - celle du creneau lu (variable IHM, retour),
    // propagee a travers les calculs ; vide : aucune (un nombre nu, qui adopte l'unite de l'autre operande).
    std::string unit;
    // 1.13.0 : lue telle quelle sur un chemin de variable IHM (Pression, Ligne.Vannes[2], entre parentheses au
    // besoin) - la seule forme dont le moteur lit l'unite (EN la convertit, l'affectation aussi) ; un calcul, un
    // appel, un signe : non. `unitFrom` : le chemin, tel qu'ecrit, qui lui donne son unite (PressionBasse dans
    // PressionBasse * 2) ; [t0, t1) : ses jetons (le texte exact, pour la correction d'un clic).
    bool        plain{false};
    std::string unitFrom;
    std::size_t t0{0}, t1{0};
};

// Les fonctions standard : nombre d'arguments (min, max ; -1 : sans limite).
struct Arity { const char* name; int min; int max; };
constexpr Arity kArity[] = {
    {"ABS", 1, 1},  {"SQRT", 1, 1}, {"LN", 1, 1},    {"LOG", 1, 1},   {"EXP", 1, 1},   {"SIN", 1, 1},     {"COS", 1, 1},
    {"TAN", 1, 1},  {"ASIN", 1, 1}, {"ACOS", 1, 1},  {"ATAN", 1, 1},  {"TRUNC", 1, 1}, {"ROUND", 1, 2},   {"NEG", 1, 1},
    {"LEN", 1, 1},  {"EXPT", 2, 2}, {"LEFT", 2, 2},  {"RIGHT", 2, 2}, {"FIND", 2, 2},  {"SHL", 2, 2},     {"SHR", 2, 2},
    {"ROL", 2, 2},  {"ROR", 2, 2},  {"LIMIT", 3, 3}, {"SEL", 3, 3},   {"MID", 3, 3},   {"INSERT", 3, 3},  {"DELETE", 3, 3},
    {"REPLACE", 4, 4}, {"MIN", 2, -1}, {"MAX", 2, -1}, {"CONCAT", 2, -1}, {"MUX", 2, -1},
};

std::string plural(int n, const char* one) { return std::to_string(n) + " " + one + (n > 1 ? "s" : ""); }

std::string numberText(double v) {
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) return std::to_string(static_cast<long long>(std::llround(v)));
    char buf[40];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

// Une couleur ecrite : #RRGGBB, #RRGGBBAA, 16#RRGGBB, RRGGBB, transparent.
bool colorText(std::string_view t) {
    const std::string s = trimmed(t);
    if (s.empty() || s == "transparent" || s == "aucun") return true;
    std::string_view h = s;
    if (h.front() == '#') h.remove_prefix(1);
    else if (h.size() > 3 && h.substr(0, 3) == "16#") h.remove_prefix(3);
    if (h.size() != 6 && h.size() != 8) return false;
    return std::all_of(h.begin(), h.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

std::size_t distance(std::string_view a, std::string_view b) {
    const std::string x = up(a.substr(0, 64)), y = up(b.substr(0, 64));
    std::vector<std::size_t> row(y.size() + 1);
    for (std::size_t j = 0; j <= y.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= x.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= y.size(); ++j) {
            const std::size_t keep = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (x[i - 1] == y[j - 1] ? 0u : 1u)});
            diag = keep;
        }
    }
    return row[y.size()];
}

std::string closestAmong(std::string_view name, const std::vector<std::string>& pool) {
    const std::string u = up(name);
    std::string best;
    std::size_t bestScore = static_cast<std::size_t>(-1);
    const std::size_t limit = std::max<std::size_t>(2, u.size() / 3);
    for (const auto& c : pool) {
        if (c.empty() || up(c) == u) continue;
        const std::string cu = up(c);
        std::size_t d = distance(u, cu);
        // L'un commence par l'autre (Vitesse -> Vitesse_Moteur) : tres proche.
        if (u.size() >= 3 && (cu.rfind(u, 0) == 0 || u.rfind(cu, 0) == 0 || cu.find(u) != std::string::npos)) d = std::min<std::size_t>(d, 1);
        if (d <= limit && d < bestScore) {
            bestScore = d;
            best = c;
        }
    }
    return best;
}

// 1.13.0 (lot J8) : LES ESPACES DE NOMS, LUS AVANT TOUT - Util . Moyenne (trois jetons) devient un seul nom complet
// quand Util est un espace declare qui a Moyenne (la regle du plus long chemin, celle du moteur et des scripts).
std::vector<Tok> withNamespaces(const Context& ctx, std::vector<Tok> toks) {
    if (!ctx.project || toks.empty() || !names::active(*ctx.project)) return toks;
    const names::Index ix(*ctx.project);
    std::vector<names::TokenView> views;
    views.reserve(toks.size());
    for (const auto& t : toks) views.push_back({t.text, t.k == Tok::K::Ident, t.k == Tok::K::Op && t.text == "."});
    const auto runs = names::qualifiedRuns(ix, views, nullptr);
    for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
        toks[it->first].text = it->text;
        if (it->count > 1 && it->first + it->count - 1 < toks.size()) toks[it->first].end = toks[it->first + it->count - 1].end;   // 1.13.0
        if (it->count > 1)
            toks.erase(toks.begin() + static_cast<std::ptrdiff_t>(it->first + 1), toks.begin() + static_cast<std::ptrdiff_t>(it->first + it->count));
    }
    return toks;
}

// 1.13.0 : LE VERDICT D'UNITE D'UN PASSAGE DE VALEUR - la source `src` (son unite `su`, lue telle quelle sur un
// chemin si `plain`, sinon tiree de `unitFrom`) vers une cible d'unite `tu` (`target` la nomme). Le moteur ne
// convertit d'office qu'une affectation := dont la source est un chemin (il lit son unite, comme EN) ; un calcul,
// un RETURN, un argument ne se convertissent pas : la compilation demande EN. Rien : pas d'unite des deux cotes,
// la meme, ou un symbole inconnu (Generer le dit).
std::optional<Problem> unitPassVerdict(const units::Catalog& cat, const std::string& tu, const std::string& su, const std::string& src,
                                       bool plain, const std::string& unitFrom, UnitPass pass, const std::string& target, bool strict) {
    if (tu.empty() || su.empty()) return std::nullopt;
    const units::Rel rel = cat.relation(su, tu);
    if (rel != units::Rel::SameFamily && rel != units::Rel::OtherFamily) return std::nullopt;
    Problem pb;
    pb.unit = true;
    if (rel == units::Rel::OtherFamily) {
        if (pass == UnitPass::Argument)
            pb.message = "tu passes " + cat.familyPhrase(su) + " (" + su + ") " + "\xC3\xA0" + " " + target + ", qui attend " + cat.familyPhrase(tu) + " ("
                       + tu + ") : deux familles diff\xC3\xA9rentes ne se convertissent pas";
        else
            pb.message = std::string(pass == UnitPass::Return ? "tu rends " : "tu affectes ") + cat.familyPhrase(su) + " " + "\xC3\xA0" + " "
                       + cat.familyPhrase(tu) + " (" + su + " vers " + tu + ") : deux familles diff\xC3\xA9rentes ne se convertissent pas";
        return pb;
    }
    if (pass == UnitPass::Assign && plain && !strict) {
        pb.message = su + " converti en " + tu + " d'office (une info)";
        pb.warning = true;                              // une info, pas une erreur : le projet demarre, le moteur convertit
        return pb;
    }
    // Ce que la correction emballe dans EN : la source si elle est un chemin, sinon le chemin qui lui donne son unite.
    const std::string what = plain ? src : unitFrom;
    const std::string canon = cat.canonical(tu).empty() ? tu : cat.canonical(tu);
    const std::string en = "EN(" + (what.empty() ? std::string("variable") : what) + ", '" + canon + "')";
    if (pass == UnitPass::Assign && strict)
        pb.message = tu + " exige une conversion explicite (option \xC2\xAB Unit\xC3\xA9s strictes \xC2\xBB) : \xC3\xA9" "cris " + en;
    else if (pass == UnitPass::Assign)
        pb.message = target + " est en " + tu + " et le calcul (" + src + ") en " + su
                   + " : seule une variable ou un membre lu tel quel se convertit d'office ; \xC3\xA9" "cris " + en + " dans le calcul";
    else if (pass == UnitPass::Return)
        pb.message = target + " rend des " + tu + " et cette valeur est en " + su + " : un RETURN ne convertit pas, \xC3\xA9" "cris " + en;
    else
        pb.message = target + " attend des " + tu + " et " + src + " est en " + su + " : un argument ne se convertit pas, passe " + en;
    if (!what.empty()) {
        pb.fixFrom = what;
        pb.fixTo = en;
    }
    return pb;
}

class Checker {
public:
    Checker(const Context& ctx, std::vector<Tok> toks, std::string_view source = {})
        : ctx_(ctx), toks_(std::move(toks)), src_(source), cat_(ctx.project ? units::Catalog(biblio::unitConfig(*ctx.project)) : units::Catalog()) {}

    Val run() { return orExpr(); }
    std::vector<Problem> problems;
    // 1.11.1 (REP-10) : les racines des reperes de l'expression (en majuscules) - un ancien
    // `$Vanne$` qui n'est pas une variable n'a pas de "veux-tu dire Vanne_Purge ?" : la
    // fausse piste ; c'est Dupliquer qui le remplit.
    std::set<std::string> markerRoots;
    // 1.11.20 : l'expression se lit et ses appels de fonctions de l'utilisateur sont juges par
    // le controle des appels (la regle du moteur) : leur nombre d'arguments n'est plus compte ici.
    bool callsChecked{false};

private:
    const Context&   ctx_;
    std::vector<Tok> toks_;
    std::size_t      at_{0};
    std::string_view src_;                                // 1.13.0 : le texte lu (les jetons y pointent)
    units::Catalog   cat_;                                // 1.13.0 (lot J6) : la bibliotheque des unites du projet

    // 1.13.0 : le texte exact d'une valeur, tel qu'ecrit (ses jetons) - la correction d'un clic le remplace ; a
    // defaut, ce qu'on en montre.
    std::string spanText(const Val& v) const {
        if (v.t1 <= v.t0 || v.t1 > toks_.size()) return v.show;
        const std::size_t from = toks_[v.t0].at, to = toks_[v.t1 - 1].end;
        if (to <= from || to > src_.size()) return v.show;
        return std::string(src_.substr(from, to - from));
    }
    // 1.13.0 : le chemin qui porte l'unite d'un resultat - celui de l'operande dont il garde l'unite.
    static std::string unitSource(const std::string& unit, const Val& a, const Val& b) {
        if (unit.empty()) return {};
        if (unit == a.unit) return a.unitFrom;
        if (unit == b.unit) return b.unitFrom;
        return {};
    }
    // 1.13.0 (lot J6) : LA REGLE DE + - ET DE LA COMPARAISON (meme unite exacte ; MIN, MAX, LIMIT aussi). Deux
    // unites de meme famille mais differentes, ou de familles differentes : une erreur, avec la correction d'un clic
    // (inserer EN sur l'operande de droite - ou, dans un calcul, sur le chemin qui lui donne son unite : EN ne
    // convertit qu'un chemin). Rend l'unite du resultat (celle qui reste) pour la suite.
    std::string unitAddCompare(const Val& a, const Val& b, const std::string& op, bool compare) {
        (void)op;
        const units::Rel rel = cat_.relation(a.unit, b.unit);
        const char* verb = compare ? "ne se comparent pas" : "ne s'additionnent pas";
        if (rel == units::Rel::SameFamily) {
            const std::string target = cat_.canonical(a.unit);
            const std::string what = b.plain ? spanText(b) : b.unitFrom;
            Problem pb{a.unit + " et " + b.unit + " " + verb + " : convertis l'un des deux, par exemple EN("
                           + (what.empty() ? std::string("variable") : what) + ", '" + target + "')", {}, {}};
            pb.unit = true;
            if (!what.empty()) {
                pb.fixFrom = what;
                pb.fixTo = "EN(" + what + ", '" + target + "')";
            }
            pushUnit(std::move(pb));
            return a.unit;
        }
        if (rel == units::Rel::OtherFamily) {
            Problem pb{(compare ? "tu compares " : "tu additionnes ") + cat_.familyPhrase(a.unit) + " et " + cat_.familyPhrase(b.unit)
                           + " (" + a.unit + " et " + b.unit + ") : deux familles diff\xC3\xA9rentes ne se m\xC3\xAAlent pas", {}, {}};
            pb.unit = true;
            pushUnit(std::move(pb));
            return {};
        }
        // None (une sans unite : le nu adopte l'autre), Same (ok), Unknown (Generer le dit) : l'unite qui reste.
        return a.unit.empty() ? b.unit : a.unit;
    }
    void pushUnit(Problem pb) {
        for (const auto& p : problems) if (p.message == pb.message) return;
        problems.push_back(std::move(pb));
    }
    // 1.13.0 : une faute d'unite (EN mal employe) - une erreur, un script compris.
    void unitProblem(std::string msg) {
        Problem pb{std::move(msg), {}, {}};
        pb.unit = true;
        pushUnit(std::move(pb));
    }

    std::vector<std::set<std::string>> lambdaNames_;      // 1.12.19 : les parametres des LAMBDA ouvertes (en majuscules)
    int lambdaBody_{0};                                   // 1.12.20 : dans le corps d'une LAMBDA (une expression ne modifie rien)
    // 1.12.20 : une LAMBDA d'une expression de propriete (pas d'un script) qui modifie quelque chose - refusee.
    void lambdaEffect(const std::string& what) {
        if (ctx_.script) return;
        problem("une expression de propri\xC3\xA9t\xC3\xA9 ne modifie rien : la LAMBDA " + what
                + " \xE2\x80\x94 \xC3\xA9" "cris-la dans une action ou un script");
    }
    // ... une fonction appelee dans son corps : ce qu'elle ecrit, ce qu'elle fait.
    void lambdaCall(const std::vector<const HmiFunction*>& fs, const std::string& name) {
        if (lambdaBody_ == 0 || ctx_.script || !ctx_.project) return;
        for (const auto* f : fs) {
            const auto e = functionEffects(*ctx_.project, *f, ctx_.view);
            if (!e.writes.empty()) {
                lambdaEffect("appelle " + name + ", qui \xC3\xA9" "crit " + e.writes.front());
                return;
            }
            if (!e.acts.empty()) {
                lambdaEffect("appelle " + name + ", qui appelle " + e.acts.front());
                return;
            }
        }
    }
    // 1.12.20 : un nom de fonction en texte la ou va un rappel (LIST_TRANSFORM(S, D, 'Carre')) - il se lit encore,
    // mais n'est ni verifie ni renomme : un avertissement, et la correction d'un clic ('Carre' -> Carre).
    void quotedCallbacks(const std::string& u, const std::vector<Val>& args) {
        const auto* f = natives::function(u);
        if (!f || !ctx_.project) return;
        for (std::size_t k = 0; k < args.size() && k < f->params.size(); ++k) {
            if (!args[k].strKnown || std::string(f->params[k].type).find("CALLBACK") == std::string::npos) continue;
            const std::string name = trimmed(args[k].str);
            bool fn = !ctx_.project->functionsNamed(name).empty();
            if (!fn && ctx_.view)
                for (const auto& vf : ctx_.view->functions) fn = fn || up(vf.name) == up(name);
            if (!fn) continue;
            Problem pb{"'" + name + "' : un nom de fonction en texte n'est ni v\xC3\xA9rifi\xC3\xA9 ni renomm\xC3\xA9 \xE2\x80\x94 \xC3\xA9" "cris "
                           + name + " (sans apostrophes)", {}, {}};
            pb.warning = true;
            pb.fixFrom = "'" + args[k].str + "'";
            pb.fixTo = name;
            bool said = false;
            for (const auto& p : problems) said = said || p.message == pb.message;
            if (!said) problems.push_back(std::move(pb));
        }
    }
    const Tok& peek() const { return toks_[std::min(at_, toks_.size() - 1)]; }
    const Tok& peekAt(std::size_t k) const { return toks_[std::min(at_ + k, toks_.size() - 1)]; }
    [[nodiscard]] bool inLambda(const std::string& name) const {
        const std::string u = up(name);
        for (const auto& scope : lambdaNames_)
            if (scope.count(u)) return true;
        return false;
    }
    // 1.12.19 : le type d'un rappel de ce nom - une variable IHM, un parametre de la vue (ou du symbole qui porte la
    // popup) de type CALLBACK ; vide : pas un rappel.
    [[nodiscard]] std::string callbackTypeOf(const std::string& name) const {
        // 1.12.20 : la forme ecrite d'un rappel, ou celle d'un rappel nomme du projet (T_Filtre).
        if (const Project* p = ctx_.project) {
            if (const Variable* var = p->variable(name))
                if (std::string f = types::callbackForm(p, var->type); !f.empty()) return f;
            if (ctx_.view) {
                if (const ViewParam* prm = ctx_.view->param(name))
                    if (std::string f = types::callbackForm(p, prm->type); !f.empty()) return f;
                if (const View* owner = popupOwner(*p, *ctx_.view))
                    if (const ViewParam* prm = owner->param(name))
                        if (std::string f = types::callbackForm(p, prm->type); !f.empty()) return f;
            }
        }
        return {};
    }
    // 1.12.19 : LAMBDA (x : REAL, y : INT) : BOOL => x > Seuil - ses parametres sont connus dans son corps ; elle vaut
    // un rappel (rien a montrer). Sa forme se verifie a l'appel (le moteur). Dans une fx, ses parametres se separent
    // par des virgules (le ; n'y est pas permis).
    Val lambda() {
        Val v;
        v.show = "LAMBDA";
        std::set<std::string> names;
        if (isOp("(")) {
            next();
            int depth = 0;
            bool expectName = true;
            while (peek().k != Tok::K::End) {
                if (isOp("(")) { ++depth; next(); continue; }
                if (isOp(")")) {
                    next();
                    if (depth == 0) break;
                    --depth;
                    continue;
                }
                if (depth == 0 && (isOp(",") || isOp(";"))) { expectName = true; next(); continue; }
                if (depth == 0 && isOp(":")) { expectName = false; next(); continue; }
                if (expectName && peek().k == Tok::K::Ident) {
                    names.insert(up(peek().text));
                    expectName = false;
                }
                next();
            }
        }
        const auto arrow = [&] { return isOp("=") && peekAt(1).k == Tok::K::Op && peekAt(1).text == ">"; };
        // 1.12.20 : la forme en bloc - LAMBDA (...) : T VAR ... END_VAR instructions END_LAMBDA.
        const auto blockStart = [&] {
            return peek().k == Tok::K::Ident
                && (isWord("VAR") || isWord("VAR_TEMP") || isWord("RETURN") || isWord("IF") || isWord("FOR") || isWord("WHILE")
                    || isWord("REPEAT") || isWord("CASE") || isWord("END_LAMBDA") || (peekAt(1).k == Tok::K::Op && peekAt(1).text == ":="));
        };
        if (isOp(":")) {                                     // son type de retour, jusqu'a => (ou le debut du bloc)
            next();
            while (peek().k != Tok::K::End && !arrow()) {
                if (peek().k == Tok::K::Ident && blockStart() && !isWord("OF") && !isWord("ARRAY")) break;
                next();
            }
        }
        if (arrow()) {
            next();
            next();
            lambdaNames_.push_back(std::move(names));
            ++lambdaBody_;
            (void)orExpr();
            --lambdaBody_;
            lambdaNames_.pop_back();
            return v;
        }
        // Le bloc : ses locales, ses ecritures, ses appels - jusqu'a son END_LAMBDA (les LAMBDA imbriquees comptees).
        std::set<std::string> locals = names;
        int depth = 1;
        bool inVar = false;
        Tok previous;
        while (peek().k != Tok::K::End) {
            const Tok t = next();
            const std::string w = t.k == Tok::K::Ident ? up(t.text) : std::string{};
            if (w == "LAMBDA") ++depth;
            else if (w == "END_LAMBDA" && --depth == 0) break;
            else if (w == "VAR" || w == "VAR_TEMP") inVar = true;
            else if (w == "END_VAR") inVar = false;
            else if (t.k == Tok::K::Ident && inVar && previous.k == Tok::K::Op && (previous.text == ";" || previous.text == ","))
                locals.insert(w);
            else if (t.k == Tok::K::Ident && inVar && (previous.k == Tok::K::Ident && (up(previous.text) == "VAR" || up(previous.text) == "VAR_TEMP")))
                locals.insert(w);
            else if (t.k == Tok::K::Ident && !inVar && depth == 1 && isOp(":=") && !locals.count(w)) {
                if (ctx_.project && ctx_.project->variable(t.text)) lambdaEffect("\xC3\xA9" "crit " + t.text);
            } else if (t.k == Tok::K::Ident && !inVar && isOp("(") && !locals.count(w)) {
                if (hmiFunctionActs(w)) lambdaEffect("appelle " + w);
                else if (ctx_.project) {
                    ++lambdaBody_;
                    lambdaCall(ctx_.project->functionsNamed(t.text), t.text);
                    --lambdaBody_;
                }
            }
            previous = t;
        }
        return v;
    }
    const Tok& next() {
        const Tok& t = peek();
        if (at_ < toks_.size() - 1) ++at_;
        return t;
    }
    bool isOp(const char* op) const { return peek().k == Tok::K::Op && peek().text == op; }
    bool isWord(const char* w) const { return peek().k == Tok::K::Ident && up(peek().text) == w; }

    void problem(std::string msg, bool whole = false) {
        for (const auto& p : problems) if (p.message == msg) return;
        Problem pb{std::move(msg), {}, {}};
        pb.whole = whole;                                   // 1.12.17 : un tableau entier (bloquant)
        problems.push_back(std::move(pb));
    }

    static bool textish(const Val& v) { return v.type == T::Text; }

    // 1.12.17 : le type d'un nom lu - une valeur seule (T), ou un conteneur, une structure entiers.
    void typed(Val& v, const std::string& type) {
        v.type = typeOfName(type);
        if (v.type != T::Unknown) return;
        const auto w = types::wholeOf(ctx_.project, type, v.show);
        v.whole = w.what;
        v.entire = w.entire;
        v.pick = w.pick;
        v.wholeType = trimmed(type);
    }
    // ... une structure de l'automate (un DDT) : ses membres seuls se lisent.
    void typedPlc(Val& v, const std::string& type) {
        typed(v, type);
        if (v.whole.empty() && v.type == T::Unknown && ctx_.plc.isStruct && ctx_.plc.isStruct(type)) {
            v.whole = "une structure";
            v.entire = "une structure enti\xC3\xA8re";
            v.pick = "un membre : " + v.show + ".<membre>";
            v.wholeType = trimmed(type);
        }
    }
    // Un operateur du projet pour ce geste et ce type (T_VEC + T_VEC, T_VEC * REAL, T_VEC < T_VEC ; "TO" : sa
    // conversion, TO_REAL(V0)) : ce type entier se calcule, se compare, se convertit (le moteur l'appelle).
    bool ownOperator(const std::string& type, std::string_view op) const {
        if (!ctx_.project || type.empty()) return false;
        for (const auto& x : allOperators(*ctx_.project))
            if (x.op && x.op->op == op && (sameTypeName(x.op->left, type) || (op != "TO" && sameTypeName(x.op->right, type)))) return true;
        return false;
    }
    // Un conteneur, une structure entiers ne se calculent, ne se comparent, ni ne se combinent - sauf par un
    // operateur du projet (`op`).
    void wholeIn(const Val& a, const std::string& how, std::string_view op = {}) {
        if (a.whole.empty() || (!op.empty() && ownOperator(a.wholeType, op))) return;
        problem(a.show + " est " + a.entire + " (" + a.wholeType + ") : " + how + " - choisis " + a.pick, true);
    }

    Val combine(const Val& a, const Val& b, const std::string& op) {
        wholeIn(a, "il ne se combine pas avec " + op, op);   // 1.12.17
        wholeIn(b, "il ne se combine pas avec " + op, op);
        if (textish(a) || textish(b)) {
            const Val& t = textish(a) ? a : b;
            problem("un texte ne se combine pas avec " + op + " (" + t.show + ") : compare-le d'abord (" + t.show
                    + " = 'Marche') ou utilise un bool\xC3\xA9" "en");
        }
        Val r;
        r.type = (a.type == T::Bool && b.type == T::Bool) ? T::Bool
               : (a.type == T::Num || b.type == T::Num)   ? T::Num
                                                          : T::Unknown;
        r.show = a.show + " " + op + " " + b.show;
        r.t0 = a.t0;
        r.t1 = b.t1;
        return r;
    }

    Val orExpr() {
        Val a = xorExpr();
        while (isWord("OR")) { next(); a = combine(a, xorExpr(), "OR"); }
        return a;
    }
    Val xorExpr() {
        Val a = andExpr();
        while (isWord("XOR")) { next(); a = combine(a, andExpr(), "XOR"); }
        return a;
    }
    Val andExpr() {
        Val a = cmpExpr();
        while (isWord("AND") || isOp("&")) { next(); a = combine(a, cmpExpr(), "AND"); }
        return a;
    }
    Val cmpExpr() {
        Val a = addExpr();
        while (isOp("=") || isOp("<>") || isOp("<") || isOp(">") || isOp("<=") || isOp(">=")) {
            const std::string op = next().text;
            const Val b = addExpr();
            // 1.12.17 : un tableau, une liste, une structure entiers ne se comparent qu'a un autre (=, <>).
            if (a.whole.empty() != b.whole.empty() || (!a.whole.empty() && op != "=" && op != "<>")) {
                wholeIn(a, "il ne se compare pas (" + op + ") \xC3\xA0 une valeur", op);
                wholeIn(b, "il ne se compare pas (" + op + ") \xC3\xA0 une valeur", op);
            }
            // Le simulateur compare tout, mais un texte et un nombre ne sont jamais
            // egaux : la condition serait toujours fausse (ou toujours vraie).
            const bool clash = (a.type == T::Text && (b.type == T::Num || b.type == T::Bool))
                            || (b.type == T::Text && (a.type == T::Num || a.type == T::Bool));
            if (clash) {
                const std::string hint = (a.type == T::Num || b.type == T::Num)
                    ? " : \xC3\xA9" "cris le nombre sans apostrophes, ou convertis le texte (STRING_TO_REAL)"
                    : " : \xC3\xA9" "cris TRUE ou FALSE sans apostrophes";
                problem("tu compares " + typeWord(a.type) + " (" + a.show + ") \xC3\xA0 " + typeWord(b.type) + " (" + b.show
                        + "), ils ne se comparent pas" + hint);
            }
            // 1.13.0 (lot J6) : comparer deux nombres exige la meme unite exacte (meme regle que + et -).
            if (a.type == T::Num && b.type == T::Num && (!a.unit.empty() || !b.unit.empty()))
                unitAddCompare(a, b, op, true);
            Val r;
            r.type = T::Bool;
            r.show = a.show + " " + op + " " + b.show;
            r.t0 = a.t0;
            r.t1 = b.t1;
            a = r;
        }
        return a;
    }
    Val arith(const Val& a, const Val& b, const std::string& op) {
        Val r;
        r.show = a.show + " " + op + " " + b.show;
        r.t0 = a.t0;
        r.t1 = b.t1;
        wholeIn(a, "il ne se calcule pas (" + op + ")", op);   // 1.12.17
        wholeIn(b, "il ne se calcule pas (" + op + ")", op);
        // '+' met des textes bout a bout (le simulateur le fait) ; le reste non.
        if (op == "+" && (textish(a) || textish(b))) {
            r.type = T::Text;
            return r;
        }
        if (textish(a) || textish(b)) {
            const Val& t = textish(a) ? a : b;
            problem("un texte ne se calcule pas (" + t.show + " " + op + ") : pour calculer, convertis-le (STRING_TO_REAL)");
        }
        if (a.type == T::Time && (b.type == T::Time || b.type == T::Num)) r.type = T::Time;
        else if (a.type == T::Num && b.type == T::Num) r.type = T::Num;
        else if (a.type == T::Num && b.type == T::Time && op == "*") r.type = T::Time;
        if (a.numKnown && b.numKnown) {
            r.numKnown = true;
            if (op == "+") r.num = a.num + b.num;
            else if (op == "-") r.num = a.num - b.num;
            else if (op == "*") r.num = a.num * b.num;
            else if (op == "/" && b.num != 0) r.num = a.num / b.num;
            else if (op == "MOD" && b.num != 0) r.num = std::fmod(a.num, b.num);
            else if (op == "**") r.num = std::pow(a.num, b.num);
            else r.numKnown = false;
        }
        // 1.13.0 (lot J6) : les deux regles d'unite. + et - : meme unite exacte (sinon EN). Produit/division :
        // x par un nombre nu garde l'unite ; a / b de la meme unite donne un rapport SANS unite ; un autre
        // produit/division perd l'unite (une info, pas d'algebre d'unites composees type « bar.m »).
        if (r.type == T::Num && (!a.unit.empty() || !b.unit.empty())) {
            if (op == "+" || op == "-") {
                r.unit = unitAddCompare(a, b, op, false);
            } else if (op == "*") {
                if (a.unit.empty() || b.unit.empty()) r.unit = a.unit.empty() ? b.unit : a.unit;   // x par un nombre nu
                else unitLost(a, b, op);                                                             // bar x m3/h : perdue
            } else if (op == "/") {
                if (!a.unit.empty() && !b.unit.empty()) {
                    const units::Rel rel = cat_.relation(a.unit, b.unit);
                    if (rel != units::Rel::Same && rel != units::Rel::SameFamily) unitLost(a, b, op);   // rapport de deux memes : SANS unite
                } else {
                    r.unit = a.unit;                                                                 // unite / nombre : garde l'unite
                }
            } else if (op == "MOD") {
                r.unit = a.unit;
            }
            r.unitFrom = unitSource(r.unit, a, b);     // 1.13.0 : le chemin qui porte cette unite
        }
        return r;
    }
    // 1.13.0 (lot J6) : un produit/division sans regle simple perd l'unite - une info (warning), pas une erreur.
    void unitLost(const Val& a, const Val& b, const std::string& op) {
        Problem pb{a.unit + " " + op + " " + b.unit + " : l'unit\xC3\xA9 est perdue (" + a.show + " " + op + " " + b.show
                       + " donne un nombre nu)", {}, {}};
        pb.warning = true;
        pb.unit = true;
        pushUnit(std::move(pb));
    }
    Val addExpr() {
        Val a = mulExpr();
        while (isOp("+") || isOp("-")) {
            const std::string op = next().text;
            a = arith(a, mulExpr(), op);
        }
        return a;
    }
    Val mulExpr() {
        Val a = powExpr();
        while (isOp("*") || isOp("/") || isWord("MOD")) {
            const std::string op = up(next().text);
            const Val b = powExpr();
            if ((op == "/" || op == "MOD") && b.numKnown && b.num == 0)
                problem("division par z\xC3\xA9ro (" + a.show + " " + op + " " + b.show
                        + ") : la valeur sera fausse \xC3\xA0 chaque cycle ; corrige le diviseur");
            a = arith(a, b, op);
        }
        return a;
    }
    Val powExpr() {
        Val a = unary();
        while (isOp("**")) { next(); a = arith(a, unary(), "**"); }
        return a;
    }
    Val unary() {
        const std::size_t start = at_;                  // 1.13.0 : le signe fait partie du texte de la valeur
        if (isWord("NOT")) {
            next();
            Val a = unary();
            a.plain = false;
            a.t0 = start;
            if (textish(a)) problem("NOT ne s'applique pas \xC3\xA0 un texte (" + a.show + ") : compare-le d'abord (" + a.show + " = 'Marche')");
            wholeIn(a, "NOT ne s'y applique pas");             // 1.12.17
            a.whole.clear();
            a.show = "NOT " + a.show;
            a.numKnown = false;
            if (a.type != T::Bool && a.type != T::Num) a.type = T::Unknown;
            return a;
        }
        if (isOp("-") || isOp("+")) {
            const std::string op = next().text;
            Val a = unary();
            a.plain = false;                            // -PressionBasse : un calcul, plus un chemin
            a.t0 = start;
            if (textish(a)) problem("un texte n'a pas de signe (" + op + a.show + ") : convertis-le (STRING_TO_REAL)");
            wholeIn(a, "il n'a pas de signe", op);             // 1.12.17
            a.whole.clear();
            if (a.numKnown && op == "-") a.num = -a.num;
            a.show = op + a.show;
            return a;
        }
        return primary();
    }

    // 1.13.0 : une valeur primaire et sa place (ses jetons) ; un chemin qui porte une unite dit lequel il est.
    Val primary() {
        const std::size_t start = at_;
        Val v = primaryValue();
        v.t0 = start;
        v.t1 = std::max(at_, start);
        if (v.plain && !v.unit.empty() && v.unitFrom.empty()) v.unitFrom = spanText(v);
        return v;
    }
    Val primaryValue() {
        const Tok t = peek();
        Val v;
        switch (t.k) {
            case Tok::K::Num:
                next();
                v.type = T::Num;
                v.numKnown = t.numKnown;
                v.num = t.num;
                v.show = t.text;
                return v;
            case Tok::K::Str:
                next();
                v.type = T::Text;
                v.strKnown = true;
                v.str = t.text;
                v.show = "'" + t.text + "'";
                v.choices.push_back(t.text);
                return v;
            case Tok::K::Lit:
                next();
                v.type = t.litType;
                v.show = t.text;
                return v;
            case Tok::K::Op:
                if (t.text == "(") {
                    next();
                    v = orExpr();
                    if (isOp(")")) next();
                    v.show = "(" + v.show + ")";
                    return v;
                }
                return v;     // la syntaxe est l'affaire de sim::parse
            case Tok::K::Ident: {
                next();
                const std::string u = up(t.text);
                if (u == "TRUE" || u == "FALSE") {
                    v.type = T::Bool;
                    v.show = t.text;
                    return v;
                }
                if (u == "LAMBDA") return lambda();                  // 1.12.19
                if (isOp("(")) return call(t.text);
                // 1.11.10 : Vanne_3.Etat(), Vue_Vannes.Vanne_3.Etat() - la fonction d'une instance.
                if (isOp(".")) {
                    std::vector<std::string> segs{t.text};
                    std::size_t k = at_;
                    while (k + 1 < toks_.size() && toks_[k].k == Tok::K::Op && toks_[k].text == "." && toks_[k + 1].k == Tok::K::Ident) {
                        segs.push_back(toks_[k + 1].text);
                        k += 2;
                    }
                    if (segs.size() >= 2 && k < toks_.size() && toks_[k].k == Tok::K::Op && toks_[k].text == "(")
                        if (const HmiFunction* fn = instanceFunction(segs)) {
                            at_ = k;
                            std::string dotted;
                            for (const auto& sg : segs) dotted += (dotted.empty() ? "" : ".") + sg;
                            // 1.12.16 : Vue_Four.CouleurNiveau(...) d'ailleurs - une fonction privee de la vue.
                            if (segs.size() == 2 && fn->visibility == Visibility::Private && ctx_.project)
                                if (const View* owner = ctx_.project->viewByName(segs[0]);
                                    owner && !isSymbolView(*owner) && (!ctx_.view || ctx_.view->id != owner->id))
                                    problem(owner->name + "." + fn->name + " est priv\xC3\xA9" "e : seule " + owner->name + " l'appelle (rends-la publique)");
                            pendingFn_ = fn;
                            return call(dotted);
                        }
                }
                return path(t.text);
            }
            case Tok::K::End:
                break;
        }
        return v;
    }

    std::vector<std::string> pool() const {
        std::vector<std::string> out;
        if (ctx_.project) {
            for (const auto& var : ctx_.project->programs.variables) out.push_back(var.name);
            for (const auto& f : ctx_.project->programs.functions) out.push_back(f.name);
        }
        if (ctx_.view)
            for (const auto& prm : ctx_.view->params) out.push_back(prm.name);
        for (const auto& c : ctx_.candidates) out.push_back(c);
        return out;
    }

    // Un morceau de chemin : un membre, ou des indices (constants ou non).
    struct Seg {
        bool                                    index{false};
        std::string                             member;
        std::vector<std::pair<bool, long long>> idx;    // (constant, valeur)
    };

    // Les membres et les indices constants d'un chemin de l'automate ; le type
    // du bout ("" : il ne se lit pas).
    std::string plcPath(const std::string& root, const std::vector<Seg>& segs) {
        std::string type = ctx_.plc.rootType(root);
        std::string where = root;
        for (const auto& sg : segs) {
            if (type.empty()) return {};
            if (sg.index) {
                const auto shape = project::members::parseArray(type);
                if (!shape.valid()) return {};
                std::string shown;
                for (std::size_t d = 0; d < sg.idx.size(); ++d) {
                    shown += (d ? "," : "") + (sg.idx[d].first ? std::to_string(sg.idx[d].second) : std::string("i"));
                    if (sg.idx.size() != shape.dims.size() || !sg.idx[d].first) continue;
                    const auto [lo, hi] = shape.dims[d];
                    if (sg.idx[d].second < lo || sg.idx[d].second > hi) {
                        problem("indice " + std::to_string(sg.idx[d].second) + " hors des bornes de " + where + " (" + std::to_string(lo) + ".."
                                + std::to_string(hi) + ", un tableau de l'automate) : corrige l'indice");
                        return {};
                    }
                }
                type = shape.element;
                where += "[" + shown + "]";
                continue;
            }
            if (!sg.member.empty() && std::isdigit(static_cast<unsigned char>(sg.member[0]))) return {};   // un bit : Mot.3
            const std::string mt = ctx_.plc.memberType ? ctx_.plc.memberType(type, sg.member) : std::string{};
            if (mt.empty()) {
                const bool elementary = typeOfName(type) != T::Unknown;
                if (elementary)
                    problem(where + "." + sg.member + " : " + where + " est un " + type + " de l'automate, il n'a pas de membre ; corrige le chemin");
                else if (ctx_.plc.isStruct && ctx_.plc.isStruct(type))
                    problem(where + "." + sg.member + " : le type " + type + " de l'automate n'a pas de membre " + sg.member + " ; corrige le chemin");
                return {};
            }
            type = mt;
            where += "." + sg.member;
        }
        return type;
    }

    Val path(const std::string& root) {
        std::string text = root;
        std::vector<Seg> segs;
        for (;;) {
            if (isOp(".")) {
                next();
                if (peek().k == Tok::K::Ident || peek().k == Tok::K::Num) {
                    Seg sg;
                    sg.member = next().text;
                    text += "." + sg.member;
                    segs.push_back(std::move(sg));
                } else {
                    break;
                }
            } else if (isOp("[")) {
                next();
                text += "[";
                Seg sg;
                sg.index = true;
                bool first = true;
                for (;;) {
                    const Val idx = orExpr();
                    if (!first) text += ",";
                    first = false;
                    text += idx.numKnown ? numberText(idx.num) : std::string("0");
                    const bool whole = idx.numKnown && std::fabs(idx.num - std::round(idx.num)) < 1e-9 && std::fabs(idx.num) < 1e15;
                    sg.idx.emplace_back(whole, whole ? static_cast<long long>(std::llround(idx.num)) : 0LL);
                    if (isOp(",")) { next(); continue; }
                    break;
                }
                if (isOp("]")) next();
                text += "]";
                segs.push_back(std::move(sg));
            } else {
                break;
            }
        }
        Val v;
        v.show = text;
        const Project* p = ctx_.project;
        if (inLambda(root)) return v;                         // 1.12.19 : un parametre d'une LAMBDA
        // 1.12.19 : une fonction passee en rappel (LIST_ANY(Mesures, Tres_Grand)) - son nom seul est une valeur.
        if (segs.empty() && p && !p->variable(root)) {
            bool fn = !p->functionsNamed(root).empty();
            if (!fn && ctx_.view)
                for (const auto& f : ctx_.view->functions) fn = fn || up(f.name) == up(root);
            if (fn) return v;
        }
        // 1.13.0 : un nom que le code declare lui-meme (une locale, un parametre) cache la variable IHM du meme nom.
        const bool hidden = ctx_.hidden && ctx_.hidden(root);
        if (const Variable* var = p && !hidden ? p->variable(root) : nullptr) {
            if (text == root) { typed(v, var->type); v.unit = var->unit; }       // 1.13.0 (J6) : l'unite de la variable
            else if (const auto ty = types::typeOfPath(*p, text); !ty.empty()) {
                typed(v, ty);
                // 1.13.0 (J6) : l'unite du membre lu - la meme lecture que le moteur (units::unitOfPath) : un tableau
                // de premier niveau garde l'unite de la variable, un membre herite (EXTENDS) la sienne.
                v.unit = units::unitOfPath(*p, text);
            }
            v.plain = true;                                                      // 1.13.0 : lue telle quelle
            return v;
        }
        if (hidden) return v;                                                    // une locale : rien de connu ici
        // 1.9 : un parametre type de la vue - Moteur.Vitesse est REAL d'apres T_Moteur.
        // 1.11.13 : ou du symbole qui porte la popup (Vanne.POSITION dans Pop_Vanne de S_Vanne).
        if (p && ctx_.view) {
            const View* where = ctx_.view;
            const ViewParam* prm = ctx_.view->param(root);
            if (!prm)
                if (const View* owner = popupOwner(*p, *ctx_.view)) {
                    prm = owner->param(root);
                    where = owner;
                }
            if (prm && !prm->type.empty()) {
                if (const auto ty = params::expressionType(*p, where, text); !ty.empty()) typed(v, ty);
                return v;
            }
        }
        // 1.12.16 : une constante de la vue (Seuil_Haut), ou d'une vue nommee (Vue_Four.Seuil_Haut) - publique
        // d'ailleurs ; une fonction de vue nommee sans parentheses : dite par son appel.
        if (p) {
            const Declaration* c = ctx_.view ? viewmembers::constant(*ctx_.view, root) : nullptr;
            std::string_view rest = std::string_view(text).substr(std::min(text.size(), root.size()));
            if (!c && !segs.empty() && !segs.front().index && pub::viewNamed(*p, root)) {
                viewmembers::Member m;
                std::string why;
                if (viewmembers::resolve(*p, root + "." + segs.front().member, m, &why, ctx_.view)) {
                    if (!m.constant) return v;
                    c = m.constant;
                    rest = std::string_view(text).substr(std::min(text.size(), root.size() + 1 + segs.front().member.size()));
                } else if (!why.empty()) {
                    Problem pb;
                    pb.message = why;
                    for (const auto& q : problems) if (q.message == pb.message) return v;
                    problems.push_back(std::move(pb));
                    return v;
                }
            }
            if (c) {
                if (const std::string t = viewmembers::typeAlong(*p, c->type, rest); !t.empty()) typed(v, t);
                return v;
            }
        }
        // 1.11.1 (API-M) : API.<globale>, API.<Unite>.<variable>, jusqu'au bout des
        // membres (une variable IHM, une vue nommee API sont vues avant ; un
        // parametre de la vue aussi). Sans le modele : rien a dire.
        if (core::hasApi() && up(root) == "API" && !segs.empty() && !segs.front().index && !(p && pub::viewNamed(*p, root))) {   // 1.12.0 : pas dans IHM Studio
            if (!ctx_.plc.api) return v;
            std::string apiText = root;
            for (const auto& sg : segs) {
                if (!sg.index) {
                    apiText += "." + sg.member;
                    continue;
                }
                apiText += "[";
                for (std::size_t d = 0; d < sg.idx.size(); ++d)
                    apiText += (d ? "," : "") + (sg.idx[d].first ? std::to_string(sg.idx[d].second) : std::string("i"));
                apiText += "]";
            }
            const auto r = ctx_.plc.api->resolve(apiText);
            if (!r.ok) {
                Problem pb;
                pb.unknownName = r.missing;
                pb.suggestion = r.suggestion;
                pb.message = r.error;
                for (const auto& q : problems) if (q.message == pb.message) return v;
                problems.push_back(std::move(pb));
                return v;
            }
            typedPlc(v, r.type);
            return v;
        }
        // 1.11.1 (remarque d'API-V) : API seul, API[...], ou API. en cours de frappe,
        // n'est pas un nom inconnu (« API n'existe pas ») : le chemin est incomplet.
        if (core::hasApi() && up(root) == "API" && !(p && pub::viewNamed(*p, root))) {
            if (!ctx_.plc.api) return v;
            Problem pb;
            pb.message = "API : chemin incomplet \xE2\x80\x94 \xC3\xA9" "cris API.<globale> ou API.<Unit\xC3\xA9>.<variable>";
            for (const auto& q : problems) if (q.message == pb.message) return v;
            problems.push_back(std::move(pb));
            return v;
        }
        // 1.12.7 : Reglages.Seuil - la constante publique d'un script general (une variable IHM passe avant).
        if (p && p->generalScript(root) && !segs.empty() && !segs.front().index) {
            std::string why;
            if (const Declaration* q = calc::qualified(*p, root + "." + segs.front().member, nullptr, &why)) {
                if (segs.size() == 1) typed(v, q->type);
                return v;
            }
            Problem pb;
            pb.message = why;
            for (const auto& q : problems) if (q.message == pb.message) return v;
            problems.push_back(std::move(pb));
            return v;
        }
        // 1.13.0 (lot J8) : Gaz.Inconnu - un espace de noms qui n'a pas ce nom (avec le plus proche de l'espace).
        if (p && !segs.empty() && !segs.front().index && p->isNamespace(root)) {
            const names::Index ix(*p);
            std::vector<std::string> pool = ix.itemsOf(root);
            for (const auto& sub : ix.childSpaces(root)) pool.push_back(sub);
            Problem pb;
            pb.unknownName = root + "." + segs.front().member;
            const std::string near = closestAmong(segs.front().member, pool);
            if (!near.empty()) {
                pb.suggestion = ix.spelled(root) + "." + near;
                pb.fixFrom = root + "." + segs.front().member;
                pb.fixTo = pb.suggestion;
            }
            pb.message = ix.spelled(root) + " est un espace de noms : il n'a pas de " + segs.front().member
                       + (near.empty() ? std::string(" (ni type, ni fonction, ni script)") : " : veux-tu dire " + pb.suggestion + " ?");
            for (const auto& q : problems) if (q.message == pb.message) return v;
            problems.push_back(std::move(pb));
            return v;
        }
        if (ctx_.known && !ctx_.known(root)) {
            Problem pb;
            pb.unknownName = root;
            const bool marker = markerRoots.count(up(root)) != 0;
            if (!marker) pb.suggestion = closestAmong(root, pool());
            pb.message = root + " n'existe pas";
            if (!pb.suggestion.empty())
                pb.message += " : veux-tu dire " + pb.suggestion + " ?";
            else if (marker)
                pb.message += core::hasApi() ? " (ni variable IHM, ni variable syst\xC3\xA8me, ni variable de l'automate)"
                                             : " (ni variable IHM, ni variable syst\xC3\xA8me)";   // 1.12.0
            else
                pb.message += std::string(core::hasApi() ? " (ni variable IHM, ni variable syst\xC3\xA8me, ni variable de l'automate)"
                                                         : " (ni variable IHM, ni variable syst\xC3\xA8me)")
                              + " : corrige le nom ou d\xC3\xA9" "clare la variable";
            for (const auto& q : problems) if (q.message == pb.message) return v;
            problems.push_back(std::move(pb));
            return v;
        }
        // Tranche 2 : un chemin de l'automate (pas un parametre de la vue, ni du symbole de la popup).
        const bool ownerParam = ctx_.view && ctx_.project && popupOwner(*ctx_.project, *ctx_.view)
                                && popupOwner(*ctx_.project, *ctx_.view)->param(root);
        if (ctx_.plc.rootType && !(ctx_.view && ctx_.view->param(root)) && !ownerParam) {
            const std::string end = plcPath(root, segs);
            if (!end.empty()) typedPlc(v, end);
        }
        return v;
    }

    // ---- 1.11.10 : les fonctions des symboles ----
    // Le symbole dont l'expression est controlee : le symbole lui-meme, ou celui qui porte la popup.
    const View* symbolHere() const {
        if (!ctx_.view || !ctx_.project) return nullptr;
        if (isSymbolView(*ctx_.view)) return ctx_.view;
        return popupOwner(*ctx_.project, *ctx_.view);
    }
    // ["Vanne_3", "Etat"] (une instance de la vue ou du symbole) ou ["Vue", "Vanne_3", "Etat"] : la fonction.
    const HmiFunction* instanceFunction(const std::vector<std::string>& segs) const {
        const Project* p = ctx_.project;
        if (!p || segs.size() < 2) return nullptr;
        const View* here = symbolHere() ? symbolHere() : ctx_.view;
        if (here) {
            const View* sym = nullptr;
            for (const auto& o : here->objects)
                if (o.kind == Kind::SymbolInstance && up(o.name) == up(segs[0])) sym = symbolOf(*p, o);
            for (std::size_t k = 1; sym && k < segs.size(); ++k) {
                if (k + 1 == segs.size()) return symbolFunction(*sym, segs[k]);
                const View* inner = nullptr;
                for (const auto& o : sym->objects)
                    if (o.kind == Kind::SymbolInstance && up(o.name) == up(segs[k])) inner = symbolOf(*p, o);
                sym = inner;
            }
        }
        std::string dotted;
        for (const auto& sg : segs) dotted += (dotted.empty() ? "" : ".") + sg;
        if (BoundFunction b; boundSymbolFunction(*p, dotted, b))
            if (const View* sv = p->viewByName(b.symbol)) return symbolFunction(*sv, b.function.name);
        return nullptr;
    }
    const HmiFunction* pendingFn_{nullptr};

    // 1.11.20 : les surcharges d'une fonction de symbole (les fonctions de meme nom de son symbole).
    std::vector<const HmiFunction*> siblingsOf(const HmiFunction* f) const {
        if (!f) return {};
        if (ctx_.project)
            for (const auto& v : ctx_.project->views)
                for (const auto& g : v.functions)
                    if (&g == f) return symbolFunctions(v, f->name);
        return {f};
    }
    // 1.11.20 : un appel d'une fonction de l'utilisateur (ses surcharges) : le nombre d'arguments
    // (E/S et sorties comptees) - sauf si le controle des appels l'a juge - et le type rendu (celui
    // de la surcharge qui prend ce nombre ; plusieurs : leur type commun).
    // 1.13.0 : une fonction generique rend son T DEDUIT a l'appel - le type coarse de l'argument qui porte le generique
    //  du retour (Max(a : T; b : T) : T, appelee sur des REAL -> Num). Un retour compose (LIST OF T) reste Unknown.
    T deducedGenericReturn(const HmiFunction& f, const std::vector<Val>& args) {
        if (f.generics.empty()) return T::Unknown;
        const std::string ret = up(trimmed(f.returnType));
        const GenericParam* g = nullptr;
        for (const auto& gp : f.generics) if (ret == up(gp.name)) g = &gp;
        if (!g) return T::Unknown;
        const auto parts = splitDeclarations(decl::codeOf(f), true);
        T best = T::Unknown;
        std::size_t k = 0;
        for (const auto* in : parts.parameters()) {
            if (k < args.size() && up(trimmed(in->type)) == up(g->name) && args[k].type != T::Unknown)
                best = best == T::Unknown ? args[k].type : (best == args[k].type ? best : T::Unknown);
            ++k;
        }
        return best;
    }
    // 1.13.0 : passer un argument a un parametre qui porte une unite (VAR_INPUT Consigne : REAL [bar]) - le moteur
    // ne convertit rien a l'appel : meme famille, EN ; familles differentes, un refus. Par position seulement (un
    // appel nomme se juge ailleurs), et pour une seule surcharge possible.
    void argumentUnits(const std::string& name, const HmiFunction& f, const std::vector<Val>& args) {
        const auto sig = overload::signatureOf(f);
        for (std::size_t k = 0; k < args.size() && k < sig.params.size(); ++k) {
            const auto& prm = sig.params[k];
            if (prm.mode == overload::Mode::Out || args[k].unit.empty()) continue;
            std::string pu;
            for (const auto& d : f.decls)
                if (d.kind == DeclKind::Parameter && up(d.name) == up(prm.name)) { pu = trimmed(d.unit); break; }
            if (pu.empty()) continue;
            const std::string target = "le param\xC3\xA8tre " + prm.name + " de " + name;
            if (auto pb = unitPassVerdict(cat_, pu, args[k].unit, spanText(args[k]), args[k].plain, args[k].unitFrom, UnitPass::Argument,
                                          target, false))
                pushUnit(std::move(*pb));
        }
    }
    void userCall(const std::string& name, const std::vector<const HmiFunction*>& fs, int n, Val& r,
                  const std::vector<Val>& args = {}, bool named = false) {
        std::vector<const HmiFunction*> fitting;
        int lo = -1, hi = 0;
        for (const auto* f : fs) {
            const auto sig = overload::signatureOf(*f);
            int last = -1;
            for (std::size_t k = 0; k < sig.params.size(); ++k)
                if (!sig.params[k].optional) last = static_cast<int>(k);
            const int mn = last + 1, mx = static_cast<int>(sig.params.size());
            lo = lo < 0 ? mn : std::min(lo, mn);
            hi = std::max(hi, mx);
            if (n >= mn && n <= mx) fitting.push_back(f);
        }
        const auto& pool = fitting.empty() ? fs : fitting;
        std::string rt = pool.front()->returnType;
        for (const auto* f : pool)
            if (up(trimmed(f->returnType)) != up(trimmed(rt))) rt.clear();
        bool anyResult = false;
        for (const auto* f : pool) anyResult = anyResult || !trimmed(f->returnType).empty();
        r.type = rt.empty() ? T::Unknown : typeOfName(rt);
        // 1.13.0 : un retour generique (la lettre T) : le type deduit des arguments, pas Unknown.
        if (r.type == T::Unknown && pool.size() == 1 && !pool.front()->generics.empty())
            r.type = deducedGenericReturn(*pool.front(), args);
        // 1.13.0 (lot J6) : l'unite du retour se propage comme une variable de ce type (toutes les surcharges
        // qui conviennent doivent s'accorder, sinon pas d'unite).
        if (r.type == T::Num) {
            std::string ru = pool.front()->returnUnit;
            for (const auto* f : pool) if (f->returnUnit != ru) ru.clear();
            r.unit = ru;
        }
        if (!named && pool.size() == 1 && !fitting.empty()) argumentUnits(name, *pool.front(), args);   // 1.13.0
        if (!anyResult) {
            problem(name + " ne rend pas de valeur : une expression ne peut appeler qu'une fonction qui rend quelque chose");
            return;
        }
        if (!fitting.empty() || callsChecked) return;
        if (fs.size() > 1)
            problem(name + " : aucune de ses " + std::to_string(fs.size()) + " surcharges ne prend " + plural(n, "argument"));
        else
            problem(name + " prend " + (lo == hi ? plural(lo, "argument") : std::to_string(lo) + " \xC3\xA0 " + plural(hi, "argument")) + ", pas "
                    + std::to_string(n));
    }

    Val call(const std::string& name) {
        // 1.11.10 : une fonction de symbole (par son nom dans le symbole, ou Instance.Fonction).
        const HmiFunction* symFn = pendingFn_;
        pendingFn_ = nullptr;
        if (!symFn)
            if (const View* sv = symbolHere()) symFn = symbolFunction(*sv, name);
        // 1.12.16 : une fonction de la vue (ou de la popup), par son nom - elle passe avant celles du projet.
        if (!symFn && ctx_.view)
            if (const auto fs = viewmembers::functions(*ctx_.view, name); !fs.empty()) symFn = fs.front();
        next();    // (
        std::vector<Val> args;
        bool named = false;                       // 1.13.0 : F(a := 1.5) - les arguments ne vont pas par position
        if (!isOp(")"))
            for (;;) {
                args.push_back(orExpr());
                if (isOp(":=") || isOp("=>")) named = true;
                if (isOp(",")) { next(); continue; }
                break;
            }
        if (isOp(")")) next();
        const std::string u = up(name);
        const int n = static_cast<int>(args.size());
        Val r;
        r.show = name + "(...)";
        // 1.12.17 : les arguments [from, to) qui doivent etre des valeurs seules - pas un tableau entier.
        const auto singleArgs = [&](std::size_t from, std::size_t to) {
            if (ctx_.script && (u == "ASCII_TO_STRING" || u == "STRING_TO_ASCII")) return;   // les cases d'une chaine
            for (std::size_t k = from; k < args.size() && k < to; ++k)
                if (!args[k].whole.empty() && !(u.rfind("TO_", 0) == 0 && ownOperator(args[k].wholeType, "TO"))) {
                    problem(u + " attend une valeur seule, et " + args[k].show + " est " + args[k].entire + " (" + args[k].wholeType + ") - choisis "
                                + args[k].pick,
                            true);
                    return;
                }
        };
        if (symFn) {
            userCall(name, siblingsOf(symFn), n, r, args, named);
            lambdaCall(siblingsOf(symFn), name);                // 1.12.20 : dans une LAMBDA d'une fx
            return r;
        }
        // 1.13.0 (lot J6) : EN, UNITE, SANS_UNITE - les natives d'unite du dialecte. Une fonction du projet
        // de meme nom passe avant (comme les autres natives).
        if ((u == "EN" || u == "UNITE" || u == "SANS_UNITE") && !(ctx_.project && !ctx_.project->functionsNamed(name).empty())) {
            singleArgs(0, args.size());
            if (u == "EN") {
                r.type = T::Num;
                if (n != 2) {
                    problem("EN prend 2 arguments (la valeur, puis l'unit\xC3\xA9 cible entre apostrophes) : EN(valeur, 'bar')");
                } else {
                    const Val& val = args[0];
                    const std::string target = args[1].strKnown ? trimmed(args[1].str) : std::string{};
                    if (!args[1].strKnown)
                        unitProblem("EN veut l'unit\xC3\xA9 cible entre apostrophes : EN(" + val.show + ", 'bar')");
                    else if (val.unit.empty())
                        unitProblem("EN demande une valeur qui porte une unit\xC3\xA9 (" + val.show
                                    + " n'en a pas) : pose une unit\xC3\xA9 sur sa d\xC3\xA9" "claration, ou enl\xC3\xA8ve EN");
                    else if (!cat_.known(target))
                        unitProblem("unit\xC3\xA9 inconnue : '" + target + "' \xE2\x80\x94 choisis une unit\xC3\xA9 de la biblioth\xC3\xA8que");
                    else if (cat_.relation(val.unit, target) == units::Rel::OtherFamily)
                        unitProblem("on ne convertit pas des " + val.unit + " en " + target + " : " + cat_.familyPhrase(val.unit)
                                    + " et " + cat_.familyPhrase(target) + " sont deux familles");
                    else {
                        // 1.13.0 : le moteur lit l'unite de la valeur sur son chemin (la declaration) : EN ne convertit
                        // qu'une variable ou un membre lu tel quel, pas un calcul, un appel ni un autre EN.
                        if (!val.plain)
                            unitProblem("EN convertit une variable ou un membre (Pression, Ligne.Vannes[2]), pas un calcul (" + val.show
                                        + ") : \xC3\xA9" "cris EN(" + (val.unitFrom.empty() ? std::string("variable") : val.unitFrom) + ", '"
                                        + cat_.canonical(target) + "')" + (val.unitFrom.empty() ? std::string{} : std::string(" dans le calcul")));
                        r.unit = cat_.canonical(target);        // la valeur convertie porte l'unite cible
                    }
                }
                return r;
            }
            if (u == "UNITE") {
                r.type = T::Text;
                if (n != 1) problem("UNITE prend 1 argument : UNITE(valeur)");
                return r;                                        // le symbole de l'unite : un STRING (sans unite)
            }
            r.type = T::Num;                                     // SANS_UNITE : un nombre nu
            if (n != 1) problem("SANS_UNITE prend 1 argument : SANS_UNITE(valeur)");
            return r;
        }
        // 1.12.0 : les couleurs (RGB, COULEUR_...) et l'aleatoire (RANDOM...) : des textes de couleur
        // en arguments ; leur resultat : une couleur (un texte), une composante ou un nombre. Une
        // fonction du projet de meme nom (Random) passe avant.
        if (natives::isOwnFunction(u) && !(ctx_.project && !ctx_.project->functionsNamed(name).empty())) {
            int mn = 0, mx = 0;
            if (natives::arity(u, mn, mx) && (n < mn || (mx >= 0 && n > mx)))
                problem(u + " prend " + (mn == mx ? plural(mn, "argument") : std::to_string(mn) + " \xC3\xA0 " + plural(mx, "argument"))
                        + ", pas " + std::to_string(n));
            const auto* f = natives::function(u);
            r.type = f && f->returns == "STRING" ? T::Text : f && f->returns == "BOOL" ? T::Bool : T::Num;
            singleArgs(0, args.size());                    // 1.12.17 : des nombres, des couleurs
            return r;
        }
        // 1.12.7 : les natives des conteneurs, de std::numeric et de <bit> : le dialecte les sert dans une
        // expression (une copie des objets). Une fonction du projet du meme nom passe avant.
        if (const auto* nat = sim::lib::native(u); nat && !(ctx_.project && !ctx_.project->functionsNamed(name).empty())) {
            if (u == "SWAP" || u == "EXCHANGE")             // 1.12.16 : elles ecrivent des variables
                problem(u + (u == "SWAP" ? " \xC3\xA9" "change deux variables" : " \xC3\xA9" "crit sa variable")
                        + " : une expression ne change rien ; fais-le dans un script (une action \xC2\xAB Ex\xC3\xA9" "cuter un script \xC2\xBB)");
            else if (u == "WAIT")                           // 1.12.26
                problem("WAIT attend dans un script, pas dans une expression : une expression calcule sa valeur tout de suite");
            else if (u == "WAIT_UNTIL")                     // 1.13.0
                problem("WAIT_UNTIL attend dans un script d\xC3\xA9tach\xC3\xA9, pas dans une expression : une expression calcule sa valeur tout de suite");
            else if (u == "IHM_POINT_ARRET")                // 1.13.0 (lot J2) : le debogueur
                problem("IHM_POINT_ARRET s'arr\xC3\xAAte dans un script, pas dans une expression : une expression calcule sa valeur tout de suite"
                        " (pose plut\xC3\xB4t un point d'arr\xC3\xAAt dans la fonction qu'elle appelle)");
            else if (sim::lib::changesFiles(u))             // 1.12.26 : FILE_WRITE, FILE_DELETE, DIR_CREATE...
                problem(u + " change un fichier : une expression ne fait que lire ; fais-le dans un script (une action \xC2\xAB Ex\xC3\xA9"
                            "cuter un script \xC2\xBB)");
            else if (nat->writes || u == "TUPLE_UNPACK" || u == "LIST_COPY" || u == "LIST_TRANSFORM")
                problem(u + " change son objet : une expression en lit une copie et ne change rien ; "
                            "fais-le dans un script (une action \xC2\xAB Ex\xC3\xA9" "cuter un script \xC2\xBB)");
            quotedCallbacks(u, args);                         // 1.12.20 : LIST_TRANSFORM(S, D, 'Carre')
            if (n < nat->min || (nat->max >= 0 && n > nat->max))
                problem(u + " prend " + (nat->max < 0 ? "au moins " + plural(nat->min, "argument")
                                          : nat->min == nat->max ? plural(nat->min, "argument")
                                                                 : std::to_string(nat->min) + " \xC3\xA0 " + plural(nat->max, "argument"))
                        + ", pas " + std::to_string(n));
            const std::string_view sig = nat->signature;
            const auto colon = sig.rfind(" : ");
            const std::string_view ret = colon == std::string_view::npos ? std::string_view{} : sig.substr(colon + 3);
            r.type = ret == "BOOL" ? T::Bool : ret == "STRING" ? T::Text : ret == "DINT" || nat->group == "numeric" ? T::Num : T::Unknown;
            if (u == "MIDPOINT" || u.find("_SAT") != std::string::npos || u == "SATURATE_CAST" || u == "BIT_SET" || u == "BIT_RESET") r.type = T::Num;
            // 1.12.20 : STATIC_CAST<T>(x), DYNAMIC_CAST<T>(x) - le type de T (un nombre, un BOOL, une enumeration) : la
            // lecture des fx a deja ote le <T> (templatesAsValues), le resultat reste ouvert.
            if (sim::lib::castTemplate(u)) r.type = T::Unknown;
            if (ret == "INT" || ret == "REAL" || (ret == "comme IN" && nat->group != "utility")) r.type = T::Num;    // 1.12.16 : YEAR, FMOD, CEIL, BYTESWAP...
            if (ret == "LINT") r.type = T::Num;                                                                         // 1.12.26 : FILE_SIZE
            return r;
        }
        // 1.12.4 : les fonctions des listes, des vecteurs, des MAP et des tableaux (JOIN, LIST_COUNT,
        // MAP_HAS, SIZEOF...), TO_UPPER et TO_LOWER : le dialecte les sert dans une expression, qui
        // lit une copie de l'objet (avant : « fonction inconnue : JOIN » a Generer). Celles qui
        // changent l'objet n'y changeraient que la copie ; un iterateur se parcourt dans un script.
        if (const auto* f = natives::function(u);
            f && (f->category == "liste" || f->category == "map" || f->category == "tab" || u == "TO_UPPER" || u == "TO_LOWER")) {
            static const std::set<std::string> kChanges = {"LIST_ADD",     "LIST_INSERT",  "LIST_REMOVE_AT", "LIST_REMOVE",  "LIST_CLEAR",
                                                           "LIST_SORT",    "LIST_REVERSE", "VECTOR_PUSH",    "VECTOR_POP",   "VECTOR_INSERT",
                                                           "VECTOR_ERASE", "VECTOR_CLEAR", "VECTOR_RESIZE",  "MAP_REMOVE",   "MAP_CLEAR"};
            if (kChanges.count(u))
                problem(u + " change son objet : une expression en lit une copie et ne change rien ; "
                            "fais-le dans un script (une action \xC2\xAB Ex\xC3\xA9" "cuter un script \xC2\xBB)");
            else if (u == "MAP_BEGIN" || u == "MAP_NEXT" || u == "MAP_END")
                problem(u + " : un it\xC3\xA9rateur se parcourt dans un script (FOR EACH), pas dans une expression");
            int mn = 0, mx = 0;
            if (natives::arity(u, mn, mx) && (n < mn || (mx >= 0 && n > mx)))
                problem(u + " prend " + (mn == mx ? plural(mn, "argument") : std::to_string(mn) + " \xC3\xA0 " + plural(mx, "argument"))
                        + ", pas " + std::to_string(n));
            r.type = f->returns == "STRING"                                               ? T::Text
                   : f->returns == "BOOL"                                                 ? T::Bool
                   : f->returns == "DINT" || f->returns == "INT" || f->returns == "REAL" ? T::Num
                                                                                          : T::Unknown;
            return r;
        }
        if (isStandardFunction(u)) {
            singleArgs(0, u == "SEL" || u == "MUX" ? 1 : args.size());   // 1.12.17 : SEL et MUX choisissent parmi le reste
            const auto to = u.find("_TO_");
            if (to != std::string::npos && to > 0) {
                if (n != 1) problem(u + " prend 1 argument, pas " + std::to_string(n) + " : " + u + "(valeur)");
                const T from = typeOfName(u.substr(0, to));
                if (n >= 1 && args[0].type != T::Unknown && from != T::Unknown && args[0].type != from
                    && (from == T::Text || args[0].type == T::Text))
                    problem(u + " attend " + typeWord(from) + ", pas " + typeWord(args[0].type) + " (" + args[0].show + ")");
                r.type = typeOfName(u.substr(to + 4));
                return r;
            }
            for (const auto& a : kArity) {
                if (u != a.name) continue;
                if (n < a.min || (a.max >= 0 && n > a.max)) {
                    const std::string want = a.max < 0             ? "au moins " + plural(a.min, "argument")
                                           : a.min == a.max        ? plural(a.min, "argument")
                                                                   : std::to_string(a.min) + " ou " + plural(a.max, "argument");
                    problem(u + " prend " + want + ", pas " + std::to_string(n));
                }
                break;
            }
            static const std::set<std::string> kText = {"LEFT", "RIGHT", "MID", "CONCAT", "INSERT", "DELETE", "REPLACE"};
            static const std::set<std::string> kTextIn = {"LEN", "LEFT", "RIGHT", "MID", "FIND", "CONCAT", "INSERT", "DELETE", "REPLACE"};
            if (kTextIn.count(u)) {
                if (n >= 1 && (args[0].type == T::Num || args[0].type == T::Bool))
                    problem(u + " travaille sur un texte, pas sur " + typeWord(args[0].type) + " (" + args[0].show + ") : convertis-le (INT_TO_STRING)");
            } else if (u != "SEL" && u != "MUX") {
                for (const auto& a : args)
                    if (a.type == T::Text) {
                        problem(u + " calcule sur des nombres, pas sur un texte (" + a.show + ")");
                        break;
                    }
            }
            if (u == "SEL") {
                if (n >= 1 && args[0].type == T::Text) problem("SEL choisit avec un bool\xC3\xA9" "en, pas un texte (" + args[0].show + ")");
                if (n == 3) r.type = args[1].type == args[2].type ? args[1].type : T::Unknown;
            }
            if (u == "SEL" || u == "MUX") {
                for (std::size_t k = 1; k < args.size(); ++k) r.choices.insert(r.choices.end(), args[k].choices.begin(), args[k].choices.end());
            }
            if (u == "SEL") {
            } else if (kText.count(u)) {
                r.type = T::Text;
            } else if (u == "LEN" || u == "FIND") {
                r.type = T::Num;
            } else if (u == "MIN" || u == "MAX" || u == "LIMIT") {
                T common = n ? args[0].type : T::Unknown;
                for (const auto& a : args) if (a.type != common) common = T::Unknown;
                r.type = common == T::Time ? T::Time : common == T::Num ? T::Num : T::Unknown;
                // 1.13.0 : ils comparent leurs arguments - la meme unite exacte, comme < et > (sinon EN) ; le
                // resultat la garde (LIMIT(0.0, PressionBasse, 900.0) est en mbar).
                if (r.type == T::Num && n >= 1) {
                    Val acc = args[0];
                    for (std::size_t k = 1; k < args.size(); ++k) {
                        if (acc.unit.empty() && args[k].unit.empty()) continue;
                        const std::string ru = unitAddCompare(acc, args[k], u, true);
                        acc.unitFrom = unitSource(ru, acc, args[k]);
                        acc.unit = ru;
                    }
                    r.unit = acc.unit;
                    r.unitFrom = acc.unitFrom;
                }
            } else if (u != "MUX") {
                r.type = T::Num;
                // 1.13.0 : la valeur absolue, l'oppose, l'arrondi gardent l'unite (ABS(PressionBasse) est en mbar).
                if ((u == "ABS" || u == "NEG" || u == "TRUNC" || u == "ROUND") && n >= 1) {
                    r.unit = args[0].unit;
                    r.unitFrom = args[0].unitFrom;
                }
            }
            return r;
        }
        if (const Project* p = ctx_.project)
            if (const auto fs = p->functionsNamed(name); !fs.empty()) {
                userCall(name, fs, n, r, args, named);    // 1.11.20 : ses surcharges, tous leurs parametres
                lambdaCall(fs, name);              // 1.12.20 : dans une LAMBDA d'une fx
                return r;
            }
        if (isHmiFunction(u)) {
            if (lambdaBody_ > 0 && hmiFunctionActs(u)) lambdaEffect("appelle " + u);   // 1.12.20
            quotedCallbacks(u, args);                                                  // 1.12.20 : IHM_APRES(T#1s, 'Fermer')
            return r;
        }
        // 1.10.2 (chantier T3) : TO_STRING(Mode), TO_INT(x), TO_T_MODE(2) - les conversions
        // du dialecte et celles des types du projet, que le moteur sert (HmiExpr.cpp).
        if (ctx_.project && isConversion(*ctx_.project, u)) {
            if (n != 1) problem(u + " prend 1 argument, pas " + std::to_string(n) + " : " + u + "(valeur)");
            singleArgs(0, args.size());                    // 1.12.17 : TO_STRING(Liste) ne se convertit pas
            r.type = typeOfName(u.substr(3));
            return r;
        }
        // 1.12.19 : un rappel s'appelle comme une fonction - une variable IHM, un parametre de la vue de type CALLBACK ;
        // ce qu'il rend : le type ecrit apres sa parenthese.
        if (const std::string ct = callbackTypeOf(name); !ct.empty()) {
            const auto close = ct.rfind(')');
            const auto colon = close == std::string::npos ? std::string::npos : ct.find(':', close);
            std::string ret = colon == std::string::npos ? std::string{} : ct.substr(colon + 1);
            while (!ret.empty() && std::isspace(static_cast<unsigned char>(ret.front()))) ret.erase(0, 1);
            while (!ret.empty() && std::isspace(static_cast<unsigned char>(ret.back()))) ret.pop_back();
            r.type = ret.empty() ? T::Unknown : typeOfName(ret);
            return r;
        }
        std::vector<std::string> names;
        for (const auto& a : kArity) names.emplace_back(a.name);
        for (const auto& f : natives::functions())        // 1.12.0 : RGB, COULEUR_..., RANDOM...
            if (f.category == "couleur" || f.category == "alea") names.emplace_back(f.name);
        for (const char* conv : {"INT_TO_REAL", "REAL_TO_INT", "INT_TO_STRING", "REAL_TO_STRING", "BOOL_TO_INT"}) names.emplace_back(conv);
        if (ctx_.project) for (const auto& f : ctx_.project->programs.functions) names.push_back(f.name);
        if (const View* sv = symbolHere()) for (const auto& f : sv->functions) names.push_back(f.name);   // 1.11.10
        const std::string near = closestAmong(name, names);
        problem("fonction inconnue : " + name + (near.empty() ? std::string() : " (veux-tu dire " + near + " ?)")
                + " ; une expression n'appelle que les fonctions standard (ABS, MIN, LIMIT, INT_TO_REAL...), les fonctions IHM du projet"
                  " et celles des symboles (dans le symbole : Nom(), dans la vue : Instance.Nom())");
        return r;
    }
};

} // namespace

std::string closest(const Context& ctx, std::string_view name) {
    std::vector<std::string> pool;
    if (ctx.project) {
        for (const auto& var : ctx.project->programs.variables) pool.push_back(var.name);
        for (const auto& f : ctx.project->programs.functions) pool.push_back(f.name);
    }
    if (ctx.view)
        for (const auto& prm : ctx.view->params) pool.push_back(prm.name);
    for (const auto& c : ctx.candidates) pool.push_back(c);
    return closestAmong(name, pool);
}

namespace {
// 1.12.6 (lot B) : le role d'une propriete (HmiRoles.hpp), en ce qu'attend la verification.
Want wantOfRole(roles::Role role) {
    switch (role) {
        case roles::Role::Number: case roles::Role::Duration: return Want::Number;
        case roles::Role::Boolean: return Want::Bool;
        case roles::Role::Color: return Want::Color;
        default: return Want::Any;
    }
}
} // namespace

Want wantOf(std::string_view key) {
    // 1.12.6 (lot B) : la table des roles (avant : kPropTypes et le suffixe « Color », sans le genre).
    if (const auto* r = roles::keyRole(key)) {
        if (r->role == roles::Role::Formula) return wantOfRole(r->item);
        return wantOfRole(r->role);
    }
    const std::string k(key);
    if (k == "color" || (k.size() > 5 && (k.compare(k.size() - 5, 5, "Color") == 0 || k.compare(k.size() - 5, 5, "color") == 0)))
        return Want::Color;
    return Want::Any;
}

Want wantOf(Kind kind, std::string_view key) {
    if (!roles::keyRole(key)) return wantOf(key);
    const auto r = roles::roleOf(kind, key);
    if (r.role == roles::Role::List) return Want::Any;      // une formule qui rend la liste (a;b;...) : un texte
    return wantOfRole(r.role == roles::Role::Formula ? r.item : r.role);
}

namespace {
// 1.12.6 : CE QUE COMPILER EXIGEAIT EN 1.12.5 - le type public des proprietes (l'ancienne table
// kPropTypes : un REAL ou un INT, un nombre ; un BOOL, un booleen ; une STRING, rien) et les
// couleurs (fill, stroke, color, ...Color). Un ecart y reste une erreur ; ailleurs, un avertissement.
Want wantIn1125(std::string_view key) {
    static constexpr std::string_view kNumber[] = {
        "x", "y", "w", "h", "rot", "pivotX", "pivotY", "opacity", "strokeWidth", "fontSize", "radius", "min", "max", "step",
        "ymin", "ymax", "duration", "window", "deadband", "speed", "ticks", "digits", "decimals", "maxVisible", "holdMs",
        "refresh", "access", "rows", "maxLength", "minLength", "margin", "period", "page", "tabPage", "maxItems", "scrollX",
        "scrollY", "contentWidth", "contentHeight", "headerHeight", "tabHeight", "wheelStep", "gap"};
    static constexpr std::string_view kBool[] = {
        "visible", "blink", "flipH", "flipV", "wrap", "showValue", "continuous", "seconds", "showText", "showDate",
        "leadingZeros", "legend", "clip", "inverted", "userList", "showUser", "confirm", "icon", "requireDigit",
        "validateOnExit", "autoplay", "muted", "bold", "italic", "collapsed", "pushBelow", "backForward", "showScrollbar",
        "showNames", "showCounts", "home", "pilotable"};
    for (const auto k : kNumber) if (k == key) return Want::Number;
    for (const auto k : kBool) if (k == key) return Want::Bool;
    if (key == "fill" || key == "stroke" || key == "color"
        || (key.size() > 5 && (key.substr(key.size() - 5) == "Color" || key.substr(key.size() - 5) == "color")))
        return Want::Color;
    return Want::Any;
}
} // namespace

bool strictType(Kind kind, std::string_view key) {
    const Want now = wantOf(kind, key);
    return now == Want::Any || wantIn1125(key) == now;
}

// 1.12.7 : les templates se lisent ici comme une valeur (NUMERIC_LIMITS<UINT>.MAX() : un nombre,
// NUMERIC_LIMITS<REAL>.IS_SIGNED : un BOOL, NUMBERS.PI : un reel) ou un appel (ADD_SAT<INT>(a, b) :
// ADD_SAT(a, b)) ; leur type et leur membre, c'est la lecture de l'expression qui les dit.
std::string templatesAsValues(const Context& ctx, std::string_view s) {
    std::string out;
    std::size_t i = 0;
    const auto ident = [&](std::size_t k) { return k < s.size() && (std::isalpha(static_cast<unsigned char>(s[k])) || s[k] == '_'); };
    const auto identEnd = [&](std::size_t k) {
        while (k < s.size() && (std::isalnum(static_cast<unsigned char>(s[k])) || s[k] == '_')) ++k;
        return k;
    };
    const auto spaces = [&](std::size_t k) {
        while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
        return k;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\'' || c == '"') {
            const auto end = s.find(c, i + 1);
            const std::size_t stop = end == std::string_view::npos ? s.size() : end + 1;
            out.append(s.substr(i, stop - i));
            i = stop;
            continue;
        }
        if (!ident(i) || (i > 0 && (std::isalnum(static_cast<unsigned char>(s[i - 1])) || s[i - 1] == '_' || s[i - 1] == '.' || s[i - 1] == '#'))) {
            out.push_back(c);
            ++i;
            continue;
        }
        const std::size_t e = identEnd(i);
        const std::string word(s.substr(i, e - i));
        const auto* t = sim::lib::templateInfo(word);
        if (!t || (ctx.project && ctx.project->variable(word))) {
            out.append(word);
            i = e;
            continue;
        }
        std::size_t k = spaces(e);
        bool angled = false;
        if (k < s.size() && s[k] == '<') {
            const std::size_t a = spaces(k + 1);
            if (ident(a)) {
                std::size_t te = identEnd(a);
                while (te + 1 < s.size() && s[te] == '.' && ident(te + 1)) te = identEnd(te + 1);   // 1.13.0 : <Gaz.T_Pompe>
                const std::size_t b = spaces(te);
                if (b < s.size() && s[b] == '>') {
                    angled = true;
                    k = spaces(b + 1);
                }
            }
        }
        if (t->isClass) {
            if (k < s.size() && s[k] == '.' && ident(spaces(k + 1))) {
                const std::size_t m0 = spaces(k + 1), m1 = identEnd(m0);
                const std::string member(s.substr(m0, m1 - m0));
                std::size_t next = spaces(m1);
                if (next + 1 < s.size() && s[next] == '(' && s[spaces(next + 1)] == ')') next = spaces(next + 1) + 1;
                else next = m1;
                const auto* mi = sim::lib::templateMember(t->name, member);
                const std::string u = [&] { std::string x = member; for (auto& ch : x) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); return x; }();
                const bool boolean = mi && !mi->function && (u.rfind("IS_", 0) == 0 || u.rfind("HAS_", 0) == 0 || u == "TRAPS" || u == "TINYNESS_BEFORE") && u != "HAS_DENORM";
                out.append(boolean ? "TRUE" : t->name == "NUMBERS" ? "1.0" : "1");
                i = next;
                continue;
            }
            out.append(word);
            i = e;
            continue;
        }
        out.append(word);
        i = angled ? k : e;
    }
    return out;
}

std::vector<Problem> check(const Context& ctx, std::string_view written, Want want, bool strict) {
    std::vector<Problem> out;
    // 1.11 (REP) : les `$` des reperes sont transparents ($V[1].Ouv$ se verifie comme V[1].Ouv).
    // 1.12.7 : les templates, comme des valeurs (templatesAsValues).
    const std::string plain = templatesAsValues(ctx, markers::strip(written, markers::Mode::Expression));
    const std::string_view source = plain;
    if (trimmed(source).empty()) return out;
    // 1.11.20 : les appels des fonctions de l'utilisateur, lus par le simulateur et juges par la
    // regle du moteur (E/S et sorties comptees, references, surcharges).
    bool callsRead = false;
    const auto calls = ctx.calls ? callcheck::checkExpression(callcheck::Context{ctx.project, ctx.view, nullptr, ctx.plc}, source, &callsRead)
                                 : std::vector<callcheck::Problem>{};
    Checker c(ctx, withNamespaces(ctx, lex(source)), source);
    c.callsChecked = callsRead || !ctx.calls;
    for (const auto& m : markers::find(written, markers::Mode::Expression))   // 1.11.1 (REP-10)
        if (const auto roots = scanRoots(m.content(written)); !roots.empty()) c.markerRoots.insert(up(roots.front()));
    const Val v = c.run();
    out = std::move(c.problems);
    for (const auto& pr : calls) {
        bool twice = false;
        for (const auto& o : out) twice = twice || o.message == pr.message;
        if (!twice) out.push_back(Problem{pr.message, {}, {}, !pr.error});    // 1.11.21 : sa gravite
    }
    const auto add = [&out](std::string m) {
        for (const auto& p : out) if (p.message == m) return;
        out.push_back(Problem{std::move(m), {}, {}});
    };
    // 1.12.6 : le type qui ne convient pas - une erreur la ou la 1.12.5 l'exigeait deja, sinon un
    // avertissement (strictType : un projet qui demarrait demarre toujours).
    const auto addType = [&out, strict](std::string m) {
        for (const auto& p : out) if (p.message == m) return;
        out.push_back(Problem{std::move(m), {}, {}, !strict});
    };
    // 1.12.17 : un tableau, une liste, une MAP, une structure ENTIERS la ou une valeur seule est attendue (une
    // plume, la valeur d'une jauge, une condition, une couleur) : une erreur, toujours - rien ne s'y lirait.
    if (!v.whole.empty() && want != Want::Any && want != Want::Text) {
        const std::string expected = want == Want::Bool     ? "un bool\xC3\xA9" "en (vrai ou faux) est attendu"
                                   : want == Want::Number   ? "un nombre est attendu"
                                   : want == Want::Color    ? "une couleur est attendue"
                                                            : "une valeur seule est attendue";
        Problem pb{expected + ", et " + v.show + " est " + v.entire + " (" + v.wholeType + ") - choisis " + v.pick, {}, {}};
        pb.whole = true;
        out.push_back(std::move(pb));
    }
    switch (want) {
        case Want::Bool:
            if (v.type == T::Text)
                addType("un bool\xC3\xA9" "en est attendu (vrai ou faux) et l'expression donne un texte (" + v.show + ") : compare-le, par exemple "
                    + v.show + " = 'Marche'");
            else if (v.type == T::Time)
                addType("un bool\xC3\xA9" "en est attendu (vrai ou faux) et l'expression donne une dur\xC3\xA9" "e (" + v.show + ") : compare-la (" + v.show + " > T#5s)");
            break;
        case Want::Number:
            if (v.type == T::Text)
                addType("un nombre est attendu et l'expression donne un texte (" + v.show + ") : convertis-le (STRING_TO_REAL) ou choisis une variable num\xC3\xA9rique");
            break;
        case Want::Color:
            if (v.type == T::Bool || v.type == T::Num || v.type == T::Time)
                addType("une couleur est attendue (un texte '#RRGGBB') et l'expression donne " + typeWord(v.type) + " (" + v.show
                    + ") : choisis la couleur avec SEL(" + (v.type == T::Bool ? v.show : v.show + " > 0") + ", '#808080', '#00A000')");
            else
                for (const auto& s : v.choices)
                    if (!colorText(s)) {
                        addType("'" + s + "' n'est pas une couleur : \xC3\xA9" "cris '#RRGGBB' (par exemple '#FF0000')");
                        break;
                    }
            break;
        case Want::Text:
        case Want::Any:
        case Want::Value:
            break;
    }
    // Les membres et les indices constants des variables IHM composees.
    if (ctx.project)
        for (const auto& pp : types::pathProblems(*ctx.project, source)) add(pp.message);
    return out;
}

Shape shapeOf(const Context& ctx, std::string_view written) {
    Shape s;
    const std::string plain = templatesAsValues(ctx, markers::strip(written, markers::Mode::Expression));
    if (trimmed(plain).empty()) return s;
    Context quiet = ctx;
    quiet.calls = false;                       // la forme seulement : les appels sont juges ailleurs
    Checker c(quiet, withNamespaces(quiet, lex(plain)), plain);
    c.callsChecked = true;
    const Val v = c.run();
    s.single = v.type != T::Unknown;
    s.unit = v.unit;                            // 1.13.0 (lot J6) : l'unite physique de la valeur
    s.plain = v.plain;                          // 1.13.0 : lue telle quelle sur un chemin (le moteur lit son unite)
    s.unitFrom = v.unitFrom;
    if (!v.whole.empty()) {
        s.type = v.wholeType;
        s.what = v.whole;
        s.entire = v.entire;
        s.pick = v.pick;
    }
    return s;
}

std::vector<Problem> checkTarget(const Context& ctx, std::string_view target, Want writes) {
    std::vector<Problem> out;
    const std::string t = trimmed(markers::strip(target, markers::Mode::Expression));   // 1.11 (REP)
    if (t.empty()) return out;
    const auto toks = lex(t);
    const Tok& first = toks.front();
    const bool constant = first.k == Tok::K::Num || first.k == Tok::K::Str || first.k == Tok::K::Lit
                       || (first.k == Tok::K::Ident && (up(first.text) == "TRUE" || up(first.text) == "FALSE"));
    if (constant) {
        out.push_back(Problem{"tu \xC3\xA9" "cris dans une constante (" + t + ") : la cible d'une \xC3\xA9" "criture doit \xC3\xAA" "tre une variable", {}, {}});
        return out;
    }
    if (first.k != Tok::K::Ident) return out;
    if (const Variable* var = ctx.project ? ctx.project->variable(first.text) : nullptr) {
        if (var->readOnly)
            out.push_back(Problem{"la variable IHM " + var->name + " est en lecture seule : rien ne peut l'\xC3\xA9" "crire ; d\xC3\xA9" "coche "
                                  "\xC2\xAB Lecture seule \xC2\xBB dans Variables IHM, ou vise une autre variable", {}, {}});
    } else if (ctx.plc.api && up(first.text) == "API" && apivars::isApiPath(t) && !(ctx.project && pub::viewNamed(*ctx.project, first.text))) {
        // 1.11.1 (API-M) : une ecriture vers API.<...> - une constante ou une
        // variable en lecture seule de l'automate est refusee, avec la raison
        // (un chemin inconnu : check() le dit plus bas).
        const auto r = ctx.plc.api->resolve(t);
        if (r.ok && r.constant)
            out.push_back(Problem{r.path + " est une constante de l'automate : rien ne peut l'\xC3\xA9" "crire", {}, {}});
        else if (r.ok && r.access == apivars::Access::ReadOnly)
            out.push_back(Problem{r.path + " est en lecture seule (" + r.accessWhy + ") : l'IHM ne peut pas l'\xC3\xA9" "crire", {}, {}});
    }
    // 1.12.17 : mettre a 1, basculer, incrementer, ecrire une valeur seule - dans un element, pas dans un tableau entier.
    if (writes != Want::Any)
        if (const auto sh = shapeOf(ctx, t); sh.whole()) {
            const char* what = writes == Want::Bool ? "un bool\xC3\xA9" "en" : writes == Want::Number ? "un nombre" : "une valeur seule";
            Problem pb{t + " est " + sh.entire + " (" + sh.type + ") : on n'y \xC3\xA9" "crit pas " + what + " - vise " + sh.pick, {}, {}};
            pb.whole = true;
            out.push_back(std::move(pb));
        }
    for (auto& p : check(ctx, t, Want::Any)) out.push_back(std::move(p));
    return out;
}

std::vector<Problem> passUnit(const Context& ctx, std::string_view targetUnit, std::string_view source, UnitPass pass,
                              std::string_view targetText, bool strict) {
    std::vector<Problem> out;
    const std::string tu = trimmed(targetUnit);
    if (tu.empty()) return out;                        // une cible sans unite : rien a exiger (elle recoit le nombre)
    const Shape sh = shapeOf(ctx, source);
    if (sh.unit.empty()) return out;                   // une source sans unite : le nu adopte celle de la cible
    const units::Catalog cat = ctx.project ? units::Catalog(biblio::unitConfig(*ctx.project)) : units::Catalog();   // 1.13.0 : bibliotheques comprises
    // Le texte de la source, sans ses reperes : celui que la correction d'un clic retrouve.
    const std::string src = trimmed(markers::strip(source, markers::Mode::Expression));
    if (auto pb = unitPassVerdict(cat, tu, sh.unit, src, sh.plain, sh.unitFrom, pass, std::string(targetText), strict))
        out.push_back(std::move(*pb));
    return out;
}

std::vector<Problem> assignmentUnit(const Context& ctx, std::string_view target, std::string_view source, bool strict) {
    return passUnit(ctx, shapeOf(ctx, target).unit, source, UnitPass::Assign, trimmed(target), strict);
}

std::vector<std::string> templateExpressions(std::string_view text) {
    // Le meme decoupage que TextTemplate::compile : {{ et }} sont des accolades
    // litterales, un trou va jusqu'a la premiere '}' (1.12.16 : hors des chaines,
    // templateHoleEnd), son format est apres le dernier ':' (pas celui d'un ":=")
    // s'il en a l'air, ou ":u".
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) { i += 2; continue; }
        if (c != '{') { ++i; continue; }
        const auto close = templateHoleEnd(text, i + 1);
        if (close == std::string_view::npos) break;
        std::string inside(text.substr(i + 1, close - i - 1));
        const auto colon = inside.rfind(':');
        if (colon != std::string::npos && colon + 1 < inside.size() && inside[colon + 1] != '=') {
            const std::string fmt = inside.substr(colon + 1);
            if (looksLikeFormat(fmt) || fmt == "u" || fmt == "U") inside.resize(colon);
        }
        if (!trimmed(inside).empty()) out.push_back(trimmed(inside));
        i = close + 1;
    }
    return out;
}

} // namespace hmi::exprcheck
