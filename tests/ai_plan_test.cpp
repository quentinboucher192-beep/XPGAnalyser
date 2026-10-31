// =============================================================================
//  tests/ai_plan_test.cpp - 1.19.0 : le plan d'operations, la transaction, le compte rendu, les questions au projet
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau, sur un vrai projet IHM (un Document, sa pile Ctrl+Z) :
//    - l'ordre : une proposition ecrite A L'ENVERS (les objets avant leur vue, la variable avant son type, l'alarme
//      avant sa variable) est ordonnee par ses dependances, validee, appliquee en UNE commande ;
//    - scenario E (des erreurs volontaires) : chaque etape dit son erreur, ce qui en depend est ignore (et dit
//      pourquoi), un cycle de types est bloquant, RIEN ne s'applique (le projet est identique, octet pour octet) ;
//      << appliquer malgre les erreurs >> n'applique que ce qui est valide ;
//    - scenario F (l'annulation) : apres Appliquer, Ctrl+Z rend le projet EXACTEMENT d'avant, Ctrl+Y celui d'apres ;
//    - scenario H (la fidelite) : le compte rendu lu dans le projet (avant / apres) - les crees, les reutilises (jamais
//      comptes comme crees), une etape retiree (ignoree, pas appliquee) ; le plan et le compte rendu relus du fichier ;
//    - les questions au projet par l'IA integree : relations, << Verifie tout mon projet >> (des corrections proposees,
//      jamais appliquees), << Optimise les adresses >> ;
//    - scenario G (la taille) : 40 pompes, 400 variables, 40 alarmes, 120 objets - le temps de la validation et de
//      l'application.
// =============================================================================
#include "../src/app/ai/AiLocal.hpp"
#include "../src/app/ai/AiOperations.hpp"
#include "../src/app/ai/AiPlan.hpp"
#include "../src/app/ai/AiProjectAnswers.hpp"
#include "../src/core/Command.hpp"
#include "../src/hmi/HmiAudit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ai = app::ai;
namespace plan = app::ai::plan;
using A = ai::Operation::Action;

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
std::string errorsOf(const ai::Check& c) {
    std::string s;
    for (const auto& e : c.errors) s += e + " | ";
    return s;
}

// Le projet entier, tel que le disque l'ecrirait : deux projets egaux ont la meme empreinte. Sauf le compteur des
// identifiants (prochain_id) : il ne recule JAMAIS, meme a Ctrl+Z (un identifiant n'est jamais reutilise - HmiCommands).
std::string print(const hmi::Project& p) {
    std::string s;
    for (const auto& f : hmi::serializeProject(p)) {
        s += f.path + "\n";
        if (f.data) s.append(f.data->begin(), f.data->end());
        s += "\n";
    }
    if (const auto at = s.find("prochain_id="); at != std::string::npos) {
        std::size_t end = at + 12;
        while (end < s.size() && s[end] >= '0' && s[end] <= '9') ++end;
        s.erase(at, end - at);
    }
    return s;
}

// Un vrai projet : la vue Vue_Process (ouverte), un type T_Moteur, deux variables, un equipement Modbus simule.
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
    [[nodiscard]] ai::Proposal proposal(std::vector<ai::Operation> ops) const {
        ai::Proposal p;
        p.id = "op1";
        p.request = "essai";
        p.summary = "Essai";
        p.ops = std::move(ops);
        ai::scopeProposal(p, doc->project.view(view));
        p.fingerprint = ai::proposalPrint(doc->project, p);
        return p;
    }
    bool apply(const ai::Proposal& p, std::string* why = nullptr) {
        auto cmd = ai::makeCommand(doc, p, why);
        return cmd && static_cast<bool>(stack.push(std::move(cmd)));
    }
};

ai::Operation op(A a, std::string name = {}, std::vector<std::pair<std::string, std::string>> props = {}) {
    ai::Operation o;
    o.action = a;
    o.name = std::move(name);
    o.props = std::move(props);
    return o;
}
ai::Operation create(std::string kind, std::string name, std::vector<std::pair<std::string, std::string>> props = {}, std::string target = {}) {
    ai::Operation o = op(A::Create, std::move(name), std::move(props));
    o.kind = std::move(kind);
    o.target = std::move(target);
    return o;
}
ai::Operation vars(std::vector<ai::Operation::VariableSpec> v) {
    ai::Operation o = op(A::Variables);
    o.variables = std::move(v);
    return o;
}
ai::Operation type(std::string name, std::vector<ai::Operation::MemberSpec> m) {
    ai::Operation o = op(A::Type, std::move(name));
    o.members = std::move(m);
    return o;
}
ai::Operation address(std::string variable, std::string equipment, std::string at = {}) {
    ai::Operation o = op(A::Address);
    o.variable = std::move(variable);
    o.props = {{"equipement", std::move(equipment)}};
    if (!at.empty()) o.props.emplace_back("adresse", std::move(at));
    return o;
}

// Une petite installation ecrite A L'ENVERS : les objets de la vue avant la vue, l'alarme et le script avant la
// variable, la variable avant son type, l'adresse avant l'equipement.
std::vector<ai::Operation> backwards() {
    return {
        create("Indicator", "Voyant_P", {{"x", "80"}, {"y", "80"}, {"variable", "Pompe_A.Marche"}}, "Vue_Pompe_A"),
        create("Rectangle", "Fond_P", {{"x", "40"}, {"y", "40"}, {"w", "300"}, {"h", "200"}}, "Vue_Pompe_A"),
        op(A::Alarm, "Defaut_Pompe_A", {{"condition", "Pompe_A.Defaut"}, {"message", "Pompe A en d\xC3\xA9" "faut"}, {"priorite", "2"}}),
        op(A::Script, "Bilan_Pompe_A", {{"langage", "ST"}, {"evenement", "Cyclique"}, {"periode", "1000"},
                                        {"corps", "IF Pompe_A.Marche THEN Pompe_A.Heures := Pompe_A.Heures + 1; END_IF;"}}),
        address("Pompe_A", "Modbus_A"),
        vars({{"Pompe_A", "T_PompeA", "La pompe A", ""}}),
        op(A::Equipment, "Modbus_A", {{"type", "modbus_tcp"}, {"ip", "127.0.0.1"}, {"port", "5020"}, {"esclave", "1"}, {"simule", "oui"}}),
        type("T_PompeA", {{"Marche", "BOOL", ""}, {"Defaut", "BOOL", ""}, {"Heures", "DINT", ""}}),
        op(A::View, "Vue_Pompe_A", {{"largeur", "800"}, {"hauteur", "600"}}),
    };
}

// ---- l'ordre ---------------------------------------------------------------------------------------------------------
void order() {
    std::printf("l'ordre\n");
    Fixture f;
    auto p = f.proposal(backwards());
    const auto ck = ai::validate(f.doc->project, p);
    check(ck.ok(), "ecrite a l'envers : validee (" + errorsOf(ck) + ")");
    check(ck.order.size() == p.ops.size(), "toutes les operations dans le plan");
    const auto at = [&](std::size_t i) { return static_cast<std::size_t>(std::find(ck.order.begin(), ck.order.end(), i) - ck.order.begin()); };
    check(at(7) < at(5) && at(5) < at(4) && at(6) < at(4) && at(5) < at(2) && at(5) < at(3) && at(8) < at(0) && at(8) < at(1),
          "type < variable < adresse ; equipement < adresse ; variable < alarme, script ; vue < ses objets");
    const plan::Plan pl = plan::build(f.doc->project, p, ck);
    check(pl.steps.size() == p.ops.size() && pl.steps.front().id == "E1" && pl.steps.front().op == ck.order.front(), "les etapes E1.. dans l'ordre d'execution");
    check(pl.worthShowing(), "une demande qui merite un plan affiche");
    const plan::Step* var = pl.byOp(5);
    const plan::Step* typ = pl.byOp(7);
    check(var && typ && std::find(var->deps.begin(), var->deps.end(), typ->id) != var->deps.end(), "la variable depend de son type (" + (var ? var->title : "") + ")");
    check(var && var->category == plan::Category::Types && typ && has(typ->title, "T_PompeA"), "categorie et titre");
    std::size_t validated = 0;
    for (const auto& s : pl.steps) validated += s.state == plan::State::Validated;
    check(validated == pl.steps.size(), "toutes validees");
    // l'ordre d'ecriture ne change rien : la proposition a l'endroit donne le meme plan (les memes etapes)
    auto ops = backwards();
    std::reverse(ops.begin(), ops.end());
    auto q = f.proposal(ops);
    const auto ck2 = ai::validate(f.doc->project, q);
    check(ck2.ok() && ck2.create == ck.create && ck2.newVariables.size() == ck.newVariables.size(), "a l'endroit : le meme resultat");
    const std::size_t steps = f.stack.done().size();
    std::string why;
    check(f.apply(p, &why) && f.stack.done().size() == steps + 1, "appliquee en UNE commande (" + why + ")");
    const auto& pr = f.doc->project;
    const hmi::View* v = pr.viewByName("Vue_Pompe_A");
    check(pr.hmiTypeByName("T_PompeA") && pr.variable("Pompe_A") && pr.equipmentByName("Modbus_A") && pr.alarmByName("Defaut_Pompe_A")
              && pr.generalScript("Bilan_Pompe_A") && v && v->objects.size() == 2,
          "tout est cree (type, variable, equipement, alarme, script, vue et ses deux objets)");
    check(pr.variable("Pompe_A") && pr.variable("Pompe_A")->equipment == "Modbus_A", "la variable est adressee sur l'equipement");
    check(f.doc->project.view(f.view) && f.doc->project.view(f.view)->objects.empty(), "la vue ouverte n'a pas recu les objets d'une autre vue");
}

// ---- E : des erreurs volontaires -------------------------------------------------------------------------------------
void errors() {
    std::printf("E - des erreurs volontaires\n");
    Fixture f;
    const std::string before = print(f.doc->project);
    auto p = f.proposal({
        vars({{"Niveau_X", "REEL_INCONNU", "", ""}}),                                     // 0 un type inconnu
        op(A::Alarm, "Alarme_X", {{"condition", "Niveau_X > 80"}, {"message", "haut"}}), // 1 depend de 0
        vars({{"Pompe_1_Marche", "INT", "", ""}}),                                        // 2 un nom pris avec un autre type
        address("Consigne_Generale", "Equipement_Absent"),                                // 3 un equipement absent
        type("T_Boucle_A", {{"B", "T_Boucle_B", ""}}),                                    // 4 et 5 : un cycle de types
        type("T_Boucle_B", {{"A", "T_Boucle_A", ""}}),
        vars({{"Bon_1", "BOOL", "", ""}}),                                                // 6 valide
        create("Indicator", "Voyant_Bon", {{"variable", "Bon_1"}}),                       // 7 depend de 6 (valide)
    });
    const auto ck = ai::validate(f.doc->project, p);
    check(!ck.ok(), "refusee");
    check(anyHas(ck.errors, "REEL_INCONNU"), "le type inconnu dit");
    check(anyHas(ck.errors, "Pompe_1_Marche") && anyHas(ck.errors, "porte d\xC3\xA9j\xC3\xA0 ce nom avec le type BOOL"), "le nom pris dit, avec son type");
    check(anyHas(ck.errors, "Equipement_Absent"), "l'equipement absent dit");
    check(!ck.planErrors.empty() && anyHas(ck.planErrors, "T_Boucle"), "le cycle de types : une erreur du plan (" + (ck.planErrors.empty() ? std::string{} : ck.planErrors.front()) + ")");
    const plan::Plan pl = plan::build(f.doc->project, p, ck);
    const auto state = [&](std::size_t i) { return pl.byOp(i) ? pl.byOp(i)->state : plan::State::ToDo; };
    check(state(0) == plan::State::Failed && state(2) == plan::State::Failed && state(3) == plan::State::Failed, "les etapes fautives : echouees");
    check(state(1) == plan::State::Skipped && pl.byOp(1) && has(pl.byOp(1)->note, "d\xC3\xA9pend de"), "l'alarme de la variable refusee : ignoree, et pourquoi");
    check(state(4) == plan::State::Failed && state(5) == plan::State::Failed, "le cycle : bloquant pour ses deux types");
    check(state(6) == plan::State::Validated && state(7) == plan::State::Validated, "ce qui est sain reste valide");
    std::string why;
    check(!f.apply(p, &why) && print(f.doc->project) == before && f.stack.done().empty(), "RIEN ne s'applique : le projet est identique (" + why + ")");
    // appliquer malgre les erreurs : seulement le valide
    std::vector<std::string> skipped;
    auto cmd = ai::makeCommandLenient(f.doc, p, skipped, &why);
    check(cmd && f.stack.push(std::move(cmd)), "malgre les erreurs : une commande");
    check(f.doc->project.variable("Bon_1") && !f.doc->project.variable("Niveau_X") && !f.doc->project.alarmByName("Alarme_X")
              && f.doc->project.variable("Pompe_1_Marche")->type == "BOOL",
          "seul le valide est applique (Bon_1) ; Pompe_1_Marche n'est pas remplacee");
    check(!skipped.empty(), "les erreurs laissees de cote sont dites");
    (void)f.stack.undo();
    check(print(f.doc->project) == before, "Ctrl+Z : le projet d'avant, exactement");
}

// ---- F : l'annulation ------------------------------------------------------------------------------------------------
void undoRedo() {
    std::printf("F - l'annulation\n");
    Fixture f;
    const std::string before = print(f.doc->project);
    auto ops = backwards();
    ops.push_back(op(A::Folder, {}, {{"liste", "vues"}, {"dossier", "Pompes"}}));
    auto p = f.proposal(ops);
    std::string why;
    check(f.apply(p, &why), "appliquee (" + why + ")");
    const std::string after = print(f.doc->project);
    check(after != before, "le projet a change");
    const bool undone = static_cast<bool>(f.stack.undo());
    check(undone && print(f.doc->project) == before, "Ctrl+Z : EXACTEMENT le projet d'avant (types, variables, equipement, adresses, alarme, script, vue, dossier)");
    check(f.stack.redo() && print(f.doc->project) == after, "Ctrl+Y : EXACTEMENT le projet d'apres");
    check(f.stack.undo() && print(f.doc->project) == before, "Ctrl+Z encore : d'avant");
    // la proposition reconnait son etat (appliquee, annulee)
    ai::Proposal applied = p;
    check(static_cast<bool>(f.stack.redo()), "Ctrl+Y");
    applied.fingerprintAfter = ai::proposalPrint(f.doc->project, applied);
    check(applied.fingerprintAfter != p.fingerprint, "l'empreinte apres differe");
    (void)f.stack.undo();
    check(ai::proposalPrint(f.doc->project, applied) == p.fingerprint, "annulee : l'empreinte d'avant");
}

// ---- H : la fidelite du compte rendu ---------------------------------------------------------------------------------
void report() {
    std::printf("H - le compte rendu\n");
    Fixture f;
    auto ops = backwards();
    ops.push_back(type("T_Moteur", {{"Marche", "BOOL", ""}, {"Vitesse", "REAL", ""}}));   // identique : reutilise
    ops.push_back(vars({{"Pompe_1_Marche", "BOOL", "", ""}}));                              // identique : reutilisee
    ops.push_back(create("Rectangle", "Decor_Facultatif", {{"x", "500"}, {"y", "40"}}, "Vue_Pompe_A"));
    auto p = f.proposal(ops);
    const std::size_t optionalOp = p.ops.size() - 1;
    p.excluded = {optionalOp};   // l'utilisateur la retire
    const hmi::Project before = f.doc->project;
    const auto ck = ai::validate(f.doc->project, p);
    check(ck.ok(), "validee avec une etape retiree (" + errorsOf(ck) + ")");
    check(ck.reused.size() >= 2, "deux reutilisations annoncees (" + std::to_string(ck.reused.size()) + ")");
    plan::Plan pl = plan::build(f.doc->project, p, ck);
    check(pl.byOp(optionalOp) && pl.byOp(optionalOp)->excluded && pl.byOp(optionalOp)->optional, "l'etape retiree : facultative et retiree");
    check(pl.byOp(p.ops.size() - 3) && pl.byOp(p.ops.size() - 3)->verb == plan::Verb::Reuse, "le type identique : une reutilisation");
    std::string why;
    check(f.apply(p, &why), "appliquee (" + why + ")");
    const plan::Report rep = plan::report(before, f.doc->project, pl, p);
    const auto line = [&](const std::vector<plan::Line>& lines, const std::string& cat) -> const plan::Line* {
        for (const auto& l : lines)
            if (l.category == cat) return &l;
        return nullptr;
    };
    const plan::Line* types = line(rep.created, "Types");
    const plan::Line* variables = line(rep.created, "Variables");
    check(types && types->names.size() == 1 && types->names[0] == "T_PompeA", "cree : UN type (T_Moteur reutilise n'est pas compte)");
    check(variables && variables->names.size() == 1 && variables->names[0] == "Pompe_A", "cree : UNE variable (Pompe_1_Marche reutilisee n'est pas comptee)");
    check(line(rep.created, "\xC3\x89quipements") && line(rep.created, "Alarmes") && line(rep.created, "Scripts") && line(rep.created, "Vue"),
          "cree : l'equipement, l'alarme, le script, la vue");
    const plan::Line* objects = line(rep.created, "Objets dans les vues");
    check(objects && objects->names.size() == 1 && has(objects->names[0], "Vue_Pompe_A : 2"), "deux objets poses (le decor retire n'y est pas)");
    const plan::Line* reusedTypes = line(rep.reused, "Types");
    check(reusedTypes && std::find(reusedTypes->names.begin(), reusedTypes->names.end(), "T_Moteur") != reusedTypes->names.end(), "reutilise : T_Moteur");
    check(rep.skipped == 1 && anyHas(rep.failures, "Decor_Facultatif") && anyHas(rep.failures, "non ex\xC3\xA9" "cut\xC3\xA9" "e"),
          "l'etape retiree : non executee, dite");
    check(rep.applied + rep.skipped == rep.planned && rep.failed == 0, "les comptes : appliquees + ignorees = prevues");
    check(anyHas(rep.todo, "Modbus_A") && anyHas(rep.todo, "simul"), "a faire : l'adresse IP definitive de l'equipement simule");
    for (const auto& s : pl.steps)
        if (!s.excluded) check(s.state == plan::State::Applied, "etape " + s.id + " confirmee dans le projet");
    // relus du fichier (la conversation se rouvre)
    const plan::Plan pl2 = plan::fromJson(plan::toJson(pl));
    const plan::Report rep2 = plan::reportFromJson(plan::reportToJson(rep));
    check(pl2.steps.size() == pl.steps.size() && pl2.steps.back().state == pl.steps.back().state && pl2.steps.front().title == pl.steps.front().title,
          "le plan relu : ses etapes, leurs etats");
    check(rep2.verdict == rep.verdict && rep2.createdCount() == rep.createdCount() && rep2.failures == rep.failures, "le compte rendu relu");
    ai::Proposal saved = p;
    saved.plan = plan::toJson(pl);
    saved.report = plan::reportToJson(rep);
    const ai::Proposal back = ai::proposalFromJson(ai::toJson(saved));
    check(back.excluded == p.excluded && back.plan.isObject() && back.report.isObject(), "la proposition garde son plan, son compte rendu, ses retraits");
    std::printf("  %s\n", plan::reportText(rep, "essai").substr(0, 200).c_str());
}

// ---- les questions au projet (l'IA integree) -------------------------------------------------------------------------
ai::LocalReply ask(const hmi::Project& project, const hmi::View* view, const std::string& text) {
    ai::PromptInput in;
    in.project = &project;
    in.view = view;
    in.request = text;
    return ai::understand(in, text);
}

void questions() {
    std::printf("les questions au projet\n");
    Fixture f;
    std::string why;
    check(f.apply(f.proposal(backwards()), &why), "la petite installation posee (" + why + ")");
    const auto& pr = f.doc->project;
    const hmi::View* open = pr.view(f.view);
    auto r = ask(pr, open, "Quels objets utilisent Pompe_A ?");
    check(has(r.text, "Voyant_P") && !has(r.text, "Fond_P") && !r.proposal, "objets de Pompe_A : Voyant_P (pas le fond) - " + r.text.substr(0, 120));
    r = ask(pr, open, "Montre-moi les scripts qui utilisent Pompe_A.");
    check(has(r.text, "Bilan_Pompe_A"), "scripts de Pompe_A : Bilan_Pompe_A");
    r = ask(pr, open, "Quelles alarmes concernent la pompe A ?");
    check(has(r.text, "Defaut_Pompe_A") || has(r.text, "De quel"), "alarmes de la pompe A (ou la question du sujet)");
    r = ask(pr, open, "Quelles alarmes concernent Pompe_A ?");
    check(has(r.text, "Defaut_Pompe_A"), "alarmes de Pompe_A : Defaut_Pompe_A");
    r = ask(pr, open, "Quels \xC3\xA9quipements sont utilis\xC3\xA9s dans la vue Vue_Pompe_A ?");
    check(has(r.text, "Modbus_A"), "les equipements de Vue_Pompe_A : Modbus_A");
    r = ask(pr, open, "Si je supprime le type T_PompeA, que se passe-t-il ?");
    check(has(r.text, "Pompe_A") && has(r.text, "simulation") && !r.proposal, "l'impact de T_PompeA : Pompe_A touchee, rien de supprime");
    r = ask(pr, open, "Quels objets sont orphelins ?");
    check(has(r.text, "Consigne_Generale") && has(r.text, "orphelin"), "les orphelins : Consigne_Generale (jamais lue)");
    r = ask(pr, open, "Quels objets utilisent Variable_Inventee ?");
    check(has(r.text, "aucun") || has(r.text, "De quel"), "un nom absent : rien d'invente");
    // verifier : un chevauchement volontaire, sa correction proposee, jamais appliquee seule
    {
        Fixture g;
        auto ops = backwards();
        ops.push_back(vars({{"Debit_A", "REAL", "", ""}}));
        ops.push_back(address("Debit_A", "Modbus_A"));
        check(g.apply(g.proposal(ops), &why), "installation + Debit_A");
        auto& gp = g.doc->project;
        const std::string pompeAt = gp.variable("Pompe_A") ? gp.variable("Pompe_A")->address : std::string{};
        for (auto& d : gp.programs.variables)
            if (d.name == "Debit_A") d.address = pompeAt;   // a la main : sur la pompe (un chevauchement)
        const std::string before = print(gp);
        r = ask(gp, gp.view(g.view), "V\xC3\xA9rifie tout mon projet et propose les corrections.");
        check(has(r.text, "V\xC3\xA9rification du projet") && has(r.text, "erreur") && has(r.text, "avertissement"), "le rapport : les comptes par gravite");
        check(print(gp) == before, "verifier ne modifie rien");
        check(r.proposal.has_value() && !r.proposal->ops.empty() && r.proposal->ops.front().action == A::Address, "la correction d'adresse : une proposition (adresser)");
        if (r.proposal) {
            ai::Proposal fix = *r.proposal;
            fix.fingerprint = ai::proposalPrint(gp, fix);
            check(g.apply(fix, &why), "la correction validee puis appliquee (" + why + ")");
            check(hmi::audit::overlaps(gp).empty(), "plus de chevauchement");
            (void)g.stack.undo();
            check(print(gp) == before, "Ctrl+Z : la correction reprise");
        }
        r = ask(gp, gp.view(g.view), "Optimise les adresses de Debit_A");
        check(!r.text.empty() && (r.proposal.has_value() || has(r.text, "Rien")), "optimiser : un plan, ou rien a faire");
        check(print(gp) == before, "optimiser ne modifie rien");
    }
    // une demande simple : pas de plan
    {
        Fixture h;
        auto p = h.proposal({create("Indicator", "Seul", {{"x", "10"}, {"y", "10"}})});
        const auto ck = ai::validate(h.doc->project, p);
        check(!plan::build(h.doc->project, p, ck).worthShowing(), "une seule operation : pas de plan affiche");
    }
}

// ---- G : la taille ---------------------------------------------------------------------------------------------------
void large() {
    std::printf("G - une grande installation\n");
    Fixture f;
    constexpr int kPumps = 40;
    std::vector<ai::Operation> ops;
    ops.push_back(type("T_PompeG", {{"Marche", "BOOL", ""}, {"Defaut", "BOOL", ""}, {"Disponible", "BOOL", ""}, {"Mode_Auto", "BOOL", ""},
                                    {"Consigne", "REAL", ""}, {"Intensite", "REAL", ""}, {"Heures_Fonctionnement", "DINT", ""}}));
    ops.push_back(op(A::Equipment, "Modbus_G", {{"type", "modbus_tcp"}, {"ip", "127.0.0.1"}, {"port", "5021"}, {"esclave", "1"}, {"simule", "oui"}}));
    ops.push_back(op(A::View, "Vue_Grande", {{"largeur", "1920"}, {"hauteur", "1080"}}));
    std::vector<ai::Operation::VariableSpec> specs;
    for (int k = 1; k <= kPumps; ++k) {
        const std::string n = "PompeG_" + std::to_string(k);
        specs.push_back({n, "T_PompeG", "", ""});
        for (int m = 0; m < 9; ++m) specs.push_back({n + "_Mesure_" + std::to_string(m), "REAL", "", ""});
        ops.push_back(address(n, "Modbus_G"));
        ops.push_back(op(A::Alarm, "Defaut_" + n, {{"condition", n + ".Defaut"}, {"message", n + " en d\xC3\xA9" "faut"}}));
        const std::string x = std::to_string(20 + (k % 10) * 180), y = std::to_string(20 + (k / 10) * 200);
        ops.push_back(create("Indicator", "Etat_" + n, {{"x", x}, {"y", y}, {"variable", n + ".Marche"}}, "Vue_Grande"));
        ops.push_back(create("Indicator", "Defaut_V_" + n, {{"x", x}, {"y", std::to_string(60 + (k / 10) * 200)}, {"variable", n + ".Defaut"}}, "Vue_Grande"));
        ops.push_back(create("Rectangle", "Cadre_" + n, {{"x", x}, {"y", std::to_string(100 + (k / 10) * 200)}, {"w", "40"}, {"h", "20"}}, "Vue_Grande"));
    }
    ops.push_back(vars(std::move(specs)));
    auto p = f.proposal(ops);
    const auto t0 = std::chrono::steady_clock::now();
    const auto ck = ai::validate(f.doc->project, p);
    const auto t1 = std::chrono::steady_clock::now();
    const plan::Plan pl = plan::build(f.doc->project, p, ck);
    const auto t2 = std::chrono::steady_clock::now();
    const hmi::Project before = f.doc->project;
    std::string why;
    const bool ok = f.apply(p, &why);
    const auto t3 = std::chrono::steady_clock::now();
    plan::Plan applied = pl;
    const plan::Report rep = plan::report(before, f.doc->project, applied, p);
    const auto t4 = std::chrono::steady_clock::now();
    const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    check(ck.ok(), "validee (" + errorsOf(ck).substr(0, 200) + ")");
    check(ok, "appliquee (" + why + ")");
    check(f.doc->project.programs.variables.size() == 2 + kPumps * 10 && f.doc->project.alarms.size() == static_cast<std::size_t>(kPumps)
              && f.doc->project.viewByName("Vue_Grande") && f.doc->project.viewByName("Vue_Grande")->objects.size() == static_cast<std::size_t>(kPumps * 3),
          "400 variables, 40 alarmes, 120 objets");
    check(rep.applied == pl.steps.size() && rep.failed == 0, "toutes les etapes appliquees et confirmees");
    const double total = ms(t0, t4);
    std::printf("  %zu operations : valider %.0f ms, plan %.0f ms, appliquer %.0f ms, compte rendu %.0f ms - total %.0f ms\n", p.ops.size(), ms(t0, t1),
                ms(t1, t2), ms(t2, t3), ms(t3, t4), total);
    check(total < 15000.0, "moins de 15 s en -O0 (" + std::to_string(static_cast<int>(total)) + " ms)");
}

} // namespace

int main() {
    order();
    errors();
    undoRedo();
    report();
    questions();
    large();
    std::printf("ai_plan_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
