// =============================================================================
//  tests/grafcet_editor_test.cpp - 1.10, chantier R : l'editeur de grafcet
// -----------------------------------------------------------------------------
//  Sur le projet de reference (argv[1], MAST.XPG) : les 17 grafcets relus
//  (comptes), leurs divergences et convergences en OU, le dessin de chacun
//  SANS CHEVAUCHEMENT (verifie par le calcul : problems() vide), et sur des
//  grafcets faits ici : la divergence et la convergence en ET, un controle de
//  chaque genre, les raccourcis lisibles dans les deux sens.
//  Les verifications restent actives en Release (CHECK, pas assert).
//
//  Construction (en attendant une cible du CMakeLists) :
//    g++ -std=c++20 -I src -I third_party -DXPG_HAVE_MINIZ=1 tests/grafcet_editor_test.cpp
//        src/project/GrafcetDiagram.cpp build-o0/libxpg_import.a build-o0/libxpg_core.a -lpthread
// =============================================================================
#include "../src/core/EventBus.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/Grafcet.hpp"
#include "../src/project/GrafcetCheck.hpp"
#include "../src/project/GrafcetDiagram.hpp"
#include "../src/sim/Runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace grafcet;

namespace {

int failures = 0, checks = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++checks;                                                             \
        if (!(c)) {                                                           \
            std::printf("ECHEC (ligne %d) : %s\n", __LINE__, #c);             \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

Step step(int id, bool initial = false, bool isFinal = false) {
    Step s;
    s.id = id;
    s.name = "X" + std::to_string(id);
    s.initial = initial;
    s.isFinal = isFinal;
    return s;
}

Transition trans(int id, std::vector<int> from, std::vector<int> to, std::string expr = "TRUE") {
    Transition t;
    t.id = id;
    t.sources = std::move(from);
    t.destinations = std::move(to);
    t.conditionExpr = std::move(expr);
    t.conditionText = t.conditionExpr.substr(0, 8);
    return t;
}

// --- 1. le projet de reference ---------------------------------------------------
void reference(const char* path, bool verbose) {
    core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto imported = importer.importFile(path);
    CHECK(imported.has_value());
    if (!imported) return;
    const auto& p = *imported->project;
    const auto charts = findCharts(p);
    std::size_t steps = 0, transitions = 0, actions = 0, renvois = 0, orDiv = 0, orConv = 0;
    CHECK(charts.size() == 17);
    const auto measure = fixedWidthMeasure(7.f);
    for (const auto& c : charts) {
        steps += c.steps.size();
        transitions += c.transitions.size();
        actions += c.actions.size();
        for (const auto& b : branches(c)) {
            if (b.kind == BranchKind::OrDivergence) ++orDiv;
            if (b.kind == BranchKind::OrConvergence) ++orConv;
            CHECK(b.kind == BranchKind::OrDivergence || b.kind == BranchKind::OrConvergence);
        }
        const auto d = buildDiagram(c, nullptr, measure);
        const auto bad = problems(d);
        CHECK(bad.empty());
        if (!bad.empty() || verbose) {
            std::printf("  %-14s %2zu etapes %2zu transitions : %d renvois, %d essais, %.0f x %.0f, %zu chevauchements\n",
                c.name.c_str(), c.steps.size(), c.transitions.size(), d.renvoiCount, d.passes,
                d.size.w, d.size.h, bad.size());
            for (std::size_t i = 0; i < bad.size() && i < 6; ++i) std::printf("      %s\n", bad[i].c_str());
        }
        CHECK(d.steps.size() == c.steps.size());
        CHECK(d.transitions.size() == c.transitions.size());
        CHECK(d.actions.size() == c.actions.size());
        // une etape placee a la main (GrafcetLayoutFile) : elle et ses actions
        // bougent, ses liaisons suivent en equerre, le reste reste propre
        if (c.steps.size() > 1) {
            const int moved = c.steps[1].id;
            const auto* before = d.step(moved);
            CHECK(before != nullptr);
            if (before) {
                project::ChartLayout placement;
                const gfx::Point to{ before->box.x + 200.f, before->box.y + 30.f };
                placement.steps[moved] = to;
                const auto d2 = buildDiagram(c, &placement, measure);
                const auto* after = d2.step(moved);
                CHECK(after && after->moved && after->box.x == to.x && after->box.y == to.y);
                CHECK(problems(d2).empty());
                CHECK(d2.size.w >= to.x + before->box.w);
                for (const auto& a : d2.actions) {
                    if (a.step != moved) continue;
                    const auto* a0 = d.action(a.id);
                    CHECK(a0 && a.box.x == a0->box.x + 200.f && a.box.y == a0->box.y + 30.f);
                }
                // la liaison arrive toujours sur l'etape : un bout de trait touche son haut
                bool reachesTop = false, leavesBottom = false;
                const float cx = to.x + after->box.w / 2.f;
                for (const auto& s : d2.segments) {
                    if (std::abs(s.b.x - cx) < 0.5f && std::abs(s.b.y - to.y) < 0.5f) reachesTop = true;
                    if (std::abs(s.a.x - cx) < 0.5f && std::abs(s.a.y - (to.y + after->box.h)) < 0.5f) leavesBottom = true;
                }
                CHECK(reachesTop || after->initial);
                CHECK(leavesBottom || after->isFinal);
            }
        }
        renvois += static_cast<std::size_t>(d.renvoiCount);
        // tout est dans le dessin
        for (const auto& s : d.steps) CHECK(s.box.x >= 0.f && s.box.right() <= d.size.w && s.box.bottom() <= d.size.h);
        for (const auto& t : d.transitions) CHECK(t.label.right() <= d.size.w + 0.5f);
        // le projet de reference est sain
        CHECK(runChecks(c, &p, &charts).empty());
        // les raccourcis lisibles se redeveloppent exactement, sur chaque receptivite
        for (const auto& t : c.transitions) {
            const auto readable = readableCondition(c, t.conditionExpr);
            const bool same = expandShortcuts(c, readable, &charts) == t.conditionExpr;
            CHECK(same);
            if (!same) std::printf("      %s T%d : %s\n", c.name.c_str(), t.id, readable.c_str());
        }
    }
    CHECK(steps == 151);
    CHECK(transitions == 193);
    CHECK(actions == 189);
    std::printf("reference : %zu grafcets, %zu etapes, %zu transitions, %zu actions ; %zu divergences et %zu convergences en OU ; %zu renvois\n",
        charts.size(), steps, transitions, actions, orDiv, orConv, renvois);
    CHECK(orDiv > 0 && orConv > 0);

    // DetoxalA : X0 diverge en OU (T0 vers X1, T1 vers X3), X0 converge (T4, T5)
    const auto detox = std::find_if(charts.begin(), charts.end(), [](const Chart& c) { return c.name == "DetoxalA"; });
    CHECK(detox != charts.end());
    if (detox != charts.end()) {
        const auto br = branches(*detox);
        CHECK(std::any_of(br.begin(), br.end(), [](const Branch& b) {
            return b.kind == BranchKind::OrDivergence && b.at == 0 && b.members == std::vector<int>{ 0, 1 }; }));
        CHECK(std::any_of(br.begin(), br.end(), [](const Branch& b) {
            return b.kind == BranchKind::OrConvergence && b.at == 0 && b.members == std::vector<int>{ 4, 5 }; }));
        // les raccourcis : T2 se lit DUREE(X1) >= t#1s AND FIN(A0) AND ...
        const auto* t2 = transitionById(*detox, 2);
        CHECK(t2 != nullptr);
        if (t2) {
            const auto readable = readableCondition(*detox, t2->conditionExpr);
            CHECK(readable.rfind("DUREE(X1) >= t#1s AND FIN(A0) AND ", 0) == 0);
            CHECK(expandShortcuts(*detox, readable, &charts) == t2->conditionExpr);   // aller-retour exact
        }
        // le genre d'action : A0 est "limitee", K4, qualificatif L
        const auto* a0 = actionById(*detox, 0);
        CHECK(a0 && kindCode(a0->kind) == "K4" && kindQualifier(a0->kind) == "L");
        CHECK(a0 && kindLabel(a0->kind, a0->delay).rfind("limit\xC3\xA9" "e \xC3\xA0 ", 0) == 0);
    }
}

// --- 2. le ET, et les renvois, sur des grafcets faits ici --------------------------
void andBranches() {
    Chart c;
    c.name = "Essai";
    c.arrayPrefix = "Essai";
    c.steps = { step(0, true), step(1), step(2), step(3), step(4) };
    c.transitions = {
        trans(0, { 0 }, { 1, 2 }, "depart"),          // divergence en ET
        trans(1, { 1 }, { 3 }, "a"),
        trans(2, { 2 }, { 4 }, "b"),
        trans(3, { 3, 4 }, { 0 }, "fin"),             // convergence en ET, et retour : renvoi
    };
    const auto br = branches(c);
    CHECK(br.size() == 2);
    CHECK(std::any_of(br.begin(), br.end(), [](const Branch& b) { return b.kind == BranchKind::AndDivergence && b.at == 0; }));
    CHECK(std::any_of(br.begin(), br.end(), [](const Branch& b) { return b.kind == BranchKind::AndConvergence && b.at == 3; }));
    const auto d = buildDiagram(c, nullptr, fixedWidthMeasure(7.f));
    CHECK(problems(d).empty());
    for (const auto& s : problems(d)) std::printf("      %s\n", s.c_str());
    const auto* t0 = d.transition(0);
    CHECK(t0 && t0->andSplit && !t0->renvoi);
    CHECK(std::any_of(d.junctions.begin(), d.junctions.end(), [](const DiagramJunction& j) { return j.isAnd && j.divergence; }));
    const auto* t3 = d.transition(3);
    // il remonte vers X0 : par le cote, une fleche vers le haut (plus un renvoi)
    CHECK(t3 && t3->andJoin && !t3->renvoi);
    CHECK(std::any_of(d.segments.begin(), d.segments.end(), [](const DiagramSegment& g) { return g.transition == 3 && g.arrowUp; }));
    CHECK(std::none_of(d.refs.begin(), d.refs.end(), [](const DiagramRef& r) { return !r.outgoing && r.step == 0; }));
    // la liaison arrive sur X0 par le haut
    CHECK(d.step(0) && std::any_of(d.segments.begin(), d.segments.end(), [&](const DiagramSegment& g) {
        return std::abs(g.b.x - (d.step(0)->box.x + d.step(0)->box.w / 2.f)) < 0.5f && std::abs(g.b.y - d.step(0)->box.y) < 0.5f; }));
    // X1 et X2 cote a cote, sur la meme rangee
    CHECK(d.step(1) && d.step(2) && d.step(1)->box.y == d.step(2)->box.y && d.step(1)->box.x < d.step(2)->box.x);
}

// --- 3. un controle de chaque genre -------------------------------------------------
void eachCheck() {
    auto has = [](const std::vector<Check>& v, CheckKind k) {
        return std::any_of(v.begin(), v.end(), [&](const Check& c) { return c.kind == k; });
    };
    Chart c;
    c.name = "Mal";
    c.arrayPrefix = "Mal";
    c.steps = { step(0), step(1), step(1), step(2), step(5) };
    Action cont;
    cont.id = 0;
    cont.name = "MOT";
    cont.kind = ActionKind::Continuous;
    cont.boundStep = 1;
    c.actions = { cont };
    c.transitions = {
        trans(0, { 0 }, { 1 }, "TRUE"),
        trans(1, { 1 }, { 2 }, "Acts_Mal[0].Started AND Acts_Mal[0].Finished"),   // FIN d'une continue
        trans(2, { 2 }, { 0, 1, 2, 5 }, "Steps_Mal[9].Active"),                     // 4 destinations, X9 absent
        trans(3, { 2 }, { 7 }, ""),                                                  // vers X7 absent, sans receptivite
    };
    const auto v = runChecks(c, nullptr, nullptr);
    CHECK(has(v, CheckKind::NoInitial));
    CHECK(has(v, CheckKind::DuplicateId));
    CHECK(has(v, CheckKind::FinOfContinuous));
    CHECK(has(v, CheckKind::TooManyEnds));
    CHECK(has(v, CheckKind::ShortcutToMissing));
    CHECK(has(v, CheckKind::DanglingLink));
    CHECK(has(v, CheckKind::NoCondition));
    // X5 n'a pas de sortie et n'est pas finale
    CHECK(has(v, CheckKind::NoExitNotFinal));
    Chart u = c;
    u.steps = { step(0, true), step(1), step(2, false, true), step(3, false, true) };
    u.transitions = { trans(0, { 0 }, { 1 }), trans(1, { 1 }, { 2 }) };
    u.actions.clear();
    const auto w = runChecks(u, nullptr, nullptr);
    CHECK(has(w, CheckKind::Unreachable));    // X3 : rien n'y mene
    CHECK(!has(w, CheckKind::NoInitial));
    // le dessin d'un grafcet mal forme ne plante pas et reste propre
    const auto d = buildDiagram(c, nullptr, fixedWidthMeasure(7.f));
    CHECK(d.steps.size() == 4);   // le numero en double n'est dessine qu'une fois
    CHECK(problems(d).empty());
}

// --- 4. la simulation de l'API tourne (1.10, tranche 5) ------------------------------
// BUILDING (le constructeur des grafcets) compare le resultat de FIND_INT a -1 :
// avec 0 pour "pas trouve", sa boucle ne finissait jamais et le premier cycle
// s'arretait (2 000 000 d'instructions, BUILDING ligne 16). Le simulateur suit
// maintenant le programme : 60 cycles sans halte, X0 de DetoxalA active.
void simulation(const char* path) {
    core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto imported = importer.importFile(path);
    CHECK(imported.has_value());
    if (!imported) return;
    sim::Runtime rt(imported->project);
    rt.setScanLimits(2000000, 0);          // le compte d'instructions seul : pas d'horloge dans un essai
    CHECK(rt.prepare().has_value());
    bool sawFindNote = false;
    for (const auto& d : rt.preparationDiagnostics())
        if (d.message.find("FIND") != std::string::npos && d.severity == sim::Diagnostic::Severity::Info) sawFindNote = true;
    CHECK(sawFindNote);
    bool halted = false;
    std::string why;
    for (int i = 0; i < 60 && !halted; ++i) {
        const auto r = rt.step(20);
        if (r.halted) {
            halted = true;
            for (const auto& d : r.diagnostics)
                if (d.severity == sim::Diagnostic::Severity::Error) why = d.message + " (" + d.section + ")";
        }
    }
    CHECK(!halted);
    if (halted) std::printf("      halte : %s\n", why.c_str());
    // les grafcets tournent : des etapes actives (les grafcets que le programme fait tourner)
    sim::Value v;
    int active = 0, known = 0;
    std::string where;
    auto count = [&] {
        active = 0; known = 0; where.clear();
        for (const auto& c : findCharts(*imported->project))
            for (std::size_t i = 0; i < c.steps.size(); ++i) {
                if (!rt.get(c.scope + "Steps_" + c.arrayPrefix + "[" + std::to_string(i) + "].Active", v)) continue;
                ++known;
                if (v.isTruthy()) { ++active; if (where.size() < 120) where += " " + c.name + ":X" + std::to_string(c.steps[i].id); }
            }
    };
    count();
    CHECK(known == 151);
    // 1.10.2 (SIM) : les sections des grafcets (SFC_* de Logigrammes_A / B) ont la
    // condition d'activation `configuree` (Init : configuree := generalites.configuree,
    // soit ConfigArmoireUtilisee.configuree, faux tant que la configuration n'est pas
    // validee) : comme sur l'automate, elles ne tournent pas - aucune etape active.
    CHECK(active == 0);
    std::printf("simulation : non configuree, 60 cycles : %d etapes actives sur %d (sections inactives)\n", active, known);
    // Configuree (forcee) : les grafcets tournent.
    CHECK(rt.force("ConfigArmoireUtilisee.configuree", sim::Value::boolean(true)));
    for (int i = 0; i < 60 && !halted; ++i) {
        const auto r = rt.step(20);
        if (r.halted) {
            halted = true;
            for (const auto& d : r.diagnostics)
                if (d.severity == sim::Diagnostic::Severity::Error) why = d.message + " (" + d.section + ")";
        }
    }
    CHECK(!halted);
    if (halted) std::printf("      halte : %s\n", why.c_str());
    count();
    CHECK(active > 0);
    std::printf("simulation : 60 cycles %s ; %d etapes actives sur %d :%s\n", halted ? "ARRETES" : "sans halte",
                active, known, where.c_str());
}

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/MAST.XPG";
    const bool verbose = argc > 2 && std::strcmp(argv[2], "-v") == 0;
    reference(path, verbose);
    andBranches();
    eachCheck();
    simulation(path);
    std::printf("grafcet_editor_test : %d controles, %d echecs\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
