// =============================================================================
//  tests/hmi_types_test.cpp - 1.11.19 (refonte des scripts, lot 6) : LE REGISTRE
//  DES TYPES ET LA REGLE DE CONVERSION (hmi::typereg)
// -----------------------------------------------------------------------------
//  - les usages : chaque liste d'avant le registre (variable IHM, locale, parametre
//    de popup, operande, retour) est retrouvee telle quelle ;
//  - le registre d'un projet : les structures et les enumerations (cle ihm:<id>,
//    provenance, membres, valeurs), les DDT (api:NOM) ; une cle qui survit au
//    renommage ; un type ajoute apres coup ;
//  - la lecture d'un texte de type : les types de base, les tableaux (1, 2, N
//    dimensions), STRING[n], MAP, REF_TO, POINTER TO, MAP_ITERATOR, un nom inconnu,
//    un type qui ne convient pas a l'usage, la forme et la cle d'un type construit ;
//  - LA REGLE, case par case, contre une copie figee des trois anciennes : les
//    parametres (typeAccepts, typeExact) acceptent exactement les memes paires ;
//    les operateurs (matchCost) aussi, seuls des couts changent (liste exacte) ;
//    une valeur gardee (simdata::convert) passe ou non exactement comme avant.
// =============================================================================
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiPopupParams.hpp"
#include "../src/hmi/HmiTypeRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace hmi;
namespace tr = hmi::typereg;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : " ") + x;
    return s;
}
std::set<std::string> asSet(const std::vector<std::string>& v) { return {v.begin(), v.end()}; }

// =============================================================================
//  LES ANCIENNES REGLES, FIGEES (copiees telles qu'avant le lot 6) pour comparer.
// =============================================================================
namespace old {

std::string up(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}
std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
// HmiPopupParams.cpp (1.9 a 1.11.18)
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
std::string normalizedType(std::string_view type) {
    std::string out;
    for (const char c : type)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (out.empty()) return "ANY";
    if (out.rfind("STRING[", 0) == 0) return "STRING";
    return out;
}
bool typeAccepts(std::string_view declared, std::string_view given) {
    const std::string d = normalizedType(declared);
    if (trim(given).empty()) return true;
    const std::string g = normalizedType(given);
    if (d == "ANY" || g == "ANY") return true;
    if (d == g) return true;
    const IntInfo di = intInfo(d), gi = intInfo(g);
    if (di.isInt && gi.isInt) {
        if (di.bitString) return !gi.isSigned && gi.bits <= di.bits;
        if (di.isSigned) return gi.isSigned ? gi.bits <= di.bits : gi.bits < di.bits;
        return !gi.isSigned && gi.bits <= di.bits;
    }
    if (d == "REAL") return gi.isInt && gi.bits <= 16;
    if (d == "LREAL") return gi.isInt || g == "REAL";
    return false;
}
bool typeExact(std::string_view declared, std::string_view given) {
    if (trim(given).empty()) return true;
    const std::string d = normalizedType(declared), g = normalizedType(given);
    return d == "ANY" || g == "ANY" || d == g;
}
// HmiOperators.cpp (1.10 a 1.11.18)
bool sameTypeName(std::string_view a, std::string_view b) { return up(trim(a)) == up(trim(b)); }
int numericRank(std::string_view t) {
    const std::string u = up(trim(t));
    if (u == "SINT" || u == "USINT" || u == "BYTE") return 1;
    if (u == "INT" || u == "UINT" || u == "WORD") return 2;
    if (u == "DINT" || u == "UDINT" || u == "DWORD") return 3;
    if (u == "LINT" || u == "ULINT" || u == "LWORD") return 4;
    if (u == "REAL") return 10;
    if (u == "LREAL") return 11;
    return 0;
}
int matchCost(std::string_view wanted, std::string_view given) {
    if (sameTypeName(wanted, given)) return 0;
    const int w = numericRank(wanted), g = numericRank(given);
    if (w == 0 || g == 0) return -1;
    if (w >= 10 && g >= 10) return 1;
    if (w < 10 && g < 10) return g <= w ? 1 : -1;
    if (w >= 10 && g < 10) return 2;
    return -1;
}

} // namespace old

// Les types de la matrice : la base, ANY, une structure, un tableau, l'inconnu (vide).
const std::vector<std::string> kMatrix = {"BOOL", "SINT", "INT", "DINT", "LINT", "USINT", "UINT", "UDINT", "ULINT", "BYTE",
                                          "WORD", "DWORD", "LWORD", "REAL", "LREAL", "STRING", "STRING[20]", "TIME", "ANY",
                                          "T_Four", "ARRAY[0..9] OF REAL", ""};

// ---------------------------------------------------------------- les usages ----
void usages() {
    std::printf("-- les usages : les listes d'avant, retrouvees\n");
    const auto& r = tr::baseRegistry();
    // kVariableTypes (HmiModel.hpp, lot 16)
    check(asSet(r.names(tr::UseVariable)) == asSet({"BOOL", "INT", "UINT", "WORD", "DINT", "UDINT", "DWORD", "REAL", "LREAL", "STRING", "TIME"}),
          "variable IHM : les types de kVariableTypes - " + joined(r.names(tr::UseVariable)));
    // kLocalTypes (HmiScript.hpp)
    check(asSet(r.names(tr::UseDeclaration)) == asSet({"BOOL", "INT", "DINT", "UINT", "UDINT", "SINT", "USINT", "REAL", "LREAL", "WORD", "DWORD",
                                                       "BYTE", "TIME", "STRING"}),
          "declaration : les types de kLocalTypes - " + joined(r.names(tr::UseDeclaration)));
    // richLocalType acceptait aussi LINT et ULINT (sans les proposer)
    check(r.resolve("LINT", tr::UseDeclaration).ok && r.resolve("ulint", tr::UseDeclaration).ok
              && !r.resolve("LWORD", tr::UseDeclaration).ok,
          "declaration : LINT et ULINT acceptes sans etre proposes, LWORD refuse");
    // params::baseTypes (HmiPopupParams.cpp)
    check(asSet(r.names(tr::UseParameter)) == asSet({"BOOL", "INT", "UINT", "WORD", "DINT", "UDINT", "DWORD", "REAL", "LREAL", "STRING", "TIME", "ANY"}),
          "parametre de popup : les types de base proposes - " + joined(r.names(tr::UseParameter)));
    // typeKnown acceptait tout entier (SINT, LINT, BYTE...)
    for (const char* t : {"SINT", "LINT", "USINT", "ULINT", "BYTE", "LWORD"})
        check(r.resolve(t, tr::UseParameter).ok, std::string("parametre de popup : ") + t + " accepte (typeKnown)");
    // kBaseTypes (HmiOperators.cpp)
    check(asSet(r.names(tr::UseOperand)) == asSet({"BOOL", "SINT", "INT", "DINT", "LINT", "USINT", "UINT", "UDINT", "ULINT", "BYTE", "WORD", "DWORD",
                                                   "LWORD", "REAL", "LREAL", "TIME", "STRING"}),
          "operande : les types de kBaseTypes - " + joined(r.names(tr::UseOperand)));
    // le dialogue Nouvelle fonction : Aucun, puis kLocalTypes
    auto ret = r.names(tr::UseReturn);
    check(!ret.empty() && ret.back() == "Aucun", "retour : Aucun propose");
    ret.pop_back();
    check(asSet(ret) == asSet(r.names(tr::UseDeclaration)), "retour : les types d'une declaration");
    check(!r.resolve("ANY", tr::UseDeclaration).ok && r.resolve("ANY", tr::UseParameter).ok && !r.resolve("Aucun", tr::UseDeclaration).ok
              && r.resolve("aucun", tr::UseReturn).ok,
          "ANY : un parametre seulement ; Aucun : un retour seulement");
    // l'ordre : celui de la norme, le meme partout
    const auto all = r.names(tr::UseAll, false);
    check(joined(all) == "BOOL SINT INT DINT LINT USINT UINT UDINT ULINT BYTE WORD DWORD LWORD REAL LREAL STRING TIME ANY Aucun",
          "l'ordre du registre : " + joined(all));
    // chaque type de base : sa cle, sa categorie, sa provenance, sa phrase
    bool good = true;
    for (const auto& e : r.entries())
        good = good && !e.key.empty() && !e.doc.empty() && !e.provenance.empty() && r.byKey(e.key) == &e;
    check(good, "chaque entree : une cle (retrouvee), une provenance, une phrase");
    check(r.byKey("base:REAL") && r.byKey("base:REAL")->category == tr::Category::Elementary && r.byKey("base:TIME")->category == tr::Category::TextTime
              && r.byKey("any")->category == tr::Category::Generic && r.byKey("void")->name == "Aucun",
          "les cles base:REAL, base:TIME, any, void");
    check(r.byName("ebool") == r.byKey("base:BOOL") && r.byName(" string[20] ") == r.byKey("base:STRING"),
          "EBOOL vaut BOOL ; STRING[20] vaut STRING");
    check(tr::categoryLabel(tr::Category::Structure) == "Structures IHM" && tr::categoryLabel(tr::Category::PlcType) == "DDT de l'API",
          "les libelles des categories");
}

// ---------------------------------------------------------------- le projet ----
Project projet() {
    Project p;
    HmiType four;
    four.id = p.allocate();
    four.name = "T_Four";
    four.description = "Un four : sa temperature, sa consigne.";
    four.members.push_back({"Temperature", "REAL", "", ""});
    four.members.push_back({"Consigne", "REAL", "", ""});
    p.programs.types.push_back(four);
    HmiType mode;
    mode.id = p.allocate();
    mode.name = "E_Mode";
    mode.kind = HmiTypeKind::Enumeration;
    mode.values.push_back({"Auto", 0, ""});
    mode.values.push_back({"Manuel", 1, ""});
    p.programs.types.push_back(mode);
    return p;
}

params::PlcTypes plcTypes() {
    params::PlcTypes plc;
    plc.names = [] { return std::vector<std::string>{"T_ANA", "T_ARMOIRE"}; };
    plc.members = [](std::string_view t) {
        if (t == "T_ANA") return std::vector<std::pair<std::string, std::string>>{{"mes", "REAL"}, {"hs", "REAL"}};
        return std::vector<std::pair<std::string, std::string>>{};
    };
    return plc;
}

void registre() {
    std::printf("-- le registre d'un projet\n");
    Project p = projet();
    const auto plc = plcTypes();
    const auto r = tr::Registry::build(p, &plc);
    const auto* four = r->byName("t_four");
    const auto* mode = r->byName("E_MODE");
    const auto* ana = r->byName("T_ANA");
    check(four && four->key == "ihm:" + std::to_string(p.programs.types[0].id) && four->category == tr::Category::Structure
              && four->name == "T_Four" && four->members.size() == 2 && four->doc == "Un four : sa temperature, sa consigne."
              && four->provenance == "Projet \xC2\xB7 Types IHM" && four->id == p.programs.types[0].id,
          "une structure : sa cle ihm:<id>, son nom ecrit, ses membres, sa phrase, sa provenance");
    check(mode && mode->category == tr::Category::Enumeration && mode->values == std::vector<std::string>{"Auto", "Manuel"}
              && mode->provenance == "Projet \xC2\xB7 \xC3\x89num\xC3\xA9rations",
          "une enumeration : ses valeurs");
    check(ana && ana->key == "api:T_ANA" && ana->category == tr::Category::PlcType && ana->members.size() == 2 && ana->usable(tr::UseParameter)
              && !ana->usable(tr::UseDeclaration) && !ana->usable(tr::UseVariable),
          "un DDT : api:T_ANA, un parametre de popup seulement");
    // les listes : la base proposee, puis les types du projet
    const auto decl = r->names(tr::UseDeclaration);
    check(decl.size() == 16 && decl[14] == "T_Four" && decl[15] == "E_Mode", "declaration : la base, puis T_Four et E_Mode - " + joined(decl));
    const auto prm = r->names(tr::UseParameter);
    check(prm.size() == 16 && prm[12] == "T_Four" && prm[14] == "T_ANA" && prm[15] == "T_ARMOIRE",
          "parametre : la base, ANY, les types IHM, puis les DDT - " + joined(prm));
    // un nom de base dans le projet (mal venu) ne double pas la base
    Project q = projet();
    HmiType real;
    real.id = q.allocate();
    real.name = "Real";
    q.programs.types.push_back(real);
    const auto rq = tr::Registry::build(q);
    check(rq->byName("REAL")->key == "base:REAL" && rq->entries().size() == r->entries().size() - 2,
          "un type IHM nomme comme un type de base : la base garde le nom");
    // la cle survit au renommage ; un type ajoute apres coup apparait au registre suivant
    const std::string key = four->key;
    p.programs.types[0].name = "T_Fourneau";
    HmiType vanne;
    vanne.id = p.allocate();
    vanne.name = "T_Vanne";
    p.programs.types.push_back(vanne);
    const auto r2 = tr::Registry::build(p, &plc);
    check(r2->nameOfKey(key) == "T_Fourneau" && r2->byName("T_Four") == nullptr && r2->byName("T_Vanne") != nullptr,
          "renomme : la cle donne le nom d'aujourd'hui ; un type neuf est la");
    check(r2->nameOfKey("ihm:999999").empty(), "une cle disparue : aucun nom");
}

// ---------------------------------------------------------------- la lecture ----
void lecture() {
    std::printf("-- un texte de type, lu\n");
    Project p = projet();
    const auto plc = plcTypes();
    const auto r = tr::Registry::build(p, &plc);
    const std::string fk = "ihm:" + std::to_string(p.programs.types[0].id);
    struct Case { const char* text; unsigned use; bool ok; const char* shown; std::string key; tr::Category cat; };
    const std::vector<Case> cases = {
        {"real", tr::UseAll, true, "REAL", "base:REAL", tr::Category::Elementary},
        {"  Int ", tr::UseVariable, true, "INT", "base:INT", tr::Category::Elementary},
        {"STRING[20]", tr::UseDeclaration, true, "STRING[20]", "base:STRING", tr::Category::TextTime},
        {"t_four", tr::UseVariable, true, "T_Four", fk, tr::Category::Structure},
        {"ARRAY[0..9] OF REAL", tr::UseVariable, true, "ARRAY[0..9] OF REAL", "array[0..9]:base:REAL", tr::Category::Collection},
        {"array [ 0 .. 3 , 0..9 ] of int", tr::UseVariable, true, "ARRAY[0..3, 0..9] OF INT", "array[0..3,0..9]:base:INT", tr::Category::Collection},
        {"ARRAY[1..4] OF T_Four", tr::UseParameter, true, "ARRAY[1..4] OF T_Four", "array[1..4]:" + fk, tr::Category::Collection},
        {"ARRAY[-5..5] OF T_ANA", tr::UseParameter, true, "ARRAY[-5..5] OF T_ANA", "array[-5..5]:api:T_ANA", tr::Category::Collection},
        {"ARRAY[0..1, 0..1, 0..1] OF INT", tr::UseDeclaration, true, "ARRAY[0..1, 0..1, 0..1] OF INT", "array[0..1,0..1,0..1]:base:INT",
         tr::Category::Collection},
        {"MAP[STRING] OF T_Four", tr::UseDeclaration, true, "MAP[STRING] OF T_Four", "map[base:STRING]:" + fk, tr::Category::Collection},
        {"MAP[E_Mode] OF REAL", tr::UseDeclaration, true, "MAP[E_Mode] OF REAL", "map[ihm:" + std::to_string(p.programs.types[1].id) + "]:base:REAL",
         tr::Category::Collection},
        {"REF_TO T_Four", tr::UseDeclaration, true, "REF_TO T_Four", "ref:" + fk, tr::Category::Reference},
        {"REFERENCE TO INT", tr::UseDeclaration, true, "REF_TO INT", "ref:base:INT", tr::Category::Reference},
        {"POINTER TO REAL", tr::UseDeclaration, true, "POINTER TO REAL", "ptr:base:REAL", tr::Category::Reference},
        {"MAP_ITERATOR", tr::UseDeclaration, true, "MAP_ITERATOR", "iter", tr::Category::Collection},
        {"Aucun", tr::UseReturn, true, "Aucun", "void", tr::Category::Void},
        {"ARRAY[0..9] OF REAL", tr::UseReturn, true, "ARRAY[0..9] OF REAL", "array[0..9]:base:REAL", tr::Category::Collection},
    };
    for (const auto& c : cases) {
        const auto x = r->resolve(c.text, c.use);
        check(x.ok == c.ok && x.text == c.shown && x.key == c.key && x.category == c.cat,
              std::string("lu : ") + c.text + " -> " + x.text + " [" + x.key + "] " + (x.ok ? "ok" : "refuse : " + x.why));
    }
    struct Bad { const char* text; unsigned use; const char* why; bool missing; };
    const std::vector<Bad> bad = {
        {"", tr::UseDeclaration, "un type est obligatoire", false},
        {"T_Inconnu", tr::UseDeclaration, "type \xC2\xAB T_Inconnu \xC2\xBB inconnu", true},
        {"ARRAY[0..9] OF T_Inconnu", tr::UseVariable, "type \xC2\xAB T_Inconnu \xC2\xBB inconnu", true},
        {"T_ANA", tr::UseDeclaration, "T_ANA ne convient pas \xC3\xA0 une d\xC3\xA9" "claration", false},
        {"MAP[STRING] OF REAL", tr::UseVariable, "MAP ne sert qu'\xC3\xA0 une d\xC3\xA9" "claration", false},
        {"MAP[REAL] OF INT", tr::UseDeclaration, "MAP : une cl\xC3\xA9 est un STRING, un entier ou une \xC3\xA9num\xC3\xA9ration", false},
        {"ARRAY[0..1, 0..1, 0..1] OF INT", tr::UseVariable, "ARRAY : deux dimensions au plus", false},
        {"ARRAY[9..0] OF INT", tr::UseVariable, "ARRAY : la borne haute est sous la borne basse", false},
        {"ARRAY[0..9] REAL", tr::UseVariable, "ARRAY : OF attendu", false},
        {"REAL REAL", tr::UseDeclaration, "\xC2\xAB REAL \xC2\xBB en trop", false},
        {"STRING[0]", tr::UseDeclaration, "STRING[n] : une longueur", false},
        {"ANY", tr::UseVariable, "ANY ne convient pas", false},
        {"LWORD", tr::UseVariable, "LWORD ne convient pas \xC3\xA0 une variable IHM", false},
    };
    for (const auto& b : bad) {
        const auto x = r->resolve(b.text, b.use);
        check(!x.ok && x.why.find(b.why) != std::string::npos && x.missing == b.missing,
              std::string("refuse : \xC2\xAB ") + b.text + " \xC2\xBB - " + x.why);
    }
}

// ---------------------------------------------------------------- la regle ----
void regle() {
    std::printf("-- la regle, case par case, contre les trois anciennes\n");
    int pairs = 0, sameParams = 0, sameExact = 0, sameOperands = 0;
    std::vector<std::string> paramDiff, exactDiff, operandDiff, costChanges;
    for (const auto& to : kMatrix)
        for (const auto& from : kMatrix) {
            ++pairs;
            const auto v = tr::conversion(from, to);
            // les parametres en Copie : Exact et Widening
            if (v.safe() == old::typeAccepts(to, from)) ++sameParams;
            else paramDiff.push_back(from + " -> " + to);
            // les parametres en Reference : Exact
            if (v.exact() == old::typeExact(to, from)) ++sameExact;
            else exactDiff.push_back(from + " -> " + to);
            // les operateurs : un vide n'est jamais un joker (operateur unaire)
            if (from.empty() || to.empty()) continue;
            const int was = old::matchCost(to, from);
            const int now = v.lenient() ? v.cost : -1;
            if ((was >= 0) == (now >= 0)) ++sameOperands;
            else if (from != "ANY" && to != "ANY") operandDiff.push_back(from + " -> " + to + " (" + std::to_string(was) + " / " + std::to_string(now) + ")");
            if (was >= 0 && now >= 0 && was != now) costChanges.push_back(from + "->" + to + " " + std::to_string(was) + ">" + std::to_string(now));
        }
    check(paramDiff.empty(), "parametre en Copie : les memes paires que typeAccepts (" + std::to_string(sameParams) + " sur " + std::to_string(pairs)
                                 + ") - ecarts : " + joined(paramDiff));
    check(exactDiff.empty(), "parametre en Reference : les memes paires que typeExact - ecarts : " + joined(exactDiff));
    // Un seul ecart, voulu : STRING[n] vaut STRING pour un operateur aussi (comme pour un parametre).
    check(operandDiff == std::vector<std::string>{"STRING[20] -> STRING (-1 / 0)", "STRING -> STRING[20] (-1 / 0)"},
          "operateur : les memes paires acceptees que matchCost, sauf STRING[n] = STRING - ecarts : " + joined(operandDiff));
    // Les couts qui changent (l'ordre de preference entre deux operateurs) : exactement ceux-ci.
    std::printf("  couts d'operateur changes (%zu) : %s\n", costChanges.size(), joined(costChanges).c_str());
    const std::set<std::string> expected = {
        // un entier vers un entier qui ne le contient pas (meme taille ou plus grand) : 1 -> 4
        "INT->UINT 1>4", "INT->UDINT 1>4", "INT->ULINT 1>4", "INT->WORD 1>4", "INT->DWORD 1>4", "INT->LWORD 1>4",
        "SINT->USINT 1>4", "SINT->UINT 1>4", "SINT->UDINT 1>4", "SINT->ULINT 1>4", "SINT->BYTE 1>4", "SINT->WORD 1>4", "SINT->DWORD 1>4", "SINT->LWORD 1>4",
        "DINT->UDINT 1>4", "DINT->ULINT 1>4", "DINT->DWORD 1>4", "DINT->LWORD 1>4", "LINT->ULINT 1>4", "LINT->LWORD 1>4",
        "USINT->SINT 1>4", "BYTE->SINT 1>4", "UINT->INT 1>4", "WORD->INT 1>4", "UDINT->DINT 1>4", "DWORD->DINT 1>4", "ULINT->LINT 1>4", "LWORD->LINT 1>4",
        // un entier de 32 bits ou plus vers REAL : 2 -> 4
        "DINT->REAL 2>4", "LINT->REAL 2>4", "UDINT->REAL 2>4", "ULINT->REAL 2>4", "DWORD->REAL 2>4", "LWORD->REAL 2>4",
        // LREAL vers REAL : 1 -> 4
        "LREAL->REAL 1>4",
    };
    std::set<std::string> got(costChanges.begin(), costChanges.end());
    std::vector<std::string> missing, extra;
    for (const auto& e : expected) if (!got.count(e)) missing.push_back(e);
    for (const auto& g : got) if (!expected.count(g)) extra.push_back(g);
    check(missing.empty() && extra.empty(), "les couts changes sont ceux annonces - manquent : " + joined(missing) + " ; en plus : " + joined(extra));

    // Quelques verdicts, et leurs raisons.
    const auto v1 = tr::conversion("INT", "DINT");
    check(v1.kind == tr::Conversion::Widening && v1.cost == 1 && v1.why.empty(), "INT vers DINT : elargi, sans raison");
    const auto v2 = tr::conversion("UINT", "INT");
    check(v2.kind == tr::Conversion::Lossy && v2.why == "UINT vers INT : une valeur hors de -32768..32767 ne tient pas", "UINT vers INT : " + v2.why);
    const auto v3 = tr::conversion("DINT", "INT");
    check(v3.kind == tr::Conversion::Narrowing && v3.why.find("TO_INT") != std::string::npos, "DINT vers INT : " + v3.why);
    const auto v4 = tr::conversion("REAL", "DINT");
    check(v4.kind == tr::Conversion::Forbidden && v4.why.find("TO_DINT") != std::string::npos, "REAL vers DINT : " + v4.why);
    const auto v5 = tr::conversion("INT", "STRING");
    check(v5.kind == tr::Conversion::Forbidden && v5.why.find("TO_STRING") != std::string::npos, "INT vers STRING : " + v5.why);
    const auto v6 = tr::conversion("DINT", "REAL");
    check(v6.kind == tr::Conversion::Lossy && v6.why.find("24 bits") != std::string::npos, "DINT vers REAL : " + v6.why);
    const auto v7 = tr::conversion("T_Four", "T_Vanne");
    check(v7.kind == tr::Conversion::Forbidden && v7.why == "T_Four n'est pas compatible avec T_Vanne", "deux structures : " + v7.why);
    check(tr::conversion("EBOOL", "BOOL").exact() && tr::conversion("string[8]", "STRING").exact() && tr::conversion("", "T_Four").exact()
              && tr::conversion("ARRAY[0..9] OF REAL", "array [0..9] of real").exact(),
          "EBOOL, STRING[n], un inconnu, la meme forme d'un tableau : exacts");
    check(!tr::conversion("ARRAY[0..9] OF INT", "ARRAY[0..9] OF DINT").lenient() && !tr::conversion("ARRAY[1..9] OF REAL", "ARRAY[0..9] OF REAL").lenient(),
          "un tableau : le meme element et les memes bornes, sinon refuse");
    check(tr::conversionLabel(tr::Conversion::Lossy) == "avec perte possible", "les libelles de la regle");
}

// ------------------------------------------------------------ la valeur gardee ----
void valeurs() {
    std::printf("-- une valeur gardee : tient-elle ?\n");
    check(tr::valueFits(300, "INT") && !tr::valueFits(40000, "INT") && tr::valueFits(40000, "UINT") && !tr::valueFits(-1, "UINT")
              && tr::valueFits(-1, "DINT") && !tr::valueFits(1, "REAL") && tr::valueFits(4294967295LL, "UDINT") && !tr::valueFits(4294967296LL, "UDINT")
              && tr::valueFits(255, "BYTE") && !tr::valueFits(256, "byte"),
          "les plages : INT, UINT, DINT, UDINT, BYTE ; un REAL n'est pas un entier");
    const auto n = tr::numericOf("dword");
    check(n.family == tr::Family::Integer && n.bits == 32 && !n.isSigned && n.bitString, "DWORD : 32 bits, non signe, un mot de bits");
    check(tr::numericOf("T_Four").family == tr::Family::None && tr::numericOf("STRING[4]").family == tr::Family::String,
          "un type IHM n'est pas un nombre ; STRING[4] est un texte");
}

} // namespace

int main() {
    std::printf("-- 1.11.19 (refonte des scripts, lot 6) : le registre des types\n");
    usages();
    registre();
    lecture();
    regle();
    valeurs();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
