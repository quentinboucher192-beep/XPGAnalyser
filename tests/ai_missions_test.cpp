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
//    - pause, reprise, annulation ; la recherche et les outils chercher_missions / etat_mission.
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
    std::printf("ai_missions_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
