// =============================================================================
//  tests/hmi_overload_test.cpp - 1.11.20 : LES SIGNATURES ET LES SURCHARGES
// -----------------------------------------------------------------------------
//  - les modes : VAR_IN_OUT et VAR_OUTPUT sont des parametres (splitDeclarations,
//    la signature, l'arite) ;
//  - la regle (hmi::overload) : l'arite, la reference, les couts, le litteral
//    entier, l'ambiguite, le type inconnu, la forme de deux surcharges ;
//  - le moteur : un script appelle une fonction a E/S et sortie, les variables
//    de l'appelant sont ecrites ; les surcharges choisies a l'execution.
// =============================================================================
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiOverload.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/sim/Interpreter.hpp"

#include <cmath>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace hmi;
namespace ov = hmi::overload;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

ov::Param in(std::string name, std::string type, bool optional = false) { return {std::move(name), std::move(type), ov::Mode::In, optional}; }
ov::Param io(std::string name, std::string type) { return {std::move(name), std::move(type), ov::Mode::InOut, false}; }
ov::Param out(std::string name, std::string type) { return {std::move(name), std::move(type), ov::Mode::Out, true}; }

ov::Signature sig(std::string name, std::vector<ov::Param> params, std::string result = {}) {
    ov::Signature s;
    s.name = std::move(name);
    s.params = std::move(params);
    s.result = std::move(result);
    return s;
}

ov::Arg var(std::string type, std::string text = "x") {
    ov::Arg a;
    a.type = std::move(type);
    a.designator = true;
    a.text = std::move(text);
    return a;
}
ov::Arg val(std::string type, std::string text = "x + 1") {
    ov::Arg a;
    a.type = std::move(type);
    a.text = std::move(text);
    return a;
}
ov::Arg lit(long long v) {
    ov::Arg a;
    a.literal = true;
    a.value = v;
    a.text = std::to_string(v);
    return a;
}
ov::Arg named(std::string name, ov::Arg a, bool output = false) {
    a.name = std::move(name);
    a.output = output;
    return a;
}

// ---- les modes : VAR_IN_OUT et VAR_OUTPUT sont des parametres ----------------------
void modes() {
    std::printf("les modes de passage\n");
    const std::string code =
        "VAR_INPUT Min : REAL; Max : REAL; END_VAR\n"
        "VAR_IN_OUT RandomSeed : REAL; END_VAR\n"
        "VAR_OUTPUT test : REAL; END_VAR\n"
        "VAR t : REAL; END_VAR\n"
        "Random := Min;\n";
    const auto parts = splitDeclarations(code, true);
    check(parts.errors.empty(), "une fonction : VAR_IN_OUT et VAR_OUTPUT se lisent");
    const auto params = parts.parameters();
    check(params.size() == 4 && params[2]->name == "RandomSeed" && params[2]->section == LocalVar::Section::InOut
              && params[3]->name == "test" && params[3]->section == LocalVar::Section::Output,
          "parameters() : Min, Max, RandomSeed (E/S), test (sortie), dans l'ordre");
    check(parts.inputs().size() == 2, "inputs() : les deux entrees seules");
    check(parts.local("t") && !parts.local("t")->isParameter(), "t : une locale, pas un parametre");
    check(parts.body.find("VAR_IN_OUT") == std::string::npos && parts.body.find("VAR_OUTPUT") == std::string::npos,
          "les blocs E/S et sortie sont blanchis du corps (les lignes restent)");
    check(std::count(parts.body.begin(), parts.body.end(), '\n') == std::count(code.begin(), code.end(), '\n'), "les lignes restent");
    const auto script = splitDeclarations("VAR_IN_OUT x : INT; END_VAR\nx := 1;\n", false);
    check(!script.errors.empty() && script.errors[0].message.find("VAR_IN_OUT : seulement dans une fonction") != std::string::npos,
          "un script : VAR_IN_OUT refuse, dit (" + (script.errors.empty() ? std::string("rien") : script.errors[0].message) + ")");

    // La signature d'une fonction du projet : ses quatre parametres.
    HmiFunction f;
    f.id = 615;
    f.name = "Random";
    f.returnType = "REAL";
    f.body = code;
    const auto s = ov::signatureOf(f);
    check(s.params.size() == 4 && s.params[2].mode == ov::Mode::InOut && s.params[3].mode == ov::Mode::Out && s.params[3].optional
              && !s.params[0].optional && s.key == "#615",
          "signatureOf : 4 parametres, leurs modes, la cle #615");
    check(s.text() == "Random(Min : REAL; Max : REAL; VAR_IN_OUT RandomSeed : REAL; VAR_OUTPUT test : REAL) : REAL", "text() : " + s.text());
    check(s.shape() == "Random(REAL, REAL, VAR_IN_OUT REAL, VAR_OUTPUT REAL)", "shape() : " + s.shape());
    check(functionSignature(f) == "Random(Min : REAL; Max : REAL; VAR_IN_OUT RandomSeed : REAL; VAR_OUTPUT test : REAL) : REAL",
          "functionSignature : les E/S et sorties y sont (" + functionSignature(f) + ")");
}

// ---- l'arite et la reference ----------------------------------------------------------
void arite() {
    std::printf("l'arite et la reference\n");
    const auto random = sig("Random", {in("Min", "REAL"), in("Max", "REAL"), io("RandomSeed", "REAL"), out("test", "REAL")}, "REAL");
    check(ov::misfit(random, {val("REAL"), val("REAL"), var("REAL", "Graine"), var("REAL", "Tirage")}).empty(),
          "Random(a, b, Graine, Tirage) : 4 arguments, la remplit (le bug du 09/10 : « ne demande que 2 arguments »)");
    check(ov::misfit(random, {val("REAL"), val("REAL"), var("REAL", "Graine")}).empty(), "sans la sortie : la remplit (une sortie est facultative)");
    const std::string missing = ov::misfit(random, {val("REAL"), val("REAL")});
    check(missing.find("il manque RandomSeed (VAR_IN_OUT") != std::string::npos, "sans l'E/S : " + missing);
    const std::string temp = ov::misfit(random, {val("REAL"), val("REAL"), val("REAL", "Graine * 2")});
    check(temp.find("RandomSeed est pass\xC3\xA9 par r\xC3\xA9" "f\xC3\xA9rence (VAR_IN_OUT) : il faut une variable, pas \xC2\xAB Graine * 2 \xC2\xBB")
              != std::string::npos,
          "une valeur calculee pour l'E/S : " + temp);
    const std::string outVal = ov::misfit(random, {val("REAL"), val("REAL"), var("REAL"), lit(3)});
    check(outVal.find("test est pass\xC3\xA9 par r\xC3\xA9" "f\xC3\xA9rence (VAR_OUTPUT)") != std::string::npos, "un litteral pour la sortie : " + outVal);
    const std::string many = ov::misfit(random, {val("REAL"), val("REAL"), var("REAL"), var("REAL"), val("REAL")});
    check(many == "trop d'arguments (5 pour 4)", "trop : " + many);
    // Par nom
    check(ov::misfit(random, {named("Max", val("REAL")), named("Min", val("REAL")), named("RandomSeed", var("REAL")),
                              named("test", var("REAL"), true)}).empty(),
          "par nom : Max := , Min := , RandomSeed := , test => : la remplit");
    const std::string unknown = ov::misfit(random, {named("Mini", val("REAL"))});
    check(unknown == "param\xC3\xA8tre inconnu : Mini", "un nom inconnu : " + unknown);
    const std::string twice = ov::misfit(random, {val("REAL"), named("Min", val("REAL"))});
    check(twice == "param\xC3\xA8tre donn\xC3\xA9 deux fois : Min", "deux fois : " + twice);
    const std::string arrow = ov::misfit(random, {named("Min", var("REAL"), true)});
    check(arrow.find("est pour une sortie : Min") != std::string::npos, "=> sur une entree : " + arrow);
    // Une entree avec une valeur par defaut est facultative.
    const auto moyenne = sig("Moyenne", {in("A", "REAL"), in("B", "REAL"), in("Poids_A", "REAL", true)}, "REAL");
    check(ov::misfit(moyenne, {lit(1), lit(2)}).empty(), "Moyenne(1, 2) : Poids_A a sa valeur par defaut");
    check(ov::misfit(moyenne, {lit(1)}) == "il manque l'argument B (REAL)", "Moyenne(1) : " + ov::misfit(moyenne, {lit(1)}));
    // REF_TO : une variable, ou une reference.
    const auto viser = sig("Viser", {in("r", "REF_TO REAL")});
    check(ov::misfit(viser, {var("REAL")}).empty() && ov::misfit(viser, {val("REF_TO REAL", "REF(x)")}).empty()
              && !ov::misfit(viser, {lit(2)}).empty(),
          "REF_TO : une variable ou une reference, pas un litteral");
}

// ---- la regle des types ------------------------------------------------------------------
void regle() {
    std::printf("la regle des types\n");
    check(ov::literalType(5) == "INT" && ov::literalType(100000) == "DINT" && ov::literalType(5000000000LL) == "LINT"
              && ov::literalType(-32768) == "INT" && ov::literalType(-32769) == "DINT",
          "le type naturel d'un litteral entier : INT, DINT, LINT");
    const ov::Param pInt = in("v", "INT"), pDint = in("v", "DINT"), pReal = in("v", "REAL"), pStr = in("v", "STRING");
    check(ov::cost(lit(5), pInt) == 0 && ov::cost(lit(5), pDint) == 1 && ov::cost(lit(5), pReal) == 2 && ov::cost(lit(5), pStr) < 0,
          "5 : INT 0, DINT 1, REAL 2, STRING non");
    check(ov::cost(lit(100000), pInt) == 8 && ov::cost(lit(100000), pDint) == 0, "100000 : DINT 0 ; INT ne le tient pas (8)");
    check(ov::cost(val("REAL"), pReal) == 0 && ov::cost(val("REAL"), in("v", "LREAL")) == 1 && ov::cost(val("LREAL"), pReal) == 4,
          "REAL -> REAL 0, -> LREAL 1 ; LREAL -> REAL 4 (perte)");
    check(ov::cost(val("REAL"), pInt) == 16 && ov::cost(val("DINT"), pInt) == 8 && ov::cost(val("INT"), pDint) == 1,
          "REAL -> INT 16 (tronque) ; DINT -> INT 8 ; INT -> DINT 1");
    check(ov::cost(val("STRING"), pReal) < 0 && ov::cost(val("BOOL"), pInt) < 0 && ov::cost(val("T_Four"), in("v", "T_Vanne")) < 0,
          "interdites : STRING -> REAL, BOOL -> INT, deux structures");
    check(ov::cost(val(""), pStr) == 0 && ov::cost(val("T_Four"), in("v", "T_FOUR")) == 0 && ov::cost(val("INT"), in("v", "ANY")) == 0,
          "inconnu : 0 ; la meme structure (sans casse) : 0 ; ANY : 0");
    check(ov::cost(var("INT"), io("v", "INT")) == 0 && ov::cost(var("INT"), io("v", "REAL")) == 4 && ov::cost(var("STRING"), io("v", "INT")) < 0,
          "par reference : le meme type 0, un nombre pour un nombre 4, un texte pour un nombre non");
}

// ---- le choix --------------------------------------------------------------------------------
void choix() {
    std::printf("le choix d'une surcharge\n");
    const std::vector<ov::Signature> convertir = {
        sig("Convertir", {in("valeur", "INT")}, "STRING"),
        sig("Convertir", {in("valeur", "REAL")}, "STRING"),
        sig("Convertir", {in("valeur", "STRING"), in("base", "INT")}, "INT"),
    };
    auto c = ov::choose(convertir, {lit(5)});
    check(c.chosen == 0, "Convertir(5) : la version INT");
    c = ov::choose(convertir, {val("REAL", "x * 2.0")});
    check(c.chosen == 1, "Convertir(x * 2.0) : la version REAL");
    c = ov::choose(convertir, {val("STRING", "'FF'"), lit(16)});
    check(c.chosen == 2, "Convertir('FF', 16) : la version a deux arguments (par le nombre, sans les types)");
    c = ov::choose(convertir, {var("DINT")});
    check(c.chosen == 1, "Convertir(un DINT) : REAL (perte 4) plutot qu'INT (plus petit, 8)");
    c = ov::choose(convertir, {var("BOOL", "Marche")});
    check(c.chosen < 0 && c.why.find("aucune surcharge de Convertir n'accepte ces types (BOOL)") != std::string::npos
              && c.why.find("Convertir(INT) : valeur attend un INT, pas un BOOL") != std::string::npos,
          "Convertir(un BOOL) : aucune, chacune dit pourquoi (" + c.why + ")");
    c = ov::choose(convertir, {lit(1), lit(2), lit(3)});
    check(c.chosen < 0 && c.why.find("aucune surcharge de Convertir ne prend cet appel") != std::string::npos
              && c.why.find("Convertir(STRING, INT) : trop d'arguments (3 pour 2)") != std::string::npos,
          "trois arguments : aucune (" + c.why + ")");
    c = ov::choose(convertir, {var("", "Inconnue")});
    check(c.chosen < 0 && c.uncertain && c.tied.size() == 2 && c.why.find("la simulation choisira") != std::string::npos,
          "un type inconnu : incertain, la simulation choisira (" + c.why + ")");
    // Ambigu : deux surcharges a egalite.
    const std::vector<ov::Signature> melange = {sig("Melanger", {in("a", "INT"), in("b", "REAL")}), sig("Melanger", {in("a", "REAL"), in("b", "INT")})};
    c = ov::choose(melange, {lit(5), lit(5)});
    check(c.chosen < 0 && !c.uncertain && c.tied.size() == 2
              && c.why.find("appel ambigu de Melanger (INT, INT) : Melanger(INT, REAL) et Melanger(REAL, INT) conviennent autant") != std::string::npos,
          "Melanger(5, 5) : ambigu (" + c.why + ")");
    c = ov::choose(melange, {lit(5), val("REAL")});
    check(c.chosen == 0, "Melanger(5, 1.5) : la premiere");
    // Le mode departage : une valeur calculee ne va pas a une E/S.
    const std::vector<ov::Signature> echanger = {sig("Echanger", {io("a", "INT"), io("b", "INT")}), sig("Echanger", {in("a", "INT"), in("b", "INT")})};
    c = ov::choose(echanger, {lit(1), lit(2)});
    check(c.chosen == 1, "Echanger(1, 2) : la version par valeur (l'E/S veut des variables)");
    c = ov::choose(echanger, {var("INT"), var("INT")});
    check(c.chosen < 0 && c.tied.size() == 2, "Echanger(x, y) : ambigu (les deux prennent des variables)");
    // Une seule candidate qui remplit l'arite : choisie sans les types (comme une fonction seule).
    c = ov::choose({sig("Seule", {in("v", "INT")})}, {var("STRING")});
    check(c.chosen == 0, "une fonction seule : les types ne sont pas exiges (comme avant)");
    c = ov::choose({sig("Seule", {in("v", "INT")})}, {});
    check(c.chosen < 0 && c.why == "il manque l'argument v (INT)", "une fonction seule : la raison sans la forme (" + c.why + ")");
}

// ---- la forme de deux surcharges --------------------------------------------------------------
void formes() {
    std::printf("la forme de deux surcharges\n");
    const auto a = sig("F", {in("x", "INT")}, "STRING");
    check(ov::sameShape(a, sig("F", {in("y", "INT")}, "REAL")), "F(INT) : STRING et F(INT) : REAL - le retour seul ne distingue pas");
    check(ov::sameShape(a, sig("F", {in("x", "SINT")})), "INT et SINT : le moteur les calcule pareil");
    check(ov::sameShape(sig("F", {in("x", "REAL")}), sig("F", {in("x", "lreal")})), "REAL et LREAL (sans casse) : pareil");
    check(!ov::sameShape(a, sig("F", {in("x", "DINT")})), "INT et DINT : distinctes");
    check(!ov::sameShape(a, sig("F", {io("x", "INT")})), "le mode distingue : INT et VAR_IN_OUT INT");
    check(!ov::sameShape(a, sig("F", {in("x", "INT"), in("y", "INT", true)})), "un parametre de plus : distinctes");
    check(ov::sameShape(sig("F", {in("t", "ARRAY[0..9] OF REAL")}), sig("F", {in("u", "ARRAY[0..9]  OF  real")})), "deux tableaux ecrits autrement : pareils");
    check(!ov::sameShape(sig("F", {in("t", "T_Four")}), sig("F", {in("t", "T_Vanne")})), "deux structures : distinctes");
}

// ---- le moteur : E/S et sorties ecrites chez l'appelant ; les surcharges choisies ----------------
HmiFunction function(Project& p, const char* name, const char* ret, const char* body) {
    HmiFunction f;
    f.id = p.allocate();
    f.name = name;
    f.returnType = ret;
    f.body = body;
    return f;
}
Script script(Project& p, const char* name, const char* body) {
    Script sc;
    sc.id = p.allocate();
    sc.name = name;
    sc.event = "Appel";
    sc.body = body;
    return sc;
}
Variable variable(Project& p, const char* name, const char* type, const char* initial = "") {
    return Variable{p.allocate(), name, type, initial, {}};
}
double real(const Runtime& rt, const char* name) {
    const auto* v = rt.variable(name);
    return v ? v->asReal() : -1e300;
}
std::string text(const Runtime& rt, const char* name) {
    const auto* v = rt.variable(name);
    return v ? v->asString() : std::string("?");
}

void moteur() {
    std::printf("le moteur : E/S, sorties, surcharges\n");
    Project p;
    for (auto v : {variable(p, "Seed", "REAL", "1.0"), variable(p, "Draw", "REAL"), variable(p, "Resultat", "REAL"),
                   variable(p, "TexteA", "STRING"), variable(p, "TexteB", "STRING"), variable(p, "TexteC", "STRING"),
                   variable(p, "Code", "INT", "7"), variable(p, "Dbl1", "INT"), variable(p, "Dbl2", "REAL")})
        p.programs.variables.push_back(v);
    // La fonction de la capture du 09/10 : deux entrees, une E/S, une sortie.
    p.programs.functions.push_back(function(p, "Random", "REAL",
        "VAR_INPUT Min : REAL; Max : REAL; END_VAR\nVAR_IN_OUT RandomSeed : REAL; END_VAR\nVAR_OUTPUT test : REAL; END_VAR\n"
        "RandomSeed := RandomSeed * 2.0;\ntest := Min + (Max - Min) / 2.0;\nRandom := test;\n"));
    // Trois surcharges.
    p.programs.functions.push_back(function(p, "Convertir", "STRING", "VAR_INPUT valeur : INT; END_VAR\nConvertir := 'entier';\n"));
    p.programs.functions.push_back(function(p, "Convertir", "STRING", "VAR_INPUT valeur : REAL; END_VAR\nConvertir := 'reel';\n"));
    p.programs.functions.push_back(function(p, "Convertir", "INT", "VAR_INPUT texte : STRING; base : INT; END_VAR\nConvertir := base;\n"));
    p.programs.functions.push_back(function(p, "Melanger", "", "VAR_INPUT a : INT; b : REAL; END_VAR\nIHM_JOURNAL('int-real');\n"));
    p.programs.functions.push_back(function(p, "Melanger", "", "VAR_INPUT a : REAL; b : INT; END_VAR\nIHM_JOURNAL('real-int');\n"));
    p.programs.scripts.push_back(script(p, "Tirer", "Resultat := Random(0.0, 10.0, Seed, Draw);\n"));
    p.programs.scripts.push_back(script(p, "Conversions",
        "TexteA := Convertir(5);\nTexteB := Convertir(2.5);\nCode := Convertir('FF', 16);\nTexteC := Convertir(Code);\n"));
    p.programs.scripts.push_back(script(p, "Ambigu", "Melanger(5, 5);\n"));
    p.programs.scripts.push_back(script(p, "Departage", "Melanger(5, 1.5);\n"));
    p.programs.scripts.push_back(script(p, "Internes",
        "FUNCTION Double(x : INT) : INT\n    Double := x * 2;\nEND_FUNCTION\n"
        "FUNCTION Double(x : REAL) : REAL\n    Double := x * 2.0;\nEND_FUNCTION\n"
        "Dbl1 := Double(21);\nDbl2 := Double(1.25);\n"));
    p.programs.scripts.push_back(script(p, "Doublon",
        "FUNCTION F(x : INT) : INT\n    F := x;\nEND_FUNCTION\nFUNCTION F(y : INT) : INT\n    F := y;\nEND_FUNCTION\nDbl1 := F(1);\n"));

    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    std::string why;
    bool ok = rt.callScript("Tirer", 0.1, &why);
    check(ok, "un script appelle Random(0.0, 10.0, Seed, Draw)" + (why.empty() ? std::string{} : " (" + why + ")"));
    check(std::fabs(real(rt, "Seed") - 2.0) < 1e-9, "l'E/S (VAR_IN_OUT) ecrit la variable de l'appelant : Seed 1.0 -> 2.0 (" + std::to_string(real(rt, "Seed")) + ")");
    check(std::fabs(real(rt, "Draw") - 5.0) < 1e-9, "la sortie (VAR_OUTPUT) aussi : Draw = 5.0 (" + std::to_string(real(rt, "Draw")) + ")");
    check(std::fabs(real(rt, "Resultat") - 5.0) < 1e-9, "et le retour : Resultat = 5.0");
    why.clear();
    ok = rt.callScript("Conversions", 0.2, &why);
    check(ok, "les surcharges de Convertir tournent" + (why.empty() ? std::string{} : " (" + why + ")"));
    check(text(rt, "TexteA") == "entier" && text(rt, "TexteB") == "reel", "Convertir(5) : la version INT ; Convertir(2.5) : la version REAL (" + text(rt, "TexteA") + ", " + text(rt, "TexteB") + ")");
    check(rt.variable("Code") && rt.variable("Code")->asInteger() == 16, "Convertir('FF', 16) : la version a deux arguments (16)");
    check(text(rt, "TexteC") == "entier", "Convertir(Code) : Code est un INT - la version INT (" + text(rt, "TexteC") + ")");
    why.clear();
    ok = !rt.callScript("Ambigu", 0.3, &why) && why.find("appel ambigu de Melanger") != std::string::npos;
    check(ok, "Melanger(5, 5) : ambigu, dit a l'execution (" + why + ")");
    why.clear();
    ok = rt.callScript("Departage", 0.4, &why);
    bool logged = false;
    for (const auto& e : rt.journal()) logged = logged || e.message == "int-real";
    check(ok && logged, "Melanger(5, 1.5) : la version (INT, REAL)" + (why.empty() ? std::string{} : " (" + why + ")"));
    why.clear();
    ok = rt.callScript("Internes", 0.5, &why);
    check(ok && rt.variable("Dbl1") && rt.variable("Dbl1")->asInteger() == 42 && std::fabs(real(rt, "Dbl2") - 2.5) < 1e-9,
          "deux fonctions internes Double (INT, REAL) : Double(21) = 42, Double(1.25) = 2.5" + (why.empty() ? std::string{} : " (" + why + ")"));
    why.clear();
    ok = !rt.callScript("Doublon", 0.6, &why) && why.find("same parameters") != std::string::npos;
    check(ok || why.find("m\xC3\xAAmes param\xC3\xA8tres") != std::string::npos,
          "deux fonctions internes de memes parametres : refusees (" + why + ")");
    // Une expression de vue (le chemin des valeurs) : la surcharge aussi.
    const auto e1 = Expression::compile("Convertir(5)");
    const auto v1 = e1.evaluate(rt.environment());
    check(e1.valid() && v1 && v1->asString() == "entier", "une expression {Convertir(5)} : la version INT");
    const auto e2 = Expression::compile("Convertir(Seed)");
    const auto v2 = e2.evaluate(rt.environment());
    check(e2.valid() && v2 && v2->asString() == "reel", "une expression {Convertir(Seed)} (un REAL) : la version REAL");
    // L'essai d'une fonction (le volet Fonctions) : par son identifiant, E/S et sorties rendues.
    sim::Value r;
    std::vector<std::pair<std::string, sim::Value>> outs;
    ok = rt.runFunction(p.programs.functions[0].id, {{"", sim::Value::real(0.0)}, {"", sim::Value::real(4.0)}, {"", sim::Value::real(3.0)}}, r,
                        &why, &outs);
    check(ok && std::fabs(r.asReal() - 2.0) < 1e-9 && outs.size() == 2 && outs[0].first == "RandomSeed"
              && std::fabs(outs[0].second.asReal() - 6.0) < 1e-9 && outs[1].first == "test" && std::fabs(outs[1].second.asReal() - 2.0) < 1e-9,
          "Essayer Random(0, 4, 3) : 2.0 ; RandomSeed (E/S) = 6.0, test (sortie) = 2.0" + (why.empty() ? std::string{} : " (" + why + ")"));
    ok = rt.runFunction(p.programs.functions[2].id, {{"", sim::Value::real(1.5)}}, r, &why);
    check(ok && r.asString() == "reel", "Essayer une surcharge par son identifiant : Convertir(REAL)");
}

} // namespace

int main() {
    std::printf("-- 1.11.20 : les signatures et les surcharges\n");
    modes();
    arite();
    regle();
    choix();
    formes();
    moteur();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
