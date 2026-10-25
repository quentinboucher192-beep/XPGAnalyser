// =============================================================================
//  hmi/HmiScriptCheck.cpp - 1.10 : les erreurs d'un script ST, a leur place
//  (voir HmiScriptCheck.hpp). Les jetons gardent leur place dans le texte ;
//  les instructions sont suivies juste assez pour savoir ce qui est ecrit (la
//  cible d'un :=), ce qui doit etre un booleen (IF, ELSIF, WHILE, UNTIL) et ce
//  qui est appele. Les types et les divisions passent par exprcheck.
// =============================================================================
#include "HmiScriptCheck.hpp"
#include "HmiCalc.hpp"   // 1.12.7 : les constantes qualifiees (Reglages.Seuils)
#include "../core/Edition.hpp"   // 1.12.0 : IHM Studio - pas d'espace API.
#include "HmiNatives.hpp"   // 1.12.0 : les couleurs, l'aleatoire
#include "HmiSymbols.hpp"   // 1.11.10 : les fonctions des symboles
#include "HmiApiVars.hpp"   // 1.11.1 (API-M) : API.<globale>, API.<Unite>.<variable>

#include "HmiExpr.hpp"
#include "HmiLog.hpp"             // 1.11.14 : IHM_LOG et ses niveaux
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
#include "HmiCallCheck.hpp"     // 1.11.20 : les appels, controles comme le moteur les fait
#include "../sim/DialectLib.hpp"   // 1.12.7 : les natives des conteneurs, std::numeric, <bit>, les templates
#include "HmiOverload.hpp"      // 1.11.20 : les signatures (E/S, sorties) et les surcharges
#include "HmiTable.hpp"         // 1.12.13 : les parametres qu'un geste sur un tableau passe a ses actions
#include "HmiNames.hpp"         // 1.13.0 (lot J8) : les espaces de noms (USING, Gaz.T_Vanne, Util.Moyenne)

// 1.10 (decisions 13 et 13 bis) : le dialecte IHM du chantier S1 - ses
// fonctions internes, ses noms et ses constats (hmi::lang110::analyze ; voir
// /home/claude/v110/avancement/S-interface.md). Sans son en-tete : les
// constructions sont reconnues ici seulement (scanDialect).
#if __has_include("HmiScriptCheck110.hpp")
#include "HmiScriptCheck110.hpp"
#include "HmiTypeRegistry.hpp"   // 1.11.19 (refonte, lot 6) : un type est-il un nombre ?
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
    static const std::set<std::string> k = {"AND", "OR", "XOR", "NOT", "MOD", "TRUE", "FALSE", "NULL",   // NULL : 1.10 (13 bis)
                                            "ENTRE", "ET"};                                           // 1.12.1
    return k;
}
const std::set<std::string>& statementWords() {
    static const std::set<std::string> k = {"IF",    "THEN",      "ELSIF",   "ELSE",       "END_IF", "CASE",   "OF",
                                            "END_CASE", "FOR",    "TO",      "BY",         "DO",     "END_FOR", "WHILE",
                                            "END_WHILE", "REPEAT", "UNTIL",  "END_REPEAT", "EXIT",   "RETURN",
                                            "EACH",  "IN",        "FUNCTION", "END_FUNCTION",    // 1.10 : le dialecte IHM (13)
                                            "CONTINUE", "TRY", "CATCH", "END_TRY",               // 1.12.1
                                            "LEVER", "RELEVER"};                                   // 1.13.0
    return k;
}
// 1.10 (decisions 13 et 13 bis) : les fonctions du dialecte IHM - reconnues
// ici, leurs arguments controles par S1 (dialectFindings).
const std::set<std::string>& dialectFunctions() {
    static const std::set<std::string> k = [] {
        std::set<std::string> s = {"MAP_HAS",  "MAP_REMOVE", "MAP_SIZE", "MAP_CLEAR", "MAP_KEYS",    "MAP_GET",
                                   "MAP_BEGIN", "MAP_END",   "MAP_NEXT", "REF",       "ADR",         "SIZEOF",
                                   "LOWER_BOUND", "UPPER_BOUND", "ASSERT",    // ASSERT : 1.12.1
                                   // 1.12.2 : les listes et les vecteurs
                                   "LIST_ADD", "LIST_INSERT", "LIST_REMOVE_AT", "LIST_REMOVE", "LIST_CLEAR", "LIST_COUNT",
                                   "LIST_CONTAINS", "LIST_INDEX_OF", "LIST_FIRST", "LIST_LAST", "LIST_GET", "LIST_SORT",
                                   "LIST_REVERSE", "LIST_SUM", "LIST_MIN", "LIST_MAX", "LIST_AVG", "VECTOR_PUSH", "VECTOR_POP",
                                   "VECTOR_INSERT", "VECTOR_ERASE", "VECTOR_CLEAR", "VECTOR_SIZE", "VECTOR_RESIZE",
                                   "VECTOR_FRONT", "VECTOR_BACK", "JOIN", "SPLIT"};
        // 1.12.7 : les conteneurs (43 de plus), std::numeric, <bit>
        for (const auto& n : sim::lib::natives()) s.insert(std::string(n.name));
        return s;
    }();
    return k;
}
// 1.12.7 : le nombre d'arguments des natives LIST_ et VECTOR_ d'avant (Compiler ne le comptait pas :
// un ecart est un avertissement - ce qui demarrait en 1.12.6 demarre toujours).
struct OldArity { const char* name; int min; int max; };
constexpr OldArity kOldLists[] = {
    {"LIST_ADD", 2, 2}, {"LIST_INSERT", 3, 3}, {"LIST_REMOVE_AT", 2, 2}, {"LIST_REMOVE", 2, 2}, {"LIST_CLEAR", 1, 1},
    {"LIST_COUNT", 1, 1}, {"LIST_CONTAINS", 2, 2}, {"LIST_INDEX_OF", 2, 2}, {"LIST_FIRST", 1, 1}, {"LIST_LAST", 1, 1},
    {"LIST_GET", 3, 3}, {"LIST_SORT", 1, 2}, {"LIST_REVERSE", 1, 1}, {"LIST_SUM", 1, 1}, {"LIST_MIN", 1, 1}, {"LIST_MAX", 1, 1},
    {"LIST_AVG", 1, 1}, {"VECTOR_PUSH", 2, 2}, {"VECTOR_POP", 1, 1}, {"VECTOR_INSERT", 3, 3}, {"VECTOR_ERASE", 2, 2},
    {"VECTOR_CLEAR", 1, 1}, {"VECTOR_SIZE", 1, 1}, {"VECTOR_RESIZE", 2, 3}, {"VECTOR_FRONT", 1, 1}, {"VECTOR_BACK", 1, 1},
    {"JOIN", 1, 2}, {"SPLIT", 2, 2}, {"MAP_GET", 2, 3},
};
const OldArity* oldArity(std::string_view upper) {
    for (const auto& a : kOldLists)
        if (upper == a.name) return &a;
    return nullptr;
}
// 1.12.7 : ce qu'un type ecrit dit d'un conteneur (Compiler) - un ARRAY et ses cases, l'element d'une
// LIST, d'un VECTOR ou d'un ARRAY, les champs d'un TUPLE. Le texte tel qu'ecrit (les noms gardent leur casse).
struct ContainerType {
    enum class K : std::uint8_t { Other, Array, List, Map, Tuple } kind{K::Other};
    long long                cells{-1};
    std::string              element;
    std::vector<std::string> fields;
    std::string              text;      // TUPLE(STRING, REAL)
    std::string              flavor;    // 1.12.7 : une liste "SET" ou "RING" (vide : LIST, VECTOR)
};
std::string trimmedText(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
ContainerType containerOf(std::string_view written) {
    ContainerType out;
    const std::string t = trimmedText(written), U = up(t);
    const auto afterOf = [&](std::size_t from) {
        const auto of = U.find(" OF ", from);
        return of == std::string::npos ? std::string{} : trimmedText(std::string_view(t).substr(of + 4));
    };
    if (U.rfind("ARRAY", 0) == 0) {
        out.kind = ContainerType::K::Array;
        const auto open = U.find('['), close = U.find(']');
        if (open != std::string::npos && close != std::string::npos && close > open) {
            long long cells = 1;
            std::size_t from = open + 1;
            while (from < close) {
                const auto comma = std::min(U.find(',', from), close);
                const std::string range = U.substr(from, comma - from);
                const auto dots = range.find("..");
                if (dots == std::string::npos) { cells = -1; break; }
                char* e1 = nullptr;
                char* e2 = nullptr;
                const std::string lo = trimmedText(range.substr(0, dots)), hi = trimmedText(range.substr(dots + 2));
                const long long a = std::strtoll(lo.c_str(), &e1, 10), b = std::strtoll(hi.c_str(), &e2, 10);
                if (lo.empty() || hi.empty() || *e1 != '\0' || *e2 != '\0' || b < a) { cells = -1; break; }
                cells *= b - a + 1;
                from = comma + 1;
            }
            out.cells = cells;
            out.element = afterOf(close);
        }
        return out;
    }
    if (U.rfind("LIST OF ", 0) == 0 || U.rfind("VECTOR OF ", 0) == 0) {
        out.kind = ContainerType::K::List;
        out.element = afterOf(0);
        return out;
    }
    // 1.12.7 : SET OF T, RING[N] OF T - des listes (leur forme : `flavor`).
    if (U.rfind("SET OF ", 0) == 0 || (U.rfind("RING", 0) == 0 && U.find(" OF ") != std::string::npos && U.size() > 4 && (U[4] == '[' || U[4] == ' '))) {
        out.kind = ContainerType::K::List;
        out.flavor = U[0] == 'S' ? "SET" : "RING";
        out.element = afterOf(0);
        return out;
    }
    if (U.rfind("MAP", 0) == 0) {
        out.kind = ContainerType::K::Map;
        return out;
    }
    if (U.rfind("TUPLE", 0) == 0) {
        const auto open = t.find('('), close = t.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close < open) return out;
        out.kind = ContainerType::K::Tuple;
        int depth = 0;
        std::size_t from = open + 1;
        for (std::size_t k = open + 1; k <= close; ++k) {
            if (t[k] == '(' || t[k] == '[') ++depth;
            else if ((t[k] == ')' || t[k] == ']') && depth > 0) --depth;
            else if ((t[k] == ',' && depth == 0) || k == close) {
                out.fields.push_back(trimmedText(std::string_view(t).substr(from, k - from)));
                from = k + 1;
            }
        }
        out.text = "TUPLE(";
        for (std::size_t k = 0; k < out.fields.size(); ++k) out.text += (k ? ", " : "") + up(out.fields[k]);
        out.text += ")";
    }
    return out;
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
    // 1.12.2 : LIST OF T, VECTOR OF T, TUPLE(T1, T2...) ; 1.12.7 : SET OF T, RING[N] OF T.
    if ((word(k, "LIST") || word(k, "VECTOR") || word(k, "SET")) && word(k + 1, "OF")) return typeEnd(t, k + 2);
    if (word(k, "RING") && op(k + 1, "[")) {
        const std::size_t m = closeOf(k + 1);
        return word(m, "OF") ? typeEnd(t, m + 1) : m;
    }
    if (word(k, "TUPLE") && op(k + 1, "(")) {
        int depth = 0;
        for (std::size_t m = k + 1; m < t.size() && t[m].k != Tok::K::End; ++m) {
            if (op(m, "(")) ++depth;
            else if (op(m, ")") && --depth == 0) return m + 1;
        }
        return t.size();
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
    {"IHM_ESCLAVE_SIMULE", 1, 1},  {"IHM_LOG", 2, 2},
    {"IHM_APRES", 2, 2},           {"IHM_CHAQUE", 2, 2},         {"IHM_ANNULER", 1, 1},    // 1.12.19
    {"IHM_SUR_CHANGEMENT", 2, 2},                                                          // 1.12.20
    {"IHM_SCRIPT_ACTIF", 1, 1},    {"IHM_SCRIPT_ARRETER", 1, 1},                            // 1.12.26
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
    return typereg::isNumber(u) ? Kind::Num : Kind::Other;     // 1.11.19 (lot 6) : les nombres du registre
}
bool elementary(std::string_view type) { return kindOf(type) != Kind::Other; }

// 1.12.7 : SET OF T, RING[N] OF T - un type ecrit (sans casse) ; ce qu'un SET refuse (la maquette v3).
bool setText(std::string_view type) {
    const std::string u = up(trimmedText(type));
    return u.rfind("SET", 0) == 0 && u.size() > 3 && (u[3] == ' ' || u[3] == '\t') && u.find(" OF ") != std::string::npos;
}
bool ringText(std::string_view type) {
    const std::string u = up(trimmedText(type));
    return u.rfind("RING", 0) == 0 && u.size() > 4 && (u[4] == '[' || u[4] == ' ') && u.find(" OF ") != std::string::npos;
}
const char* const kSetRefusedText =
    " : un SET se range tout seul (chaque valeur une fois, dans l'ordre) \xE2\x80\x94 SET_ADD et SET_REMOVE l'\xC3\xA9" "crivent";
const char* const kSetItemText =
    "un \xC3\xA9l\xC3\xA9ment d'un SET ne s'\xC3\xA9" "crit pas en place \xE2\x80\x94 SET_REMOVE(S, ancienne) puis SET_ADD(S, nouvelle) :"
    " le SET reste rang\xC3\xA9, chaque valeur une fois";
// RING[N] OF T : N, un entier de 1 a 100000 ou une constante entiere declaree plus haut (le moteur le dit
// aussi, a la lecture du script) ; vide : bon. `constants` : les VAR CONSTANT (en majuscules).
std::string ringProblem(std::string_view type, const std::set<std::string>& constants) {
    const std::string t = trimmedText(type), U = up(t);
    const auto of = U.find(" OF "), open = t.find('[');
    if (open == std::string::npos || open > of)
        return "RING : sa capacit\xC3\xA9 est attendue, le nombre de valeurs gard\xC3\xA9" "es \xE2\x80\x94 RING[100] OF REAL";
    const auto close = t.find(']', open);
    if (close == std::string::npos || close > of) return "RING[N] : ']' attendu apr\xC3\xA8s la capacit\xC3\xA9";
    const std::string n = trimmedText(std::string_view(t).substr(open + 1, close - open - 1));
    const bool digits = !n.empty() && std::all_of(n.begin(), n.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
    if (digits) {
        const long long v = n.size() > 9 ? 1000000000LL : std::atoll(n.c_str());
        if (v < 1 || v > 100000) return "RING[" + n + "] : la capacit\xC3\xA9 va de 1 \xC3\xA0 100000";
        return {};
    }
    const bool ident = !n.empty() && (std::isalpha(static_cast<unsigned char>(n[0])) || n[0] == '_')
                       && std::all_of(n.begin(), n.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    if (ident && !constants.count(up(n)))
        return "RING[N] : N est une constante enti\xC3\xA8re ('" + n + "' n'est pas une constante enti\xC3\xA8re d\xC3\xA9" "clar\xC3\xA9" "e plus haut)";
    return {};
}
// Les natives qui ecrivent une liste a une place (ou la reordonnent) : refusees sur un SET.
bool setRefusedNative(std::string_view U) {
    for (const char* n : {"LIST_INSERT", "VECTOR_INSERT", "LIST_SORT", "LIST_REVERSE", "VECTOR_RESIZE", "LIST_SET", "LIST_FILL", "LIST_INSERT_SORTED",
                          "LIST_SORT_BY", "LIST_SWAP", "LIST_MOVE", "LIST_ROTATE", "IOTA"})
        if (U == n) return true;
    return false;
}
// "une LIST OF STRING", "un RING[5] OF REAL", "un INT" : un type ecrit avec son article.
std::string withArticle(const std::string& type) {
    const std::string u = up(type);
    const bool feminine = u.rfind("LIST", 0) == 0 || u.rfind("MAP", 0) == 0 || u.rfind("STRING", 0) == 0 || u.rfind("STRUCT", 0) == 0;
    return (feminine ? "une " : "un ") + type;
}
// 1.12.16 : un type ecrit, tel que le moteur le calcule (SWAP compare deux variables) : STRING[20], CHAR et WSTRING
// sont des STRING, LREAL un REAL, TOD un TIME_OF_DAY, DT un DATE_AND_TIME.
std::string sameWritten(const std::string& type) {
    const std::string u = up(trimmedText(type));
    if (u.rfind("STRING", 0) == 0 || u.rfind("WSTRING", 0) == 0 || u == "CHAR") return "STRING";
    if (u == "LREAL") return "REAL";
    if (u == "TOD") return "TIME_OF_DAY";
    if (u == "DT") return "DATE_AND_TIME";
    if (u == "EBOOL") return "BOOL";
    return u;
}

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
    Walker(const Scope& sc, std::string_view raw) : sc_(sc) {
        // 1.13.0 (lot J8) : les directives USING - lues (les espaces importes), puis blanchies (les colonnes restent).
        for (const auto& u : names::usings(raw)) imports_.push_back(u.ns);
        const std::string cleaned = names::blankUsings(raw);
        const std::string_view code = cleaned;
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
        mergeNamespaces();              // 1.13.0 (lot J8) : Gaz . T_Vanne -> un seul nom ; un nom court de l'espace -> complet
        foldLambdas();                  // 1.12.20 : une LAMBDA - ses parametres et ses types ne sont pas des variables
        lineStarts_.push_back(0);
        for (std::size_t i = 0; i < body_.size(); ++i)
            if (body_[i] == '\n') lineStarts_.push_back(i + 1);
        // Les compteurs de boucle : FOR i := ...
        for (std::size_t i = 0; i + 2 < toks_.size(); ++i)
            if (isWord(i, "FOR") && isIdent(i + 1) && isOp(i + 2, ":=")) counters_.insert(up(toks_[i + 1].text));
        // 1.13.0 : le nom d'un CATCH (CATCH e sur sa ligne) - un T_Erreur, connu dans son handler.
        for (std::size_t i = 0; i + 1 < toks_.size(); ++i)
            if (isCatchVar(i)) catchVars_.insert(up(toks_[i + 1].text));
        if (sc_.view && sc_.project && inherits(*sc_.project, *sc_.view)) composed_ = compose(*sc_.project, *sc_.view);
        scanDialect(code);
    }

    // 1.12.20 : LES LAMBDA, LUES AVANT TOUT - le moteur les sert, la verification ne voyait qu'un appel de
    // « LAMBDA » et des noms inconnus (x, REAL, BOOL). Une LAMBDA d'une ligne (LAMBDA (x : REAL) : BOOL => x > 5) :
    // son en-tete est retire et ses parametres, dans son corps, deviennent des valeurs (rien a chercher) - le
    // corps reste verifie (les autres noms, les appels). Une LAMBDA en bloc (... END_LAMBDA) : tout entiere une
    // valeur (ses instructions sont lues par le moteur a l'appel). Les positions des jetons restent : un message
    // pointe toujours au bon endroit, et le texte d'une expression est celui du code.
    void foldLambdas() {
        const auto word = [&](std::size_t k, const char* w) { return k < toks_.size() && toks_[k].k == Tok::K::Ident && up(toks_[k].text) == w; };
        const auto op = [&](std::size_t k, const char* o) { return k < toks_.size() && toks_[k].k == Tok::K::Op && toks_[k].text == o; };
        const auto value = [&](const Tok& at) {
            Tok v;
            v.k = Tok::K::Num;
            v.text = "0";
            v.at = at.at;
            v.len = at.len;
            return v;
        };
        for (std::size_t k = toks_.size(); k-- > 0;) {          // de la derniere a la premiere : les imbriquees d'abord
            if (!word(k, "LAMBDA") || !op(k + 1, "(")) continue;
            // Les parametres : (x : REAL; y, z : INT)
            std::set<std::string> params;
            std::size_t j = k + 2;
            int depth = 1;
            bool name = true;
            for (; j < toks_.size() && toks_[j].k != Tok::K::End; ++j) {
                if (op(j, "(")) ++depth;
                else if (op(j, ")") && --depth == 0) break;
                else if (depth == 1 && (op(j, ";") || op(j, ","))) name = true;
                else if (depth == 1 && op(j, ":")) name = false;
                else if (depth == 1 && name && toks_[j].k == Tok::K::Ident) params.insert(up(toks_[j].text));
            }
            if (j >= toks_.size() || toks_[j].k == Tok::K::End) continue;
            std::size_t h = j + 1;                               // apres la parenthese : : T, puis => ou le bloc
            std::size_t arrow = toks_.size();
            for (std::size_t m = h; m < toks_.size() && toks_[m].k != Tok::K::End; ++m) {
                if (op(m, "=>")) { arrow = m; break; }
                if (op(m, ";") || op(m, ",") || op(m, ")") || word(m, "END_LAMBDA") || word(m, "VAR") || word(m, "RETURN")) break;
            }
            if (arrow < toks_.size()) {
                // Le corps : jusqu'a la virgule, la parenthese ou le point-virgule qui le ferme.
                std::size_t e = arrow + 1;
                int d = 0;
                for (; e < toks_.size() && toks_[e].k != Tok::K::End; ++e) {
                    if (op(e, "(") || op(e, "[")) ++d;
                    else if ((op(e, ")") || op(e, "]")) && d-- == 0) break;
                    else if (d == 0 && (op(e, ",") || op(e, ";"))) break;
                    else if (d == 0 && (word(e, "THEN") || word(e, "DO") || word(e, "OF") || word(e, "ELSE") || word(e, "ELSIF")
                                        || word(e, "END_IF") || word(e, "END_FOR") || word(e, "END_WHILE") || word(e, "UNTIL")))
                        break;
                }
                for (std::size_t m = arrow + 1; m < e; ++m)
                    if (toks_[m].k == Tok::K::Ident && params.count(up(toks_[m].text)) && !(m > 0 && op(m - 1, ".")))
                        toks_[m] = value(toks_[m]);
                toks_.erase(toks_.begin() + static_cast<std::ptrdiff_t>(k), toks_.begin() + static_cast<std::ptrdiff_t>(arrow + 1));
                continue;
            }
            // Le bloc : jusqu'a son END_LAMBDA (les LAMBDA imbriquees comptees).
            std::size_t e = h;
            int nest = 1;
            for (; e < toks_.size() && toks_[e].k != Tok::K::End; ++e) {
                if (word(e, "LAMBDA")) ++nest;
                else if (word(e, "END_LAMBDA") && --nest == 0) break;
            }
            if (e >= toks_.size() || toks_[e].k == Tok::K::End) continue;      // pas de fin : la syntaxe le dit
            const Tok v = value(toks_[k]);
            toks_.erase(toks_.begin() + static_cast<std::ptrdiff_t>(k + 1), toks_.begin() + static_cast<std::ptrdiff_t>(e + 1));
            toks_[k] = v;
        }
    }

    // 1.11.1 (REP-10) : les racines des reperes du script (en majuscules) - un ancien
    // `$Vanne$` qui n'est pas une variable n'a pas de "veux-tu dire ... ?" (une fausse piste).
    std::set<std::string> markerRoots;

    // 1.13.0 (lot J8) : LES ESPACES DE NOMS, LUS AVANT TOUT. Gaz . T_Vanne (trois jetons) devient un seul nom complet
    // (Gaz.T_Vanne) quand Gaz est un espace declare qui a T_Vanne (la regle du plus long chemin) ; un nom court que
    // l'espace de l'item ou un USING designe (Borne -> Util.Borne) prend son nom complet, a sa place (les colonnes
    // restent celles du texte). Le reste du controle voit alors des noms complets, comme le moteur.
    void mergeNamespaces() {
        if (!sc_.project || toks_.empty() || !names::active(*sc_.project)) return;
        const names::Index ix(*sc_.project);
        names::Scope bare;
        bare.own = ownNamespace();
        bare.imports = imports_;
        for (const auto& l : parts_.locals) bare.hidden.insert(up(l.name));
        if (sc_.function) bare.hidden.insert(up(sc_.function->name));
        std::vector<names::TokenView> views;
        views.reserve(toks_.size());
        for (const auto& t : toks_) views.push_back({t.text, t.k == Tok::K::Ident, t.k == Tok::K::Op && t.text == "."});
        const auto runs = names::qualifiedRuns(ix, views, bare.own.empty() && bare.imports.empty() ? nullptr : &bare);
        for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
            Tok& first = toks_[it->first];
            const std::size_t end = toks_[it->first + it->count - 1].end();
            first.text = it->text;
            first.len = end - first.at;
            if (it->count > 1)
                toks_.erase(toks_.begin() + static_cast<std::ptrdiff_t>(it->first + 1), toks_.begin() + static_cast<std::ptrdiff_t>(it->first + it->count));
        }
    }
    // L'espace de l'item controle : celui du script (Scope::ns), sinon celui de la fonction.
    [[nodiscard]] std::string ownNamespace() const {
        if (!sc_.ns.empty()) return sc_.ns;
        return sc_.function ? sc_.function->ns : std::string{};
    }
    [[nodiscard]] const std::vector<std::string>& imports() const noexcept { return imports_; }
    // 1.13.0 (lot J8) : un nom court connu autrement qu'un item d'un espace (une locale, une variable, une fonction
    // standard ou IHM, une native, l'automate...) - names::check ne le dit pas « d'un autre espace ».
    [[nodiscard]] bool knownOtherwise(std::string_view n) const {
        const std::string U = up(n);
        if (local(n) || param(n) || counter(n) || selfName(n) || catchVar(n) || viewConstant(n) || viewFunctionHere(n)) return true;
        if (internalFunction(std::string(n)) || isStandardFunction(U) || isHmiFunction(U) || natives::isOwnFunction(U) || sim::lib::native(U)
            || dialectFunctions().count(U) || conversion(U))
            return true;
        const Project* p = sc_.project;
        if (p && (p->variable(n) || pub::isSysRoot(n) || pub::viewNamed(*p, n) || p->generalScript(n))) {
            // une variable, une vue, SYS : pas un item d'un espace (une fonction de la racine non plus)
            return true;
        }
        if (p) {
            for (const auto& f : p->programs.functions) if (f.ns.empty() && up(f.name) == U) return true;
            for (const auto& t : p->programs.types) if (t.ns.empty() && up(t.name) == U) return true;
            for (const auto* e : enumerations(*p))
                if (enumValueByName(*e, n)) return true;
        }
        return sc_.plcKnown && sc_.plcKnown(n);
    }

    std::vector<Finding> run() {
        // 1.12.7 : SET OF T_Lot - un SET range des valeurs simples (une structure, une liste ne se rangent pas) ;
        // RING[N] OF T - sa capacite (le moteur le dit aussi au demarrage).
        for (const auto& l : parts_.locals) {
            if (ringText(l.type)) {
                const std::string why = ringProblem(l.type, constants_);
                const auto at = static_cast<std::size_t>(std::max(0, l.line - 1));
                if (!why.empty() && at < lineStarts_.size()) report(lineStarts_[at], lineStarts_[at], Finding::Severity::Error, why);
                continue;
            }
            if (!setText(l.type)) continue;
            const std::string el = trimmedText(std::string_view(l.type).substr(up(l.type).find(" OF ") + 4));
            const bool structure = sc_.project && !types::membersOf(*sc_.project, el).empty();
            if (containerOf(el).kind == ContainerType::K::Other && !structure) continue;
            const auto at = static_cast<std::size_t>(std::max(0, l.line - 1));
            if (at < lineStarts_.size())
                report(lineStarts_[at], lineStarts_[at], Finding::Severity::Error,
                       trimmedText(l.type) + " : un SET range des valeurs simples (des nombres, des textes, des dur\xC3\xA9" "es, une \xC3\xA9num\xC3\xA9ration)"
                       " \xE2\x80\x94 pour des structures, une LIST et LIST_SORT_BY");
        }
        quotedCallbackNames();          // 1.12.20 : LIST_TRANSFORM(S, D, 'Carre')
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
    // 1.11.20 : le code se lit et ses appels de fonctions de l'utilisateur sont juges par le
    // controle des appels (callcheck, la regle du moteur) : le nombre de leurs arguments n'est
    // plus compte ici (un seul message par faute).
    void setCallsChecked(bool on) noexcept { callsChecked_ = on; }
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
    std::vector<std::string> imports_;          // 1.13.0 (lot J8) : les espaces importes (USING), tels qu'ecrits
    ScriptParts           parts_;
    std::string           body_;
    std::vector<Tok>      toks_;
    std::vector<std::size_t> lineStarts_;
    std::set<std::string> counters_;
    std::set<std::string> constants_;         // VAR CONSTANT : en lecture seule
    std::set<std::string> catchVars_;         // 1.13.0 : les noms d'un CATCH (un T_Erreur en lecture seule)
    std::vector<bool>     tryStack_;          // 1.13.0 : les TRY ouverts (true : dans leur CATCH)
    int                   handlerDepth_{0};   // 1.13.0 : combien de CATCH englobent (RELEVER n'y est permis)
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
    std::vector<DialectName> setLoopVars_;      // 1.12.7 : les variables d'un FOR EACH sur un SET (lues, jamais ecrites)
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
        if (catchVar(n)) return true;                                  // 1.13.0 : l'erreur d'un CATCH
        if (viewConstant(n) || viewFunctionHere(n)) return true;      // 1.12.16 : un membre de la vue
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
        if (catchVar(root) && !local(root) && !param(root) && !(p && p->variable(root))) {   // 1.13.0 : e.Code/e.Message (STRING), e.Ligne (DINT), e seul (STRING)
            if (cut == std::string::npos) return "STRING";
            const std::string m = up(s.substr(cut + 1));
            return m == "LIGNE" ? "DINT" : m == "CODE" || m == "MESSAGE" ? "STRING" : std::string{};
        }
        if (p && param(root)) return params::expressionType(*p, sc_.view, s);
        // 1.12.16 : une constante de la vue (Seuil_Haut), ou d'une vue nommee (Vue_Four.Seuil_Haut).
        if (const Declaration* c = viewConstant(root); p && c) return walkTypeOf(*p, c->type, cut == std::string::npos ? std::string_view{} : std::string_view(s).substr(cut));
        if (p && cut != std::string::npos && s[cut] == '.') {
            viewmembers::Member m;
            if (viewmembers::resolve(*p, s, m, nullptr, sc_.view) && m.constant) {
                std::size_t rest = cut + 1;
                while (rest < s.size() && s[rest] != '.' && s[rest] != '[') ++rest;
                return walkTypeOf(*p, m.constant->type, std::string_view(s).substr(rest));
            }
        }
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
            std::size_t in = f + 2;
            for (; !atEnd(in) && !isWord(in, "IN") && !isWord(in, "DO") && !isOp(in, ";"); ++in)
                if (t[in].k == Tok::K::Ident) add(t[in], from, to);
            // 1.12.7 : FOR EACH x IN S, S un SET (un nom) - x se lit, ne s'ecrit pas.
            if (isWord(in, "IN") && in > f + 2 && t[in - 1].k == Tok::K::Ident && isIdent(in + 1) && isWord(in + 2, "DO")
                && setText(typeOfName(t[in + 1].text)))
                setLoopVars_.push_back({t[in - 1].text, up(t[in - 1].text), from, to});
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
            g.callArity = f.arity;            // 1.11.20
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
    // 1.12.20 : un nom de fonction en texte la ou une native attend un rappel (IHM_APRES(T#1s, 'Fermer'),
    // LIST_TRANSFORM(S, D, 'Carre')) - il se lit encore, mais n'est ni verifie ni renomme : un avertissement et sa
    // correction d'un clic.
    void quotedCallbackNames() {
        if (!sc_.project) return;
        for (std::size_t k = 0; k + 1 < toks_.size() && !atEnd(k); ++k) {
            if (toks_[k].k != Tok::K::Ident || !isOp(k + 1, "(")) continue;
            const auto* f = natives::function(up(toks_[k].text));
            if (!f) continue;
            std::size_t arg = 0, start = k + 2;
            int depth = 0;
            for (std::size_t j = k + 2; !atEnd(j); ++j) {
                const bool close = isOp(j, ")") || isOp(j, "]");
                if (isOp(j, "(") || isOp(j, "[")) ++depth;
                else if (close && depth > 0) --depth;
                else if ((close || isOp(j, ",")) && depth == 0) {
                    // L'argument [start, j) : un texte seul, qui nomme une fonction du projet ou de la vue.
                    if (j == start + 1 && toks_[start].k == Tok::K::Str && arg < f->params.size()
                        && std::string(f->params[arg].type).find("CALLBACK") != std::string::npos) {
                        std::string name = toks_[start].text;
                        if (name.size() >= 2 && name.front() == '\'' && name.back() == '\'') name = name.substr(1, name.size() - 2);
                        bool fn = !sc_.project->functionsNamed(name).empty();
                        if (!fn && sc_.view)
                            for (const auto& vf : sc_.view->functions) fn = fn || up(vf.name) == up(name);
                        if (fn) {
                            const std::size_t before = out_.size();
                            reportTok(start, j, Finding::Severity::Warning,
                                      "'" + name + "' : un nom de fonction en texte n'est ni v\xC3\xA9rifi\xC3\xA9 ni renomm\xC3\xA9 \xE2\x80\x94 \xC3\xA9" "cris "
                                          + name + " (sans apostrophes)");
                            if (out_.size() > before) {
                                out_.back().fixFrom = toks_[start].text;
                                out_.back().fixTo = name;
                            }
                        }
                    }
                    if (close) break;
                    ++arg;
                    start = j + 1;
                }
            }
        }
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
    // 1.13.0 : un nom lie par un CATCH (un T_Erreur en lecture seule).
    [[nodiscard]] bool catchVar(std::string_view name) const { return catchVars_.count(up(name)) > 0; }
    // 1.13.0 : CATCH e - un nom seul sur la ligne du CATCH (ni CATCH Etat := 0;) ; comme le parseur du moteur.
    [[nodiscard]] bool isCatchVar(std::size_t i) const {
        if (!isWord(i, "CATCH") || !isIdent(i + 1)) return false;
        if (body_.find('\n', toks_[i].end()) < toks_[i + 1].at) return false;    // e est sur la ligne du CATCH
        return atEnd(i + 2) || isOp(i + 2, ";") || body_.find('\n', toks_[i + 1].end()) < toks_[i + 2].at;
    }
    [[nodiscard]] bool selfName(std::string_view name) const { return sc_.function && up(sc_.function->name) == up(name); }
    // 1.12.16 : une constante, une fonction de la vue du code (une vue ou une popup ; pas un symbole).
    [[nodiscard]] const Declaration* viewConstant(std::string_view name) const {
        return sc_.view ? viewmembers::constant(*sc_.view, name) : nullptr;
    }
    [[nodiscard]] bool viewFunctionHere(std::string_view name) const {
        return sc_.view && !viewmembers::functions(*sc_.view, name).empty();
    }
    [[nodiscard]] static std::string walkTypeOf(const Project& p, std::string type, std::string_view rest) {
        return viewmembers::typeAlong(p, std::move(type), rest);
    }
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
        // 1.12.7 : S[0] := x, S.First := x, la variable d'un FOR EACH sur un SET - un element d'un SET ne s'ecrit pas en place.
        if (write && segs.empty())
            for (const auto& v : setLoopVars_)
                if (v.upper == R && root.at >= v.from && root.at < v.to) {
                    reportTok(i, j, S::Error, kSetItemText);
                    return {};
                }
        if (write && !segs.empty()) {
            // Le chemin sans sa derniere case ([i]) ou sans .First / .Last : S, Recette.Postes.
            const std::string text = textOf(i, j);
            std::string owner;
            if (!text.empty() && text.back() == ']') {
                int depth = 0;
                for (std::size_t k = text.size(); k-- > 0;) {
                    if (text[k] == ']') ++depth;
                    else if (text[k] == '[' && --depth == 0) { owner = text.substr(0, k); break; }
                }
            } else if (const auto dot = text.rfind('.'); dot != std::string::npos) {
                const std::string m = up(text.substr(dot + 1));
                if (m == "FIRST" || m == "LAST") owner = text.substr(0, dot);
            }
            if (!owner.empty() && setText(typeOfName(owner))) {
                reportTok(i, j, S::Error, kSetItemText);
                return {};
            }
        }
        // 1.13.0 : e (l'erreur d'un CATCH) - un T_Erreur en lecture seule : e.Code, e.Message, e.Ligne ;
        // e seul se lit comme e.Message. Un nom deja declare (variable locale STRING) passe avant : il
        // garde le comportement du CATCH Erreur de la 1.12.1 ; sinon c'est l'objet T_Erreur.
        if (catchVar(root.text) && !local(root.text) && !param(root.text) && !(p && p->variable(root.text))) {
            if (write) {
                reportTok(i, i + 1, S::Error, "l'erreur attrap\xC3\xA9" "e " + root.text + " est en lecture seule (Code, Message, Ligne)");
                return {};
            }
            if (segs.empty()) return "STRING";                 // e seul = e.Message
            const Seg& s = segs.front();
            if (s.index) {
                reportTok(i, j, S::Error, root.text + " est l'erreur attrap\xC3\xA9" "e (T_Erreur) : elle n'a pas d'indice");
                return {};
            }
            const std::string m = up(s.member);
            if (segs.size() == 1 && (m == "CODE" || m == "MESSAGE" || m == "LIGNE")) return m == "LIGNE" ? "DINT" : "STRING";
            reportTok(i, j, S::Error, "T_Erreur n'a que Code, Message et Ligne, pas " + s.member);
            return {};
        }
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
        // 1.12.16 : une constante de la vue, par son nom (Seuil_Haut, Paliers[2], Recette_Defaut.Nom).
        if (const Declaration* c = p ? viewConstant(root.text) : nullptr; c && !param(root.text)) {
            if (write) {
                reportTok(i, i + 1, S::Error, root.text + " est une constante de la vue " + sc_.view->name + " : elle ne s'\xC3\xA9" "crit pas");
                return {};
            }
            return types::isRich(c->type) && !segs.empty() ? std::string{} : walkTypeOf(*p, c->type, textOf(i, j).substr(root.text.size()));
        }
        // Un parametre de la vue (popup, symbole) : son type declare.
        if (param(root.text)) {
            if (p) return params::expressionType(*p, sc_.view, textOf(i, j));
            return {};
        }
        // Une variable IHM : ses membres, ses indices (les structures, les tableaux).
        if (const Variable* var = p ? p->variable(root.text) : nullptr) {
            // 1.12.2 : une variable objet (LIST, VECTOR, MAP, TUPLE) : L[0], L.Count, M['a'], t.Item1 -
            // le moteur les lit dans l'objet (son type : celui de la variable).
            if (types::isRich(var->type)) return segs.empty() ? types::normalized(var->type) : std::string{};
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
        // 1.12.16 : Vue_Four.Seuil_Haut - la constante d'une vue (ou d'une popup) : publique d'ailleurs.
        if (p && pub::viewNamed(*p, root.text) && !segs.empty() && !segs.front().index) {
            viewmembers::Member m;
            std::string why;
            const std::size_t cut = segs.front().tok + 1;
            if (viewmembers::resolve(*p, root.text + "." + segs.front().member, m, &why, sc_.view)) {
                if (m.constant) {
                    if (write) {
                        reportTok(i, cut, S::Error, m.view->name + "." + m.constant->name + " est une constante : elle ne s'\xC3\xA9" "crit pas");
                        return {};
                    }
                    const std::string text = textOf(i, j);
                    std::size_t rest = root.text.size() + 1;
                    while (rest < text.size() && text[rest] != '.' && text[rest] != '[') ++rest;
                    return types::isRich(m.constant->type) && segs.size() > 1 ? std::string{}
                                                                              : walkTypeOf(*p, m.constant->type, std::string_view(text).substr(rest));
                }
                return {};
            }
            if (!why.empty()) {
                reportTok(i, cut, S::Error, why);
                return {};
            }
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
        // 1.12.7 : Reglages.Seuils - la constante publique d'un script general (lue seulement).
        if (p && p->generalScript(root.text)) {
            if (segs.empty() || segs.front().index) {
                reportTok(i, i + 1, S::Error, root.text + " est un script g\xC3\xA9n\xC3\xA9ral : \xC3\xA9" "cris " + root.text
                                                + ".Constante (une de ses constantes publiques)");
                return {};
            }
            std::string why;
            const Script* gs = nullptr;
            const Declaration* q = calc::qualified(*p, root.text + "." + segs.front().member, &gs, &why);
            const std::size_t cut = segs.front().tok + 1;
            if (!q) {
                reportTok(i, cut, S::Error, why);
                return {};
            }
            if (write) {
                reportTok(i, cut, S::Error, gs->name + "." + q->name + " est une constante : elle ne s'\xC3\xA9" "crit pas");
                return {};
            }
            return segs.size() == 1 ? q->type : std::string{};
        }
        // Une fonction IHM_ nommee sans parentheses : un appel mal ecrit, dit ailleurs.
        if (isHmiFunction(root.text)) return {};
        // 1.11.1 (API-M) : API.<globale>, API.<Unite>.<variable> (une variable IHM,
        // une vue nommee API sont vues plus haut) ; sans le modele, rien a dire.
        const bool apiMember = !segs.empty() && !segs.front().index && !segs.front().member.empty()
                            && (std::isalpha(static_cast<unsigned char>(segs.front().member[0])) || segs.front().member[0] == '_');
        if (core::hasApi() && R == "API" && apiMember) return apiPath(i, j, write);   // 1.12.0 : pas dans IHM Studio
        // 1.11.1 (remarque d'API-V) : API seul, ou API. en cours de frappe, n'est pas
        // un nom inconnu (« API n'existe pas ») : le chemin est incomplet.
        if (core::hasApi() && R == "API") {
            if (sc_.plc.api)
                reportTok(i, i + 1, S::Error,
                          "API : chemin incomplet \xE2\x80\x94 \xC3\xA9" "cris API.<globale> ou API.<Unit\xC3\xA9>.<variable> "
                          "(l'aide \xC3\xA0 la saisie les propose apr\xC3\xA8s API.)");
            return {};
        }
        // 1.12.19 : une fonction passee en rappel (f := Valider ; LIST_FILTER(L, Est_Valide)) - son nom seul est une valeur.
        if (segs.empty()) {
            bool fn = sc_.project && !sc_.project->functionsNamed(root.text).empty();
            if (!fn && sc_.view)
                for (const auto& f : sc_.view->functions) fn = fn || up(f.name) == R;
            for (const auto& f : internal_) fn = fn || up(f.name) == R;
            if (fn) return {};
        }
        // L'automate : ses membres et ses indices constants.
        if (sc_.plcKnown && sc_.plcKnown(root.text)) return plcPath(i, j, segs);
        if (!sc_.plcKnown) return {};           // l'automate n'est pas connu : rien a dire
        // 1.13.0 (lot J8) : un espace de noms (Gaz.Inconnu) - names::check dit ce qui lui manque, avec le plus proche.
        if (sc_.project && sc_.project->isNamespace(root.text)) return {};
        // Personne ne connait ce nom.
        const bool marker = markerRoots.count(up(root.text)) != 0;   // 1.11.1 (REP-10)
        const std::string near = marker ? std::string{} : closestName(root.text);
        std::string msg = root.text + " n'existe pas";
        if (!near.empty()) msg += " : veux-tu dire " + near + " ?";
        else if (marker) msg += core::hasApi() ? " (ni variable IHM, ni variable de l'automate, ni variable locale)" : " (ni variable IHM, ni variable locale)";
        else msg += std::string(core::hasApi() ? " (ni variable IHM, ni variable de l'automate, ni variable locale)" : " (ni variable IHM, ni variable locale)")
                  + " : corrige le nom ou d\xC3\xA9" "clare-la (VAR ... END_VAR)";   // 1.12.0 : IHM Studio n'a pas d'automate
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
    bool callsChecked_{false};                   // 1.11.20 : voir setCallsChecked
    // 1.11.20 : les surcharges d'une fonction de symbole (les fonctions de meme nom de son symbole).
    std::vector<const HmiFunction*> siblingsOf(const HmiFunction* f) const {
        if (!f) return {};
        if (sc_.project)
            for (const auto& v : sc_.project->views)
                for (const auto& g : v.functions)
                    if (&g == f) return symbolFunctions(v, f->name);
        return {f};
    }
    // Un appel de fonction d'instance : ses arguments controles comme ceux d'une fonction IHM.
    std::size_t instanceCall(std::size_t i, std::size_t j, const HmiFunction& f, bool statement) {
        // 1.12.16 : Vue_Four.CouleurNiveau(...) d'ailleurs - une fonction privee de la vue.
        if (sc_.project && f.visibility == Visibility::Private)
            for (const auto& v : sc_.project->views)
                if (!isSymbolView(v) && std::any_of(v.functions.begin(), v.functions.end(), [&](const HmiFunction& g) { return &g == &f; })
                    && (!sc_.view || sc_.view->id != v.id))
                    reportTok(i, j, Finding::Severity::Error,
                              v.name + "." + f.name + " est priv\xC3\xA9" "e : seule " + v.name + " l'appelle (rends-la publique)");
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
        // 1.12.16 : une fonction de la vue (ou de la popup), par son nom - elle passe avant celles du projet.
        if (!pendingSymbolFn_ && !ownFn && sc_.view && !internalFunction(name.text))
            if (const auto fs = viewmembers::functions(*sc_.view, name.text); !fs.empty()) ownFn = fs.front();
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
        // 1.12.0 : une fonction du projet nommee comme une native de l'IHM (Random, RGB) passe avant.
        const bool nativeShadowed = natives::isOwnFunction(U) && (pendingSymbolFn_ || (sc_.project && !sc_.project->functionsNamed(name.text).empty()));
        // 1.12.7 : une fonction du projet nommee comme une nouvelle native passe avant (le moteur fait de meme).
        const bool newShadowed = sim::lib::native(U) && (pendingSymbolFn_ || (sc_.project && !sc_.project->functionsNamed(name.text).empty()));
        if (internalFunction(name.text) || (dialectFunctions().count(U) && !newShadowed)) {
            found = true;      // 1.10 (13) : une fonction interne du script, une fonction du dialecte : S1 les controle
            if (!internalFunction(name.text) && !named) nativeChecks(i, close, n);     // 1.12.7
        } else if (conversion(U)) {
            found = true;      // 1.10 (integration I2, S2) : TO_STRING(v), TO_T_RESUME(v), TO_T_MODE('auto') - un argument
            arity(1, 1);
#ifdef XPG_HMI_LANG110
        } else if (lang110::isBuiltin(U)) {
            found = true;      // 1.10 (S1) : TO_UPPER, TO_LOWER... du dialecte (S1 les controle)
#endif
        } else if (isStandardFunction(U) && !nativeShadowed) {
            found = true;
            const auto to = U.find("_TO_");
            if (to != std::string::npos && to > 0) arity(1, 1);
            for (const auto& a : kStandard) if (U == a.name) arity(a.min, a.max);
            // 1.12.0 : les couleurs et l'aleatoire (le catalogue des natives dit leurs arguments).
            if (int mn = 0, mx = 0; natives::isOwnFunction(U) && natives::arity(U, mn, mx)) arity(mn, mx);
        } else if (isHmiFunction(U)) {
            found = true;
            for (const auto& a : kHmi) if (U == a.name) arity(a.min, a.max);
        } else if (const auto fs = pendingSymbolFn_ ? siblingsOf(pendingSymbolFn_)
                                 : sc_.project ? sc_.project->functionsNamed(name.text) : std::vector<const HmiFunction*>{};
                   !fs.empty()) {
            found = true;
            // 1.11.20 : toutes ses surcharges, et TOUS leurs parametres - VAR_INPUT, VAR_IN_OUT,
            // VAR_OUTPUT (avant : les entrees seules, « Random prend 2 arguments, pas 4 »). Une
            // entree qui a une valeur par defaut et une sortie sont facultatives. Le code se lit :
            // le controle des appels juge (types et references compris) - rien a compter ici.
            if (!named && !callsChecked_) {
                int lo = -1, hi = 0;
                bool fits = false;
                for (const auto* f : fs) {
                    const auto sig = overload::signatureOf(*f);
                    int last = -1;
                    for (std::size_t k = 0; k < sig.params.size(); ++k)
                        if (!sig.params[k].optional) last = static_cast<int>(k);
                    const int mn = last + 1, mx = static_cast<int>(sig.params.size());
                    lo = lo < 0 ? mn : std::min(lo, mn);
                    hi = std::max(hi, mx);
                    fits = fits || (n >= mn && n <= mx);
                }
                if (!fits && fs.size() == 1) arity(lo, hi);
                else if (!fits)
                    reportTok(i, i + 1, S::Error, name.text + " : aucune de ses " + std::to_string(fs.size()) + " surcharges ne prend "
                                                      + std::to_string(n) + " argument" + (n > 1 ? "s" : ""));
            }
            const bool anyResult = std::any_of(fs.begin(), fs.end(), [](const HmiFunction* f) { return !f->returnType.empty(); });
            if (!statement && !anyResult)
                reportTok(i, i + 1, S::Error, name.text + " ne rend pas de valeur : appelle-la seule sur sa ligne (" + name.text + "(...);)");
        } else if (local(name.text) || (sc_.project && sc_.project->variable(name.text)) || (sc_.plcKnown && sc_.plcKnown(name.text))
                   || !sc_.plcKnown) {
            found = true;      // une instance de bloc (TON, un bloc de l'automate) ; l'automate inconnu : rien a dire
        } else if (sc_.view && (sc_.view->param(name.text)
                                || (sc_.project && popupOwner(*sc_.project, *sc_.view) && popupOwner(*sc_.project, *sc_.view)->param(name.text)))) {
            found = true;      // 1.12.19 : un parametre rappel de la popup, du symbole (Sur_Oui();)
        }
        if (!found) {
            std::vector<std::string> names;
            for (const auto& a : kStandard) names.emplace_back(a.name);
            for (const auto& a : kHmi) names.emplace_back(a.name);
            for (const auto& f : natives::functions())        // 1.12.0 : RGB, COULEUR_..., RANDOM...
                if (f.category == "couleur" || f.category == "alea") names.emplace_back(f.name);
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
        // 1.11.14 : IHM_LOG(niveau, message) - le niveau est une valeur de NIVEAU_LOG,
        // pas un nom a chercher ; le message, un texte (ses trous bien formes).
        if (U == "IHM_LOG") {
            logCall(i, close);
            return atEnd(close) ? close : close + 1;
        }
        // Les arguments : leurs noms (un argument nomme : IN := x, Q => y).
        walkNames(i + 2, close);
        return atEnd(close) ? close : close + 1;
    }

    // ---- 1.12.7 : les natives des conteneurs, de std::numeric, de <bit> et les templates ----
    // Les arguments [debut, fin) d'un appel dont la '(' est a l'index `open` et la ')' a `close`.
    [[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> argsOf(std::size_t open, std::size_t close) const {
        std::vector<std::pair<std::size_t, std::size_t>> out;
        std::size_t from = open + 1;
        int depth = 0;
        for (std::size_t k = open + 1; k < close && !atEnd(k); ++k) {
            if (isOp(k, "(") || isOp(k, "[")) ++depth;
            else if (isOp(k, ")") || isOp(k, "]")) --depth;
            else if (depth == 0 && isOp(k, ",")) {
                out.emplace_back(from, k);
                from = k + 1;
            }
        }
        if (from < close) out.emplace_back(from, close);
        return out;
    }
    // Les controles d'une native : une nouvelle (1.12.7) - des erreurs ; une d'avant - des avertissements
    // (ce qui demarrait en 1.12.6 demarre toujours ; le moteur, lui, s'arrete sur la faute).
    // Une native qui ecrirait une constante (VAR CONSTANT) : « Seuils est une constante : LIST_ADD ne peut pas
    // la changer » (la maquette ; le moteur, lui, s'arrete : « LIST_ADD : une constante ne s'ecrit pas »).
    // Son premier argument (LIST_SET, MAP_REMOVE...), la destination de LIST_COPY, les variables de
    // TUPLE_UNPACK. Une nouvelle native : une erreur ; une d'avant (LIST_ADD, MAP_CLEAR...) : un avertissement.
    void constantWritten(std::size_t i, std::size_t close, const std::string& U, const sim::lib::NativeInfo* info) {
        static const std::set<std::string> kOldWrites = {"LIST_ADD", "LIST_INSERT", "LIST_REMOVE_AT", "LIST_REMOVE", "LIST_CLEAR", "LIST_SORT",
                                                         "LIST_REVERSE", "VECTOR_PUSH", "VECTOR_POP", "VECTOR_INSERT", "VECTOR_ERASE",
                                                         "VECTOR_CLEAR", "VECTOR_RESIZE", "MAP_REMOVE", "MAP_CLEAR"};
        const auto args = argsOf(i + 1, close);
        std::size_t from = 0, to = 0;                          // les arguments ecrits [from, to)
        if (U == "LIST_COPY" || U == "LIST_TRANSFORM") from = 1, to = 2;      // 1.12.16 : LIST_TRANSFORM ecrit sa destination
        else if (U == "TUPLE_UNPACK") from = 1, to = args.size();
        else if (U == "SWAP") from = 0, to = 2;                                // 1.12.16 : SWAP ecrit ses deux variables
        else if (info ? info->writes : kOldWrites.count(U) > 0) from = 0, to = 1;
        for (std::size_t k = from; k < to && k < args.size(); ++k) {
            const auto [a, b] = args[k];
            if (b != a + 1 || !isIdent(a) || !local(toks_[a].text) || !constants_.count(up(toks_[a].text))) continue;
            reportTok(a, b, info ? Finding::Severity::Error : Finding::Severity::Warning,
                      toks_[a].text + " est une constante : " + U + " ne peut pas la changer");
        }
    }
    void nativeChecks(std::size_t i, std::size_t close, int n) {
        using S = Finding::Severity;
        const std::string U = up(toks_[i].text);
        const auto* info = sim::lib::native(U);
        constantWritten(i, close, U, info);
        // 1.13.0 : WAIT_UNTIL(condition [, delai]) - la condition doit etre un BOOL.
        if (U == "WAIT_UNTIL")
            if (const auto args = argsOf(i + 1, close); !args.empty()) {
                const auto [ca, cb] = args.front();
                for (auto& m : typeProblems(ca, cb, exprcheck::Want::Bool))
                    reportTok(ca, cb, m.find("est attendu") != std::string::npos ? S::Error : S::Warning, "WAIT_UNTIL : " + m);
            }
        const OldArity* old = info ? nullptr : oldArity(U);
        if (!info && !old && !sim::lib::oldListResizes(U)) return;
        const S sev = info ? S::Error : S::Warning;
        const int mn = info ? info->min : old ? old->min : -1, mx = info ? info->max : old ? old->max : -1;
        if (mn >= 0 && (n < mn || (mx >= 0 && n > mx)))
            reportTok(i, i + 1, sev, toks_[i].text + " prend " + arityText(mn, mx) + ", pas " + std::to_string(n));
        const auto args = argsOf(i + 1, close);
        if (args.empty()) return;
        // Le premier argument : un nom dont le type est connu.
        const auto [a0, b0] = args.front();
        const bool designated = isIdent(a0) && pathEnd(a0) == b0;
        const ContainerType ct = containerOf(designated ? typeOfName(textOf(a0, b0)) : std::string{});
        const bool resizes = info ? info->resizes : sim::lib::oldListResizes(U);
        if (ct.kind == ContainerType::K::Array && resizes)
            reportTok(a0, b0, sev, U + " : un ARRAY a une taille fixe (" + (ct.cells >= 0 ? std::to_string(ct.cells) : std::string("?"))
                                       + " cases) \xE2\x80\x94 une LIST ou un VECTOR est attendu");
        // 1.12.7 : un SET se range tout seul - ce qui ecrit a une place lui est refuse (une erreur : SET est nouveau).
        const std::string t0 = designated ? typeOfName(textOf(a0, b0)) : std::string{};
        if (setText(t0) && setRefusedNative(U)) reportTok(i, i + 1, S::Error, U + kSetRefusedText);
        if ((U == "LIST_COPY" || U == "LIST_TRANSFORM") && args.size() >= 2) {
            const auto [a1, b1] = args[1];
            if (isIdent(a1) && pathEnd(a1) == b1 && setText(typeOfName(textOf(a1, b1)))) reportTok(i, i + 1, S::Error, U + kSetRefusedText);
        }
        if (!info) return;
        stdChecks(i, U, args);                                                  // 1.12.16
        // SET_ADD, SET_REMOVE, SET_HAS : un SET ; RING_ADD : un RING (quand le type est connu) ; la valeur
        // ecrite, un litteral, dans le type des elements ('abc' dans un SET OF INT).
        if (info->group == "set" && (U == "SET_ADD" || U == "SET_REMOVE" || U == "SET_HAS") && !t0.empty() && !setText(t0))
            reportTok(a0, b0, S::Error, U + " : un SET est attendu (SET OF T), pas " + withArticle(t0));
        if (info->group == "ring" && !t0.empty() && !ringText(t0))
            reportTok(a0, b0, S::Error, U + " : un RING est attendu (RING[N] OF T), pas " + withArticle(t0));
        if ((info->group == "ring" || (info->group == "set" && (U == "SET_ADD" || U == "SET_REMOVE" || U == "SET_HAS"))) && args.size() >= 2
            && (setText(t0) || ringText(t0)) && ct.kind == ContainerType::K::List) {
            const auto [a1, b1] = args[1];
            const Kind want = kindOf(ct.element);
            if (b1 == a1 + 1 && toks_[a1].k == Tok::K::Str && (want == Kind::Num || want == Kind::Bool || want == Kind::Time))
                reportTok(a1, b1, S::Error, U + " : " + toks_[a1].text + " n'est pas un " + up(ct.element));
            else if (b1 == a1 + 1 && toks_[a1].k == Tok::K::Num && want == Kind::Text)
                reportTok(a1, b1, S::Error, U + " : " + toks_[a1].text + " n'est pas une STRING : \xC3\xA9" "cris '" + toks_[a1].text + "'");
        }
        // TUPLE_GET, TUPLE_SET : le numero du champ, une constante, dans le TUPLE.
        if ((U == "TUPLE_GET" || U == "TUPLE_SET") && ct.kind == ContainerType::K::Tuple && args.size() >= 2) {
            const auto [a1, b1] = args[1];
            if (b1 == a1 + 1 && toks_[a1].k == Tok::K::Num) {
                const long long k = std::atoll(toks_[a1].text.c_str());
                if (k < 1 || k > static_cast<long long>(ct.fields.size()))
                    reportTok(a1, b1, S::Error, U + " : " + ct.text + " a " + std::to_string(ct.fields.size()) + " champs, pas " + std::to_string(k));
            }
        }
        // Le membre ('Poids', 'Item2') d'une liste de structures ou de TUPLE, quand le type est connu.
        std::pair<std::size_t, std::size_t> member{0, 0};
        if (U == "LIST_SORT_BY" && args.size() >= 2) member = args[1];
        else if (info->member && info->max > 0 && args.size() == static_cast<std::size_t>(info->max)) member = args.back();
        const auto [am, bm] = member;
        if (bm == am + 1 && toks_[am].k == Tok::K::Str && (ct.kind == ContainerType::K::List || ct.kind == ContainerType::K::Array)) {
            const std::string& q = toks_[am].text;
            const std::string name = q.size() >= 2 ? q.substr(1, q.size() - 2) : std::string{};
            const ContainerType el = containerOf(ct.element);
            if (el.kind == ContainerType::K::Tuple) {
                const std::string N = up(name);
                const long long k = N.rfind("ITEM", 0) == 0 ? std::atoll(N.c_str() + 4) : 0;
                if (k < 1 || k > static_cast<long long>(el.fields.size()))
                    reportTok(am, bm, S::Error, U + " : un " + el.text + " a Item1 \xC3\xA0 Item" + std::to_string(el.fields.size()) + ", pas '" + name + "'");
            } else if (const auto ms = sc_.project ? types::membersOf(*sc_.project, ct.element) : std::vector<TypeMember>{}; !ms.empty()) {
                std::vector<std::string> pool;
                bool found = false;
                for (const auto& m : ms) {
                    pool.push_back(m.name);
                    found = found || up(m.name) == up(name);
                }
                if (!found) reportTok(am, bm, S::Error, ct.element + " n'a pas de membre " + name + sim::lib::closest(name, pool));
            } else if (elementary(ct.element)) {
                reportTok(am, bm, S::Error, U + " : des " + up(ct.element) + " n'ont pas de membres");
            }
        }
    }
    // ---- 1.12.16 : les natives std:: - FORMAT (ses {} et ses valeurs, quand le modele est ecrit), SWAP et EXCHANGE
    // (des variables ; pour SWAP, du meme type), LIST_TRANSFORM (son OP : une TRANSFORMATION, ou une fonction du
    // projet a un parametre, du type des elements). Les memes mots que le moteur.
    void stdChecks(std::size_t i, const std::string& U, const std::vector<std::pair<std::size_t, std::size_t>>& args) {
        using S = Finding::Severity;
        const auto quoted = [&](std::size_t a, std::size_t b, std::string& text) {
            if (b != a + 1 || toks_[a].k != Tok::K::Str || toks_[a].text.size() < 2) return false;
            text = toks_[a].text.substr(1, toks_[a].text.size() - 2);
            return true;
        };
        const auto variable = [&](std::size_t k) { return k < args.size() && isIdent(args[k].first) && pathEnd(args[k].first) == args[k].second; };
        if (U == "FORMAT" && !args.empty()) {
            std::string model, bad;
            if (quoted(args[0].first, args[0].second, model)) {
                std::size_t holes = 0;
                for (std::size_t k = 0; k < model.size() && bad.empty(); ++k) {
                    if (model[k] == '}') {
                        if (k + 1 < model.size() && model[k + 1] == '}') ++k;
                        else bad = "une } seule \xE2\x80\x94 }} pour une accolade";
                    } else if (model[k] == '{') {
                        if (k + 1 < model.size() && model[k + 1] == '{') {
                            ++k;
                            continue;
                        }
                        const auto close = model.find('}', k + 1);
                        if (close == std::string::npos) {
                            bad = "une { sans } \xE2\x80\x94 {{ pour une accolade";
                            break;
                        }
                        const std::string inside = model.substr(k + 1, close - k - 1);
                        if (!inside.empty() && inside.front() != ':') bad = "{" + inside + "} \xE2\x80\x94 {} prend la valeur suivante, {:.1f} la met en forme";
                        ++holes;
                        k = close;
                    }
                }
                const std::size_t values = args.size() - 1;
                if (!bad.empty()) reportTok(args[0].first, args[0].second, S::Error, U + " : " + bad);
                else if (holes != values)
                    reportTok(i, i + 1, S::Error, U + " : " + std::to_string(holes) + " {} pour " + std::to_string(values) + (values > 1 ? " valeurs" : " valeur"));
            }
        }
        if (U == "SWAP" || U == "EXCHANGE") {
            for (std::size_t k = 0; k < (U == "SWAP" ? 2u : 1u) && k < args.size(); ++k)
                if (!variable(k))
                    reportTok(args[k].first, args[k].second, S::Error,
                              U + " : une variable est attendue en " + (k == 0 ? "1er" : "2e") + " argument, pas " + textOf(args[k].first, args[k].second));
            if (U == "SWAP" && variable(0) && variable(1)) {
                const std::string a = textOf(args[0].first, args[0].second), b = textOf(args[1].first, args[1].second);
                const std::string ta = typeOfName(a), tb = typeOfName(b);
                if (!ta.empty() && !tb.empty() && sameWritten(ta) != sameWritten(tb))
                    reportTok(i, i + 1, S::Error, U + " : " + a + " est un " + ta + ", " + b + " un " + tb + " \xE2\x80\x94 deux variables du m\xC3\xAAme type");
            }
        }
        if (U == "LIST_TRANSFORM" && args.size() == 3) {
            const std::size_t a2 = args[2].first, b2 = args[2].second;
            if (b2 == a2 + 1 && toks_[a2].k == Tok::K::Lit && up(toks_[a2].text.substr(0, toks_[a2].text.find('#'))) != "TRANSFORMATION")
                reportTok(a2, b2, S::Error, U + " : OP est une TRANSFORMATION (TRANSFORMATION#Carre\xE2\x80\xA6) ou le nom d'une fonction du projet ('VersKelvin'), pas "
                                                + toks_[a2].text);
            std::string name;
            if (!quoted(a2, b2, name) || !sc_.project || internalFunction(name)) return;
            const auto fs = sc_.project->functionsNamed(name);
            if (fs.empty()) {
                std::vector<std::string> pool;
                for (const auto& f : sc_.project->programs.functions) pool.push_back(f.name);
                reportTok(a2, b2, S::Error, U + " : " + name + " n'est pas une fonction du projet" + sim::lib::closest(name, pool));
                return;
            }
            if (fs.size() > 1) return;                                          // ses surcharges : le moteur choisit
            const auto sig = overload::signatureOf(*fs.front());
            const std::string& fn = fs.front()->name;
            int required = 0;
            for (const auto& p : sig.params) required += p.optional ? 0 : 1;
            if (sig.params.empty() || required > 1) {
                reportTok(a2, b2, S::Error, U + " : " + fn + (sig.params.empty() ? std::string(" ne prend aucun param\xC3\xA8tre") : " prend " + plural(required, "param\xC3\xA8tre"))
                                                + " \xE2\x80\x94 OP est une fonction \xC3\xA0 un param\xC3\xA8tre");
                return;
            }
            if (sig.params.front().mode != overload::Mode::In) {
                reportTok(a2, b2, S::Error, U + " : le param\xC3\xA8tre de " + fn + " est un VAR_IN_OUT ou un VAR_OUTPUT \xE2\x80\x94 OP lui donne une valeur");
                return;
            }
            if (trimmedText(sig.result).empty()) {
                reportTok(a2, b2, S::Error, U + " : " + fn + " ne rend pas de valeur");
                return;
            }
            // Les types : son parametre et les elements de SOURCE, ce qu'elle rend et ceux de DESTINATION (quand on les connait).
            const auto elementOf = [&](std::size_t k) -> std::string {
                if (!variable(k)) return {};
                const ContainerType ct = containerOf(typeOfName(textOf(args[k].first, args[k].second)));
                return ct.kind == ContainerType::K::List || ct.kind == ContainerType::K::Array ? ct.element : std::string{};
            };
            const std::string source = elementOf(0), destination = elementOf(1);
            const Kind param = kindOf(sig.params.front().type), result = kindOf(sig.result);
            if (!source.empty() && param != Kind::Other && kindOf(source) != Kind::Other && param != kindOf(source))
                reportTok(a2, b2, S::Error, U + " : " + fn + " prend " + withArticle(up(sig.params.front().type)) + ", les \xC3\xA9l\xC3\xA9ments de "
                                                + textOf(args[0].first, args[0].second) + " sont des " + up(source));
            if (!destination.empty() && result != Kind::Other && kindOf(destination) != Kind::Other && result != kindOf(destination))
                reportTok(a2, b2, S::Error, U + " : " + fn + " rend " + withArticle(up(sig.result)) + ", " + textOf(args[1].first, args[1].second)
                                                + " attend des " + up(destination));
        }
    }
    // Un template a l'index k : NUMERIC_LIMITS<UINT>.MAX(), NUMBERS<REAL>.PI, NUMBERS.PI, SATURATE_CAST<INT>(x),
    // ADD_SAT<INT>(a, b). Rend l'index qui le suit (ses arguments lus), ou k : pas un template (une variable
    // du meme nom garde son sens). Le type et le membre d'un NOM<T> sont dits par la lecture du script.
    std::size_t templateUse(std::size_t k) {
        if (!isIdent(k) || (k > 0 && isOp(k - 1, "."))) return k;
        const auto* info = sim::lib::templateInfo(toks_[k].text);
        if (!info) return k;
        const std::string& name = toks_[k].text;
        if (local(name) || dialectName(name, toks_[k].at) || (sc_.project && sc_.project->variable(name))) return k;
        using S = Finding::Severity;
        const bool angled = isOp(k + 1, "<") && isIdent(k + 2) && isOp(k + 3, ">");
        // NOM<TYPE sans son > (NUMERIC_LIMITS<UINT.MAX(), SATURATE_CAST<INT(x)) : « le type, puis > » (la maquette).
        // Seulement devant un nom de type : NOM < x reste une comparaison.
        if (!angled && isOp(k + 1, "<") && isIdent(k + 2) && !isOp(k + 3, "#") && sim::lib::elementaryTypeName(toks_[k + 2].text)) {
            reportTok(k, k + 3, S::Error, std::string(info->name) + " : le type, puis > \xE2\x80\x94 " + std::string(info->example));
            return k + 3;
        }
        std::size_t next = angled ? k + 4 : k + 1;
        const auto closeOf = [&](std::size_t open) {
            int depth = 0;
            std::size_t m = open;
            for (; !atEnd(m); ++m) {
                if (isOp(m, "(")) ++depth;
                else if (isOp(m, ")") && --depth == 0) break;
            }
            return m;
        };
        if (info->isClass) {
            if (!angled && info->required) reportTok(k, k + 1, S::Error, std::string(info->hint));
            else if (!angled && !isOp(next, "."))                  // NUMBERS seul : une constante est attendue
                reportTok(k, k + 1, S::Error, std::string(info->name) + " : choisis une constante \xE2\x80\x94 " + std::string(info->name) + ".PI (en "
                                                  + std::string(info->dflt) + ")");
            if (isOp(next, ".") && isIdent(next + 1)) {
                const std::string member = toks_[next + 1].text;
                const bool called = isOp(next + 2, "(");
                if (!angled && !info->required) {                  // NUMBERS.PI : la lecture du script ne le controle pas
                    if (const auto* m = sim::lib::templateMember(info->name, member); !m) {
                        std::vector<std::string> pool;
                        for (const auto& x : sim::lib::numbersMembers()) pool.emplace_back(x.name);
                        reportTok(next + 1, next + 2, S::Error, std::string(info->name) + " n'a pas de membre " + member + sim::lib::closest(member, pool));
                    } else if (called) {
                        reportTok(next + 1, next + 2, S::Error, std::string(m->name) + " est une constante : sans parenth\xC3\xA8ses");
                    }
                }
                next += 2;
                if (called) {
                    const std::size_t c = closeOf(next);
                    next = atEnd(c) ? c : c + 1;
                }
            }
            return next;
        }
        if (!angled) {
            if (info->required && isOp(k + 1, "(")) reportTok(k, k + 1, S::Error, std::string(info->hint));
            return k;                                              // MIDPOINT(a, b), ADD_SAT(a, b) : un appel ordinaire
        }
        if (!isOp(next, "(")) return next;
        const std::size_t close = closeOf(next);
        const int n = static_cast<int>(argsOf(next, close).size());
        if (const auto* nat = sim::lib::native(info->name); nat && (n < nat->min || (nat->max >= 0 && n > nat->max)))
            reportTok(k, k + 1, S::Error, std::string(info->name) + " prend " + arityText(nat->min, nat->max) + ", pas " + std::to_string(n));
        walkNames(next + 1, close);
        return atEnd(close) ? close : close + 1;
    }

    // 1.11.14 : IHM_LOG(INFO, 'Pompe {Numero} demarree') : le niveau, le message.
    void logCall(std::size_t i, std::size_t close) {
        using S = Finding::Severity;
        std::vector<std::pair<std::size_t, std::size_t>> args;     // [debut, fin) de chaque argument
        std::size_t from = i + 2;
        int depth = 0;
        for (std::size_t k = from; k < close && !atEnd(k); ++k) {
            if (isOp(k, "(") || isOp(k, "[")) ++depth;
            else if (isOp(k, ")") || isOp(k, "]")) --depth;
            else if (depth == 0 && isOp(k, ",")) {
                args.emplace_back(from, k);
                from = k + 1;
            }
        }
        if (from < close) args.emplace_back(from, close);
        if (!args.empty()) {
            const auto [a, b] = args[0];
            const bool one = b == a + 1 && (toks_[a].k == Tok::K::Ident || toks_[a].k == Tok::K::Lit);
            if (!one || !logLevelByName(toks_[a].text))
                reportTok(a, std::max(b, a + 1), S::Error,
                          "IHM_LOG : le premier argument est le niveau \xE2\x80\x94 TRACE, DEBUG, INFO, SUCCESS, WARNING, ERROR ou CRITICAL "
                          "(trouv\xC3\xA9 : " + (b > a ? textOf(a, b) : std::string("rien")) + ")");
            else if (toks_[a].k == Tok::K::Ident && (local(toks_[a].text) || (sc_.project && sc_.project->variable(toks_[a].text))))
                // une variable du meme nom passe avant le niveau (comme pour une valeur d'enumeration)
                reportTok(a, b, S::Warning,
                          "IHM_LOG : " + toks_[a].text + " est aussi une variable \xE2\x80\x94 c'est sa valeur qui servira de niveau ; "
                          "\xC3\xA9" "cris NIVEAU_LOG#" + std::string(logLevelName(*logLevelByName(toks_[a].text))) + " pour le niveau");
        }
        if (args.size() < 2) return;
        const auto [a, b] = args[1];
        if (b == a + 1 && toks_[a].k == Tok::K::Str) {
            const std::string& q = toks_[a].text;
            const std::string inner = q.size() >= 2 ? q.substr(1, q.size() - 2) : std::string{};
            for (const auto& err : TextTemplate::compile(inner).errors()) reportTok(a, b, S::Error, "IHM_LOG : le message - " + err);
            // Un { sans } reste du texte (TextTemplate) : presque toujours une faute de frappe.
            for (std::size_t k = 0; k < inner.size(); ++k) {
                if (inner.compare(k, 2, "{{") == 0 || inner.compare(k, 2, "}}") == 0) { ++k; continue; }
                if (inner[k] == '{' && inner.find('}', k + 1) == std::string::npos) {
                    reportTok(a, b, S::Warning, "IHM_LOG : le message a un \xC2\xAB { \xC2\xBB sans \xC2\xAB } \xC2\xBB \xE2\x80\x94 il s'\xC3\xA9" "crira tel quel "
                                                "(\xC2\xAB {{ \xC2\xBB pour une accolade)");
                    break;
                }
            }
            return;
        }
        const bool number = b == a + 1 && (toks_[a].k == Tok::K::Num || isWord(a, "TRUE") || isWord(a, "FALSE"));
        if (number)
            reportTok(a, b, S::Error, "IHM_LOG : le message est un texte - 'Pompe d\xC3\xA9marr\xC3\xA9" "e', ou une variable STRING (trouv\xC3\xA9 : "
                                          + textOf(a, b) + ")");
        else
            walkNames(a, b);
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
            if (const std::size_t past = templateUse(k); past != k) {                  // 1.12.7 : NUMERIC_LIMITS<UINT>.MAX()
                k = std::max(past, k + 1);
                continue;
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
        c.calls = false;                                     // 1.11.20 : les appels aussi (callcheck, une fois)
        c.script = true;                                     // 1.12.17 : ASCII_TO_STRING(tableau) y marche
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
                || m.find("n'a pas de membre") != std::string::npos
                // 1.12.16 : une native qui ecrit (Ancien := EXCHANGE(Etat, Nouvel_Etat), n := LIST_REMOVE_ALL(L, x)) ecrit
                // vraiment dans un script : ce que l'expression fx en dit ne vaut pas ici.
                || m.find(" : une expression en lit une copie") != std::string::npos || m.find(" : une expression ne change rien") != std::string::npos
                || m.find("un nom de fonction en texte n'est ni") != std::string::npos)      // 1.12.20 : dit a sa place
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
        // 1.13.0 (lot J6) : le verdict d'unite de l'affectation - meme famille : converti d'office (une info),
        // ou EN exige si l'option « Unites strictes » ; familles differentes : un refus (une erreur).
        {
            const bool strict = sc_.project && sc_.project->config.strictUnits;
            for (const auto& u : exprcheck::assignmentUnit(typeContext(), target, textOf(j + 1, e), strict))
                reportTok(j + 1, e, u.warning ? Finding::Severity::Warning : Finding::Severity::Error, u.message);
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
                    || U == "END_REPEAT" || U == "UNTIL" || U == "FUNCTION" || U == "END_FUNCTION"
                    || U == "CATCH" || U == "END_TRY"                        // 1.12.1
                    || U == "RELEVER")                                       // 1.13.0 (LEVER se reconnait a sa parenthese)
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
        // 1.12.20 : TEMPLATE <T : ANY_NUM> devant une fonction interne - ses generiques ne sont pas des noms a lire.
        if (U == "TEMPLATE" && isOp(i + 1, "<")) {
            std::size_t k = i + 2;
            while (!atEnd(k) && !isOp(k, ">")) ++k;
            i = after(k);
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
        // 1.13.0 : TRY / CATCH / END_TRY - de quel CATCH on est dans le handler (RELEVER n'y est permis).
        if (U == "TRY") { tryStack_.push_back(false); ++i; return; }
        if (U == "END_TRY") {
            if (!tryStack_.empty()) { if (tryStack_.back()) --handlerDepth_; tryStack_.pop_back(); }
            ++i;
            return;
        }
        if (U == "CATCH") {
            if (!tryStack_.empty() && !tryStack_.back()) { tryStack_.back() = true; ++handlerDepth_; }
            // 1.12.1 : CATCH e - la variable (un nom seul sur la ligne du CATCH) ; 1.13.0 : un T_Erreur.
            if (isCatchVar(i)) {
                walkNames(i + 1, i + 2);
                i = isOp(i + 2, ";") ? i + 3 : i + 2;
                return;
            }
            ++i;
            return;
        }
        // 1.13.0 : RELEVER - n'est permis que dans un CATCH ; reemet l'erreur courante.
        if (U == "RELEVER") {
            if (handlerDepth_ == 0)
                reportTok(i, i + 1, Finding::Severity::Error, "RELEVER n'est permis que dans un CATCH (il r\xC3\xA9\xC3\xA9met l'erreur attrap\xC3\xA9" "e)");
            i = after(i);
            return;
        }
        // 1.13.0 : LEVER('E_Code' [, 'message']) - le code (un texte non vide) et le message (un texte,
        // ses trous {expr} relus). La parenthese suit le mot.
        if (U == "LEVER" && isOp(i + 1, "(")) {
            int depth = 0;
            std::size_t close = i + 1;
            for (; !atEnd(close); ++close) {
                if (isOp(close, "(")) ++depth;
                else if (isOp(close, ")") && --depth == 0) break;
            }
            const auto args = argsOf(i + 1, close);
            if (args.empty())
                reportTok(i, i + 1, Finding::Severity::Error, "LEVER('E_Code' [, 'message']) : il manque le code (un texte, ex. 'E_Niveau')");
            else {
                const auto [a0, b0] = args.front();
                if (b0 == a0 + 1 && toks_[a0].k == Tok::K::Str && toks_[a0].text.find_first_not_of("'\" \t") == std::string::npos)
                    reportTok(a0, b0, Finding::Severity::Error, "LEVER : le code ne doit pas \xC3\xAAtre vide (ex. 'E_Niveau')");
            }
            for (const auto& [a, b] : args) walkNames(a, b);
            i = isOp(after(close), ";") ? after(close) + 1 : after(close);
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
    // 1.11.20 : les appels des fonctions de l'utilisateur, lus par le simulateur et juges par la
    // regle du moteur (E/S et sorties comptees, references, surcharges).
    bool callsRead = false;
    const std::string unused = names::blankUsings(code);         // 1.13.0 (lot J8) : USING ne va pas au simulateur
    const auto calls = callcheck::checkCode(callcheck::Context{scope.project, scope.view, scope.function, scope.plc, scope.ns}, unused, &callsRead);
    Walker w(scope, code);
    w.setCallsChecked(callsRead);
    for (const auto& m : markers::find(source))   // 1.11.1 (REP-10)
        if (const auto roots = scanRoots(m.content(source)); !roots.empty()) w.markerRoots.insert(up(roots.front()));
    auto out = w.run();
    bool added = false;
    // 1.13.0 (lot J8) : les espaces de noms - USING inconnu ou inutile, nom ambigu sous USING, nom d'un autre espace
    // sans USING, Gaz.Inconnu (avec la correction d'un clic).
    if (scope.project && (names::active(*scope.project) || names::mentionsUsing(code))) {
        const names::Index ix(*scope.project);
        std::vector<std::string> hidden;
        if (scope.function) hidden.push_back(scope.function->name);
        for (const auto& nf : names::check(ix, code, w.ownNamespace(), hidden, scope.function != nullptr, true,
                                           [&w](std::string_view n) { return w.knownOtherwise(n); })) {
            Finding f;
            f.severity = nf.warning ? Finding::Severity::Warning : Finding::Severity::Error;
            f.line = nf.line;
            f.column = nf.column;
            f.length = nf.length;
            f.message = nf.message;
            f.name = nf.name;
            f.suggestion = nf.suggestion;
            f.fixFrom = nf.fixFrom;
            f.fixTo = nf.fixTo;
            // un nom deja dit par le controle (le meme endroit) : une fois
            const bool twice = std::any_of(out.begin(), out.end(), [&](const Finding& o) {
                return o.line == f.line && o.column == f.column && o.name == f.name && !f.name.empty();
            });
            if (twice)
                out.erase(std::remove_if(out.begin(), out.end(), [&](const Finding& o) { return o.line == f.line && o.column == f.column && o.name == f.name; }),
                          out.end());
            out.push_back(std::move(f));
            added = true;
        }
    }
    for (const auto& c : calls) {
        Finding f;
        f.severity = c.error ? Finding::Severity::Error : Finding::Severity::Warning;
        f.line = c.line;
        f.column = c.column;
        f.length = c.length;
        f.message = c.message;
        out.push_back(std::move(f));
        added = true;
    }
    // 1.10 (decisions 13 et 13 bis) : les constructions du dialecte IHM,
    // controlees par le chantier S1 (lang110::analyze) - une seule fois
    // chaque constat, dans l'ordre du texte.
    for (auto f : w.dialect()) {
        if (callsRead && f.callArity) continue;           // 1.11.20 : le controle des appels l'a dit
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

std::vector<Finding> check(const Scope& scope, std::string_view body, const std::vector<Declaration>& decls, decl::Role role,
                           const std::vector<Declaration>* inherited) {
    const bool inherits = inherited && std::any_of(inherited->begin(), inherited->end(),
                                                   [](const Declaration& d) { return d.kind == DeclKind::Parameter; });
    if (decls.empty() && !inherits) return check(scope, body);
    const auto composed = decl::composeCode(body, decls, role, inherited);
    auto out = check(scope, composed.text);
    for (auto& f : out) {
        if (composed.inDeclarations(f.line, f.column)) {
            const Id id = composed.declarationAt(f.line, f.column);
            std::string name = "?";
            for (const auto* list : {&decls, inherited})
                if (list)
                    for (const auto& d : *list)
                        if (d.id == id) name = d.name;
            f.message = "d\xC3\xA9" "claration \xC2\xAB " + name + " \xC2\xBB : " + f.message;
            f.line = f.column = f.length = 0;
            f.fixLine = 0;                       // une correction du texte : pas dans une declaration
        } else {
            f.column = composed.bodyColumn(f.line, f.column);
        }
    }
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
        i.fixFrom = f.fixFrom;          // 1.12.20 : la correction d'un clic
        i.fixTo = f.fixTo;
        out.push_back(std::move(i));
    };
    for (const auto& sc : p.programs.scripts) {
        if (sc.lang != ScriptLang::ST || (focus && !focus->scripts.count(sc.id))) continue;
        Scope own = scopeFor(nullptr, nullptr);
        own.ns = sc.ns;                                           // 1.13.0 (lot J8) : il voit son espace sans prefixe
        for (const auto& f : check(own, sc.body, sc.decls, decl::Role::Script)) {
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
            for (const auto& f : check(scopeFor(&v, nullptr), sc.body, sc.decls, decl::Role::Script)) {
                Issue i;
                i.category = "Script";
                i.view = v.id;
                i.property = sc.name;
                i.script = sc.id;
                fill(std::move(i), f);
            }
        }
        // 1.12.16 : les fonctions de la vue (ou de la popup) - elles voient ses constantes et ses fonctions.
        if (!isSymbolView(v))
            for (const auto& fn : v.functions) {
                if (focus && !focus->functions.count(fn.id)) continue;
                for (const auto& f : check(scopeFor(&v, &fn), fn.body, fn.decls, decl::Role::Function)) {
                    Issue i;
                    i.category = "Fonction";
                    i.view = v.id;
                    i.property = v.name + "." + fn.name;
                    i.item = fn.id;
                    fill(std::move(i), f);
                }
            }
        const auto actions = [&](const Object* o, const std::vector<Action>& list) {
            for (std::size_t k = 0; k < list.size(); ++k) {
                const auto& a = list[k];
                if (a.operation != Operation::RunScript && a.operation != Operation::Call) continue;
                // 1.12.19 : Appeler - l'appel qu'elle fait (Valider(1);), une fonction ou un rappel recu.
                const bool call = a.operation == Operation::Call;
                if (call && callCode(a).empty()) continue;                 // "rien a appeler" : Generer le dit
                // 1.12.13 : un geste sur un tableau - les parametres qu'il passe (Ligne, Element, Titre...) sont connus, sans
                // type (ils dependent de la ligne) ; Element^ se lit Element, comme le moteur.
                Scope scope = scopeFor(&v, nullptr);
                std::string code = call ? callCode(a) : a.value;
                if (o && o->kind == hmi::Kind::Table && triggerIsTable(a.trigger)) {
                    std::set<std::string> names;
                    for (const auto& prm : table::receivedParams(p, *o, a.trigger)) names.insert(up(prm.name));
                    scope.plcKnown = [names, prev = scope.plcKnown](std::string_view root) { return names.count(up(root)) != 0 || !prev || prev(root); };
                    code = table::withoutCaret(code);
                }
                for (const auto& f : check(scope, code)) {
                    Issue i;
                    i.category = "Action";
                    i.view = v.id;
                    i.object = o ? o->id : kNoId;
                    i.property = actionWhere(k, a);
                    Finding g = f;
                    g.message = call ? f.message
                                     : "ligne " + std::to_string(f.line) + ", colonne " + std::to_string(f.column) + " : " + f.message;
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
        for (const auto& f : check(scopeFor(nullptr, &fn), fn.body, fn.decls, decl::Role::Function)) {
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
