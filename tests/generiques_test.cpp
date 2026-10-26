// =============================================================================
//  tests/generiques_test.cpp - 1.13.0 : les fonctions generiques (TEMPLATE <T>)
// -----------------------------------------------------------------------------
//  Une fonction IHM (de projet, de vue, de symbole) peut porter ses propres
//  types generiques : TEMPLATE <T : ANY_NUM> FUNCTION Max(a : T; b : T) : T.
//  Le moteur deduit T des arguments (ou on le donne, Max<INT>(...)), le verifie
//  contre sa contrainte, et le retour vaut le T deduit. Tout cela n'est QUE du
//  dialecte IHM : le ST de l'automate ne connait pas TEMPLATE FUNCTION.
//
//  Le lot H1 (tache #191) ne touche PAS le moteur (sim/Interpreter.cpp : la
//  deduction, les contraintes et les types donnes sont la depuis 1.12.19) : il
//  relie l'editeur a ce moteur. Ces essais verifient donc d'un bout a l'autre :
//  le modele (HmiFunction::generics), la serialisation du projet, functionText
//  qui porte le TEMPLATE, checkFunction qui accepte T, Generer, et le moteur qui
//  tourne sur les fonctions generiques du projet.
//
//  GEN_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiCheck.hpp"     // generate (Generer)
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"
#include "../src/hmi/HmiExprCheck.hpp"
#include "../src/hmi/HmiOverload.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/sim/Interpreter.hpp"     // le ST de l'automate (sans le dialecte)

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("GEN_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

std::string shown(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }

Variable& addVar(Project& p, const std::string& n, const std::string& t, const std::string& init) {
    Variable v;
    v.id = p.allocate();
    v.name = n;
    v.type = t;
    v.initial = init;
    p.programs.variables.push_back(v);
    return p.programs.variables.back();
}

HmiFunction& addFn(Project& p, const std::string& name, const std::string& ret, std::vector<GenericParam> gen,
                   std::vector<std::pair<std::string, std::string>> params, const std::string& body, bool virt = false) {
    HmiFunction f;
    f.id = p.allocate();
    f.name = name;
    f.returnType = ret;
    f.generics = std::move(gen);
    f.isVirtual = virt;
    for (auto& [pn, pt] : params) {
        Declaration d;
        d.id = p.allocate();
        d.kind = DeclKind::Parameter;
        d.name = pn;
        d.type = pt;
        d.mode = PassMode::In;
        f.decls.push_back(std::move(d));
    }
    f.body = body;
    p.programs.functions.push_back(std::move(f));
    return p.programs.functions.back();
}

// Un petit projet, un script "Essai" qui calcule R, et ce qu'il laisse dans R (ou "#").
std::string run(Project& p, const std::string& scriptBody, std::string* err = nullptr, const std::string& readVar = "R") {
    Script s;
    s.id = p.allocate();
    s.name = "Essai";
    s.event = "Appel";
    s.body = scriptBody;
    p.programs.scripts.push_back(s);
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    std::string e;
    const bool ok = rt.callScript("Essai", 0.2, &e);
    if (err) *err = ok ? std::string{} : e;
    sim::Value out;
    return rt.environment().read(readVar, out) ? shown(out) : std::string("#");
}

// ---- la declaration : une fonction generique se lit sans faute, sa signature montre <T> ----
void declaration() {
    std::printf("-- declaration : TEMPLATE <T> accepte, le retour et les parametres de type T ne sont plus refuses\n");
    Project p;
    const TypeKnown known = [&p](std::string_view t) { return types::knownTypeName(p, t); };

    HmiFunction max = addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}},
                            "IF a > b THEN Max := a; ELSE Max := b; END_IF");
    const auto d1 = checkFunction(max, known);
    bool clean = true;
    for (const auto& d : d1)
        if (d.severity == ScriptDiagnostic::Severity::Error) clean = false;
    check(clean, "Max<T : ANY_NUM> : aucune erreur de controle (retour et parametres T acceptes)");

    check(functionSignature(max).find("Max<T>") != std::string::npos, "functionSignature : montre les chevrons Max<T> (" + functionSignature(max) + ")");

    // Un retour compose d'un generique (LIST OF T) et une contrainte connue : propre aussi.
    HmiFunction duo = addFn(p, "Doubler", "LIST OF T", {{"T", "ANY"}}, {{"x", "T"}},
                            "VAR l : LIST OF T; END_VAR\nLIST_ADD(l, x);\nLIST_ADD(l, x);\nDoubler := l;");
    bool clean2 = true;
    for (const auto& d : checkFunction(duo, known))
        if (d.severity == ScriptDiagnostic::Severity::Error) clean2 = false;
    check(clean2, "retour LIST OF T : accepte (pas 'type de retour non pris en charge')");

    // Une contrainte qui n'est pas un ANY_* : refusee (parseGenerics).
    HmiFunction bad = addFn(p, "Faux", "T", {{"T", "ANY_BIDON"}}, {{"a", "T"}}, "Faux := a;");
    bool refused = false;
    for (const auto& d : checkFunction(bad, known))
        if (d.severity == ScriptDiagnostic::Severity::Error && d.message.find("G\xC3\xA9n\xC3\xA9riques") != std::string::npos) refused = true;
    check(refused, "contrainte ANY_BIDON inconnue : refusee a la saisie des generiques");
}

// ---- la deduction : T des arguments, type donne Max<INT>, le reel l'emporte ----
void deduction() {
    std::printf("-- deduction : T des arguments, Max<INT>(...), 2 et 2.5 -> REAL\n");
    const std::string body = "IF a > b THEN Max := a; ELSE Max := b; END_IF";
    {
        Project p;
        addVar(p, "R", "REAL", "0");
        addVar(p, "Niveau", "REAL", "42.5");
        addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, body);
        std::string err;
        check(run(p, "R := Max(Niveau, 10.0);", &err) == "42.5" && err.empty(), "Max(REAL, REAL) deduit REAL -> 42.5 (" + err + ")");
    }
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, body);
        std::string err;
        check(run(p, "R := Max<INT>(7, 3);", &err) == "7" && err.empty(), "Max<INT>(7, 3) type donne -> 7 (" + err + ")");
    }
    {
        Project p;
        addVar(p, "R", "REAL", "0");
        addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, body);
        std::string err;
        check(run(p, "R := Max(1, 2.5);", &err) == "2.5" && err.empty(), "Max(1, 2.5) : le reel l'emporte -> 2.5 (" + err + ")");
    }
}

// ---- la deduction a travers un conteneur et une structure generique ----
void conteneurs() {
    std::printf("-- deduction a travers LIST OF T et une structure generique T_Mesure<T>\n");
    {
        Project p;
        addVar(p, "R", "REAL", "0");
        addFn(p, "Premier", "T", {{"T", "ANY"}}, {{"liste", "LIST OF T"}}, "Premier := liste[0];");
        std::string err;
        const std::string got = run(p, "VAR l : LIST OF REAL; END_VAR\nLIST_ADD(l, 3.5);\nLIST_ADD(l, 9.0);\nR := Premier(l);", &err);
        check(got == "3.5" && err.empty(), "Premier(LIST OF REAL) : T = REAL -> 3.5 (" + got + " / " + err + ")");
    }
    {
        Project p;
        // Une structure generique T_Mesure<T> avec un membre Valeur : T.
        HmiType mesure;
        mesure.id = p.allocate();
        mesure.name = "T_Mesure";
        mesure.generics = {{"T", "ANY_NUM"}};
        TypeMember m;
        m.name = "Valeur";
        m.type = "T";
        mesure.members.push_back(m);
        p.programs.types.push_back(mesure);
        addVar(p, "R", "REAL", "0");
        addVar(p, "M", "T_Mesure<REAL>", "");
        addFn(p, "Valeur_De", "T", {{"T", "ANY"}}, {{"m", "T_Mesure<T>"}}, "Valeur_De := m.Valeur;");
        std::string err;
        const std::string got = run(p, "M.Valeur := 7.5;\nR := Valeur_De(M);", &err);
        check(got == "7.5" && err.empty(), "Valeur_De(T_Mesure<REAL>) : T deduit a travers la structure -> 7.5 (" + got + " / " + err + ")");
    }
}

// ---- les contraintes : respectee (TIME via ANY_MAGNITUDE), violee (STRING dans ANY_NUM) ----
void contraintes() {
    std::printf("-- la contrainte limite T : respectee et violee (message exact du moteur)\n");
    {
        Project p;
        addVar(p, "R", "TIME", "t#0s");
        addFn(p, "Limiter", "T", {{"T", "ANY_MAGNITUDE"}}, {{"x", "T"}, {"bas", "T"}, {"haut", "T"}},
              "IF x < bas THEN RETURN bas; END_IF\nIF x > haut THEN RETURN haut; END_IF\nRETURN x;");
        std::string err;
        const std::string got = run(p, "R := Limiter(t#45s, t#0s, t#30s);", &err);
        check(got == "T#30000ms" && err.empty(), "Limiter(TIME...) : ANY_MAGNITUDE accepte TIME -> 30 s (" + got + " / " + err + ")");
    }
    {
        Project p;
        addVar(p, "R", "STRING", "");
        addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, "IF a > b THEN Max := a; ELSE Max := b; END_IF");
        std::string err;
        (void)run(p, "R := Max('A', 'B');", &err);
        check(err.find("STRING") != std::string::npos && err.find("ANY_NUM") != std::string::npos,
              "Max('A','B') : refuse - T = STRING n'est pas un ANY_NUM (" + err + ")");
    }
}

// ---- les quatre refus de generiques.json (messages repris du moteur) ----
void refus() {
    std::printf("-- les 4 refus : STRING dans ANY_NUM, T non deductible, mauvais nombre, trop de types donnes\n");
    // 1. STRING dans ANY_NUM : voir contraintes(). Ici on verifie le libelle exact du moteur.
    {
        Project p;
        addVar(p, "R", "STRING", "");
        addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, "Max := a;");
        std::string err;
        (void)run(p, "R := Max('A', 'B');", &err);
        check(err.find("Max : T = STRING n'est pas un ANY_NUM") != std::string::npos, "refus 1 : \"Max : T = STRING n'est pas un ANY_NUM\" (" + err + ")");
    }
    // 2. T ne se deduit pas des arguments (T seulement dans le retour).
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        addFn(p, "Zero", "T", {{"T", "ANY_NUM"}}, {}, "Zero := 0;");
        std::string err;
        (void)run(p, "R := Zero();", &err);
        check(err.find("le type T ne se d\xC3\xA9" "duit pas des arguments") != std::string::npos && err.find("Zero<INT>(...)") != std::string::npos,
              "refus 2 : T non deductible -> donne-le : Zero<INT>(...) (" + err + ")");
    }
    // 3. mauvais nombre d'arguments : le moteur complete par defaut, mais le controle de l'editeur refuse.
    {
        Project p;
        addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, "Max := a;");
        scriptcheck::Scope sc;
        sc.project = &p;
        bool arity = false;
        for (const auto& fd : scriptcheck::check(sc, "VAR x : REAL; END_VAR\nx := Max(1);"))
            if (fd.severity == scriptcheck::Finding::Severity::Error && fd.message.find("manque") != std::string::npos
                && fd.message.find("b") != std::string::npos)
                arity = true;
        check(arity, "refus 3 : Max(1) - le controle dit qu'il manque un argument");
    }
    // 4. trop de types donnes.
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, "Max := a;");
        std::string err;
        (void)run(p, "R := Max<INT, REAL>(1, 2);", &err);
        check(err.find("2 types donn\xC3\xA9s pour 1") != std::string::npos && err.find("TEMPLATE <T...>") != std::string::npos,
              "refus 4 : Max<INT, REAL> -> 2 types donnes pour 1 (TEMPLATE <T...>) (" + err + ")");
    }
}

// ---- une fonction generique de vue / de symbole, et l'ordre superieur (CALLBACK sur T) ----
void vueSymboleEtCallback() {
    std::printf("-- fonction generique d'une vue, d'un symbole (virtuelle), et un parametre rappel sur T\n");
    // Une fonction generique d'une vue, appelee par Vue.Fonction(...).
    {
        Project p;
        addVar(p, "R", "STRING", "");
        addVar(p, "Debit", "REAL", "12.0");
        View syn = makeView(p, "Synoptique");
        HmiFunction fmt;
        fmt.id = p.allocate();
        fmt.name = "Formater";
        fmt.returnType = "STRING";
        fmt.generics = {{"U", "ANY_NUM"}};
        fmt.visibility = Visibility::Public;
        Declaration d1;
        d1.id = p.allocate();
        d1.kind = DeclKind::Parameter;
        d1.name = "v";
        d1.type = "U";
        fmt.decls.push_back(d1);
        Declaration d2;
        d2.id = p.allocate();
        d2.kind = DeclKind::Parameter;
        d2.name = "unite";
        d2.type = "STRING";
        fmt.decls.push_back(d2);
        fmt.body = "Formater := REAL_TO_STRING(TO_REAL(v)) + ' ' + unite;";
        syn.functions.push_back(fmt);
        p.views.push_back(syn);
        // La fonction de vue se lit sans faute (checkFunction).
        const TypeKnown known = [&p](std::string_view t) { return types::knownTypeName(p, t); };
        bool clean = true;
        for (const auto& d : checkFunction(p.views.back().functions.front(), known))
            if (d.severity == ScriptDiagnostic::Severity::Error) clean = false;
        check(clean, "fonction de vue Formater<U : ANY_NUM> : lue sans faute");
    }
    // Une fonction de symbole peut etre VIRTUELLE ET generique (une forme <U> gardee).
    {
        Project p;
        View sym = makeView(p, "Afficheur");
        sym.role = "symbole";
        HmiFunction mx;
        mx.id = p.allocate();
        mx.name = "Borne";
        mx.returnType = "U";
        mx.generics = {{"U", "ANY_NUM"}};
        mx.isVirtual = true;
        Declaration d;
        d.id = p.allocate();
        d.kind = DeclKind::Parameter;
        d.name = "x";
        d.type = "U";
        mx.decls.push_back(d);
        mx.body = "Borne := x;";
        sym.functions.push_back(mx);
        p.views.push_back(sym);
        const TypeKnown known = [&p](std::string_view t) { return types::knownTypeName(p, t); };
        bool clean = true;
        for (const auto& d2 : checkFunction(p.views.back().functions.front(), known))
            if (d2.severity == ScriptDiagnostic::Severity::Error) clean = false;
        check(clean, "fonction de symbole virtuelle ET generique : lue sans faute");
        // Une fonction de symbole ordinaire (non generique) n'est pas abimee.
        HmiFunction plain = addFn(p, "Simple", "INT", {}, {{"a", "INT"}}, "Simple := a;");
        bool okplain = true;
        for (const auto& d2 : checkFunction(plain, known))
            if (d2.severity == ScriptDiagnostic::Severity::Error) okplain = false;
        check(okplain, "une fonction NON generique reste lue sans faute");
    }
    // L'ordre superieur : un parametre CALLBACK (x : T) : BOOL, la LAMBDA passee fixe T.
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        addFn(p, "Compter", "INT", {{"T", "ANY"}}, {{"liste", "LIST OF T"}, {"test", "CALLBACK (x : T) : BOOL"}},
              "VAR n : INT := 0; END_VAR\nFOR EACH x IN liste DO\n    IF test(x) THEN n := n + 1; END_IF\nEND_FOR\nCompter := n;");
        std::string err;
        const std::string got = run(p,
            "VAR l : LIST OF INT; END_VAR\nLIST_ADD(l, 1);\nLIST_ADD(l, 8);\nLIST_ADD(l, 3);\n"
            "R := Compter(l, LAMBDA (v : INT) : BOOL => v > 2);", &err);
        check(got == "2" && err.empty(), "Compter(LIST OF INT, LAMBDA) : CALLBACK sur T, T = INT -> 2 (" + got + " / " + err + ")");
    }
}

// ---- la serialisation : une fonction generique relue a l'identique ; le format monte a 30 ----
void serialisation() {
    std::printf("-- serialisation du projet : une fonction generique relue identique, le format monte a 30\n");
    Project p;
    addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, "IF a > b THEN Max := a; ELSE Max := b; END_IF");
    // Une fonction de symbole generique aussi.
    View sym = makeView(p, "Afficheur");
    sym.role = "symbole";
    HmiFunction sf;
    sf.id = p.allocate();
    sf.name = "Borne";
    sf.returnType = "U";
    sf.generics = {{"U", "ANY_MAGNITUDE"}};
    Declaration d;
    d.id = p.allocate();
    d.kind = DeclKind::Parameter;
    d.name = "x";
    d.type = "U";
    sf.decls.push_back(d);
    sf.body = "Borne := x;";
    sym.functions.push_back(sf);
    p.views.push_back(sym);

    const auto files = serializeProject(p);
    // Le format 30 est ecrit dans l'index (une fonction est generique).
    bool at30 = false;
    for (const auto& f : files)
        if (f.path == "ihm.txt" && f.data) {
            const std::string txt(f.data->begin(), f.data->end());
            at30 = txt.find("(format 30)") != std::string::npos;
        }
    check(at30, "l'index dit (format 30) quand une fonction est generique");

    auto reader = [&](const std::string& path, std::string& content) {
        for (const auto& f : files)
            if (f.path == path && f.data) {
                content.assign(f.data->begin(), f.data->end());
                return true;
            }
        return false;
    };
    auto relu = parseProject(reader);
    check(static_cast<bool>(relu), "le projet se relit");
    if (relu) {
        const Project& q = *relu;
        bool okProj = false;
        for (const auto& f : q.programs.functions)
            if (f.name == "Max" && f.generics.size() == 1 && f.generics[0].name == "T" && f.generics[0].constraint == "ANY_NUM"
                && f.decls.size() == 2)
                okProj = true;
        check(okProj, "la fonction de projet Max<T : ANY_NUM> est relue identique (generiques + 2 parametres)");
        bool okSym = false;
        for (const auto& v : q.views)
            for (const auto& f : v.functions)
                if (f.name == "Borne" && f.generics.size() == 1 && f.generics[0].name == "U" && f.generics[0].constraint == "ANY_MAGNITUDE"
                    && f.decls.size() == 1)
                    okSym = true;
        check(okSym, "la fonction de symbole Borne<U : ANY_MAGNITUDE> est relue identique");
    }

    // Un projet SANS fonction generique s'ecrit a un format <= 28 (compatibilite).
    Project q;
    addFn(q, "Ordinaire", "INT", {}, {{"a", "INT"}}, "Ordinaire := a;");
    bool notAbove28 = true;
    for (const auto& f : serializeProject(q))
        if (f.path == "ihm.txt" && f.data) {
            const std::string txt(f.data->begin(), f.data->end());
            notAbove28 = txt.find("(format 30)") == std::string::npos && txt.find("(format 29)") == std::string::npos;
        }
    check(notAbove28, "un projet sans fonction generique ne monte PAS au format 30 (repli)");
}

// ---- Generer : l'en-tete C++ (template) et C (commentaire) ----
void generer() {
    std::printf("-- Generer : en-tete C++ template <typename T> T Max(T a, T b); et C un commentaire\n");
    Project p;
    addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, "Max := a;");
    const std::string cpp = projectHeader(p, ScriptLang::Cpp);
    check(cpp.find("template <typename T>") != std::string::npos, "C++ : template <typename T> present");
    check(cpp.find("T Max(T a, T b);") != std::string::npos, "C++ : T Max(T a, T b); (" + (cpp.find("Max") != std::string::npos ? std::string("Max trouve") : std::string("Max absent")) + ")");
    const std::string c = projectHeader(p, ScriptLang::C);
    check(c.find("Max<T>") != std::string::npos && c.find("g\xC3\xA9n\xC3\xA9rique") != std::string::npos,
          "C : un commentaire pour Max<T> generique");
}

// ---- l'editeur ne refuse PAS a tort un appel generique (signatureOf efface T) ----
void pasDeFauxRefus() {
    std::printf("-- l'editeur : un appel generique correct ne produit aucune erreur\n");
    Project p;
    addVar(p, "R", "REAL", "0");
    addVar(p, "Niveau", "REAL", "1.0");
    addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, "Max := a;");
    scriptcheck::Scope sc;
    sc.project = &p;
    bool clean = true;
    for (const auto& fd : scriptcheck::check(sc, "R := Max(Niveau, 10.0);"))
        if (fd.severity == scriptcheck::Finding::Severity::Error) clean = false;
    check(clean, "Max(Niveau, 10.0) : aucun faux refus de type a l'appel");

    // Deux fonctions generiques de meme forme : refusees (surcharge impossible).
    addFn(p, "Doubler", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}}, "Doubler := a;");
    addFn(p, "Doubler", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}}, "Doubler := a;");
    const auto clashes = overload::clashes(p.programs.functions);
    bool clash = false;
    for (const auto& c : clashes)
        if (c.message.find("m\xC3\xAAme forme") != std::string::npos) clash = true;
    check(clash, "deux fonctions generiques de meme forme : refusees");
}

// ---- le ST de l'automate n'a pas de fonction generique (hmiDialect=false) ----
void automateInchange() {
    std::printf("-- le ST de l'automate : TEMPLATE FUNCTION n'existe que dans le dialecte IHM\n");
    const auto parses = [](const char* src, bool dialect) {
        return static_cast<bool>(sim::parse(src, "essai", sim::ParseOptions{dialect}));
    };
    const char* tmpl = "TEMPLATE <T : ANY_NUM> FUNCTION Max(a : T; b : T) : T\n    Max := a;\nEND_FUNCTION";
    check(parses(tmpl, true), "dialecte : TEMPLATE <T> FUNCTION se lit");
    check(!parses(tmpl, false), "automate : TEMPLATE <T> FUNCTION ne se lit pas (pas un mot-cle du ST)");
    // Une fonction interne ordinaire reste lue par le dialecte (comme avant) ; l'automate ignore TEMPLATE,
    // mais une affectation ordinaire passe des deux cotes (le ST de l'automate n'a rien perdu).
    const char* plain = "FUNCTION Double(a : INT) : INT\n    Double := a * 2;\nEND_FUNCTION";
    check(parses(plain, true), "dialecte : une FUNCTION interne ordinaire se lit (inchangee)");
    check(parses("x := 1 + 2;", true) && parses("x := 1 + 2;", false), "une affectation ST ordinaire : lue par le dialecte ET l'automate");
}

// ---- 1.13.0 (correctif F2) : une fonction du projet qui porte le nom d'une fonction standard (Max / MAX) ----
// Le moteur l'appelle a la place de la standard (une fonction du projet passe avant : Env::call, functionByKey sans
// casse) ; "Appelee par" dit donc vrai en citant les MAX(...) du projet. Generer doit le signaler (la fenetre
// Nouvelle fonction refuse deja ce nom : "est un type ou une fonction standard").
void homonymeStandard() {
    std::printf("-- Max<T> du projet et MAX standard : le moteur prend celle du projet, Generer le dit\n");
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, "Max := a + b + 100;");
        std::string err;
        const std::string got = run(p, "R := MAX(3, 7);", &err);
        check(got == "110" && err.empty(), "MAX(3, 7) avec une fonction Max du projet : le moteur appelle Max du projet (3 + 7 + 100) [" + got + " " + err + "]");
    }
    {
        Project p;
        addVar(p, "R", "DINT", "0");
        std::string err;
        const std::string got = run(p, "R := MAX(3, 7);", &err);
        check(got == "7" && err.empty(), "sans fonction Max du projet : MAX(3, 7) est la standard (7) [" + got + " " + err + "]");
    }
    {
        Project p;
        addVar(p, "R", "REAL", "0");
        HmiFunction& max = addFn(p, "Max", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, "IF a > b THEN Max := a; ELSE Max := b; END_IF");
        const Id maxId = max.id;
        Script s;
        s.id = p.allocate();
        s.name = "Demo_Boucles";
        s.event = "Appel";
        s.body = "R := MAX(R * 1.2, 1.0);";
        p.programs.scripts.push_back(s);
        // Appelee par : le script qui ecrit MAX(...) appelle bien cette fonction (le moteur le fait).
        const auto callers = functionCallers(p, "Max");
        check(callers.size() == 1 && callers.front().find("Demo_Boucles") != std::string::npos,
              "Appelee par : le script Demo_Boucles (son MAX(...) appelle Max du projet)");
        const auto issues = generate(p, [](std::string_view) { return false; });
        bool warned = false;
        for (const auto& i : issues)
            if (i.severity == Issue::Severity::Warning && i.item == maxId && i.message.find("fonction standard MAX") != std::string::npos
                && i.message.find("Demo_Boucles") != std::string::npos)
                warned = true;
        check(warned, "Generer : Max porte le nom de la fonction standard MAX (un avertissement qui cite Demo_Boucles)");
        // Une fonction au nom libre : rien de tel.
        Project q;
        addFn(q, "Plus_Grand", "T", {{"T", "ANY_NUM"}}, {{"a", "T"}, {"b", "T"}}, "Plus_Grand := a;");
        bool quiet = true;
        for (const auto& i : generate(q, [](std::string_view) { return false; }))
            if (i.message.find("fonction standard") != std::string::npos) quiet = false;
        check(quiet, "Plus_Grand<T> : aucun avertissement de fonction standard");
    }
}

} // namespace

int main() {
    std::printf("1.13.0 : les fonctions generiques (TEMPLATE <T>) partout\n");
    declaration();
    deduction();
    conteneurs();
    contraintes();
    refus();
    vueSymboleEtCallback();
    serialisation();
    generer();
    pasDeFauxRefus();
    automateInchange();
    homonymeStandard();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
