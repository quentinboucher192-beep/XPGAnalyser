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
//  vue).
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

} // namespace

int main() {
    std::printf("== tests unitaires des fonctions IHM (lot J4) ==\n");
    moteur();
    comptes();
    generer();
    disque();
    generiquesRelus();      // 1.13.0 (relecture) : les cas d'une fonction generique relus
    std::printf("\n%d verifications, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
