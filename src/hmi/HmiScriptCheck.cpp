// =============================================================================
//  hmi/HmiScriptCheck.cpp - 1.10 : les erreurs d'un script ST, a leur place
//  (voir HmiScriptCheck.hpp). Les jetons gardent leur place dans le texte ;
//  les instructions sont suivies juste assez pour savoir ce qui est ecrit (la
//  cible d'un :=), ce qui doit etre un booleen (IF, ELSIF, WHILE, UNTIL) et ce
//  qui est appele. Les types et les divisions passent par exprcheck.
// =============================================================================
#include "HmiScriptCheck.hpp"
#include "HmiSymbols.hpp"   // 1.11.10 : les fonctions des symboles
#include "HmiApiVars.hpp"   // 1.11.1 (API-M) : API.<globale>, API.<Unite>.<variable>

#include "HmiExpr.hpp"
#include "HmiMarkers.hpp"         // 1.11.1 (REP) : les $ des reperes, transparents dans un script ST
#include "HmiDuplicate.hpp"       // 1.11.1 (REP) : la phrase de Dupliquer sur l'ancien $Vanne$ d'un script d'action
#include "HmiEnums.hpp"           // 1.10 (integration I2) : TO_<enumeration>
#include "HmiOperators.hpp"       // 1.10 (integration I2) : isConversion (S2)
#include "HmiPopupParams.hpp"
#include "HmiPublicVars.hpp"
#include "HmiScript.hpp"
#include "HmiTemplates.hpp"
#include "HmiTypes.hpp"
#include "../project/MemberTree.hpp"

// 1.10 (decisions 13 et 13 bis) : le dialecte IHM du chantier S1 - ses
// fonctions internes, ses noms et ses constats (hmi::lang110::analyze ; voir
// /home/claude/v110/avancement/S-interface.md). Sans son en-tete : les
// constructions sont reconnues ici seulement (scanDialect).
#if __has_include("HmiScriptCheck110.hpp")
#include "HmiScriptCheck110.hpp"
#define XPG_HMI_LANG110 1
#endif

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>

namespace hmi::scriptcheck {

namespace {

std::string up(std::string_view s) {
    std::string u(s);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}
bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// ------------------------------------------------------------ les jetons ---
struct Tok {
    enum class K : std::uint8_t { End, Ident, Num, Str, Lit, Op } k{K::End};
    std::string text;
    std::size_t at{0}, len{0};
    [[nodiscard]] std::size_t end() const { return at + len; }
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
        Tok t;
        t.at = i;
        if (c == '\'' || c == '"') {
            std::size_t j = i + 1;
            while (j < n && s[j] != c && s[j] != '\n') j += (s[j] == '$' && j + 1 < n) ? 2 : 1;
            j = j < n && s[j] == c ? j + 1 : j;
            t.k = Tok::K::Str;
            t.text = std::string(s.substr(i, j - i));
            t.len = j - i;
            out.push_back(std::move(t));
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            bool based = false;
            while (j < n) {
                const char d = s[j];
                if (identChar(d)) { ++j; continue; }
                // 1.5, mais pas 1..5 (un intervalle de CASE)
                if (d == '.' && !(j + 1 < n && s[j + 1] == '.')) { ++j; continue; }
                if (d == '#') { based = true; ++j; continue; }
                if ((d == '+' || d == '-') && !based && j > i && (s[j - 1] == 'e' || s[j - 1] == 'E')) { ++j; continue; }
                break;
            }
            t.k = Tok::K::Num;
            t.text = std::string(s.substr(i, j - i));
            t.len = j - i;
            out.push_back(std::move(t));
            i = j;
            continue;
        }
        if (identStart(c)) {
            std::size_t j = i;
            while (j < n && identChar(s[j])) ++j;
            if (j < n && s[j] == '#') {
                // T#5s, TIME#1h, INT#3, D#2024-01-01, TOD#12:00:00
                const std::string w = up(s.substr(i, j - i));
                const bool date = w == "D" || w == "DATE" || w == "DT" || w == "DATE_AND_TIME";
                std::size_t k = j + 1;
                while (k < n && (identChar(s[k]) || s[k] == '.' || s[k] == ':' || (date && s[k] == '-'))) ++k;
                t.k = Tok::K::Lit;
                t.text = std::string(s.substr(i, k - i));
                t.len = k - i;
                out.push_back(std::move(t));
                i = k;
                continue;
            }
            t.k = Tok::K::Ident;
            t.text = std::string(s.substr(i, j - i));
            t.len = j - i;
            out.push_back(std::move(t));
            i = j;
            continue;
        }
        t.k = Tok::K::Op;
        static const char* const kTwo[] = {"<=", ">=", "<>", "**", ":=", "=>", ".."};
        for (const char* two : kTwo)
            if (s.substr(i, 2) == two) t.text = two;
        if (t.text.empty()) t.text = std::string(1, c);
        t.len = t.text.size();
        i += t.len;
        out.push_back(std::move(t));
    }
    Tok e;
    e.at = n;
    out.push_back(std::move(e));
    return out;
}

const std::set<std::string>& operatorWords() {
    static const std::set<std::string> k = {"AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "NULL"};   // NULL : 1.10 (13 bis)
    return k;
}
const std::set<std::string>& statementWords() {
    static const std::set<std::string> k = {"IF",    "THEN",      "ELSIF",   "ELSE",       "END_IF", "CASE",   "OF",
                                            "END_CASE", "FOR",    "TO",      "BY",         "DO",     "END_FOR", "WHILE",
                                            "END_WHILE", "REPEAT", "UNTIL",  "END_REPEAT", "EXIT",   "RETURN",
                                            "EACH",  "IN",        "FUNCTION", "END_FUNCTION"};   // 1.10 : le dialecte IHM (13)
    return k;
}
// 1.10 (decisions 13 et 13 bis) : les fonctions du dialecte IHM - reconnues
// ici, leurs arguments controles par S1 (dialectFindings).
const std::set<std::string>& dialectFunctions() {
    static const std::set<std::string> k = {"MAP_HAS",  "MAP_REMOVE", "MAP_SIZE", "MAP_CLEAR", "MAP_KEYS",    "MAP_GET",
                                            "MAP_BEGIN", "MAP_END",   "MAP_NEXT", "REF",       "ADR",         "SIZEOF",
                                            "LOWER_BOUND", "UPPER_BOUND"};
    return k;
}
// Un bloc de declarations (VAR, VAR_TEMP, VAR_INPUT : blanchis par
// splitDeclarations ; VAR_IN_OUT, VAR_OUTPUT... : restes dans le corps).
bool varWord(std::string_view upper) {
    return upper == "VAR" || upper == "VAR_TEMP" || upper == "VAR_INPUT" || upper == "VAR_OUTPUT" || upper == "VAR_IN_OUT"
        || upper == "VAR_EXTERNAL" || upper == "VAR_GLOBAL" || upper == "VAR_STAT" || upper == "VAR_INST";
}
// La fin d'un type : REF_TO T, POINTER TO T, REFERENCE TO T, ARRAY[..] OF T,
// MAP[..] OF T, STRING[20], T (k : son premier jeton).
std::size_t typeEnd(const std::vector<Tok>& t, std::size_t k) {
    const auto word = [&](std::size_t m, const char* w) { return m < t.size() && t[m].k == Tok::K::Ident && up(t[m].text) == w; };
    const auto op = [&](std::size_t m, const char* o) { return m < t.size() && t[m].k == Tok::K::Op && t[m].text == o; };
    const auto closeOf = [&](std::size_t m) {      // m : un '[' ; rend l'index apres son ']'
        int depth = 0;
        for (; m < t.size() && t[m].k != Tok::K::End; ++m) {
            if (op(m, "[")) ++depth;
            else if (op(m, "]") && --depth == 0) return m + 1;
        }
        return m;
    };
    for (;;) {
        if (word(k, "REF_TO")) { ++k; continue; }
        if ((word(k, "POINTER") || word(k, "REFERENCE")) && word(k + 1, "TO")) { k += 2; continue; }
        break;
    }
    if (word(k, "ARRAY") || word(k, "MAP")) {
        ++k;
        if (op(k, "[")) k = closeOf(k);
        return word(k, "OF") ? typeEnd(t, k + 1) : k;
    }
    if (k < t.size() && t[k].k == Tok::K::Ident) {
        const bool text = word(k, "STRING") || word(k, "WSTRING");
        ++k;
        if (text && op(k, "[")) k = closeOf(k);      // STRING[20]
    }
    return k;
}

// Les fonctions standard : nombre d'arguments (min, max ; -1 : sans limite).
struct Arity { const char* name; int min; int max; };
constexpr Arity kStandard[] = {
    {"ABS", 1, 1},  {"SQRT", 1, 1}, {"LN", 1, 1},    {"LOG", 1, 1},   {"EXP", 1, 1},   {"SIN", 1, 1},     {"COS", 1, 1},
    {"TAN", 1, 1},  {"ASIN", 1, 1}, {"ACOS", 1, 1},  {"ATAN", 1, 1},  {"TRUNC", 1, 1}, {"ROUND", 1, 2},   {"NEG", 1, 1},
    {"LEN", 1, 1},  {"EXPT", 2, 2}, {"LEFT", 2, 2},  {"RIGHT", 2, 2}, {"FIND", 2, 2},  {"SHL", 2, 2},     {"SHR", 2, 2},
    {"ROL", 2, 2},  {"ROR", 2, 2},  {"LIMIT", 3, 3}, {"SEL", 3, 3},   {"MID", 3, 3},   {"INSERT", 3, 3},  {"DELETE", 3, 3},
    {"REPLACE", 4, 4}, {"MIN", 2, -1}, {"MAX", 2, -1}, {"CONCAT", 2, -1}, {"MUX", 2, -1},
};
// Les fonctions IHM_ (leurs arguments facultatifs : la signature de l'aide a la saisie).
constexpr Arity kHmi[] = {
    {"IHM_NAVIGUER", 1, 3},        {"IHM_POPUP", 1, 3},          {"IHM_CHANGER_POPUP", 1, 2},   {"IHM_POPUP_PRECEDENTE", 0, 0},
    {"IHM_CENTRER_POPUP", 0, 1},   {"IHM_FERMER_POPUP", 0, 1},   {"IHM_FERMER_POPUPS", 0, 0},   {"IHM_POPUP_OUVERTE", 1, 1},
    {"IHM_JOURNAL", 1, 1},         {"IHM_APPELER", 1, 1},        {"IHM_SON", 1, 1},             {"IHM_VUE", 0, 0},
    {"IHM_TEMPS", 0, 0},           {"IHM_UTILISATEUR", 0, 0},    {"IHM_NOM_UTILISATEUR", 0, 0}, {"IHM_GROUPE", 0, 0},
    {"IHM_NIVEAU", 0, 0},          {"IHM_DECONNECTER", 0, 0},    {"IHM_PARAMETRES_SYSTEME", 0, 1},
    {"IHM_METTRE_DE_COTE", 1, 3},  {"IHM_REMETTRE", 1, 1},       {"IHM_FAIRE_TAIRE", 0, 0},     {"IHM_EXPORTER", 1, 3},
    {"IHM_PRECEDENTE", 0, 0},      {"IHM_SUIVANTE", 0, 0},       {"IHM_ACCUEIL", 0, 0},         {"IHM_MENU_CONNEXION", 0, 1},
    {"IHM_LANGUE", 1, 1},          {"IHM_THEME", 0, 1},          {"IHM_GIF_JOUER", 1, 2},       {"IHM_GIF_PAUSE", 1, 1},
    {"IHM_GIF_ARRETER", 1, 1},     {"IHM_GIF_REJOUER", 1, 2},    {"IHM_EQUIPEMENT_OK", 1, 1},   {"IHM_EQUIPEMENT_PING", 1, 1},
    {"IHM_ESCLAVE_SIMULE", 1, 1},
};

std::string plural(int n, const char* one) { return std::to_string(n) + " " + one + (n > 1 ? "s" : ""); }
std::string arityText(int min, int max) {
    if (max < 0) return "au moins " + plural(min, "argument");
    if (min == max) return min == 0 ? std::string("aucun argument") : plural(min, "argument");
    return std::to_string(min) + " \xC3\xA0 " + plural(max, "argument");
}

// Un type elementaire : BOOL, nombre, texte, duree ; "" : autre chose.
enum class Kind : std::uint8_t { Other, Bool, Num, Text, Time };
Kind kindOf(std::string_view type) {
    const std::string u = up(type);
    if (u == "BOOL" || u == "EBOOL") return Kind::Bool;
    if (u == "STRING" || u.rfind("STRING[", 0) == 0 || u == "WSTRING") return Kind::Text;
    if (u == "TIME" || u == "LTIME") return Kind::Time;
    static const std::set<std::string> kNum = {"INT",  "UINT", "DINT", "UDINT", "SINT", "USINT", "LINT", "ULINT",
                                               "WORD", "DWORD", "BYTE", "LWORD", "REAL", "LREAL"};
    return kNum.count(u) ? Kind::Num : Kind::Other;
}
bool elementary(std::string_view type) { return kindOf(type) != Kind::Other; }

// Les constantes locales (VAR CONSTANT ... END_VAR) : un script ne les ecrit pas.
// Les noms avant le ':' de chaque declaration (a, b : INT := 1;).
std::set<std::string> constantNames(std::string_view code) {
    std::set<std::string> out;
    const auto toks = lex(code);
    // 1.10.4 : chaque acces borne (un texte coupe en pleine frappe).
    const auto word = [&](std::size_t k, const char* w) {
        return k < toks.size() && toks[k].k == Tok::K::Ident && up(toks[k].text) == w;
    };
    for (std::size_t i = 0; i + 1 < toks.size(); ++i) {
        if (!word(i, "VAR") || !word(i + 1, "CONSTANT")) continue;
        std::size_t k = i + 2;
        bool name = true;
        for (; k < toks.size() && toks[k].k != Tok::K::End && !word(k, "END_VAR"); ++k) {
            if (toks[k].k == Tok::K::Op && toks[k].text == ";") name = true;
            else if (toks[k].k == Tok::K::Op && toks[k].text == ":") name = false;
            else if (name && toks[k].k == Tok::K::Ident) out.insert(up(toks[k].text));
        }
        i = k;
    }
    return out;
}

class Walker {
public:
    Walker(const Scope& sc, std::string_view code) : sc_(sc) {
#ifdef XPG_HMI_LANG110
        // Le dialecte IHM (S1) : le 3e argument est `knownType` (integration 1.10) - une
        // locale typee par une structure IHM du projet est permise.
        parts_ = splitDeclarations(code, sc.function != nullptr, [p = sc.project](std::string_view t) {
            return lang110::knownHmiType(p, t);              // 1.10 (S1) : structure ou enumeration
        });
#else
        parts_ = splitDeclarations(code, sc.function != nullptr);
#endif
        constants_ = constantNames(code);
        body_ = parts_.body;
        toks_ = lex(body_);
        lineStarts_.push_back(0);
        for (std::size_t i = 0; i < body_.size(); ++i)
            if (body_[i] == '\n') lineStarts_.push_back(i + 1);
        // Les compteurs de boucle : FOR i := ...
        for (std::size_t i = 0; i + 2 < toks_.size(); ++i)
            if (isWord(i, "FOR") && isIdent(i + 1) && isOp(i + 2, ":=")) counters_.insert(up(toks_[i + 1].text));
        if (sc_.view && sc_.project && inherits(*sc_.project, *sc_.view)) composed_ = compose(*sc_.project, *sc_.view);
        scanDialect(code);
    }

    // 1.11.1 (REP-10) : les racines des reperes du script (en majuscules) - un ancien
    // `$Vanne$` qui n'est pas une variable n'a pas de "veux-tu dire ... ?" (une fausse piste).
    std::set<std::string> markerRoots;

    std::vector<Finding> run() {
        // 1.10.4 (le plantage de la 1.10.3) : un FOR, un IF, un CASE en fin de
        // texte menaient i apres le jeton de fin - toks_[i] lisait hors du
        // tableau (un jeton de hasard, puis bad_alloc). i reste dans le tableau.
        std::size_t i = 0;
        while (!atEnd(i)) {
            const std::size_t before = i;
            statement(i);
            if (i <= before) i = before + 1;      // jamais deux fois au meme endroit
            i = std::min(i, last());
        }
        std::stable_sort(out_.begin(), out_.end(), [](const Finding& a, const Finding& b) {
            return a.line != b.line ? a.line < b.line : a.column < b.column;
        });
        return std::move(out_);
    }
    // 1.10 : les constats du chantier S1 (lang110::analyze) - vide sans lui.
    [[nodiscard]] const std::vector<Finding>& dialect() const noexcept { return dialect_; }
    // 1.10 : les noms du dialecte de ce code (en majuscules).
    [[nodiscard]] std::set<std::string> declared() const {
        std::set<std::string> out;
        for (const auto& d : dialectNames_) out.insert(d.upper);
        for (const auto& f : internal_) out.insert(up(f.name));
        if (!out.empty()) out.insert(typeWords_.begin(), typeWords_.end());
        return out;
    }

private:
    const Scope&          sc_;
    ScriptParts           parts_;
    std::string           body_;
    std::vector<Tok>      toks_;
    std::vector<std::size_t> lineStarts_;
    std::set<std::string> counters_;
    std::set<std::string> constants_;         // VAR CONSTANT : en lecture seule
    std::optional<View>   composed_;
    std::vector<Finding>  out_;
    // 1.10 (decisions 13 et 13 bis) : les noms du dialecte IHM - les fonctions
    // internes du script ; leurs parametres, leurs variables et leur nom (la
    // valeur rendue) dans la fonction ; les variables d'un FOR EACH dans la
    // boucle ; un nom declare d'un type que seul le dialecte connait (ARRAY,
    // MAP, REF_TO, POINTER TO...). Chacun vaut sur sa plage [from, to) du
    // texte ; son type, ses membres, ses arguments : S1 (dialectFindings).
    struct DialectName { std::string name, upper; std::size_t from{0}, to{0}; };
    std::vector<DialectName> dialectNames_;
    struct InternalFunction { std::string name; std::size_t from{0}, to{0}; };
    std::vector<InternalFunction> internal_;
    std::set<std::string>    typeWords_;         // les mots des types (en-tetes, declarations), en majuscules
    std::vector<Finding>     dialect_;           // les constats de S1 (lang110::analyze)

    // 1.10 (integration I2, S2) : une conversion TO_xxx - de base (TO_STRING,
    // TO_REAL...), du projet (un operateur de conversion : TO_T_RESUME, le
    // fromString TO_T_MODE) ou vers une enumeration du projet (l'entier : S1).
    [[nodiscard]] bool conversion(const std::string& U) const {
        if (!sc_.project || U.size() <= 3 || U.compare(0, 3, "TO_") != 0) return false;
        return isConversion(*sc_.project, U) || findEnumeration(*sc_.project, std::string_view(U).substr(3)) != nullptr;
    }
    // Pour S1 : le nom existe-t-il ailleurs (IHM, automate, SYS, Vue.Objet,
    // parametre de la vue, locale du script, fonction IHM) ? Et son type.
    [[nodiscard]] bool knownElsewhere(std::string_view n) const {
        const Project* p = sc_.project;
        if (local(n) || param(n) || counter(n) || selfName(n)) return true;
        if (p && (p->variable(n) || pub::isSysRoot(n) || pub::viewNamed(*p, n) || p->functionByName(n))) return true;
        return !sc_.plcKnown || sc_.plcKnown(n);     // l'automate inconnu : tout peut etre a lui
    }
    [[nodiscard]] std::string typeOfName(std::string_view n) const {
        const Project* p = sc_.project;
        const std::string s(n);
        const auto cut = s.find_first_of(".[");
        const std::string root = s.substr(0, cut);
        if (const auto* l = local(root)) return cut == std::string::npos ? l->type : std::string{};
        if (counter(root)) return cut == std::string::npos ? std::string("INT") : std::string{};
        if (p && param(root)) return params::expressionType(*p, sc_.view, s);
        if (p && p->variable(root)) return types::typeOfPath(*p, s);
        if (p && (pub::isSysRoot(root) || pub::viewNamed(*p, root))) return pub::resolve(*p, s, pub::viewNamed(*p, root)).type;
        if (!sc_.plc.rootType || !sc_.plcKnown || !sc_.plcKnown(root)) return {};
        std::string type = sc_.plc.rootType(root);
        for (std::size_t k = cut; k != std::string::npos && k < s.size() && !type.empty();) {
            if (s[k] == '[') {
                const auto shape = project::members::parseArray(type);
                if (!shape.valid()) return {};
                type = shape.element;
                const auto close = s.find(']', k);
                k = close == std::string::npos ? close : close + 1;
                continue;
            }
            if (s[k] != '.') return {};
            const auto e = s.find_first_of(".[", k + 1);
            const std::string m = s.substr(k + 1, e == std::string::npos ? std::string::npos : e - k - 1);
            type = sc_.plc.memberType ? sc_.plc.memberType(type, m) : std::string{};
            k = e;
        }
        return type;
    }

    void scanDialect(std::string_view code) {
        const auto& t = toks_;
        const auto add = [&](const Tok& name, std::size_t from, std::size_t to) {
            dialectNames_.push_back({name.text, up(name.text), from, to});
        };
        // FUNCTION Nom (a, b : INT; c : REF_TO REAL) : Type ... END_FUNCTION
        for (std::size_t f = 0; f + 1 < t.size(); ++f) {
            if (!isWord(f, "FUNCTION") || t[f + 1].k != Tok::K::Ident) continue;
            std::size_t e = f + 2;
            while (!atEnd(e) && !isWord(e, "END_FUNCTION") && !isWord(e, "FUNCTION")) ++e;
            const std::size_t from = t[f].at, to = isWord(e, "END_FUNCTION") ? t[e].end() : (atEnd(e) ? body_.size() : t[e].at);
            internal_.push_back({t[f + 1].text, from, to});
            add(t[f + 1], from, to);                       // Nom := ... : la valeur rendue
            // Le type rendu : FUNCTION Nom [(...)] : Type
            std::size_t close = f + 1;                     // le nom, ou la ')' des parametres
            if (isOp(f + 2, "(")) {
                int d = 0;
                for (close = f + 2; !atEnd(close); ++close) {
                    if (isOp(close, "(")) ++d;
                    else if (isOp(close, ")") && --d == 0) break;
                }
            }
            if (isOp(close + 1, ":"))
                for (std::size_t k = close + 2, e2 = typeEnd(t, close + 2); k < e2; ++k)
                    if (t[k].k == Tok::K::Ident) typeWords_.insert(up(t[k].text));
            if (!isOp(f + 2, "(")) continue;
            int depth = 0;
            bool name = true;
            for (std::size_t k = f + 2; !atEnd(k); ++k) {
                if (isOp(k, "(") || isOp(k, "[")) { ++depth; continue; }
                if (isOp(k, ")") || isOp(k, "]")) { if (--depth == 0) break; continue; }
                if (depth != 1) continue;
                if (isOp(k, ";") || isOp(k, ",")) name = true;
                else if (isOp(k, ":") || isOp(k, ":=")) name = false;
                else if (name && t[k].k == Tok::K::Ident) add(t[k], from, to);
                else if (!name && t[k].k == Tok::K::Ident) typeWords_.insert(up(t[k].text));
            }
        }
        // FOR EACH cle, valeur IN m DO ... END_FOR (et FOR EACH x IN tableau)
        for (std::size_t f = 0; f + 2 < t.size(); ++f) {
            if (!isWord(f, "FOR") || !isWord(f + 1, "EACH")) continue;
            int depth = 0;
            std::size_t e = f;
            for (; !atEnd(e); ++e) {
                if (isWord(e, "FOR")) ++depth;
                else if (isWord(e, "END_FOR") && --depth == 0) break;
            }
            const std::size_t from = t[f].at, to = atEnd(e) ? body_.size() : t[e].end();
            for (std::size_t k = f + 2; !atEnd(k) && !isWord(k, "IN") && !isWord(k, "DO") && !isOp(k, ";"); ++k)
                if (t[k].k == Tok::K::Ident) add(t[k], from, to);
        }
        // Les declarations (le texte entier : VAR, VAR_TEMP et VAR_INPUT sont
        // blanchis dans le corps) : un nom que les variables locales n'ont pas.
        const auto all = lex(code);
        for (std::size_t v = 0; v < all.size(); ++v) {
            if (all[v].k != Tok::K::Ident || !varWord(up(all[v].text))) continue;
            std::size_t from = 0, to = body_.size();
            for (const auto& fn : internal_)
                if (all[v].at >= fn.from && all[v].at < fn.to) { from = fn.from; to = fn.to; }
            bool name = true, typed = false;
            std::size_t k = v + 1;
            for (; k < all.size() && all[k].k != Tok::K::End; ++k) {
                if (all[k].k == Tok::K::Ident && up(all[k].text) == "END_VAR") break;
                if (all[k].k == Tok::K::Op && all[k].text == ";") { name = true; continue; }
                if (all[k].k == Tok::K::Op && all[k].text == ":") { name = false; typed = true; continue; }
                if (all[k].k == Tok::K::Op && all[k].text == ":=") { name = false; typed = false; continue; }
                if (!name && typed && all[k].k == Tok::K::Ident) typeWords_.insert(up(all[k].text));
                if (!name || all[k].k != Tok::K::Ident) continue;
                const std::string U = up(all[k].text);
                if (U == "CONSTANT" || U == "RETAIN") continue;
                // integration 1.10 : avec S1, splitDeclarations garde aussi les locales d'un type
                // riche (MAP, POINTER TO, ARRAY a N dimensions, structure IHM) ; scriptNames
                // (Generer) les lit encore comme en 1.9 : elles restent des noms du dialecte.
                const LocalVar* l = local(all[k].text);
                if (l == nullptr || !localTypeSupported(l->type)) add(all[k], from, to);
            }
            v = k;
        }
#ifdef XPG_HMI_LANG110
        // Le chantier S1 : ses fonctions internes et ses noms (par lignes), ses constats.
        const auto lineFrom = [&](int line) {
            return line >= 1 && static_cast<std::size_t>(line) <= lineStarts_.size() ? lineStarts_[static_cast<std::size_t>(line) - 1] : 0;
        };
        const auto lineTo = [&](int line) {
            return line >= 1 && static_cast<std::size_t>(line) < lineStarts_.size() ? lineStarts_[static_cast<std::size_t>(line)] : body_.size();
        };
        const auto a = lang110::analyze(code, [this](std::string_view n) { return knownElsewhere(n); },
                                        [this](std::string_view n) { return typeOfName(n); },
                                        lang110::enumValuesOf(sc_.project), sc_.project);   // 1.10 (S1) : les enumerations
        for (const auto& fn : a.functions) {
            const std::size_t from = lineFrom(fn.firstLine), to = lineTo(fn.lastLine);
            internal_.push_back({fn.name, from, to});
            dialectNames_.push_back({fn.name, up(fn.name), from, to});
            for (const auto& pa : fn.params) dialectNames_.push_back({pa.name, up(pa.name), from, to});
            for (const auto& lo : fn.locals) dialectNames_.push_back({lo.name, up(lo.name), from, to});
        }
        for (const auto& nm : a.names) dialectNames_.push_back({nm.name, up(nm.name), lineFrom(nm.firstLine), lineTo(nm.lastLine)});
        for (const auto& f : a.findings) {
            Finding g;
            g.severity = f.error ? Finding::Severity::Error : Finding::Severity::Warning;
            g.line = f.line;
            g.column = f.column;
            g.length = f.length;
            g.message = f.message;
            g.fixLabel = f.fixLabel;          // 1.10 (integration I2) : "Ajouter les valeurs manquantes"
            g.fixLine = f.fixLine;
            g.fixText = f.fixText;
            dialect_.push_back(std::move(g));
        }
#endif
    }
    [[nodiscard]] bool dialectName(std::string_view name, std::size_t at) const {
        const std::string U = up(name);
        for (const auto& d : dialectNames_)
            if (d.upper == U && at >= d.from && at < d.to) return true;
        return false;
    }
    [[nodiscard]] bool internalFunction(std::string_view name) const {
        const std::string U = up(name);
        for (const auto& f : internal_) if (up(f.name) == U) return true;
        return false;
    }

    [[nodiscard]] bool isWord(std::size_t i, const char* w) const {
        return i < toks_.size() && toks_[i].k == Tok::K::Ident && up(toks_[i].text) == w;
    }
    [[nodiscard]] bool isOp(std::size_t i, const char* op) const {
        return i < toks_.size() && toks_[i].k == Tok::K::Op && toks_[i].text == op;
    }
    [[nodiscard]] bool atEnd(std::size_t i) const { return i >= toks_.size() || toks_[i].k == Tok::K::End; }
    // 1.10.4 : un nom a l'index i (borne) ; l'index du jeton de fin ; l'index
    // qui suit e sans jamais passer le jeton de fin.
    [[nodiscard]] bool isIdent(std::size_t i) const { return i < toks_.size() && toks_[i].k == Tok::K::Ident; }
    [[nodiscard]] std::size_t last() const { return toks_.empty() ? 0 : toks_.size() - 1; }
    [[nodiscard]] std::size_t after(std::size_t e) const { return atEnd(e) ? last() : e + 1; }

    void report(std::size_t from, std::size_t to, Finding::Severity sev, std::string msg, std::string name = {},
                std::string suggestion = {}) {
        Finding f;
        f.severity = sev;
        from = std::min(from, body_.size());     // 1.10.4 : borne
        to = std::max(from, std::min(to, body_.size()));
        const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), from);
        const std::size_t lineIndex = static_cast<std::size_t>(it - lineStarts_.begin()) - 1;
        f.line = static_cast<int>(lineIndex) + 1;
        f.column = static_cast<int>(from - lineStarts_[lineIndex]) + 1;
        // Sur la ligne seulement : un soulignement ne passe pas a la suivante.
        std::size_t lineEnd = body_.find('\n', from);
        if (lineEnd == std::string::npos) lineEnd = body_.size();
        f.length = static_cast<int>(std::max<std::size_t>(1, std::min(to, lineEnd) - from));
        f.message = std::move(msg);
        f.name = std::move(name);
        f.suggestion = std::move(suggestion);
        for (const auto& o : out_)
            if (o.line == f.line && o.column == f.column && o.message == f.message) return;
        out_.push_back(std::move(f));
    }
    void reportTok(std::size_t a, std::size_t b, Finding::Severity sev, std::string msg, std::string name = {}, std::string sug = {}) {
        if (atEnd(a)) return;
        const std::size_t lastTok = b > a ? b - 1 : a;
        report(toks_[a].at, toks_[std::min(lastTok, toks_.size() - 1)].end(), sev, std::move(msg), std::move(name), std::move(sug));
    }
    [[nodiscard]] std::string textOf(std::size_t a, std::size_t b) const {
        if (b <= a || atEnd(a)) return {};
        const std::size_t from = toks_[a].at, to = toks_[std::min(b - 1, toks_.size() - 1)].end();
        if (from >= body_.size() || to <= from) return {};      // 1.10.4 : borne
        return body_.substr(from, to - from);
    }

    // ------------------------------------------------------------ les noms --
    [[nodiscard]] const LocalVar* local(std::string_view name) const { return parts_.local(name); }
    [[nodiscard]] bool counter(std::string_view name) const { return counters_.count(up(name)) > 0; }
    [[nodiscard]] bool selfName(std::string_view name) const { return sc_.function && up(sc_.function->name) == up(name); }
    [[nodiscard]] const ViewParam* param(std::string_view name) const {
        if (!sc_.view) return nullptr;
        if (const auto* prm = sc_.view->param(name)) return prm;
        // 1.11.10 : une popup d'un symbole connait les parametres du symbole.
        if (const View* owner = sc_.project ? popupOwner(*sc_.project, *sc_.view) : nullptr) return owner->param(name);
        return nullptr;
    }

    [[nodiscard]] std::vector<std::string> namePool() const {
        std::vector<std::string> pool;
        for (const auto& l : parts_.locals) pool.push_back(l.name);
        for (std::size_t i = 0; i + 1 < toks_.size(); ++i)
            if (isWord(i, "FOR") && isIdent(i + 1) && !isWord(i + 1, "EACH")) pool.push_back(toks_[i + 1].text);
        for (const auto& d : dialectNames_) pool.push_back(d.name);     // 1.10 : le dialecte IHM
        for (const auto& c : sc_.candidates) pool.push_back(c);
        return pool;
    }
    [[nodiscard]] std::string closestName(std::string_view name) const {
        exprcheck::Context c;
        c.project = sc_.project;
        c.view = sc_.view;
        c.candidates = namePool();
        // 1.10 : pas lui-meme (une variable de FOR EACH hors de sa boucle).
        const std::string U = up(name);
        c.candidates.erase(std::remove_if(c.candidates.begin(), c.candidates.end(), [&](const std::string& n) { return up(n) == U; }),
                           c.candidates.end());
        return exprcheck::closest(c, name);
    }

    // La fin d'un chemin : nom (. nom | . chiffre | [ ... ])*.
    [[nodiscard]] std::size_t pathEnd(std::size_t i) const {
        std::size_t j = i + 1;
        for (;;) {
            if (isOp(j, ".") && j + 1 < toks_.size() && (toks_[j + 1].k == Tok::K::Ident || toks_[j + 1].k == Tok::K::Num)) { j += 2; continue; }
            if (isOp(j, "^")) { ++j; continue; }          // 1.10 (13 bis) : p^, p^.Membre, p^[i]
            if (isOp(j, "[")) {
                int depth = 0;
                std::size_t k = j;
                for (; !atEnd(k); ++k) {
                    if (isOp(k, "[")) ++depth;
                    else if (isOp(k, "]") && --depth == 0) break;
                }
                if (atEnd(k)) return k;
                j = k + 1;
                continue;
            }
            return j;
        }
    }

    // Les segments d'un chemin [i, j) : (index du jeton du membre, ou celui du '[').
    struct Seg { bool index{false}; std::size_t tok{0}; std::string member; int dims{1}; };
    [[nodiscard]] std::vector<Seg> segments(std::size_t i, std::size_t j) const {
        std::vector<Seg> segs;
        for (std::size_t k = i + 1; k < j;) {
            if (isOp(k, ".")) {
                if (k + 1 < toks_.size()) segs.push_back({false, k + 1, toks_[k + 1].text, 1});      // 1.10.4 : borne
                k += 2;
                continue;
            }
            if (isOp(k, "[")) {
                Seg s{true, k, {}, 1};
                int depth = 0;
                std::size_t m = k;
                for (; m < j; ++m) {
                    if (isOp(m, "[") || isOp(m, "(")) ++depth;
                    else if (isOp(m, "]") || isOp(m, ")")) { if (--depth == 0) break; }
                    else if (isOp(m, ",") && depth == 1) ++s.dims;
                }
                segs.push_back(s);
                k = m + 1;
                continue;
            }
            ++k;
        }
        return segs;
    }

    // Le chemin [i, j) : il existe, ses membres aussi. Rend son type ("" : inconnu).
    std::string path(std::size_t i, std::size_t j, bool write) {
        if (atEnd(i)) return {};                 // 1.10.4 : borne
        const Tok& root = toks_[i];
        const std::string R = up(root.text);
        const Project* p = sc_.project;
        const auto segs = segments(i, j);
        using S = Finding::Severity;
        // Une variable locale, un compteur, le nom de la fonction : elementaires.
        std::string type;
        std::string what;
        // 1.10 : une locale d'un type riche du dialecte (ARRAY, MAP, REF_TO...) : S1.
        if (const auto* l = local(root.text); l && !elementary(l->type)) return {};
        if (const auto* l = local(root.text)) { type = l->type; what = "variable locale"; }
        else if (counter(root.text)) { type = "INT"; what = "compteur de boucle"; }
        else if (selfName(root.text)) { type = sc_.function->returnType; what = "valeur de la fonction"; }
        if (!what.empty()) {
            if (write && local(root.text) && constants_.count(R))
                reportTok(i, i + 1, S::Error,
                          root.text + " est une constante (VAR CONSTANT) : un script ne peut pas l'\xC3\xA9" "crire ; d\xC3\xA9" "clare-la dans VAR "
                          "si elle doit changer");
            for (const auto& s : segs) {
                if (s.index) {
                    reportTok(i, j, S::Error, root.text + " n'est pas un tableau (" + what + " " + type + ") : retire l'indice");
                    return {};
                }
                if (!s.member.empty() && std::isdigit(static_cast<unsigned char>(s.member[0]))) { type = "BOOL"; continue; }   // un bit
                reportTok(i, j, S::Error, root.text + " est une " + what + " " + type + " : elle n'a pas de membre " + s.member);
                return {};
            }
            return type;
        }
        // 1.10 (decision 13) : un nom du dialecte IHM a sa place (parametre ou
        // variable d'une fonction interne, variable d'un FOR EACH, declaration
        // d'un type du dialecte), une fonction interne : son type, c'est S1.
        if (dialectName(root.text, root.at) || internalFunction(root.text)) return {};
        // Un parametre de la vue (popup, symbole) : son type declare.
        if (param(root.text)) {
            if (p) return params::expressionType(*p, sc_.view, textOf(i, j));
            return {};
        }
        // Une variable IHM : ses membres, ses indices (les structures, les tableaux).
        if (const Variable* var = p ? p->variable(root.text) : nullptr) {
            const std::string text = textOf(i, j);
            types::Spec spec;
            if (!segs.empty() && segs.front().index && (!types::parseSpec(var->type, spec) || !spec.array())) {
                reportTok(i, j, S::Error, root.text + " n'est pas un tableau (variable IHM " + var->type + ") : retire l'indice");
                return {};
            }
            if (!segs.empty() && !segs.front().index && elementary(var->type)) {
                const auto& m = segs.front().member;
                if (!(!m.empty() && std::isdigit(static_cast<unsigned char>(m[0]))) && !types::property(m)) {
                    reportTok(i, j, S::Error, root.text + " est une variable IHM " + var->type + " : elle n'a pas de membre " + m);
                    return {};
                }
            }
            for (const auto& pp : types::pathProblems(*p, write ? text + " := 0;" : text))
                reportTok(i, j, S::Error, pp.message);
            return types::typeOfPath(*p, text);
        }
        // SYS.X, Vue.Objet.Propriete : le chemin public (l'index le coupe).
        if (p && (pub::isSysRoot(root.text) || pub::viewNamed(*p, root.text))) {
            std::string dotted = root.text;
            std::size_t cut = i + 1;
            for (const auto& s : segs) {
                if (s.index) break;
                dotted += "." + s.member;
                cut = s.tok + 1;
            }
            const View* pv = pub::viewNamed(*p, root.text);
            std::optional<View> borrowed;
            if (pv && inherits(*p, *pv)) borrowed = compose(*p, *pv);
            const auto r = pub::resolve(*p, dotted, borrowed ? &*borrowed : pv);
            using W = pub::Resolved::What;
            if (r.what == W::Unknown) { reportTok(i, cut, S::Error, r.error); return {}; }
            if (r.what == W::Incomplete) {
                reportTok(i, cut, S::Error, dotted + " : chemin incomplet (" + std::string(pub::isSysRoot(root.text) ? "SYS.Variable" : "Vue.Objet.Propri\xC3\xA9t\xC3\xA9")
                                                + ")");
                return {};
            }
            if (write && r.access == pub::Access::Read) {
                if (r.what == W::Sys)
                    reportTok(i, cut, S::Error, dotted + " est une variable syst\xC3\xA8me en lecture seule : un script ne peut pas l'\xC3\xA9" "crire");
                else
                    reportTok(i, cut, S::Error, dotted + " est en lecture seule (" + std::string(pub::accessLabel(r.access))
                                                    + ") : un script ne peut pas l'\xC3\xA9" "crire");
            }
            return r.type;
        }
        // Une fonction IHM_ nommee sans parentheses : un appel mal ecrit, dit ailleurs.
        if (isHmiFunction(root.text)) return {};
        // 1.11.1 (API-M) : API.<globale>, API.<Unite>.<variable> (une variable IHM,
        // une vue nommee API sont vues plus haut) ; sans le modele, rien a dire.
        const bool apiMember = !segs.empty() && !segs.front().index && !segs.front().member.empty()
                            && (std::isalpha(static_cast<unsigned char>(segs.front().member[0])) || segs.front().member[0] == '_');
        if (R == "API" && apiMember) return apiPath(i, j, write);
        // 1.11.1 (remarque d'API-V) : API seul, ou API. en cours de frappe, n'est pas
        // un nom inconnu (« API n'existe pas ») : le chemin est incomplet.
        if (R == "API") {
            if (sc_.plc.api)
                reportTok(i, i + 1, S::Error,
                          "API : chemin incomplet \xE2\x80\x94 \xC3\xA9" "cris API.<globale> ou API.<Unit\xC3\xA9>.<variable> "
                          "(l'aide \xC3\xA0 la saisie les propose apr\xC3\xA8s API.)");
            return {};
        }
        // L'automate : ses membres et ses indices constants.
        if (sc_.plcKnown && sc_.plcKnown(root.text)) return plcPath(i, j, segs);
        if (!sc_.plcKnown) return {};           // l'automate n'est pas connu : rien a dire
        // Personne ne connait ce nom.
        const bool marker = markerRoots.count(up(root.text)) != 0;   // 1.11.1 (REP-10)
        const std::string near = marker ? std::string{} : closestName(root.text);
        std::string msg = root.text + " n'existe pas";
        if (!near.empty()) msg += " : veux-tu dire " + near + " ?";
        else if (marker) msg += " (ni variable IHM, ni variable de l'automate, ni variable locale)";
        else msg += " (ni variable IHM, ni variable de l'automate, ni variable locale) : corrige le nom ou d\xC3\xA9" "clare-la (VAR ... END_VAR)";
        reportTok(i, i + 1, S::Error, std::move(msg), root.text, near);
        (void)R;
        return {};
    }

    // 1.11.1 (API-M) : un chemin API.... : il existe (sinon ce qui manque, et la
    // piste) ; ecrit, il faut que l'IHM puisse l'ecrire (une constante, une
    // variable en lecture seule : non, avec la raison) ; sans adresse quand le
    // projet parle a l'automate reel : un avertissement.
    std::string apiPath(std::size_t i, std::size_t j, bool write) {
        using S = Finding::Severity;
        if (!sc_.plc.api) return {};
        const auto r = sc_.plc.api->resolve(textOf(i, j));
        if (!r.ok) {
            reportTok(i, j, S::Error, r.error, r.missing, r.suggestion);
            return {};
        }
        if (write && r.constant)
            reportTok(i, j, S::Error, r.path + " est une constante de l'automate : un script ne peut pas l'\xC3\xA9" "crire");
        else if (write && r.access == apivars::Access::ReadOnly)
            reportTok(i, j, S::Error, r.path + " est en lecture seule (" + r.accessWhy + ") : l'IHM ne peut pas l'\xC3\xA9" "crire");
        else if (r.access == apivars::Access::NoAddress && sc_.plc.api->realPlc()) {
            // 1.11.1 (R1111-9, decision 122) : ce que devient une ecriture, ou une
            // lecture, sur la liaison ; et le remede (pas de melange simule / reel).
            static const std::string kAddress =
                "donne-lui une adresse dans le programme de l'automate (Control Expert, puis Fichier \xE2\x80\xBA Importer) "
                "ou une ligne dans Configuration \xE2\x80\xBA Communication";
            if (write)
                reportTok(i, j, S::Warning,
                          r.path + " n'a pas d'adresse : l'\xC3\xA9" "criture ne part pas vers l'automate (simulation seulement)"
                                   " - pour qu'elle parte : " + kAddress);
            else
                reportTok(i, j, S::Warning,
                          r.path + " n'a pas d'adresse : lue en simulation seulement (la liaison Modbus ne la lit pas)"
                                   " - pour la voir calcul\xC3\xA9" "e : passe la Communication en Simulateur ; pour la lire sur l'automate : " + kAddress);
        }
        return r.type;
    }

    std::string plcPath(std::size_t i, std::size_t j, const std::vector<Seg>& segs) {
        (void)j;
        if (!sc_.plc.rootType) return {};
        std::string type = sc_.plc.rootType(toks_[i].text);
        std::string where = toks_[i].text;
        for (const auto& s : segs) {
            if (type.empty()) return {};
            if (s.index) {
                const auto shape = project::members::parseArray(type);
                if (!shape.valid()) {
                    if (elementary(type)) {
                        reportTok(i, s.tok + 1, Finding::Severity::Error,
                                  where + " n'est pas un tableau (" + type + " de l'automate) : retire l'indice");
                    }
                    return {};
                }
                type = shape.element;
                where += "[...]";
                continue;
            }
            if (!s.member.empty() && std::isdigit(static_cast<unsigned char>(s.member[0]))) return {};   // un bit
            const std::string mt = sc_.plc.memberType ? sc_.plc.memberType(type, s.member) : std::string{};
            if (mt.empty()) {
                if (elementary(type))
                    reportTok(s.tok, s.tok + 1, Finding::Severity::Error,
                              where + " est un " + type + " de l'automate : il n'a pas de membre " + s.member);
                else if (sc_.plc.isStruct && sc_.plc.isStruct(type))
                    reportTok(s.tok, s.tok + 1, Finding::Severity::Error,
                              "le type " + type + " de l'automate n'a pas de membre " + s.member + " (" + where + "." + s.member + ")");
                return {};
            }
            type = mt;
            where += "." + s.member;
        }
        return type;
    }

    // ------------------------------------------------------- les appels -----
    // [i] : le nom, [i+1] : '('. Rend l'index apres la ')'.
    // 1.11.10 : le symbole dont le code est controle (le symbole lui-meme, ou celui qui
    // porte la popup) - ses fonctions s'appellent par leur nom : Ouvrir().
    const View* symbolHere() const {
        if (!sc_.view || !sc_.project) return nullptr;
        if (isSymbolView(*sc_.view)) return sc_.view;
        if (sc_.view->ownerSymbol != kNoId)
            if (const View* o = sc_.project->view(sc_.view->ownerSymbol); o && isSymbolView(*o)) return o;
        return nullptr;
    }
    // 1.11.10 : [i, j) est-il un appel de fonction d'instance (Vanne_3.Ouvrir, SUPER.Ouvrir,
    // Vue_Vannes.Vanne_3.Ouvrir) ? Rend la fonction ; nul : non.
    const HmiFunction* instanceFunction(std::size_t i, std::size_t j) const {
        if (!sc_.project || j <= i + 1) return nullptr;
        std::vector<std::string> segs;
        for (std::size_t k = i; k < j; ++k) {
            if (toks_[k].k == Tok::K::Ident) segs.push_back(toks_[k].text);
            else if (!isOp(k, ".")) return nullptr;
        }
        if (segs.size() < 2) return nullptr;
        const Project& p = *sc_.project;
        // SUPER.Ouvrir, Ouvrir d'une instance posee : depuis le symbole (ou la vue) controle.
        const View* here = symbolHere() ? symbolHere() : sc_.view;
        if (here) {
            if (segs.size() == 2 && up(segs[0]) == kSuperName && symbolHere()) return symbolFunction(*symbolHere(), segs[1]);
            const View* sym = nullptr;
            for (const auto& o : here->objects)
                if (o.kind == hmi::Kind::SymbolInstance && up(o.name) == up(segs[0])) sym = symbolOf(p, o);
            for (std::size_t k = 1; sym && k < segs.size(); ++k) {
                if (k + 1 == segs.size()) return symbolFunction(*sym, segs[k]);
                const View* next = nullptr;
                for (const auto& o : sym->objects)
                    if (o.kind == hmi::Kind::SymbolInstance && up(o.name) == up(segs[k])) next = symbolOf(p, o);
                sym = next;
            }
        }
        std::string dotted;
        for (const auto& sg : segs) dotted += (dotted.empty() ? "" : ".") + sg;
        if (BoundFunction b; boundSymbolFunction(p, dotted, b))
            if (const View* sv = p.viewByName(b.symbol)) return symbolFunction(*sv, b.function.name);
        return nullptr;
    }
    const HmiFunction* pendingSymbolFn_{nullptr};
    // Un appel de fonction d'instance : ses arguments controles comme ceux d'une fonction IHM.
    std::size_t instanceCall(std::size_t i, std::size_t j, const HmiFunction& f, bool statement) {
        (void)i;
        pendingSymbolFn_ = &f;
        const std::size_t after = call(j - 1, statement);
        pendingSymbolFn_ = nullptr;
        return after;
    }

    std::size_t call(std::size_t i, bool statement) {
        if (atEnd(i)) return last();             // 1.10.4 : borne
        const Tok& name = toks_[i];
        const std::string U = up(name.text);
        // 1.11.10 : une fonction du symbole, par son nom (dans le symbole, ses popups).
        const HmiFunction* ownFn = nullptr;
        if (!pendingSymbolFn_)
            if (const View* sv = symbolHere()) ownFn = symbolFunction(*sv, name.text);
        if (ownFn) pendingSymbolFn_ = ownFn;
        struct Reset { const HmiFunction*& p; bool on; ~Reset() { if (on) p = nullptr; } } reset{pendingSymbolFn_, ownFn != nullptr};
        std::size_t k = i + 2;
        int n = 0, depth = 1;
        bool named = false, any = false;
        std::size_t argStart = k;
        for (; !atEnd(k); ++k) {
            if (isOp(k, "(") || isOp(k, "[")) ++depth;
            else if (isOp(k, ")") || isOp(k, "]")) {
                if (--depth == 0) break;
            } else if (isOp(k, ",") && depth == 1) {
                ++n;
                argStart = k + 1;
                continue;
            }
            if (depth == 1 && k == argStart && toks_[k].k == Tok::K::Ident && (isOp(k + 1, ":=") || isOp(k + 1, "=>"))) named = true;
            any = true;
        }
        if (any) ++n;
        const std::size_t close = k;
        using S = Finding::Severity;
        const auto arity = [&](int min, int max) {
            if (n < min || (max >= 0 && n > max))
                reportTok(i, i + 1, S::Error, name.text + " prend " + arityText(min, max) + ", pas " + std::to_string(n));
        };
        bool found = false;
        if (internalFunction(name.text) || dialectFunctions().count(U)) {
            found = true;      // 1.10 (13) : une fonction interne du script, une fonction du dialecte : S1 les controle
        } else if (conversion(U)) {
            found = true;      // 1.10 (integration I2, S2) : TO_STRING(v), TO_T_RESUME(v), TO_T_MODE('auto') - un argument
            arity(1, 1);
#ifdef XPG_HMI_LANG110
        } else if (lang110::isBuiltin(U)) {
            found = true;      // 1.10 (S1) : TO_UPPER, TO_LOWER... du dialecte (S1 les controle)
#endif
        } else if (isStandardFunction(U)) {
            found = true;
            const auto to = U.find("_TO_");
            if (to != std::string::npos && to > 0) arity(1, 1);
            for (const auto& a : kStandard) if (U == a.name) arity(a.min, a.max);
        } else if (isHmiFunction(U)) {
            found = true;
            for (const auto& a : kHmi) if (U == a.name) arity(a.min, a.max);
        } else if (const auto* f = pendingSymbolFn_ ? pendingSymbolFn_ : sc_.project ? sc_.project->functionByName(name.text) : nullptr) {
            found = true;
            // Une entree qui a une valeur initiale (Poids : REAL := 0.5) est facultative.
#ifdef XPG_HMI_LANG110
            // le dialecte IHM (S1) : entrees riches ; `parts` garde les locales que inputs() designe
            const auto parts = splitDeclarations(f->body, true, [p = sc_.project](std::string_view t) {
                return lang110::knownHmiType(p, t);          // 1.10 (S1) : structure ou enumeration
            });
#else
            const auto parts = splitDeclarations(f->body, true);
#endif
            const auto inputs = parts.inputs();
            int required = 0;
            for (const auto* in : inputs) required += in->initial.empty() ? 1 : 0;
            if (!named) arity(required, static_cast<int>(inputs.size()));
            if (!statement && f->returnType.empty())
                reportTok(i, i + 1, S::Error, name.text + " ne rend pas de valeur : appelle-la seule sur sa ligne (" + name.text + "(...);)");
        } else if (local(name.text) || (sc_.project && sc_.project->variable(name.text)) || (sc_.plcKnown && sc_.plcKnown(name.text))
                   || !sc_.plcKnown) {
            found = true;      // une instance de bloc (TON, un bloc de l'automate) ; l'automate inconnu : rien a dire
        }
        if (!found) {
            std::vector<std::string> names;
            for (const auto& a : kStandard) names.emplace_back(a.name);
            for (const auto& a : kHmi) names.emplace_back(a.name);
            for (const char* conv : {"INT_TO_REAL", "REAL_TO_INT", "INT_TO_STRING", "REAL_TO_STRING", "BOOL_TO_INT", "STRING_TO_REAL",
                                     "STRING_TO_INT"})
                names.emplace_back(conv);
            if (sc_.project) for (const auto& f : sc_.project->programs.functions) names.push_back(f.name);
            for (const auto& f : internal_) names.push_back(f.name);      // 1.10 : les fonctions internes du script
            exprcheck::Context c;
            c.candidates = std::move(names);
            const std::string near = exprcheck::closest(c, name.text);
            reportTok(i, i + 1, S::Error,
                      "fonction inconnue : " + name.text + (near.empty() ? std::string(" (ni fonction standard, ni IHM_..., ni fonction IHM du projet)")
                                                                          : " : veux-tu dire " + near + " ?"),
                      name.text, near);
        }
        // Les arguments : leurs noms (un argument nomme : IN := x, Q => y).
        walkNames(i + 2, close);
        return atEnd(close) ? close : close + 1;
    }

    // Les noms d'une plage [a, b) : chaque racine, chaque appel.
    void walkNames(std::size_t a, std::size_t b) {
        for (std::size_t k = a; k < b && !atEnd(k);) {
            const Tok& t = toks_[k];
            if (t.k != Tok::K::Ident) { ++k; continue; }
            if (k > 0 && isOp(k - 1, ".")) { ++k; continue; }                         // un membre
            const std::string U = up(t.text);
            if (operatorWords().count(U) || statementWords().count(U)) { ++k; continue; }
            if (isOp(k + 1, ":=") || isOp(k + 1, "=>")) {                             // un argument nomme
                if (k > 0 && (isOp(k - 1, "(") || isOp(k - 1, ","))) { ++k; continue; }
            }
            if (isOp(k + 1, "(")) {
                const std::size_t after = call(k, false);
                k = std::max(after, k + 1);
                continue;
            }
            const std::size_t e = std::min(pathEnd(k), b);
            if (isOp(e, "("))                                   // 1.11.10 : x := Vanne_3.Etat() + 1
                if (const HmiFunction* f = instanceFunction(k, e)) {
                    k = std::max(instanceCall(k, e, *f, false), k + 1);
                    continue;
                }
            (void)path(k, e, false);
            ++k;            // les indices sont relus (leurs noms)
        }
    }

    // ------------------------------------------------------- les types ------
    [[nodiscard]] exprcheck::Context typeContext() const {
        exprcheck::Context c;
        c.project = sc_.project;
        c.view = sc_.view;
        c.known = [](std::string_view) { return true; };    // les noms : dits ici, a leur place
        return c;
    }
    // Les problemes de type d'une expression [a, b) (pas les noms, membres ou
    // appels : dits a leur place).
    std::vector<std::string> typeProblems(std::size_t a, std::size_t b, exprcheck::Want want) const {
        std::vector<std::string> out;
        const std::string text = textOf(a, b);
        if (text.empty() || !Expression::compile(text).valid()) return out;
        std::set<std::string> members;
        if (sc_.project) for (const auto& pp : types::pathProblems(*sc_.project, text)) members.insert(pp.message);
        for (const auto& pb : exprcheck::check(typeContext(), text, want)) {
            const auto& m = pb.message;
            if (!pb.unknownName.empty() || members.count(m) || m.rfind("fonction inconnue", 0) == 0 || m.find(" prend ") != std::string::npos
                || m.find("ne rend pas de valeur") != std::string::npos || m.find("hors des bornes") != std::string::npos
                || m.find("n'a pas de membre") != std::string::npos)
                continue;
            out.push_back(m);
        }
        return out;
    }

    void condition(std::size_t a, std::size_t b, const std::string& word) {
        walkNames(a, b);
        for (auto& m : typeProblems(a, b, exprcheck::Want::Bool)) {
            const bool expected = m.find("est attendu") != std::string::npos;
            reportTok(a, b, expected ? Finding::Severity::Error : Finding::Severity::Warning, word + " : " + m);
        }
    }

    void assignment(std::size_t i, std::size_t j, std::size_t e) {
        if (atEnd(i)) return;                    // 1.10.4 : borne
        const std::string target = textOf(i, j);
        const std::string type = path(i, j, true);
        // Une variable IHM en lecture seule.
        if (const Variable* var = sc_.project && !dialectName(toks_[i].text, toks_[i].at) ? sc_.project->variable(toks_[i].text) : nullptr;
            var && var->readOnly)
            reportTok(i, j, Finding::Severity::Error,
                      "la variable IHM " + var->name + " est en lecture seule : un script ne peut pas l'\xC3\xA9" "crire ; d\xC3\xA9" "coche "
                      "\xC2\xAB Lecture seule \xC2\xBB dans Variables IHM, ou \xC3\xA9" "cris une autre variable");
        walkNames(j + 1, e);
        if (e <= j + 1) return;
        const Kind kind = kindOf(type);
        using W = exprcheck::Want;
        const W want = kind == Kind::Bool ? W::Bool : kind == Kind::Num ? W::Number : W::Any;
        for (auto& m : typeProblems(j + 1, e, want)) {
            if (m.rfind("un nombre est attendu", 0) == 0 || m.rfind("un bool\xC3\xA9" "en est attendu", 0) == 0) {
                const std::string given = textOf(j + 1, e);
                reportTok(j + 1, e, Finding::Severity::Error,
                          target + " est un " + up(type) + " : tu lui affectes " + (m.find("une dur") != std::string::npos ? "une dur\xC3\xA9" "e" : "un texte")
                              + " (" + given + ")" + (kind == Kind::Num ? " ; convertis-le (STRING_TO_REAL) ou \xC3\xA9" "cris un nombre sans apostrophes"
                                                                        : " ; compare-le (" + given + " = 'Marche') ou \xC3\xA9" "cris TRUE ou FALSE"));
                continue;
            }
            reportTok(j + 1, e, Finding::Severity::Warning, m);
        }
        // Un nombre ou un booleen dans un texte : evident quand c'est un litteral seul.
        if (kind == Kind::Text && e == j + 2 && !atEnd(j + 1)) {
            const Tok& v = toks_[j + 1];
            const bool boolean = v.k == Tok::K::Ident && (up(v.text) == "TRUE" || up(v.text) == "FALSE");
            if (v.k == Tok::K::Num || boolean)
                reportTok(j + 1, e, Finding::Severity::Error,
                          target + " est un STRING : tu lui affectes " + (boolean ? "un bool\xC3\xA9" "en" : "un nombre") + " (" + v.text
                              + ") ; \xC3\xA9" "cris '" + v.text + "' ou convertis (" + (boolean ? "BOOL_TO_STRING" : "INT_TO_STRING") + ")");
        }
    }

    // ----------------------------------------------------- les instructions -
    // La fin d'une instruction : le ';' (hors parentheses), ou le mot qui en
    // commence une autre (un ';' oublie : la syntaxe le dit).
    [[nodiscard]] std::size_t statementEnd(std::size_t k) const {
        int depth = 0;
        for (; !atEnd(k); ++k) {
            if (isOp(k, "(") || isOp(k, "[")) ++depth;
            else if (isOp(k, ")") || isOp(k, "]")) depth = std::max(0, depth - 1);
            else if (depth == 0 && isOp(k, ";")) return k;
            else if (depth == 0 && toks_[k].k == Tok::K::Ident) {
                const std::string U = up(toks_[k].text);
                if (U == "END_IF" || U == "ELSE" || U == "ELSIF" || U == "END_FOR" || U == "END_WHILE" || U == "END_CASE"
                    || U == "END_REPEAT" || U == "UNTIL" || U == "FUNCTION" || U == "END_FUNCTION")
                    return k;
            }
        }
        return k;
    }
    [[nodiscard]] std::size_t findWord(std::size_t k, std::initializer_list<const char*> words) const {
        int depth = 0;
        for (; !atEnd(k); ++k) {
            if (isOp(k, "(") || isOp(k, "[")) ++depth;
            else if (isOp(k, ")") || isOp(k, "]")) depth = std::max(0, depth - 1);
            else if (depth == 0) {
                for (const char* w : words) if (isWord(k, w)) return k;
                if (isOp(k, ";")) return k;
            }
        }
        return k;
    }

    void statement(std::size_t& i) {
        if (atEnd(i)) { i = last(); return; }      // 1.10.4 : jamais hors du tableau
        const Tok& t = toks_[i];
        if (t.k == Tok::K::Op) {
            if (t.text == ";") { ++i; return; }
            const std::size_t e = statementEnd(i);
            walkNames(i, e);
            i = e;
            return;
        }
        if (t.k != Tok::K::Ident) {
            // Une etiquette de CASE : 1:, 1, 2:, 1..5:, -1:
            std::size_t k = i;
            while (!atEnd(k) && (toks_[k].k == Tok::K::Num || isOp(k, ",") || isOp(k, "..") || isOp(k, "-") || toks_[k].k == Tok::K::Lit))
                ++k;
            if (isOp(k, ":")) { i = after(k); return; }
            const std::size_t e = statementEnd(i);
            walkNames(i, e);
            i = e;
            return;
        }
        const std::string U = up(t.text);
        if (U == "IF" || U == "ELSIF" || U == "WHILE") {
            const std::size_t e = findWord(i + 1, {U == "WHILE" ? "DO" : "THEN"});
            condition(i + 1, e, U);
            i = isOp(e, ";") ? e : after(e);
            return;
        }
        if (U == "UNTIL") {
            const std::size_t e = findWord(i + 1, {"END_REPEAT"});
            condition(i + 1, e, U);
            i = e;
            return;
        }
        if (U == "CASE") {
            const std::size_t e = findWord(i + 1, {"OF"});
            walkNames(i + 1, e);
            i = isOp(e, ";") ? e : after(e);
            return;
        }
        // 1.10 (decision 13) : l'en-tete d'une fonction interne - FUNCTION Nom
        // (params) : Type ; son corps est lu ensuite comme le reste du script.
        if (U == "FUNCTION") {
            std::size_t k = i + 1;
            if (isIdent(k)) ++k;
            if (isOp(k, "(")) {
                int depth = 0;
                for (; !atEnd(k); ++k) {
                    if (isOp(k, "(")) ++depth;
                    else if (isOp(k, ")") && --depth == 0) { ++k; break; }
                }
            }
            if (isOp(k, ":")) k = typeEnd(toks_, k + 1);
            i = std::min(k, last());
            return;
        }
        // Un bloc de declarations reste dans le corps (VAR_IN_OUT, VAR_OUTPUT...).
        if (varWord(U)) {
            std::size_t k = i + 1;
            while (!atEnd(k) && !isWord(k, "END_VAR")) ++k;
            i = after(k);
            return;
        }
        if (U == "FOR" && isWord(i + 1, "EACH")) {
            // 1.10 (decision 13) : FOR EACH cle, valeur IN m DO - ses variables
            // sont connues dans la boucle ; m est lu.
            const std::size_t in = findWord(i + 2, {"IN", "DO"});
            const std::size_t d = isWord(in, "IN") ? findWord(in + 1, {"DO"}) : in;
            if (isWord(in, "IN")) walkNames(in + 1, d);
            i = isOp(d, ";") ? d : after(d);
            return;
        }
        if (U == "FOR") {
            // 1.10.4 (le plantage de la 1.10.3) : un FOR en fin de texte - TO, BY
            // et DO absents - menait i deux jetons apres la fin.
            std::size_t k = i + 1;
            if (isIdent(k)) ++k;                            // le compteur : connu
            if (isOp(k, ":=")) ++k;
            const std::size_t to = findWord(k, {"TO"});
            walkNames(k, to);
            const std::size_t by = findWord(after(to), {"BY", "DO"});
            walkNames(to + 1, by);
            std::size_t d = by;
            if (isWord(by, "BY")) {
                d = findWord(by + 1, {"DO"});
                walkNames(by + 1, d);
            }
            i = isOp(d, ";") ? d : after(d);
            return;
        }
        if (statementWords().count(U)) { ++i; return; }
        if (operatorWords().count(U)) {                       // NOT x; TRUE; : pas une cible
            const std::size_t e = statementEnd(i);
            walkNames(i, e);
            i = e;
            return;
        }
        // Une etiquette de CASE nommee (une constante) : Arret:
        if (isOp(i + 1, ":") ) { i = after(i + 1); return; }
        const std::size_t j = pathEnd(i);
        if (isOp(j, ":=")) {
            const std::size_t e = statementEnd(j + 1);
            assignment(i, j, e);
            i = e;
            return;
        }
        if (j == i + 1 && isOp(j, "(")) {
            const std::size_t after = call(i, true);
            const std::size_t e = statementEnd(after);
            walkNames(after, e);
            i = e;
            return;
        }
        if (isOp(j, "("))                                       // 1.11.10 : Vanne_3.Ouvrir(...);
            if (const HmiFunction* f = instanceFunction(i, j)) {
                const std::size_t after = instanceCall(i, j, *f, true);
                const std::size_t e = statementEnd(after);
                walkNames(after, e);
                i = e;
                return;
            }
        const std::size_t e = statementEnd(i);
        walkNames(i, e);
        i = e;
    }
};

} // namespace

namespace {
// 1.11.1 (REP) : les constats d'un code lu sans les $ de ses reperes, rendus aux
// colonnes du texte d'origine (les lignes n'ont pas bouge).
void restoreColumns(std::vector<Finding>& out, const markers::Stripped& st, std::string_view source) {
    if (st.removed.empty()) return;
    for (auto& f : out) {
        if (f.line < 1 || f.column < 1) continue;
        const int first = st.column(source, f.line, f.column);
        if (f.length > 0) f.length = st.column(source, f.line, f.column + f.length - 1) - first + 1;
        f.column = first;
    }
}
} // namespace

std::vector<Finding> check(const Scope& scope, std::string_view source) {
    if (source.find_first_not_of(" \t\r\n") == std::string_view::npos) return {};
    // 1.11.1 (REP) : le code se lit comme une expression, sans les $ de ses reperes.
    const auto st = markers::stripKeep(source);
    const std::string_view code = st.text;
    Walker w(scope, code);
    for (const auto& m : markers::find(source))   // 1.11.1 (REP-10)
        if (const auto roots = scanRoots(m.content(source)); !roots.empty()) w.markerRoots.insert(up(roots.front()));
    auto out = w.run();
    // 1.10 (decisions 13 et 13 bis) : les constructions du dialecte IHM,
    // controlees par le chantier S1 (lang110::analyze) - une seule fois
    // chaque constat, dans l'ordre du texte.
    bool added = false;
    for (auto f : w.dialect()) {
        bool twice = false;
        for (const auto& o : out) twice = twice || (o.line == f.line && o.column == f.column && o.message == f.message);
        if (twice) continue;
        out.push_back(std::move(f));
        added = true;
    }
    if (added)
        std::stable_sort(out.begin(), out.end(), [](const Finding& a, const Finding& b) {
            return a.line != b.line ? a.line < b.line : a.column < b.column;
        });
    restoreColumns(out, st, source);
    return out;
}

std::vector<Finding> dialectFindings(const Scope& scope, std::string_view source) {
    if (source.find_first_not_of(" \t\r\n") == std::string_view::npos) return {};
    const auto st = markers::stripKeep(source);   // 1.11.1 (REP)
    auto out = Walker(scope, st.text).dialect();
    restoreColumns(out, st, source);
    return out;
}

std::set<std::string> dialectDeclared(std::string_view source) {
    if (source.find_first_not_of(" \t\r\n") == std::string_view::npos) return {};
    const std::string code = markers::strip(source);   // 1.11.1 (REP)
    const Scope none{};                      // les noms seulement : ni projet, ni automate
    auto out = Walker(none, code).declared();
    if (out.empty() && code.find('^') == std::string_view::npos) {
        // Pas de construction du dialecte : ses mots ne sont pas des noms a taire,
        // sauf s'ils sont employes (MAP_SIZE(...), NULL...).
        const std::string U = up(code);
        bool used = false;
        for (const auto& f : dialectFunctions()) used = used || U.find(f) != std::string::npos;
        if (!used && U.find("NULL") == std::string::npos) return out;
    }
    for (const char* w : {"EACH", "IN", "FUNCTION", "END_FUNCTION", "NULL", "REF_TO", "POINTER", "REFERENCE", "TO", "MAP", "ARRAY", "OF",
                          "VAR", "VAR_TEMP", "VAR_INPUT", "VAR_OUTPUT", "VAR_IN_OUT", "END_VAR", "CONSTANT", "RETAIN", "BOOL", "INT",
                          "DINT", "UINT", "UDINT", "SINT", "USINT", "LINT", "ULINT", "WORD", "DWORD", "BYTE", "LWORD", "REAL", "LREAL",
                          "STRING", "WSTRING", "TIME"})
        out.insert(w);
    for (const auto& f : dialectFunctions()) out.insert(f);
    return out;
}

bool applyFix(const Finding& f, std::string& code) {
    if (f.fixLine <= 0 || f.fixText.empty()) return false;
    const bool crlf = code.find("\r\n") != std::string::npos;
    std::string text;
    for (std::size_t k = 0; k < f.fixText.size(); ++k) {
        if (crlf && f.fixText[k] == '\n' && (k == 0 || f.fixText[k - 1] != '\r')) text += '\r';
        text += f.fixText[k];
    }
    std::size_t at = 0;
    for (int line = 1; line < f.fixLine && at < code.size(); ++line) {
        const auto nl = code.find('\n', at);
        at = nl == std::string::npos ? code.size() : nl + 1;
    }
    if (at >= code.size()) {
        at = code.size();
        if (!code.empty() && code.back() != '\n') {      // la derniere ligne n'a pas de fin : la correction va dessous
            code += crlf ? "\r\n" : "\n";
            at = code.size();
        }
    }
    code.insert(at, text);
    return true;
}

namespace {
std::string actionWhere(std::size_t index, const Action& a) {
    return "action " + std::to_string(index + 1) + " (" + std::string(triggerLabel(a.trigger)) + ")";
}
} // namespace

std::vector<Issue> projectIssues(const Project& p, const NameExists& plcHasName, const exprcheck::PlcPaths& plcPaths,
                                 const CompileFocus* focus) {
    std::vector<Issue> out;
    const auto scopeFor = [&](const View* v, const HmiFunction* f) {
        Scope s;
        s.project = &p;
        s.view = v;
        s.function = f;
        s.plcKnown = plcHasName;
        s.plc = plcPaths;
        return s;
    };
    const auto severity = [](Finding::Severity s) {
        return s == Finding::Severity::Error ? Issue::Severity::Error : Issue::Severity::Warning;
    };
    const auto fill = [&](Issue i, const Finding& f) {
        i.severity = severity(f.severity);
        i.message = f.message;
        i.line = f.line;
        i.column = f.column;
        i.length = f.length;
        out.push_back(std::move(i));
    };
    for (const auto& sc : p.programs.scripts) {
        if (sc.lang != ScriptLang::ST || (focus && !focus->scripts.count(sc.id))) continue;
        for (const auto& f : check(scopeFor(nullptr, nullptr), sc.body)) {
            Issue i;
            i.category = "Script";
            i.property = sc.name;
            i.script = sc.id;
            fill(std::move(i), f);
        }
    }
    for (const auto& v : p.views) {
        for (const auto& sc : v.scripts) {
            if (sc.lang != ScriptLang::ST || (focus && !focus->scripts.count(sc.id))) continue;
            for (const auto& f : check(scopeFor(&v, nullptr), sc.body)) {
                Issue i;
                i.category = "Script";
                i.view = v.id;
                i.property = sc.name;
                i.script = sc.id;
                fill(std::move(i), f);
            }
        }
        const auto actions = [&](const Object* o, const std::vector<Action>& list) {
            for (std::size_t k = 0; k < list.size(); ++k) {
                const auto& a = list[k];
                if (a.operation != Operation::RunScript) continue;
                for (const auto& f : check(scopeFor(&v, nullptr), a.value)) {
                    Issue i;
                    i.category = "Action";
                    i.view = v.id;
                    i.object = o ? o->id : kNoId;
                    i.property = actionWhere(k, a);
                    Finding g = f;
                    g.message = "ligne " + std::to_string(f.line) + ", colonne " + std::to_string(f.column) + " : " + f.message;
                    // 1.11.1 (REP) : l'ancien $Vanne$ (pas une variable) du script d'une action - la phrase
                    // de Dupliquer, a laquelle "Remplacer..." de Compiler s'accroche (le script d'une action
                    // se duplique avec son objet ; ceux du projet et des vues, non : pas de phrase).
                    g.message += dup::markerHint(a.value, f.name);
                    fill(std::move(i), g);
                }
            }
        };
        if (focus && !focus->act(v.id)) continue;
        actions(nullptr, v.actions);
        for (const auto& o : v.objects) actions(&o, o.actions);
    }
    for (const auto& fn : p.programs.functions) {
        if (focus && !focus->functions.count(fn.id)) continue;
        for (const auto& f : check(scopeFor(nullptr, &fn), fn.body)) {
            Issue i;
            i.category = "Fonction";
            i.property = fn.name;
            i.item = fn.id;
            fill(std::move(i), f);
        }
    }
    return out;
}

} // namespace hmi::scriptcheck
