// =============================================================================
//  tests/tests_unitaires_test.cpp - 1.13.0 (lot J4) : les tests unitaires des
//                                   fonctions IHM
// -----------------------------------------------------------------------------
//  Couvre : un cas pur exact ; un cas REAL avec tolerance (dans / hors) ; une
//  procedure avec poser-avant et verifier-apres (variable + JOURNAL) ; un cas
//  qui attend une erreur (reussi si le bon code est leve) ; un cas normal qui
//  leve (verdict erreur) ; la serialisation projet aller-retour (cas relus
//  identiques, chaque surcharge ses cas) ; le compte des verdicts ; le refus
//  d'un format trop recent ; le repli du format (sans cas : format d'avant).
//  1.13.0 (relecture) : les cas d'une fonction GENERIQUE relus (projet, methode,
//  vue) ; le cas d'une fonction de vue joue ; l'erreur attendue lue par son code
//  exact ; un STRING compare tel quel ; une enumeration comparee par son nom ;
//  Generer signale une pose / une verif sur une variable inconnue, un attendu
//  illisible.
//
//  TESTS_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiFunctionTests.hpp"
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("TESTS_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

// Une declaration de parametre (VAR_INPUT) avec son type et sa valeur par defaut.
Declaration param(Project& p, const char* name, const char* type, const char* def = "") {
    Declaration d;
    d.id = p.allocate();
    d.kind = DeclKind::Parameter;
    d.mode = PassMode::In;
    d.name = name;
    d.type = type;
    d.value = def;
    return d;
}

// Ajoute une fonction IHM au projet (ses parametres dans le modele, son corps en ST).
HmiFunction& addFunction(Project& p, const char* name, const char* ret, std::vector<Declaration> params, std::string body) {
    HmiFunction f;
    f.id = p.allocate();
    f.name = name;
    f.returnType = ret ? ret : "";
    f.decls = std::move(params);
    f.body = std::move(body);
    p.programs.functions.push_back(std::move(f));
    return p.programs.functions.back();
}

Variable& addVar(Project& p, const char* name, const char* type, const char* init) {
    Variable v;
    v.id = p.allocate();
    v.name = name;
    v.type = type;
    v.initial = init;
    p.programs.variables.push_back(v);
    return p.programs.variables.back();
}

FileReader readerOf(const std::vector<ProjectFile>& files) {
    return [files](const std::string& path, std::string& content) {
        for (const auto& f : files)
            if (f.path == path) { content.assign(f.data->begin(), f.data->end()); return true; }
        return false;
    };
}
std::string fileText(const std::vector<ProjectFile>& files, std::string_view prefix) {
    std::string out;
    for (const auto& f : files)
        if (f.path.rfind(prefix, 0) == 0) out.append(f.data->begin(), f.data->end());
    return out;
}

std::string verdict(Verdict v) { return std::string(verdictLabel(v)); }

// -- un projet d'essai : les fonctions de la fiche de conception ---------------
Project bench() {
    Project p;
    addVar(p, "Compteur_Clics", "INT", "0");
    // Moyenne ponderee des pressions de deux armoires.
    HmiFunction& moy = addFunction(p, "Moyenne_Pression", "REAL",
        {param(p, "A", "REAL"), param(p, "B", "REAL"), param(p, "Poids_A", "REAL", "0.5")},
        "Moyenne_Pression := A * Poids_A + B * (1.0 - Poids_A);");
    (void)moy;
    // Une procedure qui lit/ecrit une variable et trace au journal.
    addFunction(p, "Tracer_Evenement", nullptr,
        {param(p, "Quoi", "STRING"), param(p, "Niveau", "INT")},
        "Compteur_Clics := Compteur_Clics + 1;\nIHM_JOURNAL(CONCAT('[ALERTE] ', Quoi));");
    // Une fonction qui LEVE une erreur a elle (lot J5, deja dans lot-t).
    addFunction(p, "Verifier_Niveau", nullptr,
        {param(p, "Niveau", "REAL")},
        "IF Niveau > 100.0 THEN\n    LEVER('E_Niveau', 'niveau hors plage: {Niveau}');\nEND_IF;");
    // Une fonction qui divise (une division par zero leve une erreur du moteur).
    addFunction(p, "Diviser", "REAL",
        {param(p, "A", "REAL"), param(p, "B", "REAL")},
        "Diviser := A / B;");
    return p;
}

FunctionTest caseOf(Project& p, Id id, const char* name) {
    FunctionTest t;
    t.id = p.allocate();
    t.name = name;
    return t;
}

// =============================================================================
void moteur() {
    std::printf("-- le moteur : un verdict par cas\n");
    Project p = bench();
    HmiFunction* moy = p.functionByName("Moyenne_Pression") ? const_cast<HmiFunction*>(p.functionByName("Moyenne_Pression")) : nullptr;
    check(moy != nullptr, "Moyenne_Pression existe");
    if (!moy) return;

    // 1) Un cas pur exact : moyenne simple de 12 et 20 = 16.0 (tolerance exacte).
    {
        FunctionTest t = caseOf(p, moy->id, "moyenne simple");
        t.arguments = {"12.0", "20.0", ""};   // Poids_A vide : la valeur par defaut 0.5
        t.expected = "16.0";
        const auto r = runFunctionTest(p, *moy, t);
        check(r.verdict == Verdict::Passed, "cas pur exact : reussi (" + verdict(r.verdict) + ", " + r.detail + ")");
        check(r.got == "16.0", "la valeur obtenue est 16.0 (got=" + r.got + ")");
    }
    // 2) Un cas REAL dans la tolerance : poids 25 %, 18.0 a 0.001 pres.
    {
        FunctionTest t = caseOf(p, moy->id, "poids 25");
        t.arguments = {"12.0", "20.0", "0.25"};
        t.expected = "18.0";
        t.tolerance = "0.001";
        const auto r = runFunctionTest(p, *moy, t);
        check(r.verdict == Verdict::Passed, "cas REAL dans la tolerance : reussi (" + r.detail + ")");
    }
    // 2b) Hors tolerance : attendu 18.5, tolerance 0.001 -> echec.
    {
        FunctionTest t = caseOf(p, moy->id, "poids 25 faux");
        t.arguments = {"12.0", "20.0", "0.25"};
        t.expected = "18.5";
        t.tolerance = "0.001";
        const auto r = runFunctionTest(p, *moy, t);
        check(r.verdict == Verdict::Failed, "cas REAL hors tolerance : echec (" + r.detail + ")");
    }
    // 3) Une procedure : poser avant, verifier apres (une variable + le JOURNAL).
    {
        HmiFunction* tr = const_cast<HmiFunction*>(p.functionByName("Tracer_Evenement"));
        check(tr != nullptr, "Tracer_Evenement existe");
        FunctionTest t = caseOf(p, tr->id, "alerte compte le clic");
        t.givens = {{"Compteur_Clics", "0"}};
        t.arguments = {"'Porte A ouverte'", "2"};
        t.checks = {{"Compteur_Clics", "1", ""}, {"JOURNAL", "[ALERTE] Porte A ouverte", ""}};
        const auto r = runFunctionTest(p, *tr, t);
        check(r.verdict == Verdict::Passed, "procedure poser/verifier (variable + JOURNAL) : reussi (" + r.detail + ")");
    }
    // 3b) Une verif fausse : Compteur attendu 9 -> echec, le detail le dit.
    {
        HmiFunction* tr = const_cast<HmiFunction*>(p.functionByName("Tracer_Evenement"));
        FunctionTest t = caseOf(p, tr->id, "verif fausse");
        t.givens = {{"Compteur_Clics", "0"}};
        t.arguments = {"'x'", "1"};
        t.checks = {{"Compteur_Clics", "9", ""}};
        const auto r = runFunctionTest(p, *tr, t);
        check(r.verdict == Verdict::Failed && r.detail.find("Compteur_Clics") != std::string::npos,
              "une verif fausse : echec, le detail nomme la variable (" + r.detail + ")");
    }
    // 4) Un cas qui ATTEND une erreur : reussi si le bon code est leve.
    {
        HmiFunction* vn = const_cast<HmiFunction*>(p.functionByName("Verifier_Niveau"));
        check(vn != nullptr, "Verifier_Niveau existe");
        FunctionTest t = caseOf(p, vn->id, "niveau hors plage refuse");
        t.arguments = {"130.0"};
        t.errorCode = "E_Niveau";
        const auto r = runFunctionTest(p, *vn, t);
        check(r.verdict == Verdict::Passed, "attend E_Niveau, E_Niveau levee : reussi (" + r.detail + ")");
        check(r.detail.find("E_Niveau") != std::string::npos, "le detail porte le code E_Niveau (" + r.detail + ")");
    }
    // 4b) Attend une erreur mais la fonction n'en leve pas -> echec.
    {
        HmiFunction* vn = const_cast<HmiFunction*>(p.functionByName("Verifier_Niveau"));
        FunctionTest t = caseOf(p, vn->id, "niveau normal");
        t.arguments = {"42.0"};
        t.errorCode = "E_Niveau";
        const auto r = runFunctionTest(p, *vn, t);
        check(r.verdict == Verdict::Failed, "attend E_Niveau, aucune erreur : echec (" + r.detail + ")");
    }
    // 4c) Attend le mauvais code -> echec.
    {
        HmiFunction* vn = const_cast<HmiFunction*>(p.functionByName("Verifier_Niveau"));
        FunctionTest t = caseOf(p, vn->id, "mauvais code");
        t.arguments = {"130.0"};
        t.errorCode = "E_Autre";
        const auto r = runFunctionTest(p, *vn, t);
        check(r.verdict == Verdict::Failed, "attend E_Autre, E_Niveau levee : echec (" + r.detail + ")");
    }
    // 5) Un cas NORMAL qui leve (division par zero) -> verdict erreur.
    {
        HmiFunction* dv = const_cast<HmiFunction*>(p.functionByName("Diviser"));
        FunctionTest t = caseOf(p, dv->id, "division par zero");
        t.arguments = {"10.0", "0.0"};
        t.expected = "0.0";
        const auto r = runFunctionTest(p, *dv, t);
        check(r.verdict == Verdict::Error, "un cas normal qui leve : verdict erreur (" + verdict(r.verdict) + ", " + r.detail + ")");
    }
    // Un cas inactif : non joue.
    {
        FunctionTest t = caseOf(p, moy->id, "inactif");
        t.arguments = {"1.0", "2.0", ""};
        t.expected = "1.5";
        t.enabled = false;
        const auto r = runFunctionTest(p, *moy, t);
        check(r.verdict == Verdict::Skipped, "un cas inactif : non joue");
    }
}

// =============================================================================
void comptes() {
    std::printf("-- le compte des verdicts (reussis / echecs / erreurs)\n");
    Project p = bench();
    HmiFunction* moy = const_cast<HmiFunction*>(p.functionByName("Moyenne_Pression"));
    HmiFunction* dv = const_cast<HmiFunction*>(p.functionByName("Diviser"));
    {
        FunctionTest a = caseOf(p, moy->id, "ok");
        a.arguments = {"12.0", "20.0", ""}; a.expected = "16.0";
        FunctionTest b = caseOf(p, moy->id, "faux");
        b.arguments = {"12.0", "20.0", ""}; b.expected = "99.0";
        moy->tests = {a, b};
    }
    {
        FunctionTest c = caseOf(p, dv->id, "zero");
        c.arguments = {"1.0", "0.0"}; c.expected = "0.0";
        dv->tests = {c};
    }
    const auto rMoy = runFunctionTests(p, *moy);
    check(rMoy.results.size() == 2, "Moyenne_Pression : deux cas joues");
    check(rMoy.count(Verdict::Passed) == 1 && rMoy.count(Verdict::Failed) == 1, "un reussi, un echec");
    check(rMoy.failed() && !rMoy.ok(), "la fonction porte un echec (pastille rouge)");
    check(rMoy.summary().find("2 cas") != std::string::npos, "le bilan : " + rMoy.summary());
    const auto rDv = runFunctionTests(p, *dv);
    check(rDv.count(Verdict::Error) == 1, "Diviser : une erreur");

    // Le rapport de campagne.
    std::vector<std::pair<const HmiFunction*, const FunctionTestReport*>> runs = {{moy, &rMoy}, {dv, &rDv}};
    const ExportTable t = campaignTestTable(runs);
    check(t.title.find("1 / 3") != std::string::npos, "campagne : 1 / 3 reussi(s) (" + t.title + ")");
    const ExportTable one = functionTestTable(*moy, rMoy);
    check(one.rows.size() == 2 && one.headers.size() == 6, "rapport d'une fonction : deux lignes, six colonnes");
}

// =============================================================================
void generer() {
    std::printf("-- Generer : les controles, sans jouer\n");
    Project p = bench();
    HmiFunction* moy = const_cast<HmiFunction*>(p.functionByName("Moyenne_Pression"));
    HmiFunction* tr = const_cast<HmiFunction*>(p.functionByName("Tracer_Evenement"));
    FunctionTest good = caseOf(p, moy->id, "bon");
    good.arguments = {"1.0", "2.0", ""}; good.expected = "1.5";
    FunctionTest tooMany = caseOf(p, moy->id, "trop");
    tooMany.arguments = {"1.0", "2.0", "0.5", "7.0"}; tooMany.expected = "1.5";
    FunctionTest noName = caseOf(p, moy->id, "");
    noName.arguments = {"1.0", "2.0", ""}; noName.expected = "1.5";
    FunctionTest badTol = caseOf(p, moy->id, "tol");
    badTol.arguments = {"1.0", "2.0", ""}; badTol.expected = "1.5"; badTol.tolerance = "abc";
    moy->tests = {good, tooMany, noName, badTol};
    // Un attendu sur une procedure (sans retour).
    FunctionTest proc = caseOf(p, tr->id, "attendu sur procedure");
    proc.arguments = {"'x'", "1"}; proc.expected = "3";
    tr->tests = {proc};

    std::vector<Issue> issues;
    checkFunctionTests(p, issues);
    const auto has = [&](const std::string& needle) {
        for (const auto& i : issues) if (i.message.find(needle) != std::string::npos) return true;
        return false;
    };
    check(has("trop d'arguments"), "Generer : trop d'arguments");
    check(has("cas sans nom"), "Generer : cas sans nom");
    check(has("tol\xC3\xA9rance non num\xC3\xA9rique"), "Generer : tolerance non numerique");
    check(has("pas de retour"), "Generer : attendu sur une procedure");
    for (const auto& i : issues) if (i.category == "Test") { check(true, "une Issue de categorie Test"); break; }
}

// =============================================================================
void disque() {
    std::printf("-- la serialisation projet (format 29, repli, refus, surcharges)\n");
    Project p = bench();
    HmiFunction* moy = const_cast<HmiFunction*>(p.functionByName("Moyenne_Pression"));
    HmiFunction* tr = const_cast<HmiFunction*>(p.functionByName("Tracer_Evenement"));
    // 1.13.0 (lot J8) : l'identifiant garde a part - addFunction plus bas peut deplacer le vecteur des fonctions
    // (le pointeur `tr` ne vaut plus rien apres).
    const Id trId = tr ? tr->id : kNoId;
    // Moyenne_Pression recoit deux cas ; une procedure un cas avec pose et verif.
    {
        FunctionTest a = caseOf(p, moy->id, "poids 25");
        a.arguments = {"12.0", "20.0", "0.25"}; a.expected = "18.0"; a.tolerance = "0.001";
        FunctionTest b = caseOf(p, moy->id, "moyenne simple");
        b.arguments = {"12.0", "20.0", ""}; b.expected = "16.0";
        b.description = "le defaut Poids_A = 0.5";
        moy->tests = {a, b};
    }
    {
        FunctionTest c = caseOf(p, tr->id, "alerte");
        c.givens = {{"Compteur_Clics", "0"}};
        c.arguments = {"'Porte A ouverte'", "2"};
        c.checks = {{"Compteur_Clics", "1", ""}, {"JOURNAL", "[ALERTE] Porte A ouverte", ""}};
        c.enabled = false;
        tr->tests = {c};
    }
    // Une SURCHARGE de Moyenne_Pression (le meme nom, un parametre de moins) avec son propre cas.
    HmiFunction& moy2 = addFunction(p, "Moyenne_Pression", "REAL",
        {param(p, "A", "REAL"), param(p, "B", "REAL")},
        "Moyenne_Pression := (A + B) / 2.0;");
    {
        FunctionTest d = caseOf(p, moy2.id, "deux arguments");
        d.arguments = {"10.0", "30.0"}; d.expected = "20.0";
        moy2.tests = {d};
    }

    const auto files = serializeProject(p);
    const std::string index = fileText(files, "ihm.txt");
    check(index.find("ihm format=29") != std::string::npos && index.find("(format 29)") != std::string::npos,
          "avec des cas : l'index au format 29");
    check(index.find("cas_test id=") != std::string::npos && index.find("nom=\"poids 25\"") != std::string::npos
              && index.find("arg cas=") != std::string::npos && index.find("pose cas=") != std::string::npos
              && index.find("verif cas=") != std::string::npos,
          "les lignes cas_test / arg / pose / verif sont ecrites");
    check(index.find("actif=non") != std::string::npos, "un cas inactif s'ecrit actif=non");

    const auto back = parseProject(readerOf(files));
    check(back.has_value(), "relu");
    if (back.has_value()) {
        check(back.value().programs.functions == p.programs.functions,
              "aller-retour : les fonctions (et leurs cas) relues identiques");
        // Chaque surcharge garde ses propres cas.
        const auto named = back.value().functionsNamed("Moyenne_Pression");
        check(named.size() == 2, "deux surcharges Moyenne_Pression relues");
        if (named.size() == 2) {
            const HmiFunction* three = named[0]->decls.size() == 3 ? named[0] : named[1];
            const HmiFunction* two = named[0]->decls.size() == 3 ? named[1] : named[0];
            check(three->tests.size() == 2 && two->tests.size() == 1, "la surcharge a 3 parametres a 2 cas, celle a 2 en a 1");
            check(three->tests[0].name == "poids 25" && three->tests[0].tolerance == "0.001"
                      && three->tests[0].arguments.size() == 3, "le cas poids 25 : nom, tolerance, trois arguments");
            const HmiFunction* trBack = back.value().function(trId);
            check(trBack && !trBack->tests.empty() && trBack->tests[0].givens.size() == 1
                      && trBack->tests[0].checks.size() == 2
                      && trBack->tests[0].enabled == false,
                  "la procedure : une pose, deux verifs, cas inactif relu tel quel");
        }
        check(back.value().nextId > moy2.tests[0].id, "le compteur passe au-dessus des identifiants de cas");
    }

    // Repli : sans aucun cas, le format redescend (la 1.12.31 l'ouvre).
    {
        Project plain = p;
        for (auto& f : plain.programs.functions) f.tests.clear();
        const std::string pindex = fileText(serializeProject(plain), "ihm.txt");
        check(pindex.find("ihm format=29") == std::string::npos && pindex.find("cas_test") == std::string::npos,
              "sans cas : le format redescend sous 29, sans ligne cas_test");
    }
    // Refus d'un format plus recent (34 : au-dela du format courant - 33 est valide depuis le lot J8 espaces de noms).
    {
        auto newer = files;
        std::string text = fileText(files, "ihm.txt");
        const auto at = text.find("ihm format=29");
        text.replace(at, 13, "ihm format=34");
        for (auto& f : newer)
            if (f.path == "ihm.txt") f.data = std::make_shared<const Bytes>(text.begin(), text.end());
        const auto refused = parseProject(readerOf(newer));
        check(!refused.has_value() && refused.error().context.find("34") != std::string::npos,
              "un projet au format 34 : refuse proprement, et dit pourquoi");
    }
    // Les deux cases de Configuration survivent a l'aller-retour.
    {
        Project cfg = bench();
        cfg.config.runTestsOnBuild = false;
        cfg.config.testFailBlocksStart = true;
        const auto f2 = serializeProject(cfg);
        const auto back2 = parseProject(readerOf(f2));
        check(back2.has_value() && !back2.value().config.runTestsOnBuild && back2.value().config.testFailBlocksStart,
              "les deux cases de Configuration (build, echec bloquant) relues");
    }
}

// =============================================================================
//  1.13.0 (relecture) : les defauts trouves avant la livraison, chacun par son scenario.
// =============================================================================
const HmiFunction* projectFunction(const Project& p, const char* name) { return p.functionByName(name); }

// Les avertissements de la relecture qui disent qu'un cas ou une ligne de test est ignore.
bool testLinesIgnored(const LoadReport& report) {
    for (const auto& w : report.warnings)
        if (w.find("cas de test") != std::string::npos || w.find("ligne de test") != std::string::npos) return true;
    return false;
}

// n4 : une fonction (ou une methode) GENERIQUE et ses cas - la ligne "generique" passe entre la fonction et ses
// cas a l'ecriture ; la relecture les perdait (cas de test hors d'une fonction : ignore).
void generiquesRelus() {
    std::printf("-- les cas d'une fonction generique : relus (fonction du projet, methode d'un type, fonction de vue)\n");
    Project p = bench();
    {
        HmiFunction& mx = addFunction(p, "Max_Gen", "T", {param(p, "A", "T"), param(p, "B", "T")},
                                      "IF A > B THEN\n    Max_Gen := A;\nELSE\n    Max_Gen := B;\nEND_IF;");
        mx.generics = {{"T", "ANY_NUM"}};
        FunctionTest t = caseOf(p, mx.id, "deux entiers");
        t.arguments = {"1", "3"};
        t.expected = "3";
        t.checks = {{"Compteur_Clics", "0", ""}};
        mx.tests = {t};
    }
    {
        HmiType ty;
        ty.id = p.allocate();
        ty.name = "T_Bac";
        TypeMember m;
        m.name = "Niveau";
        m.type = "REAL";
        ty.members.push_back(m);
        HmiFunction meth;
        meth.id = p.allocate();
        meth.name = "Borne";
        meth.returnType = "U";
        meth.generics = {{"U", "ANY_NUM"}};
        meth.decls = {param(p, "X", "U")};
        meth.body = "Borne := X;";
        FunctionTest t = caseOf(p, meth.id, "borne simple");
        t.arguments = {"2"};
        t.expected = "2";
        meth.tests = {t};
        ty.methods.push_back(meth);
        p.programs.types.push_back(ty);
    }
    {
        View v = makeView(p, "Vue_Four");
        HmiFunction vf;
        vf.id = p.allocate();
        vf.name = "Moitie";
        vf.returnType = "V";
        vf.generics = {{"V", "ANY_REAL"}};
        vf.decls = {param(p, "X", "V")};
        vf.body = "Moitie := X / 2.0;";
        FunctionTest t = caseOf(p, vf.id, "moitie de 10");
        t.arguments = {"10.0"};
        t.expected = "5.0";
        t.givens = {{"Compteur_Clics", "4"}};
        vf.tests = {t};
        v.functions.push_back(vf);
        p.views.push_back(v);
    }
    const auto files = serializeProject(p);
    LoadReport report;
    const auto back = parseProject(readerOf(files), &report);
    check(back.has_value(), "relu");
    if (!back.has_value()) return;
    check(!testLinesIgnored(report), "aucune ligne de test ignoree a la relecture"
                                        + (report.warnings.empty() ? std::string{} : " (" + report.warnings.front() + ")"));
    const HmiFunction* mx = projectFunction(back.value(), "Max_Gen");
    check(mx && mx->generics.size() == 1 && mx->tests.size() == 1 && mx->tests[0].arguments.size() == 2
              && mx->tests[0].checks.size() == 1 && mx->tests[0].expected == "3",
          "fonction du projet generique : son cas, ses deux arguments et sa verif relus");
    const HmiType* bac = back.value().hmiTypeByName("T_Bac");
    check(bac && bac->methods.size() == 1 && bac->methods[0].generics.size() == 1 && bac->methods[0].tests.size() == 1
              && bac->methods[0].tests[0].arguments == std::vector<std::string>{"2"},
          "methode generique d'un type : son cas et son argument relus");
    const View* four = back.value().viewByName("Vue_Four");
    check(four && four->functions.size() == 1 && four->functions[0].generics.size() == 1 && four->functions[0].tests.size() == 1
              && four->functions[0].tests[0].givens.size() == 1,
          "fonction generique d'une vue (fichier .vue) : son cas et sa pose relus");
    // Et l'enregistrement suivant ne les efface pas : un second aller-retour garde tout.
    const auto again = parseProject(readerOf(serializeProject(back.value())));
    check(again.has_value() && again.value().programs.functions == back.value().programs.functions
              && again.value().programs.types == back.value().programs.types && again.value().views == back.value().views,
          "un second aller-retour : identique (les cas restent sur le disque)");
}

// n18 : le cas d'une fonction de VUE (ou de popup) se joue - avant : "fonction IHM introuvable", verdict erreur.
void fonctionDeVue() {
    std::printf("-- le cas d'une fonction de vue se joue (ses constantes de vue comprises)\n");
    Project p = bench();
    View v = makeView(p, "Vue_Four");
    Declaration pente;
    pente.id = p.allocate();
    pente.kind = DeclKind::Constant;
    pente.name = "Pente";
    pente.type = "REAL";
    pente.value = "2.0";
    v.constants.push_back(pente);
    HmiFunction rampe;
    rampe.id = p.allocate();
    rampe.name = "Calcul_Rampe";
    rampe.returnType = "REAL";
    rampe.decls = {param(p, "Debut", "REAL"), param(p, "Fin", "REAL")};
    rampe.body = "Calcul_Rampe := (Fin - Debut) * Pente;";
    v.functions.push_back(rampe);
    p.views.push_back(v);
    const HmiFunction& f = p.views.back().functions.back();
    FunctionTest t = caseOf(p, f.id, "rampe nominale");
    t.arguments = {"10.0", "20.0"};
    t.expected = "20.0";
    const auto r = runFunctionTest(p, f, t);
    check(r.verdict == Verdict::Passed && r.got == "20.0",
          "Vue_Four.Calcul_Rampe(10, 20) = 20.0 : reussi (" + verdict(r.verdict) + ", " + r.detail + ")");
    FunctionTest wrong = caseOf(p, f.id, "rampe fausse");
    wrong.arguments = {"10.0", "20.0"};
    wrong.expected = "15.0";
    const auto r2 = runFunctionTest(p, f, wrong);
    check(r2.verdict == Verdict::Failed, "un attendu faux sur une fonction de vue : echec, pas erreur (" + verdict(r2.verdict) + ", " + r2.detail + ")");
}

// n19 : l'erreur attendue se lit par son CODE exact - avant, une sous-chaine du message suffisait.
void codesErreur() {
    std::printf("-- attendre une erreur : le code leve, exactement\n");
    Project p = bench();
    addFunction(p, "Verifier_Bas", nullptr, {param(p, "Niveau", "REAL")},
                "IF Niveau < 10.0 THEN\n    LEVER('E_NIVEAU_BAS', 'trop bas');\nEND_IF;");
    addFunction(p, "Verifier_Cite", nullptr, {param(p, "Niveau", "REAL")},
                "LEVER('E_Autre', 'ce n est pas E_Niveau');");
    addFunction(p, "Verifier_E_Niveau", "REAL", {param(p, "A", "REAL"), param(p, "B", "REAL")}, "Verifier_E_Niveau := A / B;");
    const auto play = [&](const char* fn, std::vector<std::string> args, const char* code) {
        const HmiFunction* f = p.functionByName(fn);
        FunctionTest t = caseOf(p, f ? f->id : kNoId, code);
        t.arguments = std::move(args);
        t.errorCode = code;
        return f ? runFunctionTest(p, *f, t) : FunctionTestResult{};
    };
    {
        const auto r = play("Verifier_Bas", {"5.0"}, "E_NIVEAU");
        check(r.verdict == Verdict::Failed, "attend E_NIVEAU, la fonction leve E_NIVEAU_BAS : echec (" + verdict(r.verdict) + ", " + r.detail + ")");
    }
    {
        const auto r = play("Verifier_Bas", {"5.0"}, "E_Niveau_Bas");
        check(r.verdict == Verdict::Passed, "attend E_Niveau_Bas (sans casse), E_NIVEAU_BAS levee : reussi (" + r.detail + ")");
    }
    {
        const auto r = play("Verifier_Cite", {"5.0"}, "E_Niveau");
        check(r.verdict == Verdict::Failed, "attend E_Niveau, E_Autre levee (E_Niveau cite dans son message) : echec (" + r.detail + ")");
    }
    {
        const auto r = play("Verifier_E_Niveau", {"1.0", "0.0"}, "E_Niveau");
        check(r.verdict == Verdict::Failed,
              "attend E_Niveau, une division par zero dans Verifier_E_Niveau (le code dans le nom) : echec (" + r.detail + ")");
    }
    {
        const auto r = play("Verifier_E_Niveau", {"1.0", "0.0"}, "division");
        check(r.verdict == Verdict::Passed, "une erreur du moteur (sans code) : un mot entier de son message suffit (" + r.detail + ")");
    }
}

// n20 : un retour STRING se compare tel quel (casse et accents compris) - avant : 'AZOTE' valait 'Azote'.
void textesExacts() {
    std::printf("-- un texte se compare en egalite exacte\n");
    Project p = bench();
    addFunction(p, "Libelle_Gaz", "STRING", {param(p, "Code", "INT")},
                "CASE Code OF\n    1: Libelle_Gaz := 'AZOTE';\n    2: Libelle_Gaz := 'Arret';\nELSE\n    Libelle_Gaz := 'Azote';\nEND_CASE;");
    addVar(p, "Libelle_Lu", "STRING", "''");
    addFunction(p, "Poser_Libelle", nullptr, {param(p, "Texte", "STRING")}, "Libelle_Lu := Texte;");
    const HmiFunction* f = p.functionByName("Libelle_Gaz");
    const auto play = [&](const char* arg, const char* expected) {
        FunctionTest t = caseOf(p, f->id, expected);
        t.arguments = {arg};
        t.expected = expected;
        return runFunctionTest(p, *f, t);
    };
    check(play("1", "Azote").verdict == Verdict::Failed, "rend 'AZOTE', attendu Azote : echec (la casse compte)");
    check(play("2", "Arr\xC3\xAAt").verdict == Verdict::Failed, "rend 'Arret', attendu Arret accentue : echec (l'accent compte)");
    check(play("3", "Azote").verdict == Verdict::Passed && play("3", "'Azote'").verdict == Verdict::Passed,
          "rend 'Azote', attendu Azote ou 'Azote' : reussi");
    // Une verif sur une variable STRING : la meme regle.
    const HmiFunction* pose = p.functionByName("Poser_Libelle");
    FunctionTest t = caseOf(p, pose->id, "verif texte");
    t.arguments = {"'AZOTE'"};
    t.checks = {{"Libelle_Lu", "Azote", ""}};
    const auto r = runFunctionTest(p, *pose, t);
    check(r.verdict == Verdict::Failed, "une verif STRING 'AZOTE' contre Azote : echec (" + r.detail + ")");
}

// n37 : un retour (ou une verif) d'enumeration se compare par le nom de sa valeur - avant : "obtenu 2, attendu Auto".
void enumerations() {
    std::printf("-- une enumeration se compare par son nom\n");
    Project p = bench();
    HmiType mode;
    mode.id = p.allocate();
    mode.name = "E_Mode";
    mode.kind = HmiTypeKind::Enumeration;
    for (const auto& [n, v] : std::vector<std::pair<const char*, int>>{{"Arret", 0}, {"Manu", 1}, {"Auto", 2}}) {
        HmiEnumValue ev;
        ev.name = n;
        ev.value = v;
        mode.values.push_back(ev);
    }
    p.programs.types.push_back(mode);
    addVar(p, "Mode_Actuel", "E_Mode", "E_Mode#Arret");
    addFunction(p, "Mode_Demande", "E_Mode", {param(p, "Automatique", "BOOL")},
                "IF Automatique THEN\n    Mode_Demande := E_Mode#Auto;\nELSE\n    Mode_Demande := E_Mode#Manu;\nEND_IF;");
    addFunction(p, "Passer_Auto", nullptr, {}, "Mode_Actuel := E_Mode#Auto;");
    const HmiFunction* f = p.functionByName("Mode_Demande");
    const auto play = [&](const char* expected) {
        FunctionTest t = caseOf(p, f->id, expected);
        t.arguments = {"TRUE"};
        t.expected = expected;
        return runFunctionTest(p, *f, t);
    };
    {
        const auto r = play("Auto");
        check(r.verdict == Verdict::Passed, "rend Auto (2), attendu Auto : reussi (" + verdict(r.verdict) + ", " + r.detail + ")");
        check(r.detail.find("Auto (2)") != std::string::npos, "le detail montre le nom de la valeur obtenue (" + r.detail + ")");
    }
    check(play("E_Mode#Auto").verdict == Verdict::Passed, "attendu E_Mode#Auto : reussi");
    check(play("2").verdict == Verdict::Passed, "attendu 2 (le numero) : reussi, comme avant");
    check(play("Manu").verdict == Verdict::Failed, "attendu Manu : echec");
    const HmiFunction* pa = p.functionByName("Passer_Auto");
    FunctionTest t = caseOf(p, pa->id, "passe en auto");
    t.checks = {{"Mode_Actuel", "Auto", ""}};
    const auto r = runFunctionTest(p, *pa, t);
    check(r.verdict == Verdict::Passed, "une verif d'une variable E_Mode, attendu Auto : reussi (" + verdict(r.verdict) + ", " + r.detail + ")");
}

// n38 : Generer signale une pose ou une verif sur une variable inconnue, et un attendu illisible.
void genererInconnues() {
    std::printf("-- Generer : pose ou verif sur une variable inconnue, attendu illisible\n");
    Project p = bench();
    HmiFunction* tr = const_cast<HmiFunction*>(p.functionByName("Tracer_Evenement"));
    HmiFunction* moy = const_cast<HmiFunction*>(p.functionByName("Moyenne_Pression"));
    FunctionTest typo = caseOf(p, tr->id, "faute de frappe");
    typo.arguments = {"'x'", "1"};
    typo.givens = {{"Compteur_Clic", "0"}};
    typo.checks = {{"Compteur_Clik", "1", ""}};
    FunctionTest good = caseOf(p, tr->id, "bon");
    good.arguments = {"'x'", "1"};
    good.givens = {{"Compteur_Clics", "0"}};
    good.checks = {{"Compteur_Clics", "1", ""}, {"JOURNAL", "x", ""}};
    tr->tests = {typo, good};
    FunctionTest bad = caseOf(p, moy->id, "attendu illisible");
    bad.arguments = {"1.0", "2.0", ""};
    bad.expected = "seize";
    FunctionTest fine = caseOf(p, moy->id, "attendu lisible");
    fine.arguments = {"1.0", "2.0", ""};
    fine.expected = "1.5";
    moy->tests = {bad, fine};
    std::vector<Issue> issues;
    checkFunctionTests(p, issues);
    const auto said = [&](const std::string& caseName, const std::string& needle) {
        for (const auto& i : issues)
            if (i.property.find(caseName) != std::string::npos && i.message.find(needle) != std::string::npos) return true;
        return false;
    };
    check(said("faute de frappe", "pose \xC2\xAB Compteur_Clic \xC2\xBB : variable inconnue"), "Generer : la pose sur Compteur_Clic (inconnue) est dite");
    check(said("faute de frappe", "v\xC3\xA9rif \xC2\xAB Compteur_Clik \xC2\xBB : variable inconnue"), "Generer : la verif sur Compteur_Clik (inconnue) est dite");
    check(!said("bon", "inconnue"), "Generer : rien sur les poses et verifs d'une variable connue (ni sur JOURNAL)");
    check(said("attendu illisible", "attendu illisible : seize n'est pas un nombre"), "Generer : un attendu illisible sur un retour REAL est dit");
    check(!said("attendu lisible", "attendu illisible"), "Generer : un attendu lisible ne dit rien");
}

// =========================================== 1.13.0 (seconde passe, lot D3) ===
const Issue* issueWith(const std::vector<Issue>& issues, const std::string& caseName, const std::string& needle) {
    for (const auto& i : issues)
        if (i.property.find(caseName) != std::string::npos && i.message.find(needle) != std::string::npos) return &i;
    return nullptr;
}

// T2-26 : un attendu ecrit en formule (=Seuil_Haut - 5) ou par un nom connu (Seuil_Haut) s'evalue apres l'appel.
void attenduCalcule() {
    std::printf("-- T2-26 : un attendu calcule (=Seuil_Haut, =Seuil_Haut - 5, Seuil_Haut) s'evalue\n");
    Project p = bench();
    addVar(p, "Seuil_Haut", "REAL", "80.0");
    addVar(p, "Sortie", "REAL", "0.0");
    HmiFunction& lim = addFunction(p, "Limiter", "REAL", {param(p, "X", "REAL")},
                                   "Sortie := Seuil_Haut - 5.0;\nLimiter := MIN(X, Seuil_Haut);");
    FunctionTest formule = caseOf(p, lim.id, "formule");
    formule.arguments = {"120.0"};
    formule.expected = "=Seuil_Haut";
    FunctionTest calcul = caseOf(p, lim.id, "calcul");
    calcul.arguments = {"75.0"};
    calcul.expected = "=Seuil_Haut - 5";
    FunctionTest nom = caseOf(p, lim.id, "nom seul");
    nom.arguments = {"120.0"};
    nom.expected = "Seuil_Haut";
    FunctionTest faux = caseOf(p, lim.id, "faux");
    faux.arguments = {"10.0"};
    faux.expected = "=Seuil_Haut";
    FunctionTest verif = caseOf(p, lim.id, "verif calculee");
    verif.arguments = {"1.0"};
    verif.checks = {{"Sortie", "=Seuil_Haut - 5.0", ""}};
    lim.tests = {formule, calcul, nom, faux, verif};
    const auto rep = runFunctionTests(p, lim);
    check(rep.results.size() == 5, "cinq cas joues");
    if (rep.results.size() != 5) return;
    check(rep.results[0].verdict == Verdict::Passed, "attendu =Seuil_Haut : reussi (" + verdict(rep.results[0].verdict) + ", " + rep.results[0].detail + ")");
    check(rep.results[1].verdict == Verdict::Passed, "attendu =Seuil_Haut - 5 : reussi (" + rep.results[1].detail + ")");
    check(rep.results[2].verdict == Verdict::Passed, "attendu Seuil_Haut (un nom connu seul) : reussi (" + rep.results[2].detail + ")");
    check(rep.results[3].verdict == Verdict::Failed && rep.results[3].detail.find("attendu =Seuil_Haut (80.0)") != std::string::npos,
          "attendu =Seuil_Haut sur 10.0 : echec, le detail montre la valeur calculee (" + rep.results[3].detail + ")");
    check(rep.results[4].verdict == Verdict::Passed, "verif Sortie = =Seuil_Haut - 5.0 : reussi (" + rep.results[4].detail + ")");
    std::vector<Issue> issues;
    checkFunctionTests(p, issues);
    check(!issueWith(issues, "formule", "illisible") && !issueWith(issues, "calcul", "illisible") && !issueWith(issues, "nom seul", "illisible"),
          "Generer : un attendu calcule n'est pas illisible");
    lim.tests = {formule};
    lim.tests[0].expected = "=Seuil_Bas";
    issues.clear();
    checkFunctionTests(p, issues);
    check(issueWith(issues, "formule", "Seuil_Bas n'est pas un nom connu"), "Generer : une formule sur un nom inconnu est dite");
}

// T2-27, R26 : une pose =Variable n'est plus "illisible" ; un membre mal ecrit, l'attendu d'une verif sont controles.
void posesEtVerifs() {
    std::printf("-- T2-27 / R26 : poses en formule, membres, attendu d'une verif\n");
    Project p = bench();
    addVar(p, "Seuil", "INT", "7");
    HmiType cuve;
    cuve.id = p.allocate();
    cuve.name = "T_Cuve";
    TypeMember niveau;
    niveau.name = "Niveau";
    niveau.type = "REAL";
    cuve.members.push_back(niveau);
    p.programs.types.push_back(cuve);
    addVar(p, "Cuve", "T_Cuve", "");
    addVar(p, "Mesure", "REAL", "0.0");
    HmiFunction* tr = const_cast<HmiFunction*>(p.functionByName("Tracer_Evenement"));
    FunctionTest pose = caseOf(p, tr->id, "pose formule");
    pose.arguments = {"'x'", "1"};
    pose.givens = {{"Compteur_Clics", "=Seuil"}};
    pose.checks = {{"Compteur_Clics", "8", ""}};
    FunctionTest calc = caseOf(p, tr->id, "pose calcul");
    calc.arguments = {"'x'", "1"};
    calc.givens = {{"Compteur_Clics", "=Seuil + 1"}};
    calc.checks = {{"Compteur_Clics", "9", ""}};
    FunctionTest membre = caseOf(p, tr->id, "membre faux");
    membre.arguments = {"'x'", "1"};
    membre.givens = {{"Cuve.Niveu", "2.5"}};
    membre.checks = {{"Cuve.Niveux", "1", ""}};
    FunctionTest verif = caseOf(p, tr->id, "verif illisible");
    verif.arguments = {"'x'", "1"};
    verif.checks = {{"Mesure", "seize", ""}};
    FunctionTest virgule = caseOf(p, tr->id, "pose membre virgule");
    virgule.arguments = {"'x'", "1"};
    virgule.givens = {{"Cuve.Niveau", "2,5"}};
    virgule.checks = {{"Cuve.Niveau", "2.5", ""}};
    tr->tests = {pose, calc, membre, verif, virgule};
    std::vector<Issue> issues;
    checkFunctionTests(p, issues);
    check(!issueWith(issues, "pose formule", "valeur illisible") && !issueWith(issues, "pose calcul", "valeur illisible"),
          "Generer : une pose =Seuil (ou =Seuil + 1) n'est plus illisible");
    check(issueWith(issues, "membre faux", "pose \xC2\xAB Cuve.Niveu \xC2\xBB"), "Generer : une pose sur Cuve.Niveu (membre inconnu) est dite");
    check(issueWith(issues, "membre faux", "v\xC3\xA9rif \xC2\xAB Cuve.Niveux \xC2\xBB"), "Generer : une verif sur Cuve.Niveux (membre inconnu) est dite");
    check(issueWith(issues, "verif illisible", "attendu illisible : seize n'est pas un nombre"),
          "Generer : l'attendu d'une verif sur un REAL, illisible (seize), est dit");
    check(!issueWith(issues, "pose membre virgule", "illisible"), "Generer : une pose 2,5 sur un membre REAL se lit");
    const auto rep = runFunctionTests(p, *tr);
    check(rep.results.size() == 5 && rep.results[0].verdict == Verdict::Passed, "pose =Seuil jouee : reussi");
    check(rep.results.size() == 5 && rep.results[1].verdict == Verdict::Passed, "pose =Seuil + 1 jouee : reussi");
    check(rep.results.size() == 5 && rep.results[4].verdict == Verdict::Passed,
          "pose Cuve.Niveau := 2,5 jouee : reussi (" + (rep.results.size() == 5 ? rep.results[4].detail : std::string{}) + ")");
}

// R28 : NAN et INF pour un retour REAL ne sont pas "illisibles".
void nanEtInf() {
    std::printf("-- R28 : NAN, INF, -INF pour un retour REAL\n");
    Project p = bench();
    HmiFunction* div = const_cast<HmiFunction*>(p.functionByName("Diviser"));
    FunctionTest n = caseOf(p, div->id, "nan");
    n.arguments = {"1.0", "1.0"};
    n.expected = "NAN";
    FunctionTest i = caseOf(p, div->id, "inf");
    i.arguments = {"1.0", "1.0"};
    i.expected = "-INF";
    div->tests = {n, i};
    std::vector<Issue> issues;
    checkFunctionTests(p, issues);
    check(!issueWith(issues, "nan", "illisible") && !issueWith(issues, "inf", "illisible"), "Generer : NAN et -INF attendus d'un REAL : lisibles");
    const auto rep = runFunctionTests(p, *div);
    check(rep.results.size() == 2 && rep.results[0].verdict == Verdict::Failed && rep.results[1].verdict == Verdict::Failed,
          "1.0 / 1.0 n'est ni NAN ni -INF : echec (pas une erreur)");
}

// T2-28 / T2-54 : une fonction generique - 2,5 et VRAI se lisent dans la contrainte, la tolerance sert a un T reel.
void generiqueLu() {
    std::printf("-- T2-28 / T2-54 : un T se lit dans sa contrainte ; la tolerance sur un T reel\n");
    Project p = bench();
    HmiFunction& moy = addFunction(p, "Moyenne3", "T", {param(p, "A", "T"), param(p, "B", "T"), param(p, "C", "T")},
                                   "Moyenne3 := (A + B + C) / 3.0;");
    moy.generics = {{"T", "ANY_REAL"}};
    HmiFunction& egal = addFunction(p, "Pareil", "BOOL", {param(p, "X", "U"), param(p, "Y", "U")}, "Pareil := X = Y;");
    egal.generics = {{"U", ""}};
    check(testCellType(moy, "T") == "ANY_REAL" && testCellType(egal, "U") == "ANY" && testCellType(moy, "REAL") == "REAL",
          "testCellType : T -> ANY_REAL, U sans contrainte -> ANY, REAL tel quel");
    check(testToleranceApplies(moy) && !testToleranceApplies(egal) && testToleranceApplies(*p.functionByName("Diviser")),
          "testToleranceApplies : un T : ANY_REAL oui, un BOOL non, un REAL oui");
    FunctionTest t = caseOf(p, moy.id, "un tiers");
    t.arguments = {"1,0", "2,0", "2"};
    t.expected = "1.667";
    t.tolerance = "0.001";
    moy.tests = {t};
    FunctionTest v = caseOf(p, egal.id, "vrai et vrai");
    v.arguments = {"VRAI", "TRUE"};
    v.expected = "VRAI";
    FunctionTest a = caseOf(p, egal.id, "azote");
    a.arguments = {"Azote", "'Azote'"};
    a.expected = "TRUE";
    egal.tests = {v, a};
    std::vector<Issue> issues;
    checkFunctionTests(p, issues);
    check(!issueWith(issues, "un tiers", "se donne par une variable") && !issueWith(issues, "un tiers", "tol\xC3\xA9rance"),
          "Generer : 1,0 pour un T : ANY_REAL se lit, la tolerance sert");
    check(!issueWith(issues, "vrai et vrai", "variable") && !issueWith(issues, "azote", "variable"), "Generer : VRAI et Azote pour un U se lisent");
    const auto rep = runFunctionTests(p, moy);
    check(rep.results.size() == 1 && rep.results[0].verdict == Verdict::Passed,
          "Moyenne3(1,0 ; 2,0 ; 2) = 1.667 a 0.001 pres : reussi (" + (rep.results.empty() ? std::string{} : rep.results[0].detail + " ; " + rep.results[0].call) + ")");
    const auto rep2 = runFunctionTests(p, egal);
    check(rep2.results.size() == 2 && rep2.results[0].verdict == Verdict::Passed && rep2.results[1].verdict == Verdict::Passed,
          "Pareil(VRAI, TRUE) et Pareil(Azote, 'Azote') : reussis");
}

// T2-25 : les arguments suivent leur parametre (retire, deplace) ; le surplus reste au bout.
void argumentsSuivent() {
    std::printf("-- T2-25 : les arguments des cas suivent leur parametre\n");
    Project p = bench();
    HmiFunction* moy = const_cast<HmiFunction*>(p.functionByName("Moyenne_Pression"));
    FunctionTest poids = caseOf(p, moy->id, "poids 25 %");
    poids.arguments = {"12.0", "20.0", "0.25"};
    FunctionTest simple = caseOf(p, moy->id, "moyenne simple");
    simple.arguments = {"12.0", "20.0"};
    moy->tests = {poids, simple};
    check(testParameterNames(*moy) == std::vector<std::string>{"A", "B", "Poids_A"}, "les noms des parametres, dans l'ordre");
    HmiFunction removed = *moy;
    check(remapTestArguments(removed, {"A", "B", "Poids_A"}, {"A", "Poids_A"}), "B retire : les cas changent");
    check(removed.tests[0].arguments == std::vector<std::string>{"12.0", "0.25"} && removed.tests[1].arguments == std::vector<std::string>{"12.0"},
          "B retire : 12.0 ; 0.25 et 12.0 (pas 12.0 ; 20.0 ; 0.25)");
    HmiFunction moved = *moy;
    check(remapTestArguments(moved, {"A", "B", "Poids_A"}, {"B", "A", "Poids_A"}), "A descendu : les cas changent");
    check(moved.tests[0].arguments == std::vector<std::string>{"20.0", "12.0", "0.25"}, "A et B permutes : leurs valeurs aussi");
    HmiFunction same = *moy;
    check(!remapTestArguments(same, {"A", "B", "Poids_A"}, {"A", "B", "Poids_A"}), "rien de change : rien a remettre");
}

// D3b-4 : un mot seul egal au nom d'un objet (un bouton Arret) reste un texte dans l'attendu d'un STRING - le banc et
// Generer le lisent pareil (avant : le banc en faisait la formule =Arret, Erreur, et Generer ne disait rien).
void attenduNomDObjet() {
    std::printf("-- D3b-4 : Arret (un bouton du meme nom) attendu d'un STRING : un texte\n");
    Project p = bench();
    addVar(p, "Etat", "STRING", "''");
    addVar(p, "Seuil", "REAL", "4.0");
    View v;
    v.id = p.allocate();
    v.name = "Vue_Commandes";
    Object bouton;
    bouton.id = p.allocate();
    bouton.name = "Arret";
    v.objects.push_back(bouton);
    p.views.push_back(v);
    HmiFunction& regler = addFunction(p, "Regler_Etat", nullptr, {}, "Etat := 'Arret';");
    HmiFunction& mode = addFunction(p, "Mode_Texte", "STRING", {}, "Mode_Texte := 'Arret';");
    FunctionTest verif = caseOf(p, regler.id, "verif Arret");
    verif.checks = {{"Etat", "Arret", ""}};
    regler.tests = {verif};
    FunctionTest retour = caseOf(p, mode.id, "retour Arret");
    retour.expected = "Arret";
    mode.tests = {retour};
    const TestExpected s = testExpected(p, "STRING", "Arret");
    check(s.ok() && !s.formula && s.kept == "'Arret'", "testExpected(STRING, Arret) : le texte 'Arret' (" + s.kept + ")");
    const TestExpected var = testExpected(p, "REAL", "Seuil");
    check(var.ok() && var.formula && var.kept == "=Seuil", "testExpected(REAL, Seuil) : une variable reste un calcul (" + var.kept + ")");
    // Un chemin qui part d'une vue (une constante de vue, que le banc lit) reste un calcul.
    const TestExpected chemin = testExpected(p, "STRING", "Vue_Commandes.TITRE");
    check(chemin.ok() && chemin.formula && chemin.kept == "=Vue_Commandes.TITRE",
          "testExpected(STRING, Vue_Commandes.TITRE) : un chemin de vue reste un calcul (" + chemin.kept + ")");
    const TestExpected nombre = testExpected(p, "REAL", "Arret");
    check(!nombre.ok() && nombre.why.find("nombre") != std::string::npos, "testExpected(REAL, Arret) : illisible (" + nombre.why + ")");
    const auto r1 = runFunctionTests(p, regler);
    check(r1.results.size() == 1 && r1.results[0].verdict == Verdict::Passed,
          "verif Etat = Arret : reussi (" + (r1.results.empty() ? std::string{} : verdict(r1.results[0].verdict) + ", " + r1.results[0].detail) + ")");
    const auto r2 = runFunctionTests(p, mode);
    check(r2.results.size() == 1 && r2.results[0].verdict == Verdict::Passed,
          "retour Arret : reussi (" + (r2.results.empty() ? std::string{} : verdict(r2.results[0].verdict) + ", " + r2.results[0].detail) + ")");
    std::vector<Issue> issues;
    checkFunctionTests(p, issues);
    check(!issueWith(issues, "verif Arret", "attendu") && !issueWith(issues, "retour Arret", "attendu"), "Generer : rien a redire");
}

// D3b-5 : NAN, INF, -INF se tapent comme attendu d'un reel (avant : refuses a la saisie, " n'est pas un nombre (REAL) ").
void nanSaisi() {
    std::printf("-- D3b-5 : NAN, INF, -INF a la saisie d'un attendu REAL\n");
    Project p = bench();
    const TestExpected n = testExpected(p, "REAL", "nan"), i = testExpected(p, "LREAL", "+inf"), m = testExpected(p, "ANY_REAL", "-Inf");
    check(n.ok() && n.kept == "NAN" && i.ok() && i.kept == "+INF" && m.ok() && m.kept == "-INF",
          "nan, +inf, -Inf : NAN, +INF, -INF (" + n.kept + ", " + i.kept + ", " + m.kept + ")");
    const TestExpected entier = testExpected(p, "INT", "INF");
    check(!entier.ok(), "INF pour un INT : refuse (" + entier.why + ")");
    HmiFunction* div = const_cast<HmiFunction*>(p.functionByName("Diviser"));
    FunctionTest inf = caseOf(p, div->id, "inf");
    inf.arguments = {"1.0", "0.0"};
    inf.expected = i.kept;
    div->tests = {inf};
    const auto rep = runFunctionTests(p, *div);
    check(rep.results.size() == 1 && rep.results[0].verdict != Verdict::Pending, "1.0 / 0.0 attendu +INF : joue (" + (rep.results.empty() ? std::string{} : rep.results[0].detail) + ")");
}

} // namespace

int main() {
    std::printf("== tests unitaires des fonctions IHM (lot J4) ==\n");
    moteur();
    comptes();
    generer();
    disque();
    generiquesRelus();      // 1.13.0 (relecture) : les defauts du groupe C
    fonctionDeVue();
    codesErreur();
    textesExacts();
    enumerations();
    genererInconnues();
    attenduCalcule();       // 1.13.0 (seconde passe, lot D3)
    posesEtVerifs();
    nanEtInf();
    generiqueLu();
    argumentsSuivent();
    attenduNomDObjet();     // 1.13.0 (D3b)
    nanSaisi();
    std::printf("\n%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
