// =============================================================================
//  tests/ai_missions_test.cpp - 1.22.0 : le noyau des missions IA persistantes
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau, sur un vrai projet IHM (un Document, sa pile Ctrl+Z) :
//    - les etats d'une etape et leurs transitions controlees ; les cles du fichier ;
//    - le plan (planifier_mission) : les dependances ecrites et implicites, l'ordre calcule (jamais celui du modele),
//      un cycle, une dependance inconnue (refuses, la mission intacte) ; le plan revu FUSIONNE (rien de reinitialise) ;
//      les modifications de l'utilisateur (ajouter, ignorer, retirer, dependances) ;
//    - le prochain lot (regroupement sous la longueur d'une reponse) ;
//    - LE SUIVI REEL : une proposition generee, verifiee, appliquee (une commande), son compte rendu lu dans le projet ;
//      les etapes terminees seulement si ce qu'elles creent est dans le projet ; Ctrl+Z (la proposition annulee, les
//      etapes de nouveau pretes), Ctrl+Y (retablie) ; une validation en erreur ; une proposition refusee ;
//    - la progression (la regle) ; le rapport final (avec et sans audit) ;
//    - le fichier : aller-retour, migration d'un format 0, un format plus recent (lu, jamais reecrit) ;
//    - le disque : ecriture atomique avec copie, un fichier incomplet repris de sa copie, un fichier seul en .bak ;
//    - l'empreinte du projet : identique apres relecture, << les variables (modifiees) >>, << la vue X (ajoutee) >> ;
//    - LA REPRISE : cas A (projet identique), B (modifie : ce qui a change), C (introuvable) ; un appel a l'IA
//      interrompu pendant la 3e proposition (rien n'est rejoue) ;
//    - pause, reprise, annulation ; la recherche et les outils chercher_missions / etat_mission ;
//    - 1.22.2 : forcer la fin (les etapes ignorees, les propositions refusees, le rapport, le fichier).
// =============================================================================
#include "../src/app/ai/AiMission.hpp"
#include "../src/app/ai/AiMissionStore.hpp"
#include "../src/app/ai/AiOperations.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPlan.hpp"
#include "../src/core/AtomicFile.hpp"
#include "../src/core/Command.hpp"
#include "../src/hmi/HmiAudit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ai = app::ai;
namespace mi = app::ai::mission;
namespace plan = app::ai::plan;
namespace fs = std::filesystem;
using A = ai::Operation::Action;
using S = mi::StepState;

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
bool anyHas(const std::vector<std::string>& list, const std::string& what) {
    return std::any_of(list.begin(), list.end(), [&](const std::string& s) { return has(s, what); });
}
std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += x + " | ";
    return s;
}
std::string order(const mi::Mission& m) {
    std::string s;
    for (const auto& st : m.steps) s += st.id + " ";
    return s;
}
std::size_t pos(const mi::Mission& m, const std::string& id) {
    for (std::size_t i = 0; i < m.steps.size(); ++i)
        if (m.steps[i].id == id) return i;
    return 999;
}
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

const std::string kNow = "2026-11-01T10:00:00";

// Un vrai projet : la vue Vue_Process (ouverte), un type T_Moteur, deux variables.
struct Fixture {
    hmi::DocumentPtr   doc = std::make_shared<hmi::Document>();
    core::CommandStack stack;
    hmi::Id            view{hmi::kNoId};

    Fixture() {
        auto& p = doc->project;
        p.config.width = 1280;
        p.config.height = 800;
        p.views = {hmi::makeView(p, "Vue_Process")};
        view = p.views[0].id;
        const auto var = [&](const char* name, const char* type) {
            hmi::Variable v;
            v.id = p.allocate();
            v.name = name;
            v.type = type;
            p.programs.variables.push_back(v);
        };
        var("Pompe_1_Marche", "BOOL");
        var("Consigne_Generale", "REAL");
        hmi::HmiType t;
        t.id = p.allocate();
        t.name = "T_Moteur";
        t.members = {{"Marche", "BOOL"}, {"Vitesse", "REAL"}};
        p.programs.types.push_back(t);
    }
    [[nodiscard]] ai::Proposal proposal(std::string id, std::vector<ai::Operation> ops) const {
        ai::Proposal p;
        p.id = std::move(id);
        p.request = "mission";
        p.summary = "Essai " + p.id;
        p.ops = std::move(ops);
        ai::scopeProposal(p, doc->project.view(view));
        p.fingerprint = ai::proposalPrint(doc->project, p);
        return p;
    }
    // Generer -> verifier -> appliquer (une commande) -> le compte rendu reel -> la mission le sait.
    bool run(mi::Mission& m, ai::Proposal& p, const std::vector<std::string>& steps, std::string* why = nullptr) {
        mi::onGenerated(m, p, "c1", "m" + std::to_string(m.proposals.size() + 2), steps, kNow);
        const auto ck = ai::validate(doc->project, p);
        mi::onChecked(m, p.id, ck, kNow);
        if (!ck.ok()) {
            if (why) *why = joined(ck.errors);
            return false;
        }
        const hmi::Project before = doc->project;
        plan::Plan pl = plan::build(doc->project, p, ck);
        auto cmd = ai::makeCommand(doc, p, why);
        if (!cmd || !stack.push(std::move(cmd))) return false;
        const plan::Report rep = plan::report(before, doc->project, pl, p);
        mi::onApplied(m, p.id, pl, rep, doc->project, kNow);
        return true;
    }
};

ai::Operation op(A a, std::string name, std::vector<std::pair<std::string, std::string>> props = {}, std::string step = {}) {
    ai::Operation o;
    o.action = a;
    o.name = std::move(name);
    o.props = std::move(props);
    o.step = std::move(step);
    return o;
}
ai::Operation create(std::string kind, std::string name, std::vector<std::pair<std::string, std::string>> props, std::string target,
                     std::string step) {
    ai::Operation o = op(A::Create, std::move(name), std::move(props), std::move(step));
    o.kind = std::move(kind);
    o.target = std::move(target);
    return o;
}
ai::Operation vars(std::vector<ai::Operation::VariableSpec> v, std::string step) {
    ai::Operation o = op(A::Variables, {}, {}, std::move(step));
    o.variables = std::move(v);
    return o;
}
ai::Operation type(std::string name, std::vector<ai::Operation::MemberSpec> mem, std::string step) {
    ai::Operation o = op(A::Type, std::move(name), {}, std::move(step));
    o.members = std::move(mem);
    return o;
}

// Le plan de la station de pompage, ecrit DANS LE DESORDRE (les vues avant les variables, l'analyse a la fin).
const char* kStationPlan = R"({
  "nom": "Station de pompage",
  "description": "Deux pompes, une cuve, les equipements Modbus, les variables, les alarmes et la vue.",
  "analyse": {"conforme": ["type:T_Moteur - deja conforme"], "a_creer": ["type:T_PompeA", "variable:Pompe_A"],
              "impossible": ["La communication reelle avec le materiel"], "resume": "Un type existe, le reste est a creer."},
  "etapes": [
    {"id": "S6", "titre": "Poser les objets de la vue", "categorie": "objets", "operations": 2},
    {"id": "S5", "titre": "Creer la vue de la station", "categorie": "vues", "operations": 1, "produit": ["vue:Vue_Pompe_A"]},
    {"id": "S4", "titre": "Creer les alarmes", "categorie": "alarmes", "operations": 1},
    {"id": "S3", "titre": "Creer les variables", "categorie": "variables", "operations": 1, "depend_de": ["S2"]},
    {"id": "S2", "titre": "Creer le type de la pompe", "categorie": "types", "operations": 1},
    {"id": "S7", "titre": "Verifier le projet", "categorie": "verification", "operations": 0},
    {"id": "S1", "titre": "Analyser le projet", "categorie": "analyse", "operations": 0}
  ]
})";

// ---- les etats ------------------------------------------------------------------------------------------------------
void states() {
    std::printf("les etats\n");
    const S all[] = {S::ToPlan, S::Waiting, S::Ready, S::Running, S::AwaitingValidation, S::Applied,
                     S::Verifying, S::Done, S::Error, S::Blocked, S::Skipped, S::Cancelled};
    for (const auto s : all) {
        check(mi::stepFromKey(mi::stepKey(s)) == s, "cle " + std::string(mi::stepKey(s)) + " relue");
        check(mi::stepFromKey(mi::stepLabel(s)) == s, "libelle " + std::string(mi::stepLabel(s)) + " relu");
        check(mi::canMove(s, s), "rester dans le meme etat");
    }
    check(mi::stepLabel(S::Ready) == "Pr\xC3\xAAte" && mi::stepLabel(S::AwaitingValidation) == "En attente de validation", "les libelles");
    check(mi::canMove(S::Waiting, S::Ready) && mi::canMove(S::Ready, S::Running) && mi::canMove(S::Running, S::AwaitingValidation)
              && mi::canMove(S::AwaitingValidation, S::Applied) && mi::canMove(S::Applied, S::Done),
          "le chemin normal");
    check(!mi::canMove(S::Waiting, S::Done) && !mi::canMove(S::Ready, S::Done) && !mi::canMove(S::Waiting, S::Applied),
          "jamais terminee sans etre appliquee");
    check(!mi::canMove(S::Cancelled, S::Ready) && !mi::canMove(S::Done, S::Error) && mi::canMove(S::Done, S::Ready),
          "annulee est definitive ; terminee ne revient prete que si le projet a perdu son travail");
    check(mi::isFinished(S::Done) && mi::isFinished(S::Skipped) && !mi::isFinished(S::Applied), "finies : terminee, ignoree");
    check(mi::isBlocking(S::Error) && mi::isBlocking(S::Blocked) && mi::isBlocking(S::Cancelled) && !mi::isBlocking(S::Waiting), "bloquantes");
    check(mi::stateFromKey(mi::stateKey(mi::State::Paused)) == mi::State::Paused && mi::coherenceFromKey("modifie") == mi::Coherence::Changed,
          "les etats de mission et la coherence relus");
    check(mi::categories().size() == 16 && mi::categoryLabel("equipements") == "\xC3\x89quipements", "les categories");
}

// ---- le plan --------------------------------------------------------------------------------------------------------
void planning() {
    std::printf("le plan\n");
    const std::string request =
        "Construis une station de pompage compl\xC3\xA8te avec deux pompes, une cuve, les \xC3\xA9quipements Modbus, les variables, les "
        "alarmes, les scripts et les vues.";
    check(mi::looksComplex(request), "une demande complexe : une mission est proposee");
    check(!mi::looksComplex("Ajoute un bouton Marche"), "une demande simple : pas de mission");
    check(!mi::looksComplex("O\xC3\xB9 en est la station de pompage ?"), "une question : jamais une mission");
    mi::Mission m = mi::make(request, "c1", kNow);
    check(has(m.id, "mission-") && m.state == mi::State::Planning && m.request == request && m.conversations.size() == 1
              && m.history.size() == 1 && m.history[0].kind == "creation",
          "une mission neuve : identifiant, demande exacte, planification, historique");
    mi::Mission other = mi::make(request, "c1", kNow);
    check(other.id != m.id, "deux missions, deux identifiants");
    const mi::Batch none = mi::nextBatch(m, 10);
    check(none.steps.empty() && has(none.why, "plan"), "pas de lot avant le plan");

    std::string why;
    check(mi::planFromTool(m, kStationPlan, kNow, &why), "le plan lu (" + why + ")");
    check(m.name == "Station de pompage" && m.state == mi::State::Active && m.steps.size() == 7, "nom, etat en cours, 7 etapes");
    check(pos(m, "S1") == 0 && pos(m, "S7") == 6, "l'analyse d'abord, la verification a la fin (" + order(m) + ")");
    check(pos(m, "S2") < pos(m, "S3") && pos(m, "S3") < pos(m, "S4") && pos(m, "S3") < pos(m, "S5") && pos(m, "S5") < pos(m, "S6"),
          "type < variables < alarmes, vues < objets (" + order(m) + ")");
    const mi::Step* s3 = m.step("S3");
    check(s3 && std::find(s3->implicit.begin(), s3->implicit.end(), "S1") != s3->implicit.end() && s3->deps.size() == 2,
          "les variables : S2 ecrit, S1 implicite (dit)");
    const mi::Step* s7 = m.step("S7");
    check(s7 && s7->deps.size() == 6, "la verification attend toutes les etapes");
    check(m.step("S1")->state == S::Ready && m.step("S2")->state == S::Waiting && has(m.step("S2")->note, "S1"),
          "S1 prete ; S2 attend S1 (dit)");
    check(m.analysis.conform.size() == 1 && m.analysis.impossible.size() == 1, "l'analyse gardee");
    check(m.history.back().kind == "analyse" || m.history[m.history.size() - 2].kind == "plan", "le plan est dans l'historique");

    // les refus : la mission n'est pas touchee
    const mi::Mission keep = m;
    check(!mi::planFromTool(m, R"({"etapes":[{"id":"A","titre":"a","depend_de":["B"]},{"id":"B","titre":"b","depend_de":["A"]}]})", kNow, &why)
              && has(why, "circulaire") && (has(why, "A -> B") || has(why, "B -> A")),
          "un cycle refuse et dit (" + why + ")");
    check(!mi::planFromTool(m, R"({"etapes":[{"id":"A","titre":"a","depend_de":["Z"]}]})", kNow, &why) && has(why, "Z"), "une dependance inconnue refusee");
    check(!mi::planFromTool(m, R"({"etapes":[{"id":"A","titre":"a","depend_de":["A"]}]})", kNow, &why) && has(why, "elle-m\xC3\xAAme"), "elle-meme");
    check(!mi::planFromTool(m, R"({"etapes":[]})", kNow, &why) && !mi::planFromTool(m, "pas du json", kNow, &why)
              && !mi::planFromTool(m, R"({"etapes":[{"detail":"sans titre"}]})", kNow, &why),
          "vide, illisible, sans titre : refuses");
    check(order(m) == order(keep) && m.planRevision == keep.planRevision && m.history.size() == keep.history.size(), "la mission intacte apres les refus");
    check(has(mi::planToolJson(), "planifier_mission") && has(mi::planToolJson(), "depend_de"), "la definition de l'outil");

    // sans identifiants : S1.. dans l'ordre ecrit
    mi::Mission n = mi::make("x", "c1", kNow);
    check(mi::planFromTool(n, R"({"etapes":[{"titre":"un","categorie":"analyse"},{"titre":"deux","categorie":"variables"}]})", kNow, &why)
              && n.step("S1") && n.step("S2") && n.step("S2")->deps.size() == 1,
          "les identifiants donnes, la regle des categories appliquee");

    // le prochain lot
    mi::Batch b = mi::nextBatch(m, 1);
    check(b.steps.size() == 1 && b.steps[0] == "S1", "budget 1 : l'analyse seule");
    b = mi::nextBatch(m, 4);
    check(b.steps.size() >= 3 && b.steps[0] == "S1" && b.ops <= 4, "budget 4 : l'analyse et la suite qui en depend (" + joined(b.steps) + ")");
    check(mi::independent(m, "S4", "S5") && !mi::independent(m, "S3", "S4"), "independantes : alarmes et vue ; pas variables et alarmes");
}

// ---- modifier le plan -------------------------------------------------------------------------------------------------
void editing() {
    std::printf("modifier le plan\n");
    mi::Mission m = mi::make("station", "c1", kNow);
    std::string why, added;
    check(mi::planFromTool(m, kStationPlan, kNow, &why), "plan");
    mi::PlanEdit e;
    e.kind = mi::PlanEdit::Kind::Add;
    e.title = "Cr\xC3\xA9" "er le script de comptage";
    e.category = "scripts";
    e.deps = {"S3"};
    check(mi::editPlan(m, e, kNow, &why, &added) && added == "S8" && m.planRevision == 1 && m.step("S8"), "une etape ajoutee : S8");
    check(pos(m, "S3") < pos(m, "S8"), "rangee apres ses dependances");
    e = {};
    e.kind = mi::PlanEdit::Kind::SetDeps;
    e.step = "S2";
    e.deps = {"S3"};
    check(!mi::editPlan(m, e, kNow, &why) && has(why, "circulaire"), "un cycle refuse (" + why + ")");
    e = {};
    e.kind = mi::PlanEdit::Kind::Skip;
    e.step = "S4";
    check(mi::editPlan(m, e, kNow, &why) && m.step("S4")->state == S::Skipped, "ignorer les alarmes");
    // une etape terminee ne se retire pas
    m.step("S1")->state = S::Done;
    e = {};
    e.kind = mi::PlanEdit::Kind::Remove;
    e.step = "S1";
    check(!mi::editPlan(m, e, kNow, &why) && has(why, "appliqu\xC3\xA9" "e"), "retirer une etape terminee : refuse (" + why + ")");
    e.step = "S8";
    check(mi::editPlan(m, e, kNow, &why) && !m.step("S8"), "retirer une etape non commencee");
    const int rev = m.planRevision;
    // LE PLAN REVU (l'IA, << actualiser >>) : S1 terminee gardee, S4 ignoree gardee, une etape nouvelle S9
    const char* revised = R"({"etapes":[
        {"id":"S1","titre":"Analyser","categorie":"analyse"},
        {"id":"S2","titre":"Creer le type","categorie":"types","operations":1},
        {"id":"S3","titre":"Creer les variables","categorie":"variables","depend_de":["S2"]},
        {"id":"S4","titre":"Creer les alarmes","categorie":"alarmes"},
        {"id":"S9","titre":"Creer les recettes","categorie":"autre"}]})";
    check(mi::planFromTool(m, revised, kNow, &why), "le plan revu (" + why + ")");
    check(m.planRevision == rev + 1 && m.step("S1")->state == S::Done && m.step("S4")->state == S::Skipped && m.step("S9")
              && !m.step("S6") && m.step("S2")->state == S::Ready,
          "revu : terminee et ignoree gardees, S9 ajoutee, S6 non commencee retiree, S2 prete");
    check(m.history.back().kind == "plan_modifie" && has(m.history.back().text, "S9") && has(m.history.back().text, "S6"),
          "l'historique dit ce qui a change : " + m.history.back().text);
}

// ---- le suivi reel ---------------------------------------------------------------------------------------------------
mi::Mission stationOn(Fixture&) {
    mi::Mission m = mi::make("Station de pompage", "c1", kNow);
    std::string why;
    const char* p = R"({"nom":"Station","etapes":[
        {"id":"S1","titre":"Creer le type","categorie":"types","operations":1,"produit":["type:T_PompeA"]},
        {"id":"S2","titre":"Creer les variables","categorie":"variables","operations":1,"depend_de":["S1"]},
        {"id":"S3","titre":"Creer la liaison Modbus","categorie":"equipements","operations":1},
        {"id":"S4","titre":"Creer les alarmes","categorie":"alarmes","operations":1,"depend_de":["S2"]},
        {"id":"S5","titre":"Creer la vue","categorie":"vues","operations":3,"depend_de":["S2"]}]})";
    check(mi::planFromTool(m, p, kNow, &why), "plan de la station (" + why + ")");
    return m;
}

void tracking() {
    std::printf("le suivi reel\n");
    Fixture f;
    mi::Mission m = stationOn(f);
    // P1 : le type et les variables (deux etapes)
    ai::Proposal p1 = f.proposal("op1", {type("T_PompeA", {{"Marche", "BOOL", ""}, {"Defaut", "BOOL", ""}}, "S1"),
                                         vars({{"Pompe_A", "T_PompeA", "La pompe A", ""}}, "S2")});
    mi::onGenerated(m, p1, "c1", "m2", {"S1", "S2"}, kNow);
    check(m.proposals.size() == 1 && m.proposals[0].order == 1 && m.proposals[0].lines.size() == 2 && m.proposals[0].lines[1].step == "S2",
          "P1 : deux operations, chacune avec son etape");
    check(m.step("S1")->state == S::AwaitingValidation && m.step("S2")->state == S::AwaitingValidation, "S1, S2 en attente de validation");
    const mi::Progress gen = mi::progressOf(m);
    check(gen.generation > 0.2 && gen.global == 0.0, "generee : la generation avance, pas la progression globale");
    // pas encore appliquee : rien n'est termine (generee != appliquee)
    check(m.step("S1")->state != S::Done, "generee n'est pas appliquee");
    std::string why;
    m.proposals.clear();
    for (auto& s : m.steps) s.proposals.clear();
    m.step("S1")->state = S::Ready;
    m.step("S2")->state = S::Waiting;
    check(f.run(m, p1, {"S1", "S2"}, &why), "P1 appliquee (" + why + ")");
    const mi::ProposalLink* l1 = m.proposal("op1");
    check(l1 && l1->state == "appliquee" && l1->applied == 2 && l1->failed == 0 && l1->report.isObject(), "P1 appliquee : le compte rendu reel garde");
    check(m.step("S1")->state == S::Done && m.step("S2")->state == S::Done, "S1, S2 terminees (verifiees dans le projet)");
    check(m.step("S4")->state == S::Ready && m.step("S5")->state == S::Ready && m.step("S3")->state == S::Ready, "S3, S4, S5 pretes");
    check(l1 && l1->lines[0].state == "appliquee" && !l1->lines[0].creates.empty() && has(l1->lines[0].creates[0], "T_PompeA"),
          "l'operation sait ce qu'elle a cree");
    mi::Progress pr = mi::progressOf(m);
    check(pr.done == 2 && pr.steps == 5 && pr.percent() == 29, "progression : 2 / 5 etapes, 2 / 7 operations = 29 % (" + pr.text() + ")");
    check(has(mi::progressRule(), "TERMIN\xC3\x89" "ES"), "la regle dite");
    check(has(mi::nextAction(m), "Proposer la suite"), "la prochaine action : la suite");

    // CTRL+Z : la proposition annulee, les etapes de nouveau pretes
    check(static_cast<bool>(f.stack.undo()), "Ctrl+Z");
    check(mi::verifyAgainst(m, f.doc->project, kNow), "la verification voit le changement");
    check(m.proposal("op1")->state == "annulee" && m.step("S1")->state == S::Ready && m.step("S2")->state == S::Waiting,
          "P1 annulee apres coup ; S1 prete, S2 attend S1");
    check(m.step("S4")->state == S::Waiting, "S4 attend de nouveau S2");
    check(static_cast<bool>(f.stack.redo()), "Ctrl+Y");
    check(mi::verifyAgainst(m, f.doc->project, kNow) && m.proposal("op1")->state == "appliquee" && m.step("S1")->state == S::Done
              && m.step("S2")->state == S::Done,
          "P1 retablie : S1, S2 terminees");
    check(!mi::verifyAgainst(m, f.doc->project, kNow), "rien ne change : rien n'est dit");

    // P2 : une alarme sur une variable inventee -> la validation refuse (rien d'applique)
    ai::Proposal bad = f.proposal("op2", {op(A::Alarm, "Defaut_X", {{"condition", "Variable_Inventee"}, {"message", "x"}}, "S4")});
    const hmi::Project before = f.doc->project;
    check(!f.run(m, bad, {"S4"}, &why), "une variable inventee : refusee a la validation (" + why.substr(0, 80) + ")");
    check(m.proposal("op2") && m.proposal("op2")->state == "generee" && !m.proposal("op2")->errors.empty(), "P2 : ses erreurs exactes gardees");
    check(m.step("S4")->state == S::AwaitingValidation, "S4 attend toujours (rien d'applique)");
    mi::onDropped(m, "op2", "refusee", "variable inconnue", kNow);
    check(m.proposal("op2")->state == "refusee" && m.step("S4")->state == S::Ready, "P2 refusee : S4 de nouveau prete");
    check(mi::printOf(before).all == mi::printOf(f.doc->project).all, "le projet n'a pas change");

    // P3 : l'alarme correcte, l'equipement et la vue (trois etapes independantes)
    ai::Proposal p3 = f.proposal(
        "op3", {op(A::Alarm, "Defaut_Pompe_A", {{"condition", "Pompe_A.Defaut"}, {"message", "Pompe A en d\xC3\xA9" "faut"}, {"priorite", "2"}}, "S4"),
                op(A::Equipment, "Modbus_A", {{"type", "modbus_tcp"}, {"ip", "127.0.0.1"}, {"port", "5020"}, {"esclave", "1"}, {"simule", "oui"}}, "S3"),
                op(A::View, "Vue_Pompe_A", {{"largeur", "800"}, {"hauteur", "600"}}, "S5"),
                create("Indicator", "Voyant_P", {{"x", "80"}, {"y", "80"}, {"variable", "Pompe_A.Marche"}}, "Vue_Pompe_A", "S5")});
    check(f.run(m, p3, {"S3", "S4", "S5"}, &why), "P3 appliquee (" + why + ")");
    check(m.step("S3")->state == S::Done && m.step("S4")->state == S::Done && m.step("S5")->state == S::Done,
          "S3, S4, S5 terminees : " + std::string(mi::stepLabel(m.step("S5")->state)) + " " + m.step("S5")->note);
    pr = mi::progressOf(m);
    check(pr.done == 5 && pr.percent() == 100, "toutes les etapes terminees (" + pr.text() + ")");
    check(m.state == mi::State::Active && has(mi::nextAction(m), "audit"), "pas terminee sans l'audit final : " + mi::nextAction(m));

    // LE RAPPORT FINAL
    const mi::FinalReport noAudit = mi::finalReport(m, f.doc->project, nullptr, kNow);
    check(!noAudit.complete && !noAudit.auditRun && anyHas(noAudit.toCheck, "pas pu"), "sans audit : jamais << verifie >>");
    const auto audit = hmi::audit::run(f.doc->project, {}, {});
    const mi::FinalReport fr = mi::finalReport(m, f.doc->project, &audit, kNow);
    check(fr.auditRun && fr.complete == (audit.errors == 0), "avec l'audit : complete si 0 erreur bloquante (" + std::to_string(audit.errors) + ")");
    check(anyHas(fr.created, "T_PompeA") && anyHas(fr.created, "Pompe_A") && anyHas(fr.created, "Vue_Pompe_A") && anyHas(fr.created, "Modbus_A"),
          "cree : " + joined(fr.created));
    check(fr.finished.size() == 5 && fr.notDone.empty(), "terminees : 5, non realisees : 0");
    check(anyHas(fr.toCheck, "communication r\xC3\xA9" "elle"), "a verifier : la communication reelle (equipement)");
    m.finalReport = mi::reportToJson(fr);
    mi::refreshState(m, kNow);
    check(m.state == (fr.complete ? mi::State::Done : mi::State::Active), "l'etat suit le rapport");
    const std::string text = mi::reportText(mi::reportFromJson(m.finalReport));
    check(has(text, "Cr\xC3\xA9\xC3\xA9s") && has(text, "R\xC3\xA9utilis\xC3\xA9s") && has(text, "Non r\xC3\xA9" "alis\xC3\xA9s") && has(text, "\xC3\x80 v\xC3\xA9rifier"), "le rapport en clair");
    const std::string detail = mi::detailText(m);
    check(has(detail, "P1") && has(detail, "P3") && has(detail, "Rapport final") && has(detail, "Cr\xC3\xA9\xC3\xA9 par la mission"), "la fiche complete");
    // le contexte de la proposition suivante : ce qui est REELLEMENT dans le projet
    const std::string bp = mi::batchPrompt(m, f.doc->project, mi::Batch{{"S5"}, 1, {}, {}}, "la vue Vue_Process (modifi\xC3\xA9" "e)");
    check(has(bp, "R\xC3\x89" "ELLEMENT DANS LE PROJET") && has(bp, "T_PompeA") && has(bp, "Vue_Process (modifi\xC3\xA9" "e)") && has(bp, "\"etape\""),
          "le contexte de la suite : les elements reels, ce qui a change, la regle << etape >>");
}

void errors() {
    std::printf("une erreur d'application\n");
    Fixture f;
    mi::Mission m = stationOn(f);
    // une operation qui echoue a l'application : simulee par un compte rendu reel en echec
    ai::Proposal p = f.proposal("op9", {type("T_PompeA", {{"Marche", "BOOL", ""}}, "S1")});
    mi::onGenerated(m, p, "c1", "m2", {"S1"}, kNow);
    plan::Plan pl;
    plan::Step st;
    st.id = "E1";
    st.op = 0;
    st.title = "Cr\xC3\xA9" "er le type T_PompeA";
    st.state = plan::State::Failed;
    st.errors = {"un type du m\xC3\xAAme nom existe dans la biblioth\xC3\xA8que"};
    pl.steps.push_back(st);
    plan::Report rep;
    rep.planned = 1;
    rep.failed = 1;
    rep.failures = {"E1 Cr\xC3\xA9" "er le type T_PompeA : un type du m\xC3\xAAme nom existe dans la biblioth\xC3\xA8que"};
    rep.restored = true;
    mi::onApplied(m, "op9", pl, rep, f.doc->project, kNow);
    check(m.proposal("op9")->state == "echouee" && m.step("S1")->state == S::Error && has(m.step("S1")->error, "biblioth\xC3\xA8que"),
          "S1 en erreur, l'erreur exacte : " + m.step("S1")->error);
    check(m.step("S2")->state == S::Blocked && has(m.step("S2")->note, "S1"), "S2 bloquee par S1 (dit)");
    check(m.step("S4")->state == S::Blocked && m.step("S3")->state == S::Ready, "S4 bloquee par S2 ; S3 (independante) prete");
    check(has(mi::nextAction(m), "Corriger S1"), "la prochaine action : corriger S1");
    const std::string cp = mi::correctionPrompt(m, f.doc->project, "S1");
    check(has(cp, "biblioth\xC3\xA8que") && has(cp, "S2") && has(cp, "\"etape\": \"S1\""), "la consigne de correction : l'erreur, les dependantes");
    const mi::Batch b = mi::nextBatch(m, 10);
    check(b.steps.size() == 1 && b.steps[0] == "S3" && !b.blocked.empty(), "le lot : S3 seulement, les bloquees dites");
    // la correction appliquee : S1 terminee, les bloquees se debloquent
    ai::Proposal fix = f.proposal("op10", {type("T_PompeB", {{"Marche", "BOOL", ""}}, "S1")});
    std::string why;
    check(f.run(m, fix, {"S1"}, &why), "la correction appliquee (" + why + ")");
    check(m.step("S1")->state == S::Done && m.step("S2")->state == S::Waiting && has(m.step("S2")->note, "S3") && m.step("S4")->state == S::Waiting,
          "S1 corrigee ; S2 attend encore S3 (des variables dependent des equipements : implicite) ; S4 attend");
}

// ---- pause, annulation -----------------------------------------------------------------------------------------------
void lifecycle() {
    std::printf("pause, reprise, annulation\n");
    Fixture f;
    mi::Mission m = stationOn(f);
    ai::Proposal p = f.proposal("op1", {type("T_PompeA", {{"Marche", "BOOL", ""}}, "S1")});
    mi::onGenerated(m, p, "c1", "m2", {"S1"}, kNow);
    std::string why;
    check(mi::pause(m, kNow, &why) && m.state == mi::State::Paused, "en pause");
    check(!mi::mayApply(m, "op1", &why) && has(why, "pause"), "en pause : rien ne s'applique (" + why + ")");
    check(mi::nextBatch(m, 10).steps.empty(), "en pause : pas de lot");
    check(mi::resume(m, kNow, &why) && m.state == mi::State::Active && mi::mayApply(m, "op1", &why), "reprise");
    check(mi::cancel(m, kNow, &why) && m.state == mi::State::Cancelled, "annulee");
    check(!mi::mayApply(m, "op1", &why) && m.proposal("op1")->state == "annulee", "annulee : sa proposition ne s'applique plus");
    check(std::all_of(m.steps.begin(), m.steps.end(), [](const mi::Step& s) { return s.state == S::Cancelled; }), "les etapes annulees");
    check(!mi::resume(m, kNow, &why) && !mi::setStepState(m, "S1", S::Ready, kNow, {}, &why), "une mission annulee ne reprend pas");
    mi::archive(m, true, kNow);
    check(m.archived && m.history.back().kind == "archive", "archivee (rien d'efface)");
}

// ---- le fichier ------------------------------------------------------------------------------------------------------
void files() {
    std::printf("le fichier\n");
    Fixture f;
    mi::Mission m = stationOn(f);
    ai::Proposal p = f.proposal("op1", {type("T_PompeA", {{"Marche", "BOOL", ""}}, "S1")});
    std::string why;
    check(f.run(m, p, {"S1"}, &why), "P1 (" + why + ")");
    m.decisions.push_back({kNow, "Deux pompes ou trois ?", "Deux"});
    m.attachments = {"p-20261101-1"};
    const ai::json::Value v = mi::toJson(m);
    int format = -1;
    bool readOnly = true;
    const mi::Mission back = mi::fromJson(v, &format, &readOnly);
    check(format == mi::kFormat && !readOnly, "format courant, pas en lecture seule");
    check(back.id == m.id && back.name == m.name && back.request == m.request && back.steps.size() == m.steps.size()
              && back.step("S1")->state == S::Done && back.step("S2")->deps == m.step("S2")->deps,
          "relue : identite, demande, etapes, etats, dependances");
    check(back.proposals.size() == 1 && back.proposals[0].lines.size() == 1 && back.proposals[0].lines[0].creates == m.proposals[0].lines[0].creates
              && back.proposals[0].report.isObject(),
          "relue : la proposition, ses operations, ce qu'elles ont cree, le compte rendu");
    check(back.history.size() == m.history.size() && back.decisions.size() == 1 && back.attachments == m.attachments, "relue : historique, decisions, pieces jointes");
    check(ai::json::write(mi::toJson(back)) == ai::json::write(v), "aller-retour exact");

    // FORMAT 0 (sans << format >>) : des libelles, la liste des propositions en identifiants
    ai::json::Value old;
    check(ai::json::parse(R"({"id":"mission-ancienne","nom":"Ancienne","demande":"x","projet":"Projet_A","etat":"En pause",
        "etapes":[{"id":"S2","titre":"b","depend_de":["S1"],"etat":"Pr\u00eate"},{"id":"S1","titre":"a","etat":"Termin\u00e9e"}],
        "propositions":["op4","op5"]})", old),
          "format 0 lu");
    const mi::Mission mig = mi::fromJson(old, &format, &readOnly);
    check(format == 0 && !readOnly && mig.format == mi::kFormat, "migre vers le format courant");
    check(mig.state == mi::State::Paused && mig.project.name == "Projet_A" && mig.steps.front().id == "S1" && mig.step("S1")->state == S::Done
              && mig.step("S2")->state == S::Ready,
          "les libelles compris, l'ordre refait");
    check(mig.proposals.size() == 2 && mig.proposals[1].order == 2 && mig.proposals[1].id == "op5", "les propositions en liens P1, P2");

    // UN FORMAT PLUS RECENT : lu (les champs inconnus gardes), jamais reecrit
    ai::json::Value future = v;
    future.set("format", 9);
    future.set("champ_futur", "garde");
    const mi::Mission fut = mi::fromJson(future, &format, &readOnly);
    check(format == 9 && readOnly && fut.format == 9, "format 9 : lecture seule");
    check(mi::toJson(fut).str("champ_futur") == "garde", "le champ inconnu garde");
    check(!mi::save(fut, &why) && has(why, "r\xC3\xA9" "cente"), "jamais reecrit (" + why + ")");
}

// ---- le disque -------------------------------------------------------------------------------------------------------
void disk() {
    std::printf("le disque\n");
    const fs::path dir = fs::temp_directory_path() / "xpg_ai_missions";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ai::setRoot(U(dir / "IA"));
    Fixture f;
    mi::Mission m = stationOn(f);
    std::string why;
    check(mi::save(m, &why), "ecrite (" + why + ")");
    const fs::path file = fs::path(mi::missionFile(m.id));
    check(fs::exists(file) && has(U(file), "missions"), "IA/missions/<id>.json");
    mi::log(m, "decision", "deuxieme version", kNow);
    check(mi::save(m, &why), "reecrite");
    fs::path bak = file;
    bak += ".bak";
    check(fs::exists(bak), "la version precedente en .bak");
    mi::Mission other = mi::make("\xC3\x89" "clairage du hall", "c2", "2026-11-01T11:00:00");
    check(mi::save(other, &why), "une deuxieme mission");
    auto loaded = mi::loadAll();
    check(loaded.missions.size() == 2 && loaded.problems.empty() && loaded.missions[0].id == other.id, "relues : les plus recentes d'abord");
    // un ARRET BRUTAL pendant l'ecriture : le fichier incomplet
    {
        std::ofstream out(file, std::ios::trunc);
        out << "{\"format\":1,\"id\":\"" << m.id << "\",\"nom\":\"Sta";
    }
    loaded = mi::loadAll();
    check(loaded.missions.size() == 2 && anyHas(loaded.problems, "repris de sa copie"), "le fichier incomplet repris de sa copie : " + joined(loaded.problems));
    fs::path broken = file;
    broken += ".abime";
    check(fs::exists(broken), "le fichier abime garde a cote (jamais efface)");
    const auto again = mi::load(m.id, &why);
    check(again && again->steps.size() == 5, "le fichier remis d'aplomb");
    // le fichier principal disparu, la copie seule
    fs::remove(file, ec);
    loaded = mi::loadAll();
    check(loaded.missions.size() == 2 && anyHas(loaded.problems, "absent"), "un .bak seul : repris");
    // un fichier illisible sans copie : signale, laisse tel quel
    {
        std::ofstream out(dir / "IA" / "missions" / "mission-cassee.json");
        out << "pas du json";
    }
    loaded = mi::loadAll();
    check(anyHas(loaded.problems, "mission-cassee.json") && fs::exists(dir / "IA" / "missions" / "mission-cassee.json"), "illisible : signale, laisse");
    check(mi::remove(other.id, &why) && !fs::exists(mi::missionFile(other.id)), "supprimee a la demande");
    ai::setRoot({});
    fs::remove_all(dir, ec);
}

// ---- l'empreinte du projet et la reprise -----------------------------------------------------------------------------
void recovery() {
    std::printf("l'empreinte et la reprise\n");
    Fixture f;
    mi::Mission m = stationOn(f);
    const mi::ProjectPrint p0 = mi::printOf(f.doc->project);
    check(!p0.all.empty() && p0.files.size() >= 3, "l'empreinte : l'ensemble et par famille (" + std::to_string(p0.files.size()) + ")");
    // relu du disque (sans disque : les fichiers), la meme empreinte
    const auto filesOf = hmi::serializeProject(f.doc->project);
    auto reread = hmi::parseProject([&](const std::string& path, std::string& content) {
        for (const auto& x : filesOf)
            if (x.path == path && x.data) {
                content.assign(x.data->begin(), x.data->end());
                return true;
            }
        return false;
    });
    check(reread.has_value() && mi::printOf(*reread).all == p0.all, "le projet relu : la meme empreinte");
    hmi::Project dated = f.doc->project;
    dated.config.modified = "2030-01-01";
    check(mi::printOf(dated).all == p0.all, "la date d'enregistrement ne compte pas");
    m.project.id = "projet-A";
    m.project.name = "Projet A";
    mi::stamp(m, p0, kNow);

    // CAS A : le projet identique
    mi::Recovery r;
    mi::recover(m, "projet-A", &f.doc->project, kNow, &r);
    check(r.coherence == mi::Coherence::Same && m.coherence == mi::Coherence::Same && has(r.text, "identique"), "cas A : identique");

    // CAS B : modifie (une variable, une vue ajoutee)
    f.doc->project.programs.variables[1].initial = "42";
    f.doc->project.views.push_back(hmi::makeView(f.doc->project, "Vue_Nouvelle"));
    const mi::Recovery seen = mi::inspect(m, "projet-A", &f.doc->project);
    check(seen.coherence == mi::Coherence::Changed && anyHas(seen.changes, "les variables (modifi\xC3\xA9" "es)") && anyHas(seen.changes, "la vue Vue_Nouvelle (ajout\xC3\xA9" "e)")
              && !anyHas(seen.changes, "les vues"),
          "cas B : " + joined(seen.changes));
    check(m.coherence == mi::Coherence::Same, "inspect ne modifie rien");
    check(mi::recover(m, "projet-A", &f.doc->project, kNow, &r) && m.coherence == mi::Coherence::Changed && m.history.back().kind == "actualisation",
          "cas B : la mission le sait (historique)");
    check(has(mi::nextAction(m), "Actualiser"), "cas B : la prochaine action : actualiser");
    mi::stamp(m, mi::printOf(f.doc->project), kNow);

    // CAS C : un autre projet, aucun projet
    check(mi::inspect(m, "projet-B", &f.doc->project).coherence == mi::Coherence::Missing, "cas C : un autre projet ouvert");
    mi::recover(m, {}, nullptr, kNow, &r);
    check(r.coherence == mi::Coherence::Missing && m.coherence == mi::Coherence::Missing && has(r.nextAction, "Ouvrir"), "cas C : aucun projet - l'ouvrir");
    check(mi::nextBatch(m, 10).steps.empty() && has(mi::nextBatch(m, 10).why, "ouvre"), "cas C : rien ne se propose");

    // Une mission lancee sur un projet jamais enregistre (sans identite) : elle suit le projet ouvert, et prend son
    // identite des qu'il est enregistre ; un autre projet ensuite n'est plus le sien.
    mi::Mission u = mi::make("x", "c1", kNow);
    mi::stamp(u, mi::printOf(f.doc->project), kNow);
    check(mi::inspect(u, {}, &f.doc->project).coherence == mi::Coherence::Same, "projet jamais enregistre : le meme");
    mi::recover(u, "projet-A", &f.doc->project, kNow, &r);
    check(r.coherence == mi::Coherence::Same && u.project.id == "projet-A", "enregistre depuis : son identite prise");
    check(mi::inspect(u, "projet-B", &f.doc->project).coherence == mi::Coherence::Missing, "puis un autre projet : introuvable");
    check(mi::inspect(m, {}, &f.doc->project).coherence == mi::Coherence::Missing, "une mission d'un projet enregistre : pas un projet sans identite");
}

// SCENARIO B : l'application est fermee pendant la 3e proposition (l'appel a l'IA en cours) - rien n'est rejoue.
void interrupted() {
    std::printf("scenario B : arret pendant la 3e proposition\n");
    const fs::path dir = fs::temp_directory_path() / "xpg_ai_missions_b";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ai::setRoot(U(dir / "IA"));
    Fixture f;
    mi::Mission m = stationOn(f);
    m.project.id = "projet-A";
    std::string why;
    ai::Proposal p1 = f.proposal("op1", {type("T_PompeA", {{"Marche", "BOOL", ""}, {"Defaut", "BOOL", ""}}, "S1")});
    check(f.run(m, p1, {"S1"}, &why), "P1 (" + why + ")");
    ai::Proposal p2 = f.proposal("op2", {vars({{"Pompe_A", "T_PompeA", "", ""}}, "S2"),
                                         op(A::Equipment, "Modbus_A", {{"type", "modbus_tcp"}, {"ip", "127.0.0.1"}, {"port", "5020"}, {"esclave", "1"}, {"simule", "oui"}}, "S3")});
    check(f.run(m, p2, {"S2", "S3"}, &why), "P2 (" + why + ")");
    // la 3e : l'appel part (ecrit AVANT l'appel), les etapes en cours
    const mi::Batch b = mi::nextBatch(m, 10);
    check(b.steps.size() == 2, "le lot de P3 : S4, S5 (" + joined(b.steps) + ")");
    m.pending = {true, "proposition", "c1", kNow, b.steps};
    for (const auto& id : b.steps) check(mi::setStepState(m, id, S::Running, kNow), "en cours : " + id);
    mi::stamp(m, mi::printOf(f.doc->project), kNow);
    check(mi::save(m, &why), "sauvee avant l'appel");
    const std::string projectBefore = mi::printOf(f.doc->project).all;
    // ... arret brutal. Au redemarrage : relue du disque.
    auto loaded = mi::loadAll();
    check(loaded.missions.size() == 1, "relue apres le redemarrage");
    mi::Mission back = loaded.missions.front();
    check(back.pending.active && back.step("S4")->state == S::Running, "l'appel etait en cours");
    mi::Recovery r;
    check(mi::recover(back, "projet-A", &f.doc->project, "2026-11-01T12:00:00", &r), "reprise");
    check(r.interrupted && has(r.text, "interrompu") && r.coherence == mi::Coherence::Same, "dit : interrompu, projet identique");
    check(!back.pending.active && back.step("S4")->state == S::Ready && back.step("S5")->state == S::Ready, "S4, S5 de nouveau pretes");
    check(back.step("S1")->state == S::Done && back.step("S2")->state == S::Done && back.step("S3")->state == S::Done, "P1, P2 restent terminees");
    check(back.proposals.size() == 2 && mi::printOf(f.doc->project).all == projectBefore, "rien de rejoue : 2 propositions, le projet identique");
    check(std::any_of(back.history.begin(), back.history.end(), [](const mi::Event& e) { return e.kind == "interruption"; }), "l'historique dit l'interruption");
    check(has(mi::nextAction(back), "S4"), "la prochaine action : relancer S4, S5 - " + mi::nextAction(back));
    ai::setRoot({});
    fs::remove_all(dir, ec);
}

// ---- la recherche (scenario D : une autre conversation) ----------------------------------------------------------------
void searching() {
    std::printf("scenario D : depuis une autre conversation\n");
    Fixture f;
    std::vector<mi::Mission> all;
    all.push_back(stationOn(f));
    all.back().name = "Station de pompage";
    all.back().project.id = "projet-A";
    all.back().modified = "2026-10-31T18:00:00";
    all.push_back(mi::make("\xC3\x89" "clairage du hall", "c2", "2026-11-01T09:00:00"));
    all.back().project.id = "projet-A";
    all.push_back(mi::make("Archive", "c3", kNow));
    all.back().archived = true;
    const auto found = mi::search(all, "O\xC3\xB9 en est la station de pompage ?");
    check(!found.empty() && found.front() == 0, "<< ou en est la station de pompage ? >> : la station d'abord");
    check(mi::search(all, "").size() == 2, "les archivees a part");
    check(mi::search(all, "", true).size() == 3, "avec les archivees");
    const std::string list = mi::runSearchTool(all, R"({"texte":"station pompage"})", "projet-A");
    check(has(list, "Station de pompage") && has(list, all[0].id) && has(list, "prochaine action"), "chercher_missions : nom, identifiant, etat");
    check(has(mi::runSearchTool(all, R"({"texte":"sous-marin"})", "projet-A"), "Aucune mission"), "rien trouve : dit, rien d'invente");
    const std::string detail = mi::runDetailTool(all, R"({"mission":"station de pompage"})");
    check(has(detail, "Station de pompage") && has(detail, "S1") && has(detail, "Prochaine action"), "etat_mission : la fiche");
    check(has(mi::runDetailTool(all, R"({"mission":"inconnue-123"})"), "Aucune mission"), "etat_mission : une mission inconnue");
    const std::string ov = mi::overviewPrompt(all, "projet-A");
    check(has(ov, "Station de pompage") && has(ov, "\xC3\x89" "clairage") && !has(ov, "Archive"), "le resume pour toute conversation (sans les archivees)");
    check(mi::overviewPrompt(all, "projet-Z").find("Station") == std::string::npos, "pas les missions d'un autre projet");
    check(has(mi::searchToolJson(), "chercher_missions") && has(mi::detailToolJson(), "etat_mission"), "les definitions des outils");
    check(has(mi::summaryOf(all[0]), "Prochaine action"), "le resume a jour");
}

void history() {
    std::printf("l'historique borne\n");
    mi::Mission m = mi::make("x", "c1", kNow);
    for (int i = 0; i < 500; ++i) mi::log(m, "etape", "evenement " + std::to_string(i), kNow);
    check(m.history.size() == mi::kMaxHistory && m.history.front().kind == "creation" && has(m.history.back().text, "499"),
          "400 evenements au plus, le premier (la creation) garde");
}

// ==== 1.22.1 : CE QUE FAIT UNE ETAPE (sa progression, son activite du moment, son journal) ===============================
std::vector<std::string> texts(const std::vector<mi::JournalLine>& j) {
    std::vector<std::string> out;
    for (const auto& l : j) out.push_back(l.text);
    return out;
}

// Un compte rendu reel simule : E1 appliquee, E2 en echec (l'erreur exacte) - rien n'est restaure.
void applyHalf(Fixture& f, mi::Mission& m, const std::string& id) {
    plan::Plan pl;
    plan::Step a;
    a.id = "E1";
    a.op = 0;
    a.title = "Cr\xC3\xA9" "er le type T_PompeA";
    a.state = plan::State::Applied;
    plan::Step b;
    b.id = "E2";
    b.op = 1;
    b.title = "Cr\xC3\xA9" "er le type T_PompeB";
    b.state = plan::State::Failed;
    b.errors = {"un type du m\xC3\xAAme nom existe"};
    pl.steps = {a, b};
    plan::Report rep;
    rep.planned = 2;
    rep.applied = 1;
    rep.failed = 1;
    rep.failures = {"E2 Cr\xC3\xA9" "er le type T_PompeB : un type du m\xC3\xAAme nom existe"};
    mi::onApplied(m, id, pl, rep, f.doc->project, kNow);
}

void stepProgress() {
    std::printf("1.22.1 : la progression de chaque etape\n");
    Fixture f;
    mi::Mission m = stationOn(f);
    // 0 % : rien de propose encore, l'estimation du plan
    mi::StepProgress sp = mi::stepProgressOf(m, *m.step("S5"));
    check(sp.percent == 0 && sp.estimated && sp.planned == 3 && sp.counted() && sp.text() == "0 %" && has(sp.detail(), "estimation"),
          "S5 : 0 %, l'estimation du plan (" + sp.detail() + ")");
    // une proposition de deux operations pour S1 : generee (0 / 2), puis appliquee a moitie (une operation en echec)
    ai::Proposal p = f.proposal("op1", {type("T_PompeA", {{"Marche", "BOOL", ""}}, "S1"), type("T_PompeB", {{"Marche", "BOOL", ""}}, "S1")});
    mi::onGenerated(m, p, "c1", "m2", {"S1"}, kNow);
    sp = mi::stepProgressOf(m, *m.step("S1"));
    check(sp.percent == 0 && !sp.estimated && sp.planned == 2 && sp.toValidate == 2, "P1 generee : 0 / 2, deux a valider (" + sp.detail() + ")");
    applyHalf(f, m, "op1");
    check(m.step("S1")->state == S::Error, "S1 en erreur (une operation en echec)");
    sp = mi::stepProgressOf(m, *m.step("S1"));
    check(sp.applied == 1 && sp.failed == 1 && sp.planned == 2 && sp.percent == 50 && sp.text() == "50 %" && sp.frac() == 0.5,
          "PARTIEL : 1 / 2 appliquee = 50 % (" + sp.detail() + ")");
    check(has(sp.detail(), "1 / 2 op\xC3\xA9rations appliqu\xC3\xA9" "es") && has(sp.detail(), "1 en \xC3\xA9" "chec"), "le detail : " + sp.detail());
    // la correction proposee reprend l'operation en echec : le pourcentage ne recule pas
    ai::Proposal fix = f.proposal("op2", {type("T_PompeC", {{"Marche", "BOOL", ""}}, "S1")});
    mi::onGenerated(m, fix, "c1", "m4", {"S1"}, kNow);
    sp = mi::stepProgressOf(m, *m.step("S1"));
    check(sp.applied == 1 && sp.failed == 0 && sp.toValidate == 1 && sp.planned == 2 && sp.percent == 50,
          "la correction proposee : l'echec repris, toujours 50 % (" + sp.detail() + ")");
    // une proposition refusee ne compte plus
    mi::onDropped(m, "op2", "refusee", "pas maintenant", kNow);
    sp = mi::stepProgressOf(m, *m.step("S1"));
    check(sp.failed == 1 && sp.toValidate == 0 && sp.planned == 2 && sp.percent == 50, "refusee : elle ne compte plus, l'echec revient (" + sp.detail() + ")");
    // 100 % : la correction appliquee, l'etape terminee (verifiee dans le projet)
    std::string why;
    ai::Proposal fix2 = f.proposal("op3", {type("T_PompeC", {{"Marche", "BOOL", ""}}, "S1")});
    check(f.run(m, fix2, {"S1"}, &why), "la correction appliquee (" + why + ")");
    sp = mi::stepProgressOf(m, *m.step("S1"));
    check(m.step("S1")->state == S::Done && sp.percent == 100 && sp.text() == "100 %" && sp.applied == 2 && sp.failed == 0,
          "TERMINEE : 100 % (" + sp.detail() + ")");
    // jamais 100 % sans etre terminee (le projet a perdu ce qu'elle avait cree : de nouveau prete)
    m.step("S1")->state = S::Ready;
    check(mi::stepProgressOf(m, *m.step("S1")).percent == 99, "toutes ses operations appliquees mais pas terminee : 99 %");
    m.step("S1")->state = S::Done;
    // IGNOREE : pas de pourcentage, l'etat le dit
    mi::PlanEdit e;
    e.kind = mi::PlanEdit::Kind::Skip;
    e.step = "S4";
    check(mi::editPlan(m, e, kNow, &why), "S4 ignoree");
    sp = mi::stepProgressOf(m, *m.step("S4"));
    check(sp.percent == -1 && !sp.counted() && sp.text() == "\xE2\x80\x94" && sp.frac() == 0.0, "ignoree : pas de pourcentage");
    check(has(mi::activityOf(m, *m.step("S4")).text, "Ignor\xC3\xA9" "e par l'utilisateur"), "ignoree : l'etat le dit");
    // la regle dite a l'utilisateur
    check(has(mi::progressRule(), "Par \xC3\xA9tape") && has(mi::stepProgressRule(), "99 %") && has(mi::stepProgressRule(), "100 %"),
          "la regle par etape est dite (progressRule)");
}

void stepActivities() {
    std::printf("1.22.1 : ce que fait chaque etape, en une ligne\n");
    Fixture f;
    mi::Mission m = stationOn(f);
    using K = mi::Activity;
    // prete, en attente d'une dependance
    auto a = mi::activityOf(m, *m.step("S1"));
    check(a.kind == K::Idle && a.text == "Pr\xC3\xAAte : \xC2\xAB Proposer la suite \xC2\xBB la demande \xC3\xA0 l'IA.", "prete : " + a.text);
    a = mi::activityOf(m, *m.step("S4"));
    check(a.kind == K::Waiting && a.text == "Attend S2 (d\xC3\xA9pendance)", "en attente : " + a.text);
    a = mi::activityOf(m, *m.step("S2"));
    check(a.text == "Attend S1, S3 (d\xC3\xA9pendances)", "deux dependances (dont l'implicite) : " + a.text);
    // L'IA PREPARE LA PROPOSITION (l'appel ecrit avant de partir, la reponse en cours)
    m.pending = {true, "proposition", "c1", kNow, {"S1", "S3"}};
    check(mi::setStepState(m, "S1", S::Running, kNow) && mi::setStepState(m, "S3", S::Running, kNow), "S1, S3 en cours");
    mi::Live lv;
    lv.calling = true;
    lv.elapsed = 42;
    a = mi::activityOf(m, *m.step("S1"), lv);
    check(a.kind == K::Working && a.live && a.text == "L'IA pr\xC3\xA9pare la proposition P1\xE2\x80\xA6 (depuis 0:42)", "en direct : " + a.text);
    check(mi::currentStep(m, lv) == m.step("S1"), "l'etape du moment : S1 (l'IA travaille pour elle)");
    check(mi::missionActivity(m, lv) == "L'IA pr\xC3\xA9pare la proposition P1 (S1, S3)\xE2\x80\xA6 (depuis 0:42)", "la mission : " + mi::missionActivity(m, lv));
    a = mi::activityOf(m, *m.step("S1"));
    check(a.kind == K::Blocked && !a.live && has(a.text, "interrompu"), "un appel en attente sans reponse en cours : interrompu (" + a.text + ")");
    // l'appel fini sans proposition : dit dans l'historique de l'etape (1.22.1)
    check(mi::setStepState(m, "S3", S::Ready, kNow, "Appel interrompu : la r\xC3\xA9ponse a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e"), "S3 de nouveau prete");
    check(m.history.back().kind == "etape" && m.history.back().step == "S3" && has(m.history.back().text, "Appel interrompu"),
          "en cours -> prete : l'historique le dit (" + m.history.back().text + ")");
    m.pending = {};
    // P1 GENEREE : a valider ; VERIFIEE ; en cours d'APPLICATION de fond
    ai::Proposal p = f.proposal("op1", {type("T_PompeA", {{"Marche", "BOOL", ""}}, "S1"), type("T_PompeB", {{"Marche", "BOOL", ""}}, "S1")});
    mi::onGenerated(m, p, "c1", "m2", {"S1"}, kNow);
    a = mi::activityOf(m, *m.step("S1"));
    check(a.kind == K::ToValidate && a.proposal == "op1" && a.text == "P1 : 2 op\xC3\xA9rations \xC3\xA0 valider \xE2\x80\x94 Pr\xC3\xA9visualiser puis Appliquer",
          "a valider : " + a.text);
    check(mi::currentStep(m) == m.step("S1"), "l'etape du moment : celle qui attend une decision");
    mi::onChecked(m, "op1", ai::Check{}, kNow);
    a = mi::activityOf(m, *m.step("S1"));
    check(a.text == "P1 v\xC3\xA9rifi\xC3\xA9" "e : 2 op\xC3\xA9rations pr\xC3\xAAtes \xE2\x80\x94 Appliquer", "verifiee : " + a.text);
    lv = {};
    lv.applying = "op1";
    lv.applyPercent = 45;
    a = mi::activityOf(m, *m.step("S1"), lv);
    check(a.kind == K::Working && a.live && a.text == "Application de P1\xE2\x80\xA6 45 %", "application de fond : " + a.text);
    check(mi::currentStep(m, lv) == m.step("S1"), "l'etape du moment : celle qui s'applique");
    // les erreurs de validation
    ai::Check bad;
    bad.errors = {"La variable Variable_Inventee est introuvable."};
    mi::onChecked(m, "op1", bad, kNow);
    a = mi::activityOf(m, *m.step("S1"));
    check(a.kind == K::Blocked && a.text == "P1 : 1 erreur \xC3\xA0 la v\xC3\xA9rification \xE2\x80\x94 La variable Variable_Inventee est introuvable.",
          "erreur de validation : " + a.text);
    // EN ERREUR, BLOQUEE (une application a moitie)
    applyHalf(f, m, "op1");
    a = mi::activityOf(m, *m.step("S1"));
    check(a.kind == K::Error && has(a.text, "En erreur : P1 : E2 Cr\xC3\xA9" "er le type T_PompeB : un type du m\xC3\xAAme nom existe"), "en erreur : " + a.text);
    a = mi::activityOf(m, *m.step("S2"));
    check(a.kind == K::Blocked && a.text == "Bloqu\xC3\xA9" "e : d\xC3\xA9pend de S1 (en erreur)", "bloquee : " + a.text);
    // UNE CORRECTION en direct
    m.pending = {true, "correction", "c1", kNow, {"S1"}};
    lv = {};
    lv.calling = true;
    lv.elapsed = 3725;
    a = mi::activityOf(m, *m.step("S1"), lv);
    check(a.live && a.text == "L'IA pr\xC3\xA9pare une correction (P2)\xE2\x80\xA6 (depuis 1:02:05)", "correction en direct : " + a.text);
    m.pending = {};
    // APPLIQUEE, VERIFICATION, A PLANIFIER (des etats que le modele traverse)
    m.step("S3")->state = S::Applied;
    check(has(mi::activityOf(m, *m.step("S3")).text, "la v\xC3\xA9rification dans le projet suit"), "appliquee : la verification suit");
    m.step("S3")->state = S::Verifying;
    check(mi::activityOf(m, *m.step("S3")).text == "V\xC3\xA9rification sur le projet r\xC3\xA9" "el\xE2\x80\xA6", "verification en cours");
    m.step("S3")->state = S::ToPlan;
    check(has(mi::activityOf(m, *m.step("S3")).text, "\xC3\x80 planifier"), "a planifier");
    m.step("S3")->state = S::Ready;
    // TERMINEE : combien d'operations, quand
    std::string why;
    ai::Proposal fix = f.proposal("op2", {type("T_PompeC", {{"Marche", "BOOL", ""}}, "S1")});
    check(f.run(m, fix, {"S1"}, &why), "la correction appliquee (" + why + ")");
    a = mi::activityOf(m, *m.step("S1"));
    check(a.kind == K::Done && a.text == "Termin\xC3\xA9" "e \xE2\x80\x94 2 op\xC3\xA9rations appliqu\xC3\xA9" "es le 01/11 \xC3\xA0 10:00", "terminee : " + a.text);
    // EN PAUSE, ANNULEE
    check(mi::pause(m, kNow, &why), "en pause");
    check(mi::activityOf(m, *m.step("S3")).text == "Pr\xC3\xAAte \xE2\x80\x94 la mission est en pause : reprends-la d'abord.", "prete, mission en pause");
    check(has(mi::activityOf(m, *m.step("S4")).text, "Mission en pause \xE2\x80\x94 Attend S2"), "en attente, mission en pause");
    check(mi::cancel(m, kNow, &why), "annulee");
    a = mi::activityOf(m, *m.step("S4"));
    check(a.kind == K::Off && has(a.text, "Annul\xC3\xA9" "e") && mi::stepProgressOf(m, *m.step("S4")).percent == -1, "annulee : pas de pourcentage, l'etat");
    check(mi::currentStep(m) == nullptr, "annulee : plus d'etape du moment");
    // LA MISSION ELLE-MEME : le plan en preparation, l'audit final ; le temps ecoule
    mi::Mission n = mi::make("x", "c1", kNow);
    n.pending = {true, "plan", "c1", kNow, {}};
    lv = {};
    lv.calling = true;
    lv.elapsed = 42;
    check(mi::missionActivity(n, lv) == "L'IA analyse le projet et pr\xC3\xA9pare le plan\xE2\x80\xA6 (depuis 0:42)", "le plan : " + mi::missionActivity(n, lv));
    check(has(mi::missionActivity(n), "interrompu"), "le plan sans reponse en cours : interrompu");
    n.pending = {};
    lv = {};
    lv.auditing = true;
    check(has(mi::missionActivity(n, lv), "Audit final en cours") && mi::missionActivity(n).empty(), "l'audit final ; rien sinon");
    check(mi::elapsedText(0) == "0:00" && mi::elapsedText(42) == "0:42" && mi::elapsedText(725) == "12:05" && mi::elapsedText(3725) == "1:02:05",
          "le temps ecoule : 0:42, 12:05, 1:02:05");
}

void stepJournals() {
    std::printf("1.22.1 : le journal d'une etape (apres une application reelle)\n");
    Fixture f;
    mi::Mission m = stationOn(f);
    std::string why;
    // la demande a l'IA (comme l'Assistant l'ecrit), P1 generee, verifiee, appliquee (une commande, le compte rendu reel)
    mi::log(m, "proposition", "Proposition demand\xC3\xA9" "e \xC3\xA0 l'IA pour S1, S2 (2 op\xC3\xA9ration(s) estim\xC3\xA9" "e(s)).", kNow);
    ai::Proposal p1 = f.proposal("op1", {type("T_PompeA", {{"Marche", "BOOL", ""}, {"Defaut", "BOOL", ""}}, "S1"),
                                         vars({{"Pompe_A", "T_PompeA", "La pompe A", ""}}, "S2")});
    check(f.run(m, p1, {"S1", "S2"}, &why), "P1 appliquee (" + why + ")");
    const auto j1 = mi::stepJournal(m, *m.step("S1"));
    const auto t1 = texts(j1);
    check(anyHas(t1, "demand\xC3\xA9" "e \xC3\xA0 l'IA pour S1"), "S1 : la demande a l'IA (le texte la cite)");
    check(anyHas(t1, "P1 \xC2\xAB Essai op1 \xC2\xBB") && anyHas(t1, "v\xC3\xA9rifi\xC3\xA9" "e sur une copie"), "S1 : P1 generee, verifiee");
    check(anyHas(t1, "\xE2\x9C\x93 Cr\xC3\xA9" "er le type T_PompeA"), "S1 : l'operation appliquee, son libelle lisible (" + joined(t1) + ")");
    check(!anyHas(t1, "la variable Pompe_A"), "S1 : pas l'operation de S2");
    check(anyHas(t1, "Termin\xC3\xA9" "e") && anyHas(t1, "P1 appliqu\xC3\xA9" "e"), "S1 : terminee, P1 appliquee");
    check(std::all_of(j1.begin(), j1.end(), [](const mi::JournalLine& l) { return l.date == kNow; }), "chaque ligne a son heure");
    check(std::any_of(j1.begin(), j1.end(), [](const mi::JournalLine& l) { return l.kind == "operation" && l.proposal == "op1"; }), "l'operation : son genre, sa proposition");
    // l'operation apres l'en-tete de son application (le plus ancien d'abord)
    std::size_t head = 99, opAt = 99;
    for (std::size_t i = 0; i < j1.size(); ++i) {
        if (j1[i].kind == "application" && head == 99) head = i;
        if (j1[i].kind == "operation" && opAt == 99) opAt = i;
    }
    check(head < opAt && opAt < j1.size(), "l'operation suit son application");
    const auto t2 = texts(mi::stepJournal(m, *m.step("S2")));
    check(anyHas(t2, "\xE2\x9C\x93 Cr\xC3\xA9" "er la variable Pompe_A") && !anyHas(t2, "le type T_PompeA"), "S2 : son operation seulement");
    check(mi::stepJournal(m, *m.step("S4")).empty() || !anyHas(texts(mi::stepJournal(m, *m.step("S4"))), "\xE2\x9C\x93"), "S4 : aucune operation");
    // P2 refusee a la verification (S4) : ses erreurs, son refus
    ai::Proposal bad = f.proposal("op2", {op(A::Alarm, "Defaut_X", {{"condition", "Variable_Inventee"}, {"message", "x"}}, "S4")});
    check(!f.run(m, bad, {"S4"}, &why), "P2 : refusee a la verification");
    mi::onDropped(m, "op2", "refusee", "variable inconnue", kNow);
    const auto t4 = texts(mi::stepJournal(m, *m.step("S4")));
    check(anyHas(t4, "erreur(s) de validation") && anyHas(t4, "P2 refus\xC3\xA9" "e : variable inconnue"), "S4 : la verification en erreur, le refus (" + joined(t4) + ")");
    // un appel interrompu (en cours -> prete), puis a la reprise apres une fermeture : dit dans le journal de l'etape
    m.pending = {true, "proposition", "c1", kNow, {"S3"}};
    check(mi::setStepState(m, "S3", S::Running, kNow), "S3 en cours");
    mi::recover(m, {}, &f.doc->project, kNow);
    const auto t3 = texts(mi::stepJournal(m, *m.step("S3")));
    check(anyHas(t3, "interrompu (proposition pour S3"), "S3 : l'appel interrompu (la reprise le dit) - " + joined(t3));
    // une operation en echec : l'erreur exacte ; une decision qui cite l'etape
    Fixture g;
    mi::Mission e = stationOn(g);
    ai::Proposal half = g.proposal("op9", {type("T_PompeA", {{"Marche", "BOOL", ""}}, "S1"), type("T_PompeB", {{"Marche", "BOOL", ""}}, "S1")});
    mi::onGenerated(e, half, "c1", "m2", {"S1"}, kNow);
    applyHalf(g, e, "op9");
    e.decisions.push_back({kNow, "S1 : garder le type de la biblioth\xC3\xA8que ?", "Oui"});
    const auto te = texts(mi::stepJournal(e, *e.step("S1")));
    check(anyHas(te, "\xE2\x9C\x93 Cr\xC3\xA9" "er le type T_PompeA") && anyHas(te, "\xC3\x89" "chec : Cr\xC3\xA9" "er le type T_PompeB \xE2\x80\x94 un type du m\xC3\xAAme nom existe"),
          "l'operation appliquee et celle en echec (l'erreur exacte) - " + joined(te));
    check(anyHas(te, "En erreur") && anyHas(te, "garder le type de la biblioth\xC3\xA8que ? \xE2\x86\x92 Oui"), "l'erreur de l'etape, la decision qui la cite");
    check(!anyHas(texts(mi::stepJournal(e, *e.step("S3"))), "garder le type"), "la decision n'est pas dans le journal d'une autre etape");
    // l'historique borne a perdu la trace de P1 : repris de ce que la mission garde (rien n'est perdu)
    mi::Mission lost = e;
    lost.history.clear();
    const auto tl = texts(mi::stepJournal(lost, *lost.step("S1")));
    check(anyHas(tl, "P1 \xC2\xAB") && anyHas(tl, "P1 appliqu\xC3\xA9" "e") && anyHas(tl, "\xE2\x9C\x93 Cr\xC3\xA9" "er le type T_PompeA"),
          "sans historique : la proposition, son application, ses operations (" + joined(tl) + ")");
}

// Un fichier ecrit par la 1.22.0 : relu sans perte ; la progression, l'activite et le journal de chaque etape s'en deduisent
// (la 1.22.1 n'ajoute aucun champ au fichier : tout vient de ce que la 1.22.0 gardait).
void file1220() {
    std::printf("1.22.1 : un fichier de la 1.22.0\n");
    const char* text = R"({"format":1,"id":"mission-20261101090000-ab12","nom":"Station","description":"","demande":"Construis la station",
      "cree":"2026-11-01T09:00:00","modifie":"2026-11-01T12:04:00",
      "projet":{"id":"projet-A","nom":"Station","chemin":"/projets/Station","empreinte":"e1","empreinte_prise":"2026-11-01T12:04:00","fichiers":{}},
      "conversation":"c1","conversations":["c1"],"fournisseur":"anthropic","modele":"m","etat":"en_cours","archivee":false,"revision_plan":0,
      "etapes":[
        {"id":"S1","titre":"Cr\u00e9er le type","detail":"","categorie":"types","depend_de":[],"implicites":[],"etat":"terminee","obligatoire":true,
         "operations":1,"produit":[],"propositions":["op1"],"note":"Appliqu\u00e9e par P1","erreur":"","maj":"2026-11-01T12:04:00"},
        {"id":"S2","titre":"Cr\u00e9er les variables","detail":"","categorie":"variables","depend_de":["S1"],"implicites":[],"etat":"erreur",
         "obligatoire":true,"operations":2,"produit":[],"propositions":["op1"],"note":"","erreur":"Pompe_B : nom pris","maj":"2026-11-01T12:04:00"},
        {"id":"S3","titre":"Cr\u00e9er la vue","detail":"","categorie":"vues","depend_de":["S1"],"implicites":[],"etat":"a_valider","obligatoire":true,
         "operations":4,"produit":[],"propositions":["op2"],"note":"","erreur":"","maj":"2026-11-01T12:10:00"}],
      "analyse":{"conforme":[],"a_modifier":[],"a_completer":[],"a_creer":[],"impossible":[],"resume":""},
      "propositions":[
        {"id":"op1","ordre":1,"conversation":"c1","message":"m2","etapes":["S1","S2"],"titre":"Les types et les variables","etat":"appliquee",
         "operations":3,"creer":3,"modifier":0,"reutiliser":0,"supprimer":0,"appliquees":2,"echouees":1,"ignorees":0,
         "cree":"2026-11-01T12:00:00","appliquee_le":"2026-11-01T12:04:00","erreurs":["E3 : nom pris"],
         "lignes":[{"id":"E1","etape":"S1","titre":"Cr\u00e9er le type T_PompeA","verbe":"creation","etat":"appliquee","cree":["type|T_PompeA|"]},
                   {"id":"E2","etape":"S2","titre":"Cr\u00e9er la variable Pompe_A","verbe":"creation","etat":"appliquee","cree":["variable|Pompe_A|"]},
                   {"id":"E3","etape":"S2","titre":"Cr\u00e9er la variable Pompe_B","verbe":"creation","etat":"echouee","cree":[],"erreur":"nom pris"}],
         "suite":""},
        {"id":"op2","ordre":2,"conversation":"c1","message":"m4","etapes":["S3"],"titre":"La vue","etat":"generee","operations":2,"creer":2,
         "modifier":0,"reutiliser":0,"supprimer":0,"appliquees":0,"echouees":0,"ignorees":0,"cree":"2026-11-01T12:10:00","appliquee_le":"",
         "erreurs":[],"lignes":[{"id":"","etape":"S3","titre":"creer_vue Vue_Station","verbe":"","etat":"a_faire","cree":[]},
                                {"id":"","etape":"S3","titre":"creer Voyant","verbe":"","etat":"a_faire","cree":[]}],"suite":""}],
      "historique":[
        {"date":"2026-11-01T09:00:00","genre":"creation","texte":"Mission cr\u00e9\u00e9e"},
        {"date":"2026-11-01T12:00:00","genre":"proposition","texte":"P1 \u00ab Les types et les variables \u00bb : 3 op\u00e9ration(s) pour S1, S2.","proposition":"op1"},
        {"date":"2026-11-01T12:04:00","genre":"etape","texte":"S1 \u00ab Cr\u00e9er le type \u00bb : Termin\u00e9e","etape":"S1"},
        {"date":"2026-11-01T12:04:00","genre":"etape","texte":"S2 \u00ab Cr\u00e9er les variables \u00bb : En erreur \u2014 P1 : E3 nom pris","etape":"S2"},
        {"date":"2026-11-01T12:04:00","genre":"application","texte":"P1 appliqu\u00e9e : 2 op\u00e9ration(s) appliqu\u00e9e(s), 1 \u00e9chou\u00e9e(s).","proposition":"op1"},
        {"date":"2026-11-01T12:10:00","genre":"proposition","texte":"P2 \u00ab La vue \u00bb : 2 op\u00e9ration(s) pour S3.","proposition":"op2"}],
      "decisions":[],"appel_en_cours":{"actif":false,"objet":"","conversation":"","debut":"","etapes":[]},
      "coherence":"identique","coherence_texte":"","point_de_reprise":"Apr\u00e8s P1","resume":"En cours","pieces_jointes":[]})";
    ai::json::Value v;
    check(ai::json::parse(text, v), "le fichier 1.22.0 se lit");
    int format = -1;
    bool readOnly = true;
    const mi::Mission m = mi::fromJson(v, &format, &readOnly);
    check(format == 1 && !readOnly && m.format == mi::kFormat && mi::kFormat == 1, "format 1 : le meme (kFormat inchange), pas en lecture seule");
    check(m.steps.size() == 3 && m.proposals.size() == 2 && m.proposals[0].lines.size() == 3 && m.history.size() == 6, "tout est relu");
    // la progression de chaque etape
    check(mi::stepProgressOf(m, *m.step("S1")).percent == 100, "S1 terminee : 100 %");
    const mi::StepProgress s2 = mi::stepProgressOf(m, *m.step("S2"));
    check(s2.applied == 1 && s2.failed == 1 && s2.percent == 50, "S2 : 1 / 2 = 50 % (" + s2.detail() + ")");
    check(mi::stepProgressOf(m, *m.step("S3")).percent == 0 && mi::stepProgressOf(m, *m.step("S3")).toValidate == 2, "S3 : 0 %, 2 a valider");
    // l'activite de chaque etape
    check(mi::activityOf(m, *m.step("S1")).text == "Termin\xC3\xA9" "e \xE2\x80\x94 1 op\xC3\xA9ration appliqu\xC3\xA9" "e le 01/11 \xC3\xA0 12:04",
          "S1 : " + mi::activityOf(m, *m.step("S1")).text);
    check(mi::activityOf(m, *m.step("S2")).text == "En erreur : Pompe_B : nom pris", "S2 : " + mi::activityOf(m, *m.step("S2")).text);
    check(mi::activityOf(m, *m.step("S3")).text == "P2 : 2 op\xC3\xA9rations \xC3\xA0 valider \xE2\x80\x94 Pr\xC3\xA9visualiser puis Appliquer",
          "S3 : " + mi::activityOf(m, *m.step("S3")).text);
    // le journal de S2
    const auto t2 = texts(mi::stepJournal(m, *m.step("S2")));
    check(anyHas(t2, "\xE2\x9C\x93 Cr\xC3\xA9" "er la variable Pompe_A") && anyHas(t2, "\xC3\x89" "chec : Cr\xC3\xA9" "er la variable Pompe_B \xE2\x80\x94 nom pris")
              && anyHas(t2, "P1 \xC2\xAB Les types") && !anyHas(t2, "le type T_PompeA"),
          "S2 : P1, son operation appliquee, celle en echec (" + joined(t2) + ")");
    // sans perte : reecrit puis relu, identique ; rien de nouveau dans le fichier
    const ai::json::Value once = mi::toJson(m);
    check(ai::json::write(mi::toJson(mi::fromJson(once))) == ai::json::write(once), "reecrit puis relu : identique");
    check(once.integer("format") == 1 && once.at("propositions").items().size() == 2 && once.at("historique").items().size() == 6
              && once.at("etapes").items()[1].str("erreur") == "Pompe_B : nom pris"
              && once.at("propositions").items()[0].at("lignes").items()[2].str("erreur") == "nom pris",
          "les champs de la 1.22.0 gardes tels quels");
}


// ---- 1.22.2 : forcer la fin d'une mission ------------------------------------------------------------------------------
//  Les etapes non terminees ignorees (la raison dite), les propositions en attente refusees (jamais appliquees), l'appel
//  a l'IA efface, l'historique, l'etat final (que rien ne rouvre), la progression, le rapport final, le refus sur une
//  mission terminee ou annulee, le fichier relu ; une mission en pause peut finir ainsi ; Ctrl+Z apres coup.
void forcing() {
    std::printf("1.22.2 : forcer la fin d'une mission\n");
    Fixture f;
    mi::Mission m = stationOn(f);
    std::string why;
    // P1 appliquee : S1 (le type) et S2 (les variables) terminees, verifiees dans le projet
    ai::Proposal p1 = f.proposal("op1", {type("T_PompeA", {{"Marche", "BOOL", ""}, {"Defaut", "BOOL", ""}}, "S1"),
                                         vars({{"Pompe_A", "T_PompeA", "La pompe A", ""}}, "S2")});
    check(f.run(m, p1, {"S1", "S2"}, &why), "P1 appliquee (" + why + ")");
    // P2 a valider (S4), S3 en erreur, un appel a l'IA en cours pour S5
    ai::Proposal p2 = f.proposal("op2", {op(A::Alarm, "Defaut_Pompe_A", {{"condition", "Pompe_A.Defaut"}, {"message", "Pompe A en d\xC3\xA9" "faut"}}, "S4")});
    mi::onGenerated(m, p2, "c1", "m4", {"S4"}, kNow);
    check(mi::setStepState(m, "S3", S::Running, kNow, {}, &why) && mi::setStepState(m, "S3", S::Error, kNow, "Modbus_A : adresse refus\xC3\xA9" "e", &why),
          "S3 en erreur (" + why + ")");
    check(mi::setStepState(m, "S5", S::Running, kNow, {}, &why), "S5 en cours (" + why + ")");
    m.pending.active = true;
    m.pending.purpose = "proposition";
    m.pending.started = kNow;
    m.pending.steps = {"S5"};
    check(m.step("S1")->state == S::Done && m.step("S2")->state == S::Done && m.step("S4")->state == S::AwaitingValidation
              && m.proposal("op2")->state == "generee",
          "avant : S1, S2 terminees ; S4 attend la validation de P2 ; S3 en erreur ; S5 en cours");
    check(mi::mayForceFinish(m, &why), "une mission en cours : sa fin se force");
    const auto left = mi::unfinishedSteps(m);
    check(left.size() == 3 && std::find(left.begin(), left.end(), "S3") != left.end() && std::find(left.begin(), left.end(), "S4") != left.end()
              && std::find(left.begin(), left.end(), "S5") != left.end(),
          "les etapes non terminees : S3, S4, S5");
    check(mi::awaitingProposals(m) == std::vector<std::string>{"op2"}, "la proposition en attente : P2");
    // aucune transition n'est ajoutee : celles qui ne vont pas directement a << ignoree >> passent par << prete >>
    for (const S st : {S::Running, S::AwaitingValidation, S::Applied, S::Verifying})
        check(!mi::canMove(st, S::Skipped) && mi::canMove(st, S::Ready) && mi::canMove(S::Ready, S::Skipped),
              std::string(mi::stepLabel(st)) + " : par << prete >>, puis << ignoree >> (la table des transitions)");
    for (const S st : {S::ToPlan, S::Waiting, S::Ready, S::Error, S::Blocked})
        check(mi::canMove(st, S::Skipped), std::string(mi::stepLabel(st)) + " : directement << ignoree >>");
    check(!mi::canMove(S::Done, S::Skipped) && !mi::canMove(S::Cancelled, S::Skipped), "terminee, annulee : jamais ignoree (elles ne sont pas touchees)");

    const hmi::Project before = f.doc->project;
    const std::size_t events = m.history.size();
    const std::string at = "2026-11-01T11:30:00";
    check(mi::forceFinish(m, at, {}, &why), "forcer la fin (" + why + ")");
    // l'etat final
    check(m.state == mi::State::Done && m.forced.on() && m.forced.date == at && m.forced.reason == std::string(mi::kForcedReason),
          "terminee ; la fin forcee retenue (la date, la raison)");
    check(mi::stateText(m) == "Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)", "l'etat en clair : " + mi::stateText(m));
    // les etapes : ignorees avec la raison ; celles qui etaient terminees le restent
    for (const char* id : {"S3", "S4", "S5"}) {
        const mi::Step* s = m.step(id);
        check(s && s->state == S::Skipped && s->note == "Fin forc\xC3\xA9" "e par l'utilisateur." && mi::stepText(m, *s) == "Ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e)",
              std::string(id) + " : ignoree, << Fin forcee par l'utilisateur. >> (" + (s ? s->note : std::string("?")) + ")");
        check(std::find(m.forced.steps.begin(), m.forced.steps.end(), id) != m.forced.steps.end(), std::string(id) + " : dans la fin forcee");
    }
    check(m.forced.steps.size() == 3, "trois etapes ignorees");
    check(m.step("S1")->state == S::Done && m.step("S2")->state == S::Done && mi::stepText(m, *m.step("S1")) == "Termin\xC3\xA9" "e",
          "S1, S2 restent terminees");
    check(has(m.step("S3")->error, "adresse refus"), "l'erreur de S3 est gardee (l'historique de ce qui s'est passe)");
    // les propositions : P2 refusee, jamais appliquee ; P1 reste appliquee
    check(m.proposal("op2")->state == "refusee" && anyHas(m.proposal("op2")->errors, "fin forc\xC3\xA9" "e") && m.forced.proposals == std::vector<std::string>{"op2"},
          "P2 refusee (fin forcee)");
    check(!mi::mayApply(m, "op2", &why) && has(why, "fin forc"), "P2 ne s'applique plus (" + why + ")");
    check(m.proposal("op1")->state == "appliquee", "P1 reste appliquee (rien n'est defait)");
    check(mi::printOf(before).all == mi::printOf(f.doc->project).all, "le projet n'a pas change");
    // l'appel a l'IA : efface, et dit
    check(!m.pending.active, "l'appel a l'IA efface");
    // l'historique
    bool interruption = false, refused = false;
    int ignored = 0;
    for (std::size_t i = events; i < m.history.size(); ++i) {
        const auto& e = m.history[i];
        interruption |= e.kind == "interruption" && has(e.text, "fin forc");
        refused |= e.kind == "proposition" && e.proposal == "op2" && has(e.text, "P2 refus");
        ignored += e.kind == "etape" && has(e.text, "Ignor\xC3\xA9" "e") && has(e.text, "Fin forc") ? 1 : 0;
    }
    check(interruption && refused && ignored == 3, "l'historique : l'appel arrete, P2 refusee, les trois etapes ignorees");
    check(m.history.back().kind == "terminee" && has(m.history.back().text, "Fin forc\xC3\xA9" "e par l'utilisateur : 3 \xC3\xA9tapes ignor\xC3\xA9" "es")
              && has(m.history.back().text, "Rien n'est d\xC3\xA9" "fait"),
          "la derniere entree : " + m.history.back().text);
    // la progression : les etapes ignorees ne comptent plus
    const mi::Progress pr = mi::progressOf(m);
    check(pr.skipped == 3 && pr.steps == 2 && pr.done == 2 && pr.percent() == 100, "la progression sans les ignorees : " + pr.text());
    check(mi::stepProgressOf(m, *m.step("S4")).percent < 0, "une etape ignoree : pas de pourcentage");
    // ce que l'on dit
    check(mi::activityOf(m, *m.step("S4")).text == "Ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e) : hors du calcul de la progression.",
          "S4 : " + mi::activityOf(m, *m.step("S4")).text);
    check(mi::missionActivity(m).rfind("Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : 3 \xC3\xA9tapes ignor\xC3\xA9" "es", 0) == 0, "l'activite : " + mi::missionActivity(m));
    check(has(mi::nextAction(m), "fin forc") && has(mi::nextAction(m), "audit"), "la prochaine action : l'audit final (" + mi::nextAction(m) + ")");
    check(has(mi::summaryOf(m), "Termin\xC3\xA9" "e (fin forc\xC3\xA9" "e)") && has(mi::detailText(m), "Fin forc\xC3\xA9" "e le " + at)
              && has(mi::detailText(m), "[Ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e)] "),
          "le resume et la fiche le disent");
    // rien ne la rouvre : refreshState, un plan, une pause, une deuxieme fin forcee, une annulation
    mi::refreshState(m, at);
    check(m.state == mi::State::Done, "refreshState la laisse terminee (sans rapport final)");
    mi::PlanEdit un;
    un.kind = mi::PlanEdit::Kind::Unskip;
    un.step = "S4";
    check(!mi::editPlan(m, un, at, &why) && has(why, "fin forc") && m.step("S4")->state == S::Skipped, "son plan ne change plus (" + why + ")");
    check(!mi::forceFinish(m, at, {}, &why) && has(why, "d\xC3\xA9j\xC3\xA0 termin\xC3\xA9" "e"), "deja terminee : refuse (" + why + ")");
    check(!mi::cancel(m, at, &why) && !mi::pause(m, at, &why) && m.state == mi::State::Done, "ni annulee, ni en pause apres coup");
    // LE RAPPORT FINAL (l'audit, comme pour une fin normale) : il dit la fin forcee et les etapes ignorees
    const auto audit = hmi::audit::run(f.doc->project, {}, {});
    const mi::FinalReport fr = mi::finalReport(m, f.doc->project, &audit, at);
    check(fr.forced && fr.skipped.size() == 3 && anyHas(fr.skipped, "S4 Creer les alarmes") && !fr.complete, "le rapport : fin forcee, 3 ignorees, jamais complet");
    check(has(fr.verdict, "fin forc\xC3\xA9" "e par l'utilisateur") && has(fr.verdict, "3 \xC3\xA9tapes ignor\xC3\xA9" "es ("), "le verdict : " + fr.verdict);
    check(fr.finished.size() == 2 && fr.notDone.empty() && anyHas(fr.toCheck, "Fin forc\xC3\xA9" "e") && anyHas(fr.created, "T_PompeA"),
          "terminees : S1, S2 ; rien << non fait >> ; a verifier : la fin forcee ; cree : le type");
    const mi::FinalReport back = mi::reportFromJson(mi::reportToJson(fr));
    check(back.forced && back.skipped == fr.skipped && back.verdict == fr.verdict, "le rapport relu : la fin forcee, les etapes ignorees");
    const std::string rt = mi::reportText(back);
    check(has(rt, "Fin forc\xC3\xA9" "e par l'utilisateur") && has(rt, "Ignor\xC3\xA9" "es (fin forc\xC3\xA9" "e)** (3)"), "le rapport en clair le dit");
    check(!mi::reportToJson(mi::FinalReport{}).has("fin_forcee"), "un rapport ordinaire n'a pas ces champs (rien de change)");
    m.finalReport = mi::reportToJson(fr);
    mi::refreshState(m, at);
    check(m.state == mi::State::Done && has(mi::nextAction(m), "consulter le rapport final"), "le rapport pose : toujours terminee, le rapport a consulter");
    // LE FICHIER : relu tel quel
    const ai::json::Value v = mi::toJson(m);
    check(v.at("fin_forcee").isObject() && v.at("fin_forcee").str("date") == at, "le fichier : << fin_forcee >>");
    int format = -1;
    bool readOnly = true;
    const mi::Mission again = mi::fromJson(v, &format, &readOnly);
    check(format == mi::kFormat && !readOnly && again.forced.on() && again.forced.date == at && again.forced.reason == m.forced.reason
              && again.forced.steps == m.forced.steps && again.forced.proposals == m.forced.proposals && again.state == mi::State::Done,
          "relu : la fin forcee, l'etat");
    check(again.step("S4") && again.step("S4")->state == S::Skipped && mi::stepText(again, *again.step("S4")) == "Ignor\xC3\xA9" "e (fin forc\xC3\xA9" "e)"
              && again.proposal("op2")->state == "refusee",
          "relu : les etapes ignorees, la proposition refusee");
    check(ai::json::write(mi::toJson(again)) == ai::json::write(v), "aller-retour exact (rien en trop)");
    check(!mi::toJson(stationOn(f)).has("fin_forcee"), "une mission sans fin forcee : le fichier ne change pas");
    // CTRL+Z APRES COUP : le projet fait foi (P1 annulee, S1 prete), la mission reste terminee
    check(static_cast<bool>(f.stack.undo()) && mi::verifyAgainst(m, f.doc->project, at), "Ctrl+Z apres la fin forcee : vu");
    check(m.proposal("op1")->state == "annulee" && m.step("S1")->state != S::Done && m.state == mi::State::Done, "P1 annulee, S1 n'est plus terminee ; la mission reste terminee");
    check(mi::activityOf(m, *m.step("S1")).text == "Mission termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : plus rien ne se fait pour elle.",
          "S1 : " + mi::activityOf(m, *m.step("S1")).text);
    (void)f.stack.redo();

    // UNE MISSION ANNULEE : sa fin ne se force pas
    mi::Mission c = stationOn(f);
    check(mi::cancel(c, kNow, &why) && !mi::mayForceFinish(c, &why) && has(why, "annul"), "annulee : refuse (" + why + ")");
    check(!mi::forceFinish(c, kNow, {}, &why) && c.state == mi::State::Cancelled && !c.forced.on(), "rien n'est change");
    // UNE MISSION TERMINEE normalement : non plus
    mi::Mission d = stationOn(f);
    d.state = mi::State::Done;
    check(!mi::forceFinish(d, kNow, {}, &why) && has(why, "d\xC3\xA9j\xC3\xA0 termin\xC3\xA9" "e") && !d.forced.on(), "terminee : refuse (" + why + ")");
    // UNE MISSION EN PAUSE, rien de fait : toutes ses etapes ignorees, une raison donnee
    mi::Mission e = stationOn(f);
    check(mi::pause(e, kNow, &why) && mi::forceFinish(e, at, "le client a change d'avis", &why), "en pause : sa fin se force (" + why + ")");
    check(e.state == mi::State::Done && e.forced.steps.size() == 5 && e.step("S1")->note == "Le client a change d'avis."
              && has(e.history.back().text, "5 \xC3\xA9tapes ignor\xC3\xA9" "es") && has(e.history.back().text, "Raison : le client a change d'avis."),
          "les cinq ignorees, la raison dite (" + e.history.back().text + ")");
    check(mi::progressOf(e).steps == 0 && mi::progressOf(e).skipped == 5, "la progression : plus rien a compter");
}

} // namespace

int main() {
    states();
    planning();
    editing();
    tracking();
    errors();
    lifecycle();
    files();
    disk();
    recovery();
    interrupted();
    searching();
    history();
    stepProgress();     // 1.22.1
    stepActivities();   // 1.22.1
    stepJournals();     // 1.22.1
    file1220();         // 1.22.1
    forcing();          // 1.22.2
    std::printf("ai_missions_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
