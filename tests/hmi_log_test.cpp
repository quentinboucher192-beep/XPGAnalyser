// =============================================================================
//  tests/hmi_log_test.cpp - 1.11.14 : IHM_LOG et la Console (le moteur)
// -----------------------------------------------------------------------------
//  Les niveaux (NIVEAU_LOG), la verification d'un appel (le niveau, le message,
//  le nombre d'arguments, une variable du meme nom qu'un niveau) et l'execution :
//  chaque ligne de la Console a son niveau, sa source (le script, la fonction,
//  la vue), la ligne de l'instruction, la session ; les trous du message sont
//  remplis ; une erreur d'execution y passe aussi, avec sa ligne.
// =============================================================================
#include "../src/app/hmi/HmiConsole.hpp"
#include "../src/hmi/HmiLog.hpp"
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiScriptCheck.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

class FakePlc final : public sim::Environment {
public:
    std::map<std::string, sim::Value> values;
    bool read(std::string_view n, sim::Value& out) override {
        const auto it = values.find(std::string(n));
        if (it == values.end()) return false;
        out = it->second;
        return true;
    }
    bool write(std::string_view n, const sim::Value& v) override {
        const auto it = values.find(std::string(n));
        if (it == values.end()) return false;
        it->second = v;
        return true;
    }
    bool exists(std::string_view n) override { return values.count(std::string(n)) != 0; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override {
        return false;
    }
    void report(sim::Diagnostic) override {}
};

Variable variable(Project& p, std::string name, std::string type, std::string initial = {}) {
    Variable v;
    v.id = p.allocate();
    v.name = std::move(name);
    v.type = std::move(type);
    v.initial = std::move(initial);
    return v;
}

Script script(Project& p, std::string name, std::string event, std::string body) {
    Script sc;
    sc.id = p.allocate();
    sc.name = std::move(name);
    sc.event = std::move(event);
    sc.body = std::move(body);
    sc.lang = ScriptLang::ST;
    return sc;
}

std::vector<JournalEntry> entries(const Runtime& rt, std::string_view kind) {
    std::vector<JournalEntry> out;
    for (const auto& e : rt.journal())
        if (e.kind == kind) out.push_back(e);
    return out;
}

void niveaux() {
    std::printf("-- les niveaux (NIVEAU_LOG)\n");
    check(logLevelName(LogLevel::Trace) == "TRACE" && logLevelName(LogLevel::Critical) == "CRITICAL", "les noms ST : TRACE ... CRITICAL");
    check(logLevelLabel(LogLevel::Warning) == "Avertissement" && logLevelLabel(LogLevel::Debug) == "D\xC3\xA9" "bogage", "les libelles en francais");
    check(logLevelByName("info") == LogLevel::Info && logLevelByName(" ERROR ") == LogLevel::Error, "par le nom, sans casse, espaces admis");
    check(logLevelByName("NIVEAU_LOG#Success") == LogLevel::Success && logLevelByName("niveau_log # critical") == LogLevel::Critical,
          "le litteral NIVEAU_LOG#X");
    check(!logLevelByName("INFOS") && !logLevelByName("T_MODE#INFO") && !logLevelByName("") && !logLevelByName("NIVEAU_LOG#"),
          "ni un autre nom, ni une autre enumeration");
    check(logLevelOf(0) == LogLevel::Trace && logLevelOf(6) == LogLevel::Critical && !logLevelOf(7) && !logLevelOf(-1), "par le nombre : 0 a 6");
    check(logLevelOfKind("Erreur") == LogLevel::Error && logLevelOfKind("Journal") == LogLevel::Info && logLevelOfKind("Action") == LogLevel::Info,
          "le niveau d'une ligne du moteur : Erreur -> ERROR, le reste -> INFO");
}

void verification() {
    std::printf("-- la verification d'un appel (Compiler)\n");
    namespace sc = scriptcheck;
    Project p;
    p.programs.variables.push_back(variable(p, "Temperature", "REAL", "0"));
    p.programs.variables.push_back(variable(p, "Message", "STRING"));
    p.programs.variables.push_back(variable(p, "Debug", "INT", "0"));
    sc::Scope scope;
    scope.project = &p;
    scope.plcKnown = [](std::string_view) { return false; };      // l'automate est connu : un nom inconnu est dit
    const auto errors = [](const std::vector<sc::Finding>& f) {
        int n = 0;
        for (const auto& x : f) n += x.severity == sc::Finding::Severity::Error ? 1 : 0;
        return n;
    };
    const auto says = [](const std::vector<sc::Finding>& f, std::string_view text) {
        for (const auto& x : f)
            if (x.message.find(text) != std::string::npos) return true;
        return false;
    };
    const auto dump = [](const std::vector<sc::Finding>& f) {
        std::string out;
        for (const auto& x : f) out += " [" + std::to_string(x.line) + "] " + x.message;
        return out;
    };
    {
        const auto f = sc::check(scope, "IHM_LOG(INFO, 'Ouverture de la vue principale');\n"
                                        "IHM_LOG(NIVEAU_LOG#WARNING, 'Temp\xC3\xA9rature actuelle : {Temperature:0.0} \xC2\xB0" "C');\n"
                                        "IHM_LOG(critical, Message);\n"
                                        "IHM_LOG(SUCCESS, CONCAT('Recette ', Message));\n");
        check(f.empty(), "des appels justes : rien a dire" + dump(f));
    }
    {
        const auto f = sc::check(scope, "IHM_LOG(BLABLA, 'x');");
        check(errors(f) == 1 && says(f, "premier argument est le niveau") && says(f, "BLABLA") && f[0].line == 1,
              "un niveau inconnu : une erreur, qui dit les niveaux permis" + dump(f));
    }
    {
        const auto f = sc::check(scope, "IHM_LOG('INFO', 'x');");
        check(errors(f) == 1 && says(f, "premier argument est le niveau"), "un texte n'est pas un niveau" + dump(f));
    }
    {
        const auto f = sc::check(scope, "x := 1;\nIHM_LOG(INFO, 3);");
        check(errors(f) >= 1 && says(f, "le message est un texte") && f.back().line == 2, "un nombre n'est pas un message (ligne 2)" + dump(f));
    }
    {
        const auto f = sc::check(scope, "IHM_LOG(INFO);");
        check(errors(f) == 1 && says(f, "IHM_LOG prend 2 arguments, pas 1"), "un argument de moins" + dump(f));
    }
    {
        const auto f = sc::check(scope, "IHM_LOG(INFO, 'Niveau {Temperature');");
        check(errors(f) == 0 && says(f, "sans") && f.size() == 1 && f[0].severity == sc::Finding::Severity::Warning,
              "un trou mal ferme : un avertissement (il s'ecrira tel quel)" + dump(f));
    }
    {
        const auto f = sc::check(scope, "IHM_LOG(ERROR, CONCAT('x', Inconnue));");
        check(errors(f) == 1 && says(f, "Inconnue"), "un nom inconnu dans le message" + dump(f));
    }
    {
        // La validation du projet (Generer, Compiler) lit les noms des scripts : le niveau n'en est pas un.
        const auto names = scriptNames("IHM_LOG(INFO, 'x');\nIHM_LOG( debug , Message);\nIHM_LOG(NIVEAU_LOG#ERROR, Message);\ny := Inconnue;");
        bool level = false, message = false, unknown = false;
        for (const auto& n : names) {
            level = level || n.name == "INFO" || n.name == "debug" || n.name == "NIVEAU_LOG" || n.name == "ERROR";
            message = message || n.name == "Message";
            unknown = unknown || n.name == "Inconnue";
        }
        check(!level && message && unknown, "les noms d'un script : ni INFO ni debug (des niveaux), mais Message et Inconnue");
        Project q = p;
        Script s;
        s.id = q.allocate();
        s.name = "Init";
        s.event = "Demarrage";
        s.body = "IHM_LOG(TRACE, 'a');\nIHM_LOG(INFO, 'b {Temperature:0.0}');\nIHM_LOG(SUCCESS, Message);\nIHM_LOG(WARNING, 'c');\n"
                 "IHM_LOG(ERROR, 'd');\nIHM_LOG(NIVEAU_LOG#CRITICAL, 'e');\n";
        s.lang = ScriptLang::ST;
        q.programs.scripts.push_back(s);
        const auto plcHas = [](std::string_view) { return false; };
        bool flagged = false;
        std::string said;
        for (const auto& i : generate(q, plcHas))
            if (i.severity == Issue::Severity::Error && i.script == s.id) { flagged = true; said += " [G] " + i.message; }
        for (const auto& i : compileWith(q, plcHas))
            if (i.severity == Issue::Severity::Error && i.script == s.id) { flagged = true; said += " [C] " + i.message; }
        check(!flagged, "Generer et Compiler : les sept niveaux ne sont pas des variables inexistantes" + said);
    }
    {
        const auto f = sc::check(scope, "IHM_LOG(Debug, 'x');");
        check(errors(f) == 0 && says(f, "est aussi une variable") && says(f, "NIVEAU_LOG#DEBUG"),
              "une variable du meme nom qu'un niveau : un avertissement, et la forme sure" + dump(f));
    }
}

void execution() {
    std::printf("-- l'execution : la Console (niveau, source, ligne, session)\n");
    Project p;
    View a = makeView(p, "Vue_A");
    a.scripts.push_back(script(p, "A_open", "OnOpen", "IHM_LOG(TRACE, 'vue ouverte');"));
    p.programs.variables.push_back(variable(p, "Temperature", "REAL", "0"));
    p.programs.variables.push_back(variable(p, "x", "REAL", "0"));
    HmiFunction f;
    f.id = p.allocate();
    f.name = "Moyenne";
    f.returnType = "REAL";
    f.body = "VAR_INPUT\n    a : REAL;\n    b : REAL;\nEND_VAR\nIHM_LOG(DEBUG, 'moyenne de {a} et {b}');\nMoyenne := (a + b) / 2.0;";
    p.programs.functions.push_back(f);
    const Script horloge = script(p, "Horloge", "Appel",
                                  "IHM_LOG(INFO, 'Ouverture de la vue principale');\n"
                                  "Temperature := 21.54;\n"
                                  "IHM_LOG(WARNING, 'Temp\xC3\xA9rature actuelle : {Temperature:0.0} \xC2\xB0" "C');\n"
                                  "IHM_LOG(NIVEAU_LOG#CRITICAL, 'Perte de communication avec l$'API');\n"
                                  "x := Moyenne(1.0, 3.0);\n"
                                  "IHM_LOG(SUCCESS, 'fin : {x}');");
    p.programs.scripts.push_back(horloge);
    const Script faux = script(p, "Faux", "Appel", "x := 1.0;\nInconnue := 2;");
    p.programs.scripts.push_back(faux);
    p.views = {a};
    p.config.startView = a.id;

    FakePlc plc;
    Runtime rt;
    std::vector<JournalEntry> hooked;
    Runtime::Hooks hooks;
    hooks.journaled = [&hooked](const JournalEntry& e) { hooked.push_back(e); };
    rt.setHooks(hooks);
    rt.bind(&p, &plc);
    rt.start(0.0);
    check(rt.session() == 1, "le premier demarrage : la session 1");
    {
        const auto open = entries(rt, "IHM_LOG");
        check(open.size() == 1 && open[0].level == LogLevel::Trace && open[0].view == a.id && open[0].line == 1
                  && open[0].code == "Vue_A.OnOpen" && open[0].session == 1,
              "le script d'ouverture de la vue : TRACE, sa vue, ligne 1, la session 1");
    }
    std::string why;
    check(rt.callScript("Horloge", 1.0, &why), "le script tourne (" + why + ")");
    const auto logs = entries(rt, "IHM_LOG");
    check(logs.size() == 6, "cinq lignes de plus (dont celle de la fonction) : " + std::to_string(logs.size()));
    if (logs.size() == 6) {
        const auto& e1 = logs[1];
        check(e1.level == LogLevel::Info && e1.message == "Ouverture de la vue principale" && e1.line == 1 && e1.code == "Horloge"
                  && e1.script == horloge.id && e1.view == kNoId && e1.function == kNoId && e1.session == 1 && !e1.stamp.empty(),
              "INFO : le message, l'heure, le script et sa ligne (1)");
        check(logs[2].level == LogLevel::Warning && logs[2].message == "Temp\xC3\xA9rature actuelle : 21.5 \xC2\xB0" "C" && logs[2].line == 3,
              "WARNING : le trou {Temperature:0.0} rempli, ligne 3 (" + logs[2].message + ")");
        check(logs[3].level == LogLevel::Critical && logs[3].message == "Perte de communication avec l'API" && logs[3].line == 4,
              "NIVEAU_LOG#CRITICAL, ligne 4");
        check(logs[4].level == LogLevel::Debug && logs[4].function == f.id && logs[4].code == "fonction Moyenne" && logs[4].line == 5
                  && logs[4].message == "moyenne de 1 et 3",
              "dans la fonction : sa source, SA ligne (5) (" + logs[4].code + ", " + std::to_string(logs[4].line) + ", " + logs[4].message + ")");
        check(logs[5].level == LogLevel::Success && logs[5].script == horloge.id && logs[5].function == kNoId && logs[5].line == 6
                  && logs[5].message == "fin : 2",
              "au retour de la fonction : le script redevient la source, ligne 6 (" + std::to_string(logs[5].line) + ")");
    }
    check(!rt.callScript("Faux", 2.0, &why), "une erreur d'execution (" + why + ")");
    {
        const auto errs = entries(rt, "Erreur");
        check(!errs.empty() && errs.back().level == LogLevel::Error && errs.back().script == faux.id && errs.back().line == 2,
              "l'erreur d'execution : ERROR, le script, la ligne 2 (" + (errs.empty() ? std::string("rien") : std::to_string(errs.back().line)) + ")");
    }
    check(hooked.size() == rt.journal().size() && !hooked.empty() && hooked.back().kind == "Erreur",
          "chaque ligne passe par le crochet journaled (la Console la recoit)");
    rt.stop(3.0);
    rt.start(4.0);
    check(rt.session() == 2 && !entries(rt, "IHM_LOG").empty() && entries(rt, "IHM_LOG").back().session == 2,
          "un nouveau demarrage : la session 2");
}

void console() {
    std::printf("-- la Console : conservation, filtres, compteurs, export\n");
    app::HmiConsole c;
    c.setRetention(10);
    check(c.retention() == app::HmiConsole::kMinRetention, "la conservation a un minimum (500 lignes)");
    for (int i = 0; i < 600; ++i) {
        app::ConsoleEntry e;
        e.level = static_cast<LogLevel>(i % kLogLevelCount);
        e.category = "IHM_LOG";
        e.source = "script Horloge";
        e.message = i == 599 ? "Pompe P1 d\xC3\xA9marr\xC3\xA9" "e ; d\xC3\xA9" "bit \"5\"" : "ligne " + std::to_string(i);
        e.line = 3;
        e.session = i < 300 ? 1 : 2;
        c.add(std::move(e));
    }
    int sum = 0;
    for (int k = 0; k < kLogLevelCount; ++k) sum += c.count(static_cast<LogLevel>(k));
    check(c.entries().size() == 500 && c.dropped() == 100 && sum == 500 && c.entries().front().seq == 101,
          "au-dela de la conservation, les plus anciennes tombent (et les compteurs suivent)");
    check(c.errors() == c.count(LogLevel::Error) + c.count(LogLevel::Critical) && c.session() == 2, "les erreurs, la derniere session");
    app::HmiConsole::Filter f;
    f.levels.fill(false);
    f.levels[static_cast<std::size_t>(LogLevel::Error)] = true;
    int shown = 0;
    for (const auto& e : c.entries()) shown += app::HmiConsole::matches(e, f) ? 1 : 0;
    check(shown == c.count(LogLevel::Error), "le filtre par niveau");
    f.levels.fill(true);
    f.search = "POMPE";
    shown = 0;
    for (const auto& e : c.entries()) shown += app::HmiConsole::matches(e, f) ? 1 : 0;
    check(shown == 1, "la recherche, sans casse");
    const std::string csv = c.exportCsv(f);
    check(csv.rfind("Date;Heure;Niveau;Cat\xC3\xA9gorie;Source;Ligne;Session;Cycle;Message\n", 0) == 0
              && csv.find("\"Pompe P1 d\xC3\xA9marr\xC3\xA9" "e ; d\xC3\xA9" "bit \"\"5\"\"\"") != std::string::npos,
          "l'export CSV : l'en-tete, un champ avec ; et \" entre guillemets");
    f.search.clear();
    const std::string txt = c.exportText(f);
    check(txt.find("# --- session 1 ---") != std::string::npos && txt.find("# --- session 2 ---") != std::string::npos
              && txt.find("100 plus ancienne(s) tomb\xC3\xA9" "e(s)") != std::string::npos && txt.find("script Horloge:3") != std::string::npos,
          "l'export texte : les sessions, ce qui est tombe, la source et sa ligne");
    check(app::HmiConsole::nextRetention(5000) == 20000 && app::HmiConsole::nextRetention(100000) == 500, "Garder : 500, 1 000, 5 000, 20 000, 100 000");
    const auto rev = c.revision();
    c.clear();
    check(c.entries().empty() && c.errors() == 0 && c.revision() > rev, "Effacer");
    JournalEntry j;
    j.stamp = "18:42:10.350";
    j.kind = "IHM_LOG";
    j.source = "script Horloge";
    j.message = "x";
    j.level = LogLevel::Warning;
    j.code = "Horloge";
    j.script = 69;
    j.line = 4;
    j.session = 3;
    j.cycle = 12;
    c.addRuntime(j);
    const auto& e = c.entries().back();
    check(e.time.size() == 12 && e.time[2] == ':' && e.time[8] == '.' && e.level == LogLevel::Warning && e.script == 69 && e.line == 4 && e.session == 3 && e.cycle == 12
              && e.hasSource() && e.where() == "script Horloge, ligne 4" && e.date.size() == 10,
          "une ligne du moteur : tout ce qu'il faut pour la retrouver");
}

} // namespace

int main() {
    niveaux();
    verification();
    execution();
    console();
    std::printf("%d verification(s), %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
