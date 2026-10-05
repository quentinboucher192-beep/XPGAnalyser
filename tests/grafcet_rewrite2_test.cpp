// =============================================================================
//  tests/grafcet_rewrite2_test.cpp - 1.10, chantier R2 : les reecritures du grafcet
// -----------------------------------------------------------------------------
//  Sur le projet de reference (argv[1], MAST.XPG, 17 grafcets) :
//   - chaque transition supprimee une a une -> relue : comptes, numeros contigus,
//     le reste du modele identique (renumerote), pas de nouvel avertissement de
//     structure ; l'apercu calcule AVANT = exactement les lignes changees ;
//     defaire -> tout le projet octet pour octet ;
//   - idem pour chaque action (ou le refus, quand une receptivite ou une autre
//     section l'emploie : le refus doit etre fonde) ;
//   - chaque action modifiee (genre, retard, nom, etape, tout a la fois) ->
//     relue : la modification est lue, rien d'autre n'a change ; apercu ; defaire ;
//   - inserer puis supprimer (transition au debut et a la fin, action a la fin)
//     -> texte d'avant, octet pour octet ;
//   - les refus (nom trop long, etape absente, retard manquant...) ne touchent a rien.
//  Les verifications restent actives en Release (CHECK, pas assert).
//
//  Construction (pas encore de cible CMake, comme grafcet_editor_test ;
//  GrafcetRewrite2.cpp est dans libxpg_import.a par le glob de src/project/) :
//    ninja -C build-o0 xpg_import xpg_core
//    g++ -std=c++20 -O0 -I src -I third_party -DXPG_HAVE_MINIZ=1 tests/grafcet_rewrite2_test.cpp
//        build-o0/libxpg_import.a build-o0/libxpg_xls.a build-o0/libxpg_core.a -lpthread
//    ./a.out /home/claude/v190/fixtures/MAST.XPG        (3 min a -O0 ; 30 s avec -O2 et
//    src/project/{Grafcet,GrafcetRewrite,GrafcetRewrite2,GrafcetCheck}.cpp avant les bibliotheques)
// =============================================================================
#include "../src/core/EventBus.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/Grafcet.hpp"
#include "../src/project/GrafcetCheck.hpp"
#include "../src/project/GrafcetRewrite.hpp"
#include "../src/project/GrafcetRewrite2.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace grafcet;
using project::StLineChange;

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

struct Counts {
    int transDone = 0, transRefused = 0;
    int actDone = 0, actRefused = 0, actRefusedJustified = 0;
    int modDone = 0, modRefused = 0;
    int previewLines = 0, previewExact = 0;
    int undoExact = 0;
    int roundTrips = 0;
    int refusals = 0;
    int expectedNewChecks = 0;
    int consequences = 0;
    int debugFollowed = 0, roundTripsHead = 0;
} n;

std::vector<std::string> bodies(const domain::Project& p) {
    std::vector<std::string> out;
    for (const auto& s : p.sections) out.push_back(s.body);
    return out;
}

// array[k] -> array[k-1] pour k > removed (le comportement attendu, ecrit a part).
std::string renumbered(const std::string& text, const std::string& array, int removed) {
    std::string out;
    std::size_t at = 0;
    while (true) {
        const auto f = text.find(array + "[", at);
        if (f == std::string::npos || (f > 0 && (std::isalnum(static_cast<unsigned char>(text[f - 1]))
            || text[f - 1] == '_'))) {
            if (f == std::string::npos) { out += text.substr(at); break; }
            out += text.substr(at, f + array.size() + 1 - at);
            at = f + array.size() + 1;
            continue;
        }
        const auto open = f + array.size() + 1;
        const auto close = text.find(']', open);
        out += text.substr(at, open - at);
        if (close == std::string::npos) { at = open; continue; }
        const auto digits = text.substr(open, close - open);
        bool num = !digits.empty() && std::all_of(digits.begin(), digits.end(),
            [](unsigned char c) { return std::isdigit(c) != 0; });
        if (num) {
            int k = std::atoi(digits.c_str());
            out += std::to_string(k > removed ? k - 1 : k);
        }
        else out += digits;
        at = close;
    }
    return out;
}

std::size_t structureWarnings(const Chart& c) { return c.countOf(Diagnostic::Level::Structure); }

// Les controles de R (GrafcetCheck) : combien de chaque genre.
std::map<int, int> checkCounts(const Chart& c, const domain::Project& p) {
    std::map<int, int> out;
    for (const auto& k : runChecks(c, &p)) ++out[static_cast<int>(k.kind)];
    return out;
}
// Aucun genre de controle n'augmente, sauf ceux permis.
bool onlyExpectedNewChecks(const std::map<int, int>& before, const std::map<int, int>& after,
    std::initializer_list<CheckKind> allowed) {
    for (const auto& [kind, count] : after) {
        const auto it = before.find(kind);
        const int was = it == before.end() ? 0 : it->second;
        if (count <= was) continue;
        bool ok = false;
        for (auto a : allowed) if (static_cast<int>(a) == kind) ok = true;
        if (!ok) return false;
        ++n.expectedNewChecks;
    }
    return true;
}

bool sameStep(const Step& a, const Step& b) {
    return a.id == b.id && a.name == b.name && a.initial == b.initial && a.isFinal == b.isFinal;
}

// L'apercu donne EXACTEMENT les lignes changees : chaque ligne citee est bien a ce
// numero avant/apres, et les lignes non citees, dans l'ordre, sont identiques.
bool previewExact(const std::vector<std::string>& before, const std::vector<std::string>& after,
    const std::vector<StLineChange>& changes) {
    std::map<domain::Index, std::vector<const StLineChange*>> bySection;
    for (const auto& c : changes) bySection[c.section].push_back(&c);
    for (std::size_t s = 0; s < before.size(); ++s) {
        const auto it = bySection.find(s);
        if (it == bySection.end()) {
            if (before[s] != after[s]) return false;   // une section changee non citee
            continue;
        }
        const auto a = splitLines(before[s]);
        const auto b = splitLines(after[s]);
        std::vector<bool> gone(a.size(), false), come(b.size(), false);
        for (const auto* c : it->second) {
            if (c->lineBefore) {
                if (c->lineBefore > a.size()) return false;
                auto line = a[c->lineBefore - 1];
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line != c->before) return false;
                gone[c->lineBefore - 1] = true;
            }
            if (c->lineAfter) {
                if (c->lineAfter > b.size()) return false;
                auto line = b[c->lineAfter - 1];
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line != c->after) return false;
                come[c->lineAfter - 1] = true;
            }
            if (c->lineBefore && c->lineAfter && c->before == c->after) return false;   // pas de faux changement
        }
        std::vector<std::string> keptA, keptB;
        for (std::size_t i = 0; i < a.size(); ++i) if (!gone[i]) keptA.push_back(a[i]);
        for (std::size_t i = 0; i < b.size(); ++i) if (!come[i]) keptB.push_back(b[i]);
        if (keptA != keptB) return false;
    }
    return true;
}

bool samePreview(const std::vector<StLineChange>& x, const std::vector<StLineChange>& y) {
    if (x.size() != y.size()) return false;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (x[i].section != y[i].section || x[i].lineBefore != y[i].lineBefore
            || x[i].lineAfter != y[i].lineAfter || x[i].before != y[i].before || x[i].after != y[i].after)
            return false;
    return true;
}

// Apres execute() : l'apercu demande avant, le texte d'apres, le retour octet pour octet.
void checkAppliedThenUndo(const std::shared_ptr<domain::Project>& p, core::ICommand& cmd,
    const std::vector<StLineChange>& previewed, const std::vector<std::string>& original) {
    const auto now = bodies(*p);
    std::vector<StLineChange> actual;
    for (std::size_t s = 0; s < now.size(); ++s)
        if (now[s] != original[s]) {
            auto part = project::diffSectionText(s, std::string(p->strings.text(p->sections[s].name)),
                original[s], now[s]);
            actual.insert(actual.end(), part.begin(), part.end());
        }
    CHECK(samePreview(previewed, actual));
    const bool exact = previewExact(original, now, previewed);
    CHECK(exact);
    if (exact) ++n.previewExact;
    n.previewLines += static_cast<int>(previewed.size());
    CHECK(cmd.undo().has_value());
    const bool back = bodies(*p) == original;
    CHECK(back);
    if (back) ++n.undoExact;
    // lineCount suit le texte
    for (const auto& s : p->sections)
        if (!s.body.empty()) CHECK(s.lineCount == static_cast<std::uint32_t>(
            std::count(s.body.begin(), s.body.end(), '\n') + 1));
}

const Action* actionOf(const Chart& c, int id) {
    for (const auto& a : c.actions) if (a.id == id) return &a;
    return nullptr;
}
const Transition* transOf(const Chart& c, int id) {
    for (const auto& t : c.transitions) if (t.id == id) return &t;
    return nullptr;
}

// ---- 1. supprimer chaque transition ---------------------------------------------
void removeTransitions(const std::shared_ptr<domain::Project>& p, const Chart& c,
    const std::vector<std::string>& original) {
    const std::string trans = "Trans_" + c.arrayPrefix;
    const auto beforeChecks = checkCounts(c, *p);
    for (const auto& t : c.transitions) {
        project::RemoveTransitionCommand cmd(p, c.section, t.id);
        std::string error;
        const auto previewed = cmd.preview(&error);
        const auto said = cmd.consequences();
        CHECK(bodies(*p) == original);   // l'apercu ne touche a rien
        auto st = cmd.execute();
        if (!st) {
            ++n.transRefused;
            CHECK(!error.empty());
            CHECK(bodies(*p) == original);
            std::printf("  refus T%d de %s : %s\n", t.id, c.name.c_str(), error.c_str());
            continue;
        }
        ++n.transDone;
        CHECK(error.empty());
        const auto after = findChart(*p, c.section);
        CHECK(after.transitions.size() + 1 == c.transitions.size());
        CHECK(after.steps.size() == c.steps.size());
        CHECK(after.actions.size() == c.actions.size());
        for (std::size_t i = 0; i < after.transitions.size(); ++i)
            CHECK(after.transitions[i].id == static_cast<int>(i));
        for (const auto& o : c.transitions) {
            if (o.id == t.id) continue;
            const auto* m = transOf(after, o.id > t.id ? o.id - 1 : o.id);
            CHECK(m != nullptr);
            if (!m) continue;
            CHECK(m->sources == o.sources);
            CHECK(m->destinations == o.destinations);
            CHECK(m->conditionText == o.conditionText);
            CHECK(m->delay == o.delay);
            CHECK(m->conditionExpr == renumbered(o.conditionExpr, trans, t.id));
        }
        for (std::size_t i = 0; i < c.steps.size() && i < after.steps.size(); ++i)
            CHECK(sameStep(c.steps[i], after.steps[i]));
        for (std::size_t i = 0; i < c.actions.size() && i < after.actions.size(); ++i) {
            CHECK(after.actions[i].name == c.actions[i].name);
            CHECK(after.actions[i].kind == c.actions[i].kind);
            CHECK(after.actions[i].boundStep == c.actions[i].boundStep);
            CHECK(after.actions[i].body == c.actions[i].body);
            CHECK(after.actions[i].enableExpr == renumbered(c.actions[i].enableExpr, trans, t.id));
        }
        CHECK(structureWarnings(after) <= structureWarnings(c));
        CHECK(after.actionSection == c.actionSection);
        CHECK(onlyExpectedNewChecks(beforeChecks, checkCounts(after, *p),
            { CheckKind::NoExitNotFinal, CheckKind::Unreachable }));
        // consequences() : exactement les etapes (non finales) sans sortie et (non
        // initiales) sans entree que la suppression cree, recomptees ici sur le grafcet relu.
        {
            std::vector<std::string> expected;
            auto count = [&](const Chart& ch, int step, bool asSource) {
                int k = 0;
                for (const auto& o : ch.transitions) {
                    const auto& ends = asSource ? o.sources : o.destinations;
                    if (std::find(ends.begin(), ends.end(), step) != ends.end()) ++k;
                }
                return k;
            };
            std::vector<int> seen;
            for (int s : t.sources) {
                const auto* st2 = c.stepById(s);
                if (!st2 || st2->isFinal || std::find(seen.begin(), seen.end(), s) != seen.end()) continue;
                seen.push_back(s);
                if (count(after, s, true) == 0 && count(c, s, true) > 0)
                    expected.push_back("X" + std::to_string(s) + " ");
            }
            seen.clear();
            for (int d : t.destinations) {
                const auto* st2 = c.stepById(d);
                if (!st2 || st2->initial || std::find(seen.begin(), seen.end(), d) != seen.end()) continue;
                seen.push_back(d);
                if (count(after, d, false) == 0 && count(c, d, false) > 0)
                    expected.push_back("X" + std::to_string(d) + " ");
            }
            CHECK(said.size() == expected.size());
            for (std::size_t i = 0; i < said.size() && i < expected.size(); ++i)
                CHECK(said[i].rfind(expected[i], 0) == 0);
            n.consequences += static_cast<int>(said.size());
        }
        checkAppliedThenUndo(p, cmd, previewed, original);
    }
}

// ---- 2. supprimer chaque action --------------------------------------------------
void removeActions(const std::shared_ptr<domain::Project>& p, const Chart& c,
    const std::vector<std::string>& original) {
    const std::string acts = "Acts_" + c.arrayPrefix;
    const auto beforeChecks = checkCounts(c, *p);
    for (const auto& a : c.actions) {
        project::RemoveActionCommand cmd(p, c.section, a.id);
        std::string error;
        const auto previewed = cmd.preview(&error);
        const auto used = cmd.usedBy();
        CHECK(bodies(*p) == original);
        auto st = cmd.execute();
        if (!st) {
            ++n.actRefused;
            CHECK(!error.empty());
            std::printf("  refus A%d de %s : %s\n", a.id, c.name.c_str(), error.c_str());
            CHECK(!used.empty());
            CHECK(bodies(*p) == original);
            // Le refus est fonde : Acts_X[id] est bien nomme hors de ses lignes.
            const std::string mark = acts + "[" + std::to_string(a.id) + "]";
            bool inCondition = false;
            for (const auto& t : c.transitions)
                if (t.conditionExpr.find(mark) != std::string::npos) inCondition = true;
            for (const auto& o : c.actions)
                if (o.id != a.id && (o.enableExpr.find(mark) != std::string::npos
                    || o.body.find(mark) != std::string::npos)) inCondition = true;
            bool elsewhere = false;
            for (std::size_t s = 0; s < p->sections.size(); ++s)
                if (s != c.section && s != c.actionSection
                    && p->sections[s].body.find(mark) != std::string::npos) elsewhere = true;
            CHECK(inCondition || elsewhere);
            if (inCondition || elsewhere) ++n.actRefusedJustified;
            continue;
        }
        ++n.actDone;
        CHECK(used.empty());
        const auto after = findChart(*p, c.section);
        CHECK(after.actions.size() + 1 == c.actions.size());
        CHECK(after.transitions.size() == c.transitions.size());
        CHECK(after.steps.size() == c.steps.size());
        for (std::size_t i = 0; i < after.actions.size(); ++i)
            CHECK(after.actions[i].id == static_cast<int>(i));
        for (const auto& o : c.actions) {
            if (o.id == a.id) continue;
            const auto* m = actionOf(after, o.id > a.id ? o.id - 1 : o.id);
            CHECK(m != nullptr);
            if (!m) continue;
            CHECK(m->name == o.name);
            CHECK(m->kind == o.kind);
            CHECK(m->boundStep == o.boundStep);
            CHECK(m->delay == o.delay);
            CHECK(m->enableExpr == renumbered(o.enableExpr, acts, a.id));
            CHECK(m->body == renumbered(o.body, acts, a.id));
        }
        for (std::size_t i = 0; i < c.transitions.size() && i < after.transitions.size(); ++i) {
            CHECK(after.transitions[i].sources == c.transitions[i].sources);
            CHECK(after.transitions[i].destinations == c.transitions[i].destinations);
            CHECK(after.transitions[i].conditionExpr
                == renumbered(c.transitions[i].conditionExpr, acts, a.id));
        }
        for (std::size_t i = 0; i < c.steps.size() && i < after.steps.size(); ++i)
            CHECK(sameStep(c.steps[i], after.steps[i]));
        CHECK(structureWarnings(after) <= structureWarnings(c));
        CHECK(onlyExpectedNewChecks(beforeChecks, checkCounts(after, *p), {}));
        // Les autres sections de l'unite suivent (SFC_DEBUG...).
        for (std::size_t s = 0; s < p->sections.size(); ++s)
            if (s != c.section && s != c.actionSection && original[s] != p->sections[s].body)
                CHECK(p->sections[s].body == renumbered(original[s], acts, a.id));
        checkAppliedThenUndo(p, cmd, previewed, original);
    }
}

// ---- 3. modifier chaque action -----------------------------------------------------
void modifyActions(const std::shared_ptr<domain::Project>& p, const Chart& c,
    const std::vector<std::string>& original) {
    const auto beforeChecks = checkCounts(c, *p);
    for (const auto& a : c.actions) {
        int otherStep = -1;
        for (const auto& s : c.steps) if (s.id != a.boundStep) { otherStep = s.id; break; }

        std::vector<project::ActionChange> variants;
        auto v = project::ActionChange::from(a);
        v.kind = (a.kind == ActionKind::Continuous) ? ActionKind::Rising : ActionKind::Continuous;
        variants.push_back(v);                                   // le genre
        v = project::ActionChange::from(a);
        v.kind = (a.kind == ActionKind::DelayOn) ? ActionKind::LimitedOn : ActionKind::DelayOn;
        v.delay = "t#3s";
        variants.push_back(v);                                   // genre retarde + retard
        v = project::ActionChange::from(a);
        v.delay = "T#12S";
        if (!usesDelay(v.kind)) v.kind = ActionKind::LimitedOn;
        variants.push_back(v);                                   // le retard
        v = project::ActionChange::from(a);
        v.name = "R2_" + std::to_string(a.id);
        variants.push_back(v);                                   // le nom
        if (otherStep >= 0) {
            v = project::ActionChange::from(a);
            v.boundStep = otherStep;
            variants.push_back(v);                               // l'etape
            v.name = "TOUT" + std::to_string(a.id);
            v.kind = ActionKind::PulseOn;
            v.delay = "t#250ms";
            variants.push_back(v);                               // tout a la fois
        }

        for (const auto& change : variants) {
            project::ModifyActionCommand cmd(p, c.section, a.id, change);
            std::string error;
            const auto previewed = cmd.preview(&error);
            auto st = cmd.execute();
            CHECK(st.has_value());
            if (!st) {
                ++n.modRefused;
                std::printf("  refus modif A%d de %s : %s\n", a.id, c.name.c_str(), error.c_str());
                continue;
            }
            ++n.modDone;
            const auto after = findChart(*p, c.section);
            CHECK(after.actions.size() == c.actions.size());
            const auto* m = actionOf(after, a.id);
            CHECK(m != nullptr);
            if (m) {
                CHECK(m->name == change.name);
                CHECK(m->kind == change.kind);
                CHECK(m->boundStep == change.boundStep);
                CHECK(m->delay == (change.delay.empty() ? a.delay : change.delay));
                CHECK(m->body == a.body);
                CHECK(m->enableExpr == a.enableExpr);
            }
            for (const auto& o : c.actions) {
                if (o.id == a.id) continue;
                const auto* x = actionOf(after, o.id);
                CHECK(x && x->name == o.name && x->kind == o.kind && x->boundStep == o.boundStep
                    && x->delay == o.delay && x->body == o.body && x->enableExpr == o.enableExpr);
            }
            CHECK(after.transitions.size() == c.transitions.size());
            for (std::size_t i = 0; i < c.transitions.size() && i < after.transitions.size(); ++i)
                CHECK(after.transitions[i].conditionExpr == c.transitions[i].conditionExpr
                    && after.transitions[i].sources == c.transitions[i].sources
                    && after.transitions[i].destinations == c.transitions[i].destinations);
            CHECK(after.steps.size() == c.steps.size());
            for (std::size_t i = 0; i < c.steps.size() && i < after.steps.size(); ++i)
                CHECK(sameStep(c.steps[i], after.steps[i]));
            CHECK(structureWarnings(after) == structureWarnings(c));
            CHECK(onlyExpectedNewChecks(beforeChecks, checkCounts(after, *p),
                { CheckKind::FinOfContinuous }));
            // Seule la section du grafcet change ici (pas de commentaires generes dans le projet).
            for (const auto& ch : previewed) CHECK(ch.section == c.section);
            checkAppliedThenUndo(p, cmd, previewed, original);
        }

        // Rien ne change -> aucune ligne, et execute() n'ecrit rien.
        project::ModifyActionCommand same(p, c.section, a.id, project::ActionChange::from(a));
        CHECK(same.preview().empty());
        CHECK(same.execute().has_value());
        CHECK(bodies(*p) == original);
        CHECK(same.undo().has_value());
    }
}

// ---- 4. inserer puis supprimer -> le texte d'avant ----------------------------------
void insertThenRemove(const std::shared_ptr<domain::Project>& p, const Chart& c,
    const std::vector<std::string>& original) {
    if (c.steps.empty()) return;
    const int s0 = c.steps.front().id, s1 = c.steps.back().id;
    for (int at : { 0, static_cast<int>(c.transitions.size()) }) {
        project::InsertTransitionCommand ins(p, c.section, at, { s0 }, { s1 }, "R2", "TRUE");
        if (!ins.execute()) continue;   // grafcet plein : rien a verifier
        project::RemoveTransitionCommand rm(p, c.section, at);
        CHECK(rm.execute().has_value());
        const bool same = bodies(*p) == original;
        CHECK(same);
        if (same) ++n.roundTrips;
        CHECK(rm.undo().has_value());
        CHECK(ins.undo().has_value());
        CHECK(bodies(*p) == original);
    }
    {
        const int at = static_cast<int>(c.actions.size());
        project::InsertActionCommand ins(p, c.section, at, "R2ACT", ActionKind::DelayOn, s0,
            "x := TRUE;", "t#2s");
        if (ins.execute()) {
            const auto mid = findChart(*p, c.section);
            const auto* added = actionOf(mid, at);
            CHECK(added && added->name == "R2ACT" && added->delay == "t#2s");
            // modifier l'action inseree : ses commentaires suivent (genre, etape)
            auto change = project::ActionChange::from(*added);
            change.kind = ActionKind::LimitedOn;
            change.boundStep = s1;
            project::ModifyActionCommand mod(p, c.section, at, change);
            const auto previewed = mod.preview();
            CHECK(mod.execute().has_value());
            bool commentFollows = false;
            for (const auto& ch : previewed)
                if (ch.after.find("(* LIMITEDON - ") != std::string::npos) commentFollows = true;
            CHECK(commentFollows || c.actionSection == domain::kNoIndex);
            CHECK(mod.undo().has_value());

            project::RemoveActionCommand rm(p, c.section, at);
            CHECK(rm.execute().has_value());
            const bool same = bodies(*p) == original;
            CHECK(same);
            if (same) ++n.roundTrips;
            CHECK(rm.undo().has_value());
            CHECK(ins.undo().has_value());
            CHECK(bodies(*p) == original);
        }
    }
}

// ---- 5. les refus ne touchent a rien ------------------------------------------------
void refusals(const std::shared_ptr<domain::Project>& p, const Chart& c,
    const std::vector<std::string>& original) {
    auto refused = [&](core::ICommand& cmd) {
        auto st = cmd.execute();
        CHECK(!st.has_value());
        if (!st) {
            CHECK(!st.error().context.empty());
            ++n.refusals;
        }
        else {
            std::printf("  pas de refus : %s\n", cmd.label().c_str());
            (void)cmd.undo();
        }
        CHECK(bodies(*p) == original);
    };
    project::RemoveTransitionCommand t(p, c.section, 999); refused(t);
    project::RemoveActionCommand a(p, c.section, 999); refused(a);
    if (c.actions.empty()) return;
    const auto& first = c.actions.front();
    auto change = project::ActionChange::from(first);
    change.name = "NEUFCHARS";                       // 9 caracteres
    { project::ModifyActionCommand m(p, c.section, first.id, change); refused(m); }
    change = project::ActionChange::from(first);
    change.boundStep = 999;
    { project::ModifyActionCommand m(p, c.section, first.id, change); refused(m); }
    change = project::ActionChange::from(first);
    change.kind = ActionKind::DelayOn;
    change.delay = "";
    { project::ModifyActionCommand m(p, c.section, first.id, change); refused(m); }
    change.delay = "t#1s; x := 1";
    { project::ModifyActionCommand m(p, c.section, first.id, change); refused(m); }
    change = project::ActionChange::from(first);
    change.name = "A|B";
    { project::ModifyActionCommand m(p, c.section, first.id, change); refused(m); }
    { project::RemoveActionCommand m(p, static_cast<domain::Index>(p->sections.size() + 3), 0); refused(m); }
}

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/home/claude/v190/fixtures/MAST.XPG";
    core::EventBus bus;
    importer::ProjectImporter importer(bus);
    auto imported = importer.importFile(path);
    CHECK(imported.has_value());
    if (!imported) { std::printf("projet illisible : %s\n", path); return 1; }
    auto p = imported->project;
    const auto original = bodies(*p);
    const auto charts = findCharts(*p);
    CHECK(charts.size() == 17);
    std::size_t steps = 0, transitions = 0, actions = 0;
    for (const auto& c : charts) {
        steps += c.steps.size();
        transitions += c.transitions.size();
        actions += c.actions.size();
    }
    CHECK(steps == 151);
    CHECK(transitions == 193);
    CHECK(actions == 189);

    for (const auto& c : charts) {
        removeTransitions(p, c, original);
        removeActions(p, c, original);
        modifyActions(p, c, original);
        insertThenRemove(p, c, original);
        refusals(p, c, original);
        CHECK(bodies(*p) == original);
    }

    // L'apercu generique (une commande de GrafcetRewrite.hpp) ne laisse rien derriere lui.
    {
        const auto& c = charts.front();
        project::InsertStepCommand ins(p, c.section, static_cast<int>(c.steps.size()), "R2");
        std::vector<StLineChange> lines;
        std::string error;
        CHECK(project::previewCommand(*p, ins, lines, error));
        CHECK(!lines.empty());
        CHECK(error.empty());
        CHECK(bodies(*p) == original);
        bool added = false;
        for (const auto& l : lines) if (l.added() && l.after.find("id=") != std::string::npos) added = true;
        CHECK(added);
    }

    // 1.10 (decision 11 bis) : shiftIds suit les autres sections de l'unite (SFC_DEBUG).
    // Inserer en tete (etape, action, transition) puis supprimer -> le texte d'avant,
    // octet pour octet, sur les 17 grafcets ; SFC_DEBUG decale d'un cran entre les deux.
    for (const auto& c : charts) {
        domain::Index debug = domain::kNoIndex;   // le SFC_DEBUG de l'unite du grafcet
        for (std::size_t s = 0; s < p->sections.size(); ++s)
            if (p->strings.text(p->sections[s].name) == "SFC_DEBUG"
                && p->sections[s].owner == p->sections[c.section].owner) debug = s;
        auto up = [](const std::string& text, const std::string& array) {
            // array[k] -> array[k+1] pour tout k (insertion en 0)
            std::string out;
            std::size_t at = 0;
            while (true) {
                const auto f = text.find(array + "[", at);
                if (f == std::string::npos) { out += text.substr(at); break; }
                const auto open = f + array.size() + 1;
                const auto close = text.find(']', open);
                out += text.substr(at, open - at);
                if (close == std::string::npos) { at = open; continue; }
                const auto digits = text.substr(open, close - open);
                const bool num = !digits.empty() && std::all_of(digits.begin(), digits.end(),
                    [](unsigned char ch) { return std::isdigit(ch) != 0; });
                out += num ? std::to_string(std::atoi(digits.c_str()) + 1) : digits;
                at = close;
            }
            return out;
        };
        const bool debugNames = debug != domain::kNoIndex
            && p->sections[debug].owner == p->sections[c.section].owner;
        {   // une action en tete
            project::InsertActionCommand ins(p, c.section, 0, "R2", ActionKind::Continuous, c.steps.front().id, "");
            if (ins.execute()) {
                const std::string acts = "Acts_" + c.arrayPrefix;
                if (debugNames && original[debug].find(acts + "[") != std::string::npos) {
                    CHECK(p->sections[debug].body == up(original[debug], acts));
                    ++n.debugFollowed;
                }
                project::RemoveActionCommand rm(p, c.section, 0);
                CHECK(rm.execute().has_value());
                const bool same = bodies(*p) == original;
                CHECK(same);
                if (same) ++n.roundTripsHead;
                CHECK(rm.undo().has_value());
                CHECK(ins.undo().has_value());
                CHECK(bodies(*p) == original);
            }
        }
        {   // une etape en tete, puis la meme supprimee (renumerotee)
            project::InsertStepCommand ins(p, c.section, 0, "R2");
            if (ins.execute()) {
                const std::string stepArray = "Steps_" + c.arrayPrefix;
                if (debugNames && original[debug].find(stepArray + "[") != std::string::npos) {
                    CHECK(p->sections[debug].body == up(original[debug], stepArray));
                    ++n.debugFollowed;
                }
                project::RemoveStepCommand rm(p, c.section, 0, true);
                CHECK(rm.execute().has_value());
                const bool same = bodies(*p) == original;
                CHECK(same);
                if (same) ++n.roundTripsHead;
                CHECK(rm.undo().has_value());
                CHECK(ins.undo().has_value());
                CHECK(bodies(*p) == original);
            }
        }
    }

    std::printf("grafcets %zu : %zu etapes, %zu transitions, %zu actions\n",
        charts.size(), steps, transitions, actions);
    std::printf("transitions supprimees : %d (refusees %d)\n", n.transDone, n.transRefused);
    std::printf("actions supprimees : %d (refusees %d, refus fondes %d)\n",
        n.actDone, n.actRefused, n.actRefusedJustified);
    std::printf("actions modifiees : %d (refusees %d)\n", n.modDone, n.modRefused);
    std::printf("apercus exacts : %d (%d lignes en tout) ; defaire octet pour octet : %d\n",
        n.previewExact, n.previewLines, n.undoExact);
    std::printf("inserer puis supprimer -> texte d'avant : %d ; refus sans effet : %d\n",
        n.roundTrips, n.refusals);
    std::printf("shiftIds (1.9 corrige) : SFC_DEBUG suit %d insertions en tete ; "
        "inserer en tete puis supprimer -> texte d'avant : %d\n", n.debugFollowed, n.roundTripsHead);
    std::printf("consequences annoncees avant suppression (sans sortie, plus atteinte) : %d\n",
        n.consequences);
    std::printf("controles de R apparus (attendus : sans sortie, jamais atteinte, FIN d'une continue) : %d\n",
        n.expectedNewChecks);
    std::printf("%d controles, %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
