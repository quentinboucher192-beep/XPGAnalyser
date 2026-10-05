// =============================================================================
//  tests/simdebug_test.cpp - lot API 8 : le moteur de simulation
// -----------------------------------------------------------------------------
//  LA MODIFICATION EN LIGNE : les valeurs gardees, le cycle qui continue, une
//  variable ajoutee a sa valeur initiale, une retiree, une temporisation en
//  cours, un forcage ; un echec de preparation garde l'ancien programme ; un
//  changement qui n'est pas du programme ne rend rien perime.
//  LES POINTS D'ARRET : les valeurs de la ligne, la pile, la pause en fin de
//  cycle, une condition qui DEVIENT vraie (une fois), une ligne vide decalee,
//  une condition invalide qui ne s'arrete jamais.
//  LE PAS A PAS par section, QUI A ECRIT, OU PASSE LE TEMPS.
//  LOT API 8, 2e PARTIE : en pause, une section et une variable ajoutees, puis
//  Ctrl+Z - a chaque fois le cycle est garde (6) ; LE PROGRAMME DIRAIT - la
//  case sous le forcage, et relachee, elle la reprend (7).
//  Sur un petit programme fait ici, puis sur tests/fixtures/MAST.XPG (argv[1]).
//  Les verifications restent actives en Release (CHECK, pas assert).
// =============================================================================
#include "../src/app/SimulationHost.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/EditCommands.hpp"
#include "../src/project/ProjectStore.hpp"
#include "../src/project/SharedLibrary.hpp"
#include "../src/sim/Runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace sim;
using namespace project;
using namespace domain;

namespace {

int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("ECHEC (ligne %d) : %s\n", __LINE__, #c);             \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

using clk = std::chrono::steady_clock;
double msSince(clk::time_point t0) { return std::chrono::duration<double, std::milli>(clk::now() - t0).count(); }

// Un projet d'une ou plusieurs sections de MAST, et ses globales.
struct Fixture {
    std::shared_ptr<Project> project;
    core::CommandStack       stack;

    Fixture() : project(ProjectStore::createEmpty("SimDebug", "BMXP342020")) {}

    void global(const std::string& name, const std::string& type, const std::string& init = {}) {
        AddVariableCommand::Spec s;
        s.name = name;
        s.type = type;
        s.initValue = init;
        s.scope = VariableScope::Global;
        const bool ok = stack.push(std::make_unique<AddVariableCommand>(project, s)).has_value();
        CHECK(ok);
    }
    void section(const std::string& name, const std::string& code) {
        bool ok = stack.push(std::make_unique<AddSectionCommand>(project, name, "MAST", PouLanguage::ST)).has_value();
        CHECK(ok);
        ok = stack.push(std::make_unique<SetSectionBodyCommand>(project, static_cast<Index>(project->sections.size() - 1), code)).has_value();
        CHECK(ok);
    }
    void body(const std::string& name, const std::string& code) {
        for (Index i = 0; i < project->sections.size(); ++i)
            if (project->strings.text(project->sections[i].name) == name) {
                const bool ok = stack.push(std::make_unique<SetSectionBodyCommand>(project, i, code)).has_value();
                CHECK(ok);
                return;
            }
        std::printf("ECHEC : section introuvable %s\n", name.c_str());
        ++failures;
    }
};

std::int64_t intOf(const Runtime& rt, const char* name) {
    Value v;
    const bool ok = rt.get(name, v);
    CHECK(ok);
    return v.asInteger();
}
std::string strOf(const Runtime& rt, const char* name) {
    Value v;
    const bool ok = rt.get(name, v);
    CHECK(ok);
    return v.asString();
}
bool boolOf(const Runtime& rt, const char* name) {
    Value v;
    const bool ok = rt.get(name, v);
    CHECK(ok);
    return v.isTruthy();
}
std::string valueIn(const std::vector<std::pair<std::string, std::string>>& values, const std::string& name) {
    for (const auto& [n, v] : values)
        if (n == name) return v;
    return "(absente)";
}
std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? std::string{} : std::string(" > ")) + p;
    return out;
}

// Le programme des essais. Ligne 2 : vide (un point d'arret s'y decale).
const char* kCycle =
    "counter := counter + 1;\n"          // 1
    "\n"                                 // 2
    "IF counter > 2 THEN\n"              // 3
    "  level := counter * 10;\n"         // 4
    "END_IF;\n"                          // 5
    "gone := gone + 2;\n"                // 6
    "Timer(IN := run, PT := T#1s);\n"    // 7
    "done := Timer.Q;\n";                // 8

void declareCycle(Fixture& f, bool withGone) {
    f.global("counter", "INT");
    f.global("level", "INT");
    if (withGone) f.global("gone", "INT");
    f.global("run", "BOOL");
    f.global("Timer", "TON");
    f.global("done", "BOOL");
}

// --- 1. la modification en ligne, dans le runtime ------------------------------
void onlineChangeRuntime() {
    Fixture before;
    declareCycle(before, true);
    before.section("Cycle", kCycle);
    Runtime rt(before.project);
    CHECK(rt.prepare().has_value());
    CHECK(rt.set("run", Value::boolean(true)));
    for (int i = 0; i < 30; ++i) (void)rt.step(20);          // la temporisation a 600 ms sur 1 s
    CHECK(rt.force("level", Value::integer(Type::Int, 777)));
    CHECK(rt.scanCount() == 30 && intOf(rt, "counter") == 30 && !boolOf(rt, "done"));

    // Le nouveau programme : "gone" retiree, "fresh" ajoutee (valeur initiale 42).
    Fixture after;
    declareCycle(after, false);
    after.global("fresh", "INT", "42");
    std::string code = kCycle;
    code.replace(code.find("gone := gone + 2;"), 17, "fresh := fresh + 1;");
    after.section("Cycle", code);
    Runtime next(after.project);
    CHECK(next.prepare().has_value());
    const auto a = next.adoptStateFrom(rt);
    std::printf("1. en ligne (runtime) : %zu gard\xC3\xA9" "es, %zu nouvelles, %zu disparues\n", a.kept, a.added, a.dropped);
    CHECK(a.added >= 1 && a.dropped >= 1 && a.kept >= 5);
    CHECK(next.scanCount() == 30 && next.clockMs() == rt.clockMs());
    CHECK(intOf(next, "counter") == 30);
    CHECK(intOf(next, "fresh") == 42);                       // nouvelle : sa valeur initiale
    CHECK(!next.known("gone"));                              // disparue : oubliee
    CHECK(next.isForced("level") && intOf(next, "level") == 777);   // le forcage, par son chemin
    (void)next.step(20);
    CHECK(next.scanCount() == 31 && intOf(next, "counter") == 31 && intOf(next, "fresh") == 43);
    // La temporisation continue : 1 s atteinte vers le cycle 51 (repartie de
    // zero, il en faudrait 50 de plus).
    for (int i = 0; i < 24; ++i) (void)next.step(20);
    CHECK(boolOf(next, "done"));

    // Une section qui ne se lit plus : la modification est refusee.
    Fixture broken;
    declareCycle(broken, true);
    broken.section("Cycle", "counter := counter + ;\n");
    Runtime bad(broken.project);
    const auto ready = bad.prepare();
    const auto errors = ready.has_value() ? bad.newPreparationErrors(rt) : std::vector<Diagnostic>{};
    CHECK(!ready.has_value() || !errors.empty());
}

// --- 2. la modification en ligne, dans l'hote (le meme projet, modifie en place) ---
void onlineChangeHost() {
    Fixture f;
    f.global("counter", "INT");
    f.section("Cycle", "counter := counter + 1;\n");
    app::SimulationHost host;
    host.setOnlineChangeDelay(0.0);
    std::uint64_t revision = 1;
    host.setProgramRevision(revision);
    CHECK(host.attach(f.project).has_value());
    for (int i = 0; i < 5; ++i) host.step();
    CHECK(host.state() == app::SimulationHost::State::Paused && host.scanCount() == 5);

    // Pas le programme (l'IHM, une version...) : rien de perime.
    host.setProgramRevision(++revision);
    CHECK(!host.stale());

    // Un point d'arret sur la ligne 2, qui n'existe pas encore : il attend.
    const auto bpId = host.addBreakpoint("Cycle", 2);
    CHECK(host.breakpoints().size() == 1 && !host.breakpoints().front().note.empty());

    // En pause : une variable ajoutee, la section changee.
    const auto gen = host.generation();
    f.global("extra", "INT", "40");
    f.body("Cycle", "counter := counter + 1;\nextra := extra + 5;\n");
    host.setProgramRevision(++revision);
    CHECK(host.stale());
    host.tick(0.05);
    CHECK(!host.stale() && host.generation() != gen);
    CHECK(host.lastOnlineChange().has_value() && !host.lastOnlineChange()->failed);
    if (host.lastOnlineChange()) std::printf("2. %s\n", host.lastOnlineChange()->summary.c_str());
    CHECK(host.state() == app::SimulationHost::State::Paused && host.scanCount() == 5);   // pas de retour au cycle 0
    CHECK(host.lastOnlineChange() && host.lastOnlineChange()->atScan == 5 && host.lastOnlineChange()->added == 1);
    host.step();
    CHECK(host.scanCount() == 6 && intOf(*host.runtime(), "counter") == 6 && intOf(*host.runtime(), "extra") == 45);
    // Le point d'arret a survecu au nouveau programme, retrouve par section et ligne.
    CHECK(host.breakpoints().size() == 1 && host.breakpoints().front().id == bpId && host.breakpoints().front().line == 2
          && host.breakpoints().front().note.empty());
    CHECK(host.lastBreakHit() && host.lastBreakHit()->line == 2 && host.lastBreakHit()->scan == 6);

    // Une erreur dans le nouveau code, en marche : l'ancien reste, en pause, et le dit.
    host.setState(app::SimulationHost::State::Running);
    f.body("Cycle", "counter := counter + ;\n");
    host.setProgramRevision(++revision);
    host.tick(0.0);
    CHECK(host.lastOnlineChange() && host.lastOnlineChange()->failed);
    if (host.lastOnlineChange()) std::printf("2. %s\n", host.lastOnlineChange()->summary.c_str());
    CHECK(host.state() == app::SimulationHost::State::Paused);
    host.step();                                             // l'ancien code tourne
    CHECK(host.scanCount() == 7 && intOf(*host.runtime(), "counter") == 7 && intOf(*host.runtime(), "extra") == 50);

    // Corrige : le nouveau code est pris, le cycle continue.
    f.body("Cycle", "counter := counter + 2;\nextra := extra + 5;\n");
    host.setProgramRevision(++revision);
    host.step();                                             // un geste n'attend pas
    CHECK(host.lastOnlineChange() && !host.lastOnlineChange()->failed);
    CHECK(host.scanCount() == 8 && intOf(*host.runtime(), "counter") == 9);

    // Arretee : le lancement suivant prepare le nouveau code, valeurs initiales.
    host.setState(app::SimulationHost::State::Stopped);
    f.body("Cycle", "counter := counter + 3;\n");
    host.setProgramRevision(++revision);
    CHECK(host.attach(f.project).has_value());
    host.step();
    CHECK(host.scanCount() == 1 && intOf(*host.runtime(), "counter") == 3);
}

// --- 3. les points d'arret ---------------------------------------------------------
void breakpoints() {
    Fixture f;
    declareCycle(f, true);
    f.section("Cycle", kCycle);

    // Dans l'hote : la condition, les valeurs de la ligne, la pile, la pause en fin de cycle.
    app::SimulationHost host;
    CHECK(host.attach(f.project).has_value());
    const auto id = host.addBreakpoint("Cycle", 4, "counter = 3");
    host.setState(app::SimulationHost::State::Running);
    host.tick(1.0);                                          // jusqu'a 20 cycles
    CHECK(host.state() == app::SimulationHost::State::Paused);
    CHECK(host.lastBreakHit().has_value());
    if (const auto& hit = host.lastBreakHit()) {
        std::printf("3. arr\xC3\xAAt : %s ligne %d, cycle %llu, pile %s ;", hit->section.c_str(), hit->line,
                    static_cast<unsigned long long>(hit->scan), joined(hit->stack).c_str());
        for (const auto& [n, v] : hit->values) std::printf(" %s = %s", n.c_str(), v.c_str());
        std::printf("\n");
        CHECK(hit->id == id && hit->line == 4 && hit->scan == 3);
        CHECK(host.scanCount() == 3);                        // le cycle est alle au bout
        CHECK(!hit->stack.empty() && hit->stack.front() == "MAST" && hit->stack.back() == "Cycle");
        CHECK(valueIn(hit->values, "counter") == "3");
        CHECK(valueIn(hit->values, "level") == "0");                 // avant la ligne
    }
    CHECK(intOf(*host.runtime(), "level") == 30);
    // Continuer : la condition ne redevient pas vraie.
    host.setState(app::SimulationHost::State::Running);
    host.tick(0.4);
    CHECK(host.state() == app::SimulationHost::State::Running && host.scanCount() == 23);

    // Dans le runtime : une condition qui DEVIENT vraie s'arrete une fois.
    Runtime rt(f.project);
    CHECK(rt.prepare().has_value());
    Breakpoint b;
    b.id = 1;
    b.section = "Cycle";
    b.line = 4;
    b.condition = "counter > 5";
    Breakpoint every;                                        // sans condition : a chaque passage
    every.id = 2;
    every.section = "cycle";
    every.line = 2;                                          // vide : decale sur la 3
    Breakpoint wrong;
    wrong.id = 3;
    wrong.section = "Cycle";
    wrong.line = 6;
    wrong.condition = "counter = = 3";
    Breakpoint nowhere;
    nowhere.id = 4;
    nowhere.section = "Nulle_part";
    nowhere.line = 1;
    rt.setBreakpoints({b, every, wrong, nowhere});
    const auto& list = rt.breakpoints();
    CHECK(list.size() == 4);
    if (list.size() == 4) {
        std::printf("3. ligne vide : %s ; condition fausse : %s ; section : %s\n", list[1].note.c_str(), list[2].note.c_str(),
                    list[3].note.c_str());
        CHECK(list[0].effectiveLine == 4 && list[0].note.empty());
        CHECK(list[1].effectiveLine == 3 && !list[1].note.empty());
        CHECK(list[2].note.find("condition invalide") != std::string::npos);
        CHECK(list[3].effectiveLine == 0 && !list[3].note.empty());
    }
    int stopsOnCondition = 0, stopsEvery = 0, stopsWrong = 0;
    std::uint64_t firstScan = 0;
    for (int i = 0; i < 12; ++i) {
        (void)rt.step(20);
        const auto hit = rt.takeBreakHit();
        if (!hit) continue;
        CHECK(hit->scan == rt.scanCount());
        if (hit->id == 2) ++stopsEvery;
        if (hit->id == 3) ++stopsWrong;
    }
    // Le premier du cycle seulement est rendu : la ligne 3 (id 2) passe avant la 4.
    CHECK(stopsEvery == 12 && stopsWrong == 0);
    for (const auto& x : rt.breakpoints()) {
        if (x.id == 1) {
            stopsOnCondition = static_cast<int>(x.hits);
            firstScan = x.hits;
        }
        if (x.id == 2) CHECK(x.hits == 12);
    }
    std::printf("3. condition \"counter > 5\" : %d passage(s) compte(s) sur 12 cycles (vraie depuis le cycle 6)\n", stopsOnCondition);
    CHECK(stopsOnCondition == 1 && firstScan == 1);
    // Seule, elle s'arrete au cycle 6 - une fois.
    Runtime rt2(f.project);
    CHECK(rt2.prepare().has_value());
    rt2.setBreakpoints({b});
    std::vector<std::uint64_t> at;
    for (int i = 0; i < 12; ++i) {
        (void)rt2.step(20);
        if (const auto hit = rt2.takeBreakHit()) at.push_back(hit->scan);
    }
    CHECK(at.size() == 1 && at.front() == 6);
    // Poser un autre point d'arret ne la fait pas "redevenir" vraie.
    rt2.setBreakpoints({b, nowhere});
    (void)rt2.step(20);
    CHECK(!rt2.takeBreakHit().has_value());
    // Cout nul sans point d'arret : aucun passage, rien a rendre.
    rt2.setBreakpoints({});
    (void)rt2.step(20);
    CHECK(!rt2.takeBreakHit().has_value());
}

// --- 4. pas a pas par section, qui a ecrit, ou passe le temps -----------------------
void stepping() {
    Fixture f;
    f.global("x", "INT");
    f.global("y", "INT");
    f.section("A", "x := x + 1;\n");
    f.section("B", "y := x * 2;\n\ny := y + 0;\n");
    Runtime rt(f.project);
    CHECK(rt.prepare().has_value());
    CHECK(rt.entries().size() == 2);
    CHECK(rt.nextEntry().empty() && !rt.scanInProgress());
    auto r = rt.stepEntry(20);
    CHECK(!r.completed && rt.scanInProgress() && rt.nextEntry() == "B" && rt.nextEntryIndex() == 1);
    CHECK(intOf(rt, "x") == 1 && intOf(rt, "y") == 0 && rt.scanCount() == 0);
    r = rt.stepEntry(20);
    CHECK(r.completed && !rt.scanInProgress() && rt.scanCount() == 1 && intOf(rt, "y") == 2 && rt.nextEntry().empty());
    (void)rt.stepEntry(20);                                  // un cycle commence...
    (void)rt.step(20);                                       // ... step() le finit
    CHECK(rt.scanCount() == 2 && intOf(rt, "x") == 2 && intOf(rt, "y") == 4 && rt.clockMs() == 40);

    WriteSite site;
    CHECK(rt.lastWrite("y", site) && site.section == "B" && site.line == 3 && site.scan == 2);
    CHECK(rt.lastWrite("x", site) && site.section == "A" && site.line == 1);
    CHECK(!rt.lastWrite("personne", site));
    CHECK(rt.lastWrite("y", site));
    std::printf("4. qui a \xC3\xA9" "crit y : %s ligne %u cycle %llu\n", site.section.c_str(), site.line, static_cast<unsigned long long>(site.scan));

    const auto times = rt.sectionTimes();
    CHECK(times.size() == 2);
    if (times.size() == 2) CHECK(times[0].section == "A" && times[0].statements == 1 && times[1].statements == 2);

    // Dans l'hote : la section suivante, puis la pause.
    app::SimulationHost host;
    CHECK(host.attach(f.project).has_value());
    CHECK(host.taskEntries().size() == 2 && host.nextSection().empty());
    host.stepSection();
    CHECK(host.state() == app::SimulationHost::State::Paused && host.scanInProgress() && host.nextSection() == "B");
    host.stepSection();
    CHECK(!host.scanInProgress() && host.scanCount() == 1 && host.sectionTimes().size() == 2);
    app::SimLastWrite w;
    CHECK(host.lastWrite("y", w) && w.section == "B" && w.scan == 1);
}

// --- 5. le projet d'essai ------------------------------------------------------------
void fixture(const char* path, const char* libs) {
    core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto imported = importer.importFile(path);
    CHECK(imported.has_value());
    if (!imported) return;
    auto p = std::make_shared<Project>(*imported->project);
    // BUILDING 0.22 du projet boucle sans fin (lot 7) : avec le dossier libs/,
    // la 0.24 le remplace, comme "Mettre a jour" dans l'appli.
    if (libs) {
        project::SharedLibrary lib(libs);
        (void)lib.scan();
        const auto r = lib.import(*p, "BUILDING", project::SharedLibrary::OnConflict::Overwrite);
        std::printf("5. BUILDING depuis %s : %s\n", libs, r ? "mis \xC3\xA0 jour" : r.error().message().c_str());
    }

    auto t0 = clk::now();
    const auto print = Runtime::programFingerprint(*p);
    std::printf("5. empreinte du programme : %016llx en %.2f ms\n", static_cast<unsigned long long>(print), msSince(t0));
    CHECK(print == Runtime::programFingerprint(*p) && print != 0);

    Runtime rt(p);
    rt.setContinueOnUnknownCalls(true);
    // Sans libs/ : une limite en instructions (pas en temps) - des cycles
    // courts et reproductibles, meme arretes par la boucle de BUILDING 0.22.
    // ---- Lot API 8 : tests ---- avec libs/ aussi, les instructions seules (2 000 000, le
    // defaut) : la limite de temps du defaut (1 500 ms) arretait CHAQUE cycle de BUILDING 0.24
    // en Debug sur une machine chargee (1 533 ms par cycle, 10 haltes sur 10) - plus de cycle
    // complet, et le test ne disait plus rien du moteur.
    rt.setScanLimits(libs ? 2000000 : 200000, 0);
    t0 = clk::now();
    CHECK(rt.prepare().has_value());
    std::printf("5. pr\xC3\xA9paration %.0f ms, %zu cases, %zu entr\xC3\xA9" "es de MAST\n", msSince(t0), rt.slotCount(), rt.entries().size());
    for (int i = 0; i < 5; ++i) (void)rt.step(20);
    t0 = clk::now();
    const int n = 10;
    int halts = 0;
    for (int i = 0; i < n; ++i) {
        const auto r = rt.step(20);
        if (!r.halted) continue;
        // Le projet brut (BUILDING d'origine) s'arrete sur une erreur du programme
        // lui-meme : dite, pas comptee comme un echec du moteur.
        if (++halts == 1)
            for (const auto& d : r.diagnostics)
                std::printf("5.   halte au cycle %u : %s (%s:%u)\n", r.scan, d.message.c_str(), d.section.c_str(), d.line);
    }
    std::printf("5. %.2f ms par cycle (moyenne de %d, %d en halte)\n", msSince(t0) / n, n, halts);

    // Ou passe le temps.
    auto times = rt.sectionTimes();
    CHECK(libs == nullptr || !times.empty());
    std::sort(times.begin(), times.end(), [](const SectionTime& a, const SectionTime& b) { return a.micros > b.micros; });
    for (std::size_t i = 0; i < times.size() && i < 3; ++i)
        std::printf("5. temps : %s > %s : %llu instructions, %lld us\n", times[i].entry.c_str(), times[i].section.c_str(),
                    static_cast<unsigned long long>(times[i].statements), static_cast<long long>(times[i].micros));

    // Qui a ecrit : une case ecrite par le programme.
    std::string written;
    WriteSite site;
    for (const auto& name : rt.names())
        if (rt.lastWrite(name, site) && !site.section.empty()) {
            written = name;
            break;
        }
    CHECK(!written.empty());
    std::printf("5. qui a \xC3\xA9" "crit %s : %s ligne %u cycle %llu\n", written.c_str(), site.section.c_str(), site.line,
                static_cast<unsigned long long>(site.scan));

    // Un point d'arret sur la section qui prend le plus de temps, ligne 1 (decale).
    if (times.empty()) std::printf("5. aucun cycle complet (BUILDING 0.22) : le dossier libs/ en 2e argument pour le reste\n");
    Breakpoint b;
    b.id = 7;
    b.section = times.empty() ? std::string{} : times.front().section;
    b.line = 1;
    rt.setBreakpoints({b});
    if (!times.empty() && rt.breakpoints().front().effectiveLine == 0) {
        b.section = times.front().entry + "." + times.front().section;
        rt.setBreakpoints({b});
    }
    std::printf("5. point d'arr\xC3\xAAt %s : ligne %u (%s)\n", b.section.c_str(), rt.breakpoints().front().effectiveLine,
                rt.breakpoints().front().note.c_str());
    (void)rt.step(20);
    const auto hit = rt.takeBreakHit();
    CHECK(times.empty() || hit.has_value());
    if (hit) {
        std::printf("5. arr\xC3\xAAt cycle %llu, pile %s, %zu valeur(s)\n", static_cast<unsigned long long>(hit->scan), joined(hit->stack).c_str(),
                    hit->values.size());
        CHECK(hit->scan == rt.scanCount() && hit->stack.size() >= 2 && hit->stack.front() == "MAST");
    }
    rt.setBreakpoints({});

    // La modification en ligne sur le meme programme : tout est garde, a l'identique.
    Runtime next(p);
    next.setContinueOnUnknownCalls(true);
    next.setScanLimits(libs ? 2000000 : 200000, 0);   // ---- Lot API 8 : tests ---- (voir plus haut)
    CHECK(next.prepare().has_value());
    t0 = clk::now();
    const auto a = next.adoptStateFrom(rt);
    std::printf("5. en ligne : %zu gard\xC3\xA9" "es, %zu nouvelles, %zu disparues en %.0f ms\n", a.kept, a.added, a.dropped, msSince(t0));
    CHECK(a.kept > 0 && a.added == 0 && a.dropped == 0 && next.scanCount() == rt.scanCount());
    (void)rt.step(20);
    (void)next.step(20);
    std::size_t differ = 0;
    rt.forEachSlot([&](const std::string& name, const Value& v) {
        Value other;
        if (!next.get(name, other) || other.display() != v.display()) {
            if (differ < 3) std::printf("5.   diff\xC3\xA9rente apr\xC3\xA8s un cycle : %s\n", name.c_str());
            ++differ;
        }
    });
    std::printf("5. apr\xC3\xA8s un cycle de plus : %zu case(s) diff\xC3\xA9rente(s)\n", differ);
    CHECK(differ == 0);
}

// --- 6. lot API 8 (2e partie) : EN PAUSE, une section, une variable, Ctrl+Z ---------
//  La plainte : "apres n'importe quelle modification du projet, une simulation en
//  pause repart du cycle 0 au lancement suivant". Chaque modification passe ici
//  comme dans l'application : la commande, la revision (App, a l'image
//  suivante), puis le lancement (attach - Simuler -, et un cycle) ; et attach()
//  dans la MEME image que la commande (l'import d'un XPG, un parcours du
//  didacticiel). A chaque fois, le cycle est garde. Arreter, lui, remet a zero.
void pausedEditsKeepCycle() {
    using State = app::SimulationHost::State;
    Fixture f;
    f.global("counter", "INT");
    f.section("Cycle", "counter := counter + 1;\n");
    app::SimulationHost host;
    std::uint64_t revision = 1;
    host.setProgramRevision(revision);
    CHECK(host.attach(f.project).has_value());
    for (int i = 0; i < 4; ++i) host.step();
    CHECK(host.state() == State::Paused && host.scanCount() == 4 && intOf(*host.runtime(), "counter") == 4);
    // Le lancement suivant : Simuler (attach) ; le cycle et les valeurs restent.
    const auto relaunch = [&](const char* what, std::uint64_t scan) {
        const auto gen = host.generation();
        CHECK(host.attach(f.project).has_value());
        const bool kept = host.state() == State::Paused && host.scanCount() == scan && !host.stale() && host.generation() != gen
                          && host.lastOnlineChange() && !host.lastOnlineChange()->failed && host.lastOnlineChange()->atScan == scan;
        std::printf("6. %s : cycle %llu%s\n", what, static_cast<unsigned long long>(host.scanCount()), kept ? " (gard\xC3\xA9)" : " - ECHEC");
        CHECK(kept);
    };

    // Une section ajoutee en pause.
    f.section("Plus", "counter := counter + 10;\n");
    host.setProgramRevision(++revision);
    CHECK(host.stale());
    relaunch("une section ajout\xC3\xA9" "e", 4);
    CHECK(intOf(*host.runtime(), "counter") == 4);
    host.step();
    CHECK(host.scanCount() == 5 && intOf(*host.runtime(), "counter") == 15);

    // Une variable ajoutee en pause (a sa valeur initiale).
    f.global("extra", "INT", "7");
    host.setProgramRevision(++revision);
    relaunch("une variable ajout\xC3\xA9" "e", 5);
    CHECK(host.runtime()->known("extra") && intOf(*host.runtime(), "extra") == 7 && intOf(*host.runtime(), "counter") == 15);
    host.step();
    CHECK(host.scanCount() == 6 && intOf(*host.runtime(), "counter") == 26);

    // Dans la meme image que la commande (App n'a pas encore donne la revision) :
    // attach() le voit quand meme (l'empreinte), tout de suite.
    f.body("Plus", "counter := counter + 10;\nextra := extra + 1;\n");
    relaunch("une section chang\xC3\xA9" "e, attach dans la m\xC3\xAAme image", 6);
    host.step();
    CHECK(host.scanCount() == 7 && intOf(*host.runtime(), "counter") == 37 && intOf(*host.runtime(), "extra") == 8);

    // Ctrl+Z : la section d'avant.
    CHECK(f.stack.undo().has_value());
    host.setProgramRevision(++revision);
    relaunch("Ctrl+Z (la section)", 7);
    host.step();
    CHECK(host.scanCount() == 8 && intOf(*host.runtime(), "counter") == 48 && intOf(*host.runtime(), "extra") == 8);

    // Ctrl+Z : la variable retiree.
    CHECK(f.stack.undo().has_value());
    host.setProgramRevision(++revision);
    relaunch("Ctrl+Z (la variable)", 8);
    CHECK(!host.runtime()->known("extra"));
    host.step();
    CHECK(host.scanCount() == 9 && intOf(*host.runtime(), "counter") == 59);

    // Ctrl+Z deux fois : la section ajoutee s'en va (son code, puis elle).
    CHECK(f.stack.undo().has_value());
    CHECK(f.stack.undo().has_value());
    host.setProgramRevision(++revision);
    relaunch("Ctrl+Z (la section ajout\xC3\xA9" "e)", 9);
    host.step();
    CHECK(host.scanCount() == 10 && intOf(*host.runtime(), "counter") == 60);

    // En marche, pareil (la modification en ligne au tick suivant, sans delai ici).
    host.setOnlineChangeDelay(0.0);
    host.setState(State::Running);
    f.global("late", "INT", "3");
    host.setProgramRevision(++revision);
    host.tick(0.0);
    CHECK(!host.stale() && host.scanCount() == 10 && host.runtime()->known("late"));

    // Arreter : LA remise a zero voulue.
    host.setState(State::Stopped);
    CHECK(host.scanCount() == 0 && intOf(*host.runtime(), "counter") == 0);
}

// --- 7. lot API 8 (2e partie) : LE PROGRAMME DIRAIT - la case sous le forcage --------
void programSays() {
    Fixture f;
    f.global("counter", "INT");
    f.global("motor", "BOOL");
    f.section("Cycle", "counter := counter + 1;\nmotor := TRUE;\n");
    app::SimulationHost host;
    host.setProgramRevision(1);
    CHECK(host.attach(f.project).has_value());
    for (int i = 0; i < 3; ++i) host.step();
    auto* rt = host.runtime();
    CHECK(rt != nullptr);
    if (!rt) return;
    std::string says;
    CHECK(host.unforcedValue("counter", says) && says == "3");          // pas force : la valeur
    CHECK(rt->force("counter", Value::integer(sim::Type::Int, 100)) && rt->force("motor", Value::boolean(false)));
    host.step();
    host.step();
    // Le programme lit le forcage (100) et ecrit 101 en dessous ; motor : TRUE sous FALSE.
    CHECK(intOf(*rt, "counter") == 100 && !boolOf(*rt, "motor"));
    Value v;
    CHECK(rt->programValue("counter", v) && v.asInteger() == 101);
    CHECK(rt->programValue("MOTOR", v) && v.isTruthy());                // sans la casse
    CHECK(host.unforcedValue("counter", says) && says == "101");
    std::printf("7. counter forc\xC3\xA9" "e \xC3\xA0 100 : le programme dirait %s\n", says.c_str());
    CHECK(host.unforcedValue("motor", says) && says == "TRUE");
    std::printf("7. motor forc\xC3\xA9" "e \xC3\xA0 FALSE : le programme dirait %s\n", says.c_str());
    CHECK(!host.unforcedValue("inconnue", says));
    // Relachee : elle prend ce que le programme dirait.
    CHECK(rt->unforce("motor") && boolOf(*rt, "motor"));
    CHECK(rt->unforce("counter") && intOf(*rt, "counter") == 101);
    host.step();
    CHECK(intOf(*rt, "counter") == 102);
    app::SimulationHost none;
    CHECK(!none.unforcedValue("counter", says));
}

// --- 8. 1.10.2 : LES PARAMETRES D'UNE UNITE DE PROGRAMME relies a une globale
// (EffectiveParameter), et LA CONDITION D'ACTIVATION d'une section ------------
//  Comme Control Expert : une entree est copiee depuis sa globale au debut de
//  l'unite (l'unite qui l'ecrit ne touche pas la globale), une sortie vers sa
//  globale a la fin, une entree/sortie EST la globale ; un tableau, en entier ;
//  un element de tableau comme globale. Une section dont la condition est
//  fausse ne tourne pas.
void unitParameters() {
    Fixture f;
    f.global("g_in", "INT", "5");
    f.global("g_out", "INT");
    f.global("g_io", "INT", "10");
    f.global("g_tab", "ARRAY[0..2] OF INT");
    f.global("g_elem", "ARRAY[0..1] OF INT");
    f.global("cfg_ok", "BOOL");
    auto& p = *f.project;
    const auto mast = p.strings.intern("MAST");
    Pou unit;
    unit.name = p.strings.intern("U");
    unit.kind = PouKind::ProgramUnit;
    unit.task = mast;
    unit.order = 1;
    p.pous.push_back(unit);
    const auto u = static_cast<Index>(p.pous.size() - 1);
    auto param = [&](const char* name, const char* type, VariableScope scope, const char* effective) {
        AddVariableCommand::Spec s;
        s.name = name;
        s.type = type;
        s.scope = scope;
        s.owner = u;
        const bool ok = f.stack.push(std::make_unique<AddVariableCommand>(f.project, s)).has_value();
        CHECK(ok);
        p.variables.back().attributes.emplace_back("EffectiveParameter", effective);
    };
    param("e", "INT", VariableScope::Input, "g_in");
    param("s", "INT", VariableScope::Output, "g_out");
    param("es", "INT", VariableScope::InOut, "g_io");
    param("t", "ARRAY[0..2] OF INT", VariableScope::Output, "g_tab");
    param("x", "INT", VariableScope::Input, "g_elem[1]");
    param("actif", "BOOL", VariableScope::Input, "cfg_ok");
    auto section = [&](const char* name, std::uint32_t order, const char* body, const char* condition) {
        Section sec;
        sec.name = p.strings.intern(name);
        sec.language = PouLanguage::ST;
        sec.order = order;
        sec.body = body;
        sec.owner = u;
        if (condition) sec.activationCondition = p.strings.intern(condition);
        p.sections.push_back(sec);
        p.pous[u].sections.push_back(static_cast<Index>(p.sections.size() - 1));
    };
    section("Calcul", 1, "s := e * 2;\nes := es + 1;\nt[0] := e;\nt[1] := x;\nt[2] := 7;\ne := 99;\n", nullptr);
    section("Garde", 2, "s := 1000;\n", "actif");
    p.buildIndices();

    Runtime rt(f.project);
    CHECK(rt.prepare().has_value());
    CHECK(rt.set("g_elem[1]", Value::integer(sim::Type::Int, 3)));
    (void)rt.step(20);
    // Garde ne tourne pas (cfg_ok = FALSE) : s = e * 2.
    CHECK(intOf(rt, "g_out") == 10);
    CHECK(intOf(rt, "g_in") == 5);                    // l'entree ecrite par l'unite : la globale n'a pas bouge
    CHECK(intOf(rt, "U.e") == 99);                    // sa copie, si
    CHECK(intOf(rt, "g_io") == 11);                   // IN_OUT : la globale elle-meme
    CHECK(intOf(rt, "U.es") == 11);
    CHECK(intOf(rt, "g_tab[0]") == 5 && intOf(rt, "g_tab[1]") == 3 && intOf(rt, "g_tab[2]") == 7);
    std::printf("8. unite U : g_out %lld, g_in %lld (U.e %lld), g_io %lld, g_tab [%lld %lld %lld]\n",
                static_cast<long long>(intOf(rt, "g_out")), static_cast<long long>(intOf(rt, "g_in")),
                static_cast<long long>(intOf(rt, "U.e")), static_cast<long long>(intOf(rt, "g_io")),
                static_cast<long long>(intOf(rt, "g_tab[0]")), static_cast<long long>(intOf(rt, "g_tab[1]")),
                static_cast<long long>(intOf(rt, "g_tab[2]")));
    // Ecrire le parametre IN_OUT, c'est ecrire la globale.
    CHECK(rt.set("U.es", Value::integer(sim::Type::Int, 20)));
    CHECK(intOf(rt, "g_io") == 20);
    // L'entree suit sa globale au cycle suivant ; la condition devient vraie.
    CHECK(rt.set("g_in", Value::integer(sim::Type::Int, 7)));
    CHECK(rt.set("cfg_ok", Value::boolean(true)));
    (void)rt.step(20);
    CHECK(intOf(rt, "g_out") == 1000);                // Garde a tourne
    CHECK(intOf(rt, "g_io") == 21);
    CHECK(intOf(rt, "g_tab[0]") == 7);
    std::printf("8. cfg_ok vraie : g_out %lld (Garde a tourne), g_io %lld\n",
                static_cast<long long>(intOf(rt, "g_out")), static_cast<long long>(intOf(rt, "g_io")));
    // Une globale forcee : l'unite lit le forcage.
    CHECK(rt.force("g_in", Value::integer(sim::Type::Int, 40)));
    CHECK(rt.set("cfg_ok", Value::boolean(false)));
    (void)rt.step(20);
    CHECK(intOf(rt, "g_out") == 80);
    // Remis a zero : les valeurs initiales des globales, pas celles des parametres.
    rt.unforceAll();
    rt.reset();
    CHECK(intOf(rt, "g_io") == 10 && intOf(rt, "g_in") == 5);
}

// --- 9. 1.10.2 : STRING_TO_ASCII / ASCII_TO_STRING (le nom d'un gaz range en %MW) --
//  Comme une STRING dans des %MW d'un M580 : les cases dans l'ordre des indices,
//  le 1er caractere dans l'octet de poids faible, 2 caracteres par INT (1 par
//  BYTE) ; ce qui depasse le tableau est perdu, le reste vaut 0 (la fin de la
//  chaine) ; l'inverse s'arrete au premier octet nul. Avant : appels inconnus,
//  le nom du gaz devenait '0' (Armoire_Gaz, page 151, code 41).
void stringAscii() {
    Fixture f;
    f.global("nom", "STRING");
    f.global("tab", "ARRAY[0..7] OF INT");
    f.global("court", "ARRAY[0..1] OF INT");
    f.global("octets", "ARRAY[1..4] OF BYTE");
    f.global("mots", "ARRAY[0..1] OF WORD");
    f.global("relu", "STRING");
    f.global("relu_court", "STRING");
    f.global("relu_octets", "STRING");
    f.global("relu_mots", "STRING");
    f.global("mot", "INT");
    f.global("relu_mot", "STRING");
    f.section("Conv",
              "tab := STRING_TO_ASCII(nom);\n"
              "relu := ASCII_TO_STRING(tab);\n"
              "court := STRING_TO_ASCII('ABCDEFG');\n"
              "relu_court := ASCII_TO_STRING(court);\n"
              "octets := STRING_TO_ASCII('PPM');\n"
              "relu_octets := ASCII_TO_STRING(octets);\n"
              "mots := STRING_TO_ASCII('AB');\n"
              "relu_mots := ASCII_TO_STRING(mots);\n"
              "mot := STRING_TO_ASCII('N2');\n"
              "relu_mot := ASCII_TO_STRING(mot);\n");
    Runtime rt(f.project);
    rt.setContinueOnUnknownCalls(true);       // un appel inconnu serait note (et rendrait 0)
    CHECK(rt.prepare().has_value());
    CHECK(rt.set("nom", Value::text("HCL")));
    (void)rt.step(20);
    CHECK(intOf(rt, "tab[0]") == ('H' | ('C' << 8)));      // 17224 : 'H' en poids faible
    CHECK(intOf(rt, "tab[1]") == 'L');                     // 'L', puis l'octet nul
    CHECK(intOf(rt, "tab[2]") == 0 && intOf(rt, "tab[7]") == 0);
    CHECK(strOf(rt, "relu") == "HCL");
    CHECK(intOf(rt, "court[0]") == ('A' | ('B' << 8)) && intOf(rt, "court[1]") == ('C' | ('D' << 8)));
    CHECK(strOf(rt, "relu_court") == "ABCD");              // 2 INT : 4 caracteres, pas de fin
    CHECK(intOf(rt, "octets[1]") == 'P' && intOf(rt, "octets[3]") == 'M' && intOf(rt, "octets[4]") == 0);
    CHECK(strOf(rt, "relu_octets") == "PPM");
    CHECK(intOf(rt, "mots[0]") == ('A' | ('B' << 8)) && intOf(rt, "mots[1]") == 0);
    CHECK(strOf(rt, "relu_mots") == "AB");
    CHECK(intOf(rt, "mot") == ('N' | ('2' << 8)));         // une seule case
    CHECK(strOf(rt, "relu_mot") == "N2");
    // Un nom plus court efface la fin de l'ancien.
    CHECK(rt.set("nom", Value::text("N2")));
    (void)rt.step(20);
    CHECK(intOf(rt, "tab[0]") == ('N' | ('2' << 8)) && intOf(rt, "tab[1]") == 0);
    CHECK(strOf(rt, "relu") == "N2");
    // Une case forcee garde son forcage ; la lecture le voit.
    CHECK(rt.force("tab[1]", Value::integer(sim::Type::Int, 'X')));
    (void)rt.step(20);
    CHECK(intOf(rt, "tab[1]") == 'X');
    CHECK(strOf(rt, "relu") == "N2X");
    CHECK(rt.unknownCalls().empty());
    std::printf("9. STRING_TO_ASCII / ASCII_TO_STRING : tab[0] %lld, relu '%s', relu_court '%s', relu_octets '%s', inconnus %zu\n",
                static_cast<long long>(intOf(rt, "tab[0]")), strOf(rt, "relu").c_str(),
                strOf(rt, "relu_court").c_str(), strOf(rt, "relu_octets").c_str(), rt.unknownCalls().size());
}

// --- 10. 1.11 : OU PASSE LE TEMPS APRES UNE HALTE --------------------------------
//  Les temps sont ceux du dernier cycle complet, et "inactive" aussi. Avant, la
//  condition etait celle du cycle arrete (une halte, une pause au pas a pas) et
//  les temps ceux du precedent : une section qui avait tourne se disait
//  "inactive (actif faux)" a tort, ou l'inverse.
void timesAfterHalt() {
    Fixture f;
    f.global("actif", "BOOL");
    f.global("boucle", "BOOL");
    f.global("n", "INT");
    f.global("k", "INT");
    f.section("Garde", "n := n + 1;\n");
    f.project->sections.back().activationCondition = f.project->strings.intern("actif");
    f.section("Suite", "WHILE boucle DO\n  k := k + 1;\nEND_WHILE;\n");
    Runtime rt(f.project);
    rt.setScanLimits(1000, 0);                      // la boucle sans fin : une halte, vite
    CHECK(rt.prepare().has_value());
    CHECK(rt.set("actif", Value::boolean(true)));
    auto r = rt.step(20);                           // un cycle complet : Garde tourne
    CHECK(!r.halted && intOf(rt, "n") == 1);
    auto times = rt.sectionTimes();
    CHECK(times.size() == 2);
    if (times.size() == 2) CHECK(times[0].section == "Garde" && times[0].active && times[0].statements == 1);
    // La condition devient fausse, puis le cycle s'arrete dans Suite.
    CHECK(rt.set("actif", Value::boolean(false)));
    CHECK(rt.set("boucle", Value::boolean(true)));
    r = rt.step(20);
    CHECK(r.halted && intOf(rt, "n") == 1);         // Garde n'a pas tourne
    times = rt.sectionTimes();                      // le dernier cycle complet : Garde active
    CHECK(times.size() == 2);
    if (times.size() == 2) CHECK(times[0].active && times[0].statements == 1 && times[0].micros >= 0);
    const bool apresHalte = times.size() == 2 && times[0].active;
    // L'inverse : Garde inactive au dernier cycle complet, puis vraie dans un cycle arrete.
    CHECK(rt.set("boucle", Value::boolean(false)));
    r = rt.step(20);
    CHECK(!r.halted);
    times = rt.sectionTimes();
    if (times.size() == 2) CHECK(!times[0].active && times[0].statements == 0 && times[0].micros == 0 && times[1].active);
    CHECK(rt.set("actif", Value::boolean(true)));
    CHECK(rt.set("boucle", Value::boolean(true)));
    r = rt.step(20);
    CHECK(r.halted && intOf(rt, "n") == 2);         // Garde a tourne, le cycle s'est arrete ensuite
    times = rt.sectionTimes();                      // toujours le cycle complet d'avant : inactive
    if (times.size() == 2) CHECK(!times[0].active && times[0].statements == 0 && times[0].micros == 0);
    // La pause au pas a pas : Garde seule, le cycle reste ouvert.
    CHECK(rt.set("boucle", Value::boolean(false)));
    r = rt.step(20);                                // complet : Garde active
    CHECK(!r.halted);
    CHECK(rt.set("actif", Value::boolean(false)));
    (void)rt.stepEntry(20);
    CHECK(rt.scanInProgress() && rt.nextEntry() == "Suite");
    times = rt.sectionTimes();
    if (times.size() == 2) CHECK(times[0].active && times[0].statements == 1);
    const bool enPause = times.size() == 2 && times[0].active;
    (void)rt.step(20);                              // le cycle fini : Garde inactive
    CHECK(!rt.scanInProgress());
    times = rt.sectionTimes();
    if (times.size() == 2) CHECK(!times[0].active && times[0].statements == 0 && times[1].active);
    std::printf("10. o\xC3\xB9 passe le temps apr\xC3\xA8s une halte : Garde %s ; en pause au pas \xC3\xA0 pas : %s ; cycle fini : %s\n",
                apresHalte ? "active (le dernier cycle complet)" : "INACTIVE (le cycle arr\xC3\xAAt\xC3\xA9)",
                enPause ? "active" : "INACTIVE", times.size() == 2 && !times[0].active ? "inactive" : "ACTIVE");
}

} // namespace

int main(int argc, char** argv) {
    onlineChangeRuntime();
    onlineChangeHost();
    breakpoints();
    stepping();
    pausedEditsKeepCycle();   // lot API 8 (2e partie)
    programSays();
    unitParameters();         // 1.10.2 : les parametres des unites, la condition d'activation
    stringAscii();            // 1.10.2 : STRING_TO_ASCII / ASCII_TO_STRING
    timesAfterHalt();         // 1.11 : ou passe le temps apres une halte
    if (argc > 1) fixture(argv[1], argc > 2 ? argv[2] : nullptr);
    else std::printf("5. (pas de projet d'essai : tests/fixtures/MAST.XPG en argument)\n");
    if (failures) std::printf("simdebug_test : %d \xC3\xA9" "chec(s)\n", failures);
    else std::printf("simdebug_test : tout est bon\n");
    return failures ? 1 : 0;
}
