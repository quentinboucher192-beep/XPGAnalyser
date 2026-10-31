// =============================================================================
//  tests/ai_sandbox_test.cpp - 1.21.0 : le bac a sable d'execution de l'Assistant IA
// -----------------------------------------------------------------------------
//  - les etapes distinctes et leur etat REEL : analyse, compilation, execution des cas (un code juste, un resultat
//    faux, une faute de syntaxe, un nom inconnu, sans cas, une fonction du projet telle qu'elle est) ;
//  - le C et le C++ : relus seulement, jamais compiles ni executes (et c'est dit) ;
//  - le vrai processus enfant (ce programme relance en --bac-a-sable) : le meme resultat, le dossier de travail
//    efface, une boucle sans fin arretee (temps borne), un chemin complet refuse par FILE_WRITE (rien d'ecrit) ;
//  - l'entree de l'outil (JSON), sa definition, le compte rendu (jamais << reussi >> sans cas joue).
// =============================================================================
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiSandbox.hpp"
#include "../src/core/Process.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace sb = app::ai::sandbox;
namespace fs = std::filesystem;

namespace {

int g_ok = 0, g_fail = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}
bool has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

const char* kMoyenne = "VAR_INPUT\n    a : REAL;\n    b : REAL;\nEND_VAR\nMoyenne := (a + b) / 2.0;\n";

sb::Case kase(const char* name, std::vector<std::string> args, const char* expected) {
    sb::Case c;
    c.name = name;
    c.arguments = std::move(args);
    c.expected = expected;
    return c;
}

sb::Job moyenne(const char* code = kMoyenne) {
    sb::Job j;
    j.language = "ST";
    j.name = "Moyenne";
    j.returnType = "REAL";
    j.code = code;
    j.cases = {kase("deux positifs", {"2.0", "4.0"}, "3.0"), kase("avec zero", {"0.0", "5.0"}, "2.5")};
    return j;
}

hmi::Project projectWithFunction() {
    hmi::Project p;
    hmi::HmiFunction f;
    f.id = p.allocate();
    f.name = "Double";
    f.returnType = "INT";
    f.body = "VAR_INPUT\n    x : INT;\nEND_VAR\nDouble := x * 2;\n";
    hmi::FunctionTest t;
    t.id = p.allocate();
    t.name = "trois";
    t.arguments = {"3"};
    t.expected = "6";
    f.tests.push_back(t);
    p.programs.functions.push_back(f);
    return p;
}

const sb::Stage* st(const sb::Outcome& o, const char* key) { return o.stage(key); }
sb::State stateOf(const sb::Outcome& o, const char* key) {
    const sb::Stage* s = o.stage(key);
    return s ? s->state : sb::State::NotRun;
}

// ---- dans ce processus -----------------------------------------------------------------------------------------
void inProcess() {
    std::printf("Les etapes, dans ce processus\n");
    const hmi::Project empty;
    {
        const sb::Outcome o = sb::runHere(moyenne(), empty);
        check(stateOf(o, "analyse") == sb::State::Ok, "un code juste : analyse reussie");
        check(stateOf(o, "compilation") == sb::State::Ok, "un code juste : compilation reussie (" + (st(o, "compilation") ? st(o, "compilation")->detail : std::string("?")) + ")");
        check(stateOf(o, "execution") == sb::State::Ok, "un code juste : les cas executes");
        check(o.cases.size() == 2 && o.cases[0].passed && o.cases[1].passed, "un code juste : 2 cas reussis");
        check(o.testsPassed(), "un code juste : testsPassed");
        const std::string r = o.report();
        check(has(r, "Tests : 2/2") && has(r, "Moyenne(") && has(r, "Int\xC3\xA9gration au projet : non") && has(r, "automate r\xC3\xA9" "el : jamais"),
              "le compte rendu dit les tests, l'appel, pas d'integration, jamais d'automate :\n" + r);
    }
    {
        const sb::Outcome o = sb::runHere(moyenne("VAR_INPUT\n    a : REAL;\n    b : REAL;\nEND_VAR\nMoyenne := a + b;\n"), empty);
        check(stateOf(o, "compilation") == sb::State::Ok && stateOf(o, "execution") == sb::State::Ok, "un resultat faux : compile et execute");
        check(o.cases.size() == 2 && !o.cases[0].passed, "un resultat faux : le cas echoue (" + (o.cases.empty() ? std::string("aucun") : o.cases[0].detail) + ")");
        check(!o.testsPassed(), "un resultat faux : pas testsPassed (compiler ne suffit pas)");
        check(has(o.report(), "Tests : 0/2"), "un resultat faux : 0/2 dans le compte rendu");
    }
    {
        const sb::Outcome o = sb::runHere(moyenne("VAR_INPUT\n    a : REAL;\n    b : REAL;\nEND_VAR\nIF a > b THEN\n    Moyenne := a;\n"), empty);
        check(stateOf(o, "analyse") == sb::State::Failed, "une faute de syntaxe : analyse en echec");
        check(stateOf(o, "compilation") == sb::State::Skipped && stateOf(o, "execution") == sb::State::Skipped, "une faute de syntaxe : le reste non fait");
        check(o.cases.empty() && !o.testsPassed(), "une faute de syntaxe : aucun cas joue");
        check(has(o.report(), "non fait") && has(o.report(), "aucun cas ex\xC3\xA9" "cut\xC3\xA9"), "une faute de syntaxe : le compte rendu dit non fait");
    }
    {
        const sb::Outcome o = sb::runHere(moyenne("VAR_INPUT\n    a : REAL;\n    b : REAL;\nEND_VAR\nMoyenne := (a + b) / Diviseur_Inconnu;\n"), empty);
        check(stateOf(o, "analyse") == sb::State::Ok, "un nom inconnu : la syntaxe passe");
        check(stateOf(o, "compilation") == sb::State::Failed, "un nom inconnu : la compilation echoue (" + (st(o, "compilation") ? st(o, "compilation")->detail : std::string("?")) + ")");
        check(stateOf(o, "execution") == sb::State::Skipped && o.cases.empty(), "un nom inconnu : rien execute");
    }
    {
        sb::Job j = moyenne();
        j.cases.clear();
        const sb::Outcome o = sb::runHere(j, empty);
        check(stateOf(o, "compilation") == sb::State::Ok && stateOf(o, "execution") == sb::State::Skipped, "sans cas : compile, rien execute");
        check(!o.testsPassed() && has(o.report(), "rien n'a \xC3\xA9t\xC3\xA9 ex\xC3\xA9" "cut\xC3\xA9"), "sans cas : dit");
    }
    {
        const hmi::Project p = projectWithFunction();
        sb::Job j;
        j.name = "double";   // sans casse
        j.existing = true;
        const sb::Outcome o = sb::runHere(j, p);
        check(o.testsPassed() && o.cases.size() == 1, "la fonction du projet telle qu'elle est, avec ses cas");
        check(p.programs.functions.size() == 1 && p.programs.functions[0].body.find("x * 2") != std::string::npos, "le projet n'est pas touche");
        sb::Job fix;
        fix.name = "Double";
        fix.returnType = "INT";
        fix.code = "VAR_INPUT\n    x : INT;\nEND_VAR\nDouble := x + x + 1;\n";
        fix.cases = {kase("trois", {"3"}, "6")};
        const sb::Outcome o2 = sb::runHere(fix, p);
        check(!o2.testsPassed() && o2.cases.size() == 1 && !o2.cases[0].passed, "une correction essayee dans la copie (fausse ici)");
        check(p.programs.functions[0].body.find("x * 2") != std::string::npos, "toujours pas touche");
        sb::Job missing;
        missing.name = "Absente";
        missing.existing = true;
        check(stateOf(sb::runHere(missing, p), "analyse") == sb::State::Failed, "une fonction absente : dit, rien d'invente");
    }
    {
        sb::Job c;
        c.language = "C++";
        c.name = "moyenne";
        c.code = "float moyenne(float a, float b) {\n    return (a + b) / 2.0f;\n}\n";
        const sb::Outcome o = sb::runHere(c, empty);
        check(o.language == "C++" && stateOf(o, "analyse") == sb::State::Ok, "C++ : la relecture passe");
        check(stateOf(o, "compilation") == sb::State::Skipped && stateOf(o, "execution") == sb::State::Skipped, "C++ : ni compile ni execute");
        check(has(o.report(), "jamais") && has(o.report(), "statique"), "C++ : le compte rendu le dit");
        c.language = "C";
        c.code = "int f(int a) {\n    return a;\n";
        const sb::Outcome o2 = sb::runHere(c, empty);
        check(stateOf(o2, "analyse") == sb::State::Failed, "C : une accolade non fermee vue (" + (st(o2, "analyse") ? st(o2, "analyse")->detail : std::string("?")) + ")");
    }
}

// ---- l'entree et la definition de l'outil ------------------------------------------------------------------------
void toolInput() {
    std::printf("L'outil executer_code\n");
    app::ai::json::Value def;
    check(app::ai::json::parse(sb::toolJson(), def) && def.str("name") == "executer_code" && def.at("input_schema").isObject(), "la definition se lit");
    sb::Job j;
    std::string why;
    check(sb::jobFromJson(R"({"nom":"Moyenne","retour":"REAL","code":"VAR_INPUT a:REAL; END_VAR Moyenne := a;","cas":[{"arguments":[2.5, "3"],"attendu":2.5}]})", j, &why),
          "une entree valide (" + why + ")");
    check(j.language == "ST" && j.cases.size() == 1 && j.cases[0].arguments.size() == 2 && j.cases[0].arguments[0] == "2.5" && j.cases[0].expected == "2.5"
              && j.cases[0].name == "Cas 1",
          "les nombres deviennent du texte de case, un cas sans nom est nomme");
    check(!sb::jobFromJson(R"({"code":"x"})", j, &why) && has(why, "nom"), "sans nom : refuse");
    check(!sb::jobFromJson(R"({"nom":"F","langage":"Python","code":"x"})", j, &why) && has(why, "langage"), "un autre langage : refuse");
    check(!sb::jobFromJson(R"({"nom":"F"})", j, &why), "sans code ni fonction existante : refuse");
    check(sb::jobFromJson(R"({"nom":"F","fonction_existante":true})", j, &why) && j.existing, "une fonction existante sans code");
    check(sb::jobFromJson(R"({"nom":"F","langage":"cpp","code":"int f();"})", j, &why) && j.language == "C++", "cpp : le C++");
}

// ---- le vrai processus enfant -------------------------------------------------------------------------------------
void childProcess(const std::string& self) {
    std::printf("Le processus enfant (--bac-a-sable)\n");
    const hmi::Project p = projectWithFunction();
    {
        const sb::Outcome o = sb::run(moyenne(), p, nullptr, self);
        check(o.started && o.error.empty(), "l'enfant demarre et repond (" + o.error + " / " + o.process + ")");
        check(o.testsPassed() && o.cases.size() == 2, "l'enfant : 2 cas reussis");
        check(has(o.report(), "processus isol\xC3\xA9"), "le compte rendu dit le processus isole");
    }
    {
        // Une boucle sans fin : arretee (le moteur ou la limite de temps), jamais un succes, jamais un blocage.
        sb::Job j;
        j.name = "Boucle";
        j.returnType = "INT";
        j.code = "VAR_INPUT\n    x : INT;\nEND_VAR\nWHILE TRUE DO\n    x := x + 1;\nEND_WHILE\nBoucle := x;\n";
        j.cases = {kase("sans fin", {"1"}, "2")};
        j.seconds = 4;
        const auto t0 = std::chrono::steady_clock::now();
        const sb::Outcome o = sb::run(j, p, nullptr, self);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(s < 8.0, "une boucle sans fin : arretee en " + std::to_string(s) + " s");
        check(!o.testsPassed(), "une boucle sans fin : jamais un succes (" + o.process + ")");
    }
    {
        // Un chemin complet : refuse par le confinement (rien d'ecrit hors du dossier de travail).
        const fs::path target = fs::temp_directory_path() / "xpg-bac-a-sable-interdit.txt";
        std::error_code ec;
        fs::remove(target, ec);
        std::string path = target.generic_string();
        sb::Job j;
        j.name = "Ecrire";
        j.returnType = "BOOL";
        j.code = "Ecrire := FILE_WRITE('" + path + "', 'intrus');\n";
        j.cases = {kase("chemin complet", {}, "FALSE")};
        const sb::Outcome o = sb::run(j, p, nullptr, self);
        check(!fs::exists(target), "un chemin complet : rien d'ecrit (" + o.report() + ")");
        // Un chemin relatif : permis, dans le dossier de travail, efface apres.
        sb::Job rel = j;
        rel.code = "Ecrire := FILE_WRITE('essai.txt', 'ok');\n";
        rel.cases = {kase("relatif", {}, "TRUE")};
        const sb::Outcome o2 = sb::run(rel, p, nullptr, self);
        check(o2.testsPassed(), "un chemin relatif : ecrit dans le dossier de travail (" + o2.report() + ")");
    }
    {
        // Annuler avant : l'enfant est arrete, c'est dit.
        std::atomic<bool> cancel{true};
        const sb::Outcome o = sb::run(moyenne(), p, &cancel, self);
        check(!o.testsPassed() && !o.error.empty(), "annule : dit (" + o.error + ")");
    }
    {
        const sb::Outcome o = sb::run(moyenne(), p, nullptr, "/chemin/qui/n/existe/pas");
        check(!o.started && !o.error.empty() && has(o.report(), "NON EX\xC3\x89" "CUT\xC3\x89"), "un programme absent : non execute, dit");
    }
}

// ---- le confinement des fichiers, dans ce processus ---------------------------------------------------------------
void confinement() {
    std::printf("Le confinement des fichiers\n");
    const fs::path box = fs::temp_directory_path() / "xpg-bac-confine-essai";
    std::error_code ec;
    fs::remove_all(box, ec);
    fs::create_directories(box, ec);
    hmi::confineFiles(box.string());
    check(hmi::filesConfinedTo() == box.string(), "le dossier pose");
    sb::Job j;
    j.name = "Sortir";
    j.returnType = "BOOL";
    j.code = "Sortir := FILE_WRITE('../dehors.txt', 'x');\n";
    j.cases = {kase("remonter", {}, "FALSE")};
    const sb::Outcome o = sb::runHere(j, hmi::Project{});
    check(o.testsPassed() && !fs::exists(box.parent_path() / "dehors.txt"), "../ : refuse (" + o.report() + ")");
    hmi::confineFiles({});
    check(hmi::filesConfinedTo().empty(), "le confinement retire");
    fs::remove_all(box, ec);
}

} // namespace

int main(int argc, char** argv) {
    // Ce programme est aussi l'enfant du bac a sable (comme IHM-Studio --bac-a-sable).
    if (argc >= 2 && std::string(argv[1]) == sb::kChildFlag) return sb::childMain();
    const std::string self = core::process::selfPath();
    inProcess();
    toolInput();
    confinement();
    childProcess(self);
    std::printf("\nai_sandbox_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
