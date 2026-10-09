// =============================================================================
//  tests/hmi_migrate_test.cpp - 1.11.18 (refonte des scripts, lot 4) : LA MIGRATION
//  DES BLOCS VAR (hmi::migrate)
// -----------------------------------------------------------------------------
//  Chaque forme (script, fonction, operateur, fonction de symbole et ses
//  redefinitions) ; ce qui ne migre pas, et pourquoi ; le code nettoye ; aucune
//  declaration perdue ; deux fois de suite (rien la seconde) ; Ctrl+Z (les corps a
//  l'octet pres) ; le rapport ; la meme execution avant et apres ; le projet de
//  demonstration migre en entier, enregistre au format 23 et relu.
//
//      hmi_migrate_test <dossier du projet de demonstration>
// =============================================================================
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiDecl.hpp"
#include "../src/hmi/HmiMigrate.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiStore.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace hmi;
namespace dc = hmi::decl;
namespace mg = hmi::migrate;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

const mg::Item* itemOf(const mg::Plan& plan, std::string_view label) {
    for (const auto& it : plan.items)
        if (it.place.label == label) return &it;
    return nullptr;
}

std::string declsOf(const std::vector<Declaration>& decls) {
    std::string s;
    for (const auto& d : decls)
        s += std::string(declKindKey(d.kind)) + " " + d.name + " : " + d.type + (d.value.empty() ? "" : " := " + d.value) + " ["
           + std::string(storageKey(d.storage)) + "," + std::string(passModeKey(d.mode)) + "," + std::string(visibilityKey(d.visibility))
           + "]" + (d.description.empty() ? "" : " (" + d.description + ")") + "\n";
    return s;
}

Script script(Project& p, std::string name, std::string body, std::string event = "Appel") {
    Script s;
    s.id = p.allocate();
    s.name = std::move(name);
    s.event = std::move(event);
    s.body = std::move(body);
    return s;
}

// ---------------------------------------------------------------- les formes --
void formes() {
    std::printf("-- chaque forme\n");
    Project p;
    p.programs.scripts.push_back(script(p, "Compter",
                                        "(* compte les appels *)\n"
                                        "VAR (* les compteurs *)\n"
                                        "    Fois : DINT := 0;      (* depuis le lancement *)\n"
                                        "    Dernier : TIME;\n"
                                        "END_VAR\n"
                                        "VAR_TEMP\n"
                                        "    i : INT;\n"
                                        "END_VAR\n"
                                        "VAR CONSTANT\n"
                                        "    Max : INT := 10;\n"
                                        "END_VAR\n"
                                        "VAR RETAIN\n"
                                        "    Total : REAL;\n"
                                        "END_VAR\n"
                                        "\n"
                                        "Fois := Fois + 1;\n"
                                        "Total := Total + 1.0;\n"));
    HmiFunction f;
    f.id = p.allocate();
    f.name = "Ponderer";
    f.returnType = "REAL";
    f.body = "VAR_INPUT\n    A : REAL;\n    B : REAL := 0.5;   (* le poids *)\nEND_VAR\nVAR_IN_OUT\n    Acc : REAL;\nEND_VAR\nVAR_OUTPUT\n    Ok : BOOL;\nEND_VAR\n"
             "VAR\n    t : REAL;\nEND_VAR\nt := A * B;\nAcc := Acc + t;\nOk := TRUE;\nPonderer := t;\n";
    p.programs.functions.push_back(f);
    p.programs.scripts.push_back(script(p, "Parametres", "VAR_INPUT\n    x : INT;\nEND_VAR\nx := 1;\n"));
    p.programs.scripts.push_back(script(p, "Ouvert", "VAR\n    a : INT;\n\nx := 1;\n"));
    p.programs.scripts.push_back(script(p, "SansValeur", "VAR CONSTANT\n    K : INT;\nEND_VAR\nx := K;\n"));
    p.programs.scripts.push_back(script(p, "Interne",
                                        "VAR\n    n : INT;\nEND_VAR\nFUNCTION Carre(x : REAL) : REAL\nVAR\n    t : REAL;\nEND_VAR\n    Carre := x * x;\nEND_FUNCTION\nn := n + 1;\n"));
    p.programs.scripts.push_back(script(p, "SansBloc", "x := 1;\n"));
    Script half = script(p, "Moitie", "VAR\n    a : INT;\nEND_VAR\na := a + 1;\n");
    half.decls.push_back(Declaration{p.allocate(), DeclKind::Variable, "a", "INT"});
    p.programs.scripts.push_back(half);
    HmiType t;
    t.id = p.allocate();
    t.name = "T_VEC";
    HmiOperator plus;
    plus.id = p.allocate();
    plus.op = "+";
    plus.left = plus.right = plus.result = "T_VEC";
    plus.body = "VAR\n    k : REAL := 1.0;\nEND_VAR\nADD.x := A.x + B.x * k;\n";
    HmiOperator bad = plus;
    bad.id = p.allocate();
    bad.op = "-";
    bad.body = "VAR_INPUT\n    z : REAL;\nEND_VAR\nSUB.x := A.x - B.x;\n";
    t.operators = {plus, bad};
    p.programs.types.push_back(t);

    check(mg::needed(p), "le projet a des blocs a migrer");
    const auto plan = mg::plan(p);
    check(plan.items.size() == 9 && plan.migrated() == 4 && plan.skipped() == 5,
          "neuf codes a blocs ; quatre migrent, cinq restent (" + std::to_string(plan.migrated()) + "/" + std::to_string(plan.items.size()) + ")");
    const auto* c = itemOf(plan, "script Compter");
    check(c && c->migrated
              && declsOf(c->decls) == "variable Fois : DINT := 0 [conservee,entree,privee] (depuis le lancement)\n"
                                      "variable Dernier : TIME [conservee,entree,privee]\n"
                                      "variable i : INT [execution,entree,privee]\n"
                                      "constante Max : INT := 10 [execution,entree,privee]\n"
                                      "variable Total : REAL [conservee,entree,privee]\n",
          "un script : VAR Conservee, VAR_TEMP Execution, VAR CONSTANT constante, VAR RETAIN Conservee ; privees ; commentaire en documentation\n"
              + (c ? declsOf(c->decls) : std::string{}));
    check(c && c->body == "(* compte les appels *)\n(* les compteurs *)\n\nFois := Fois + 1;\nTotal := Total + 1.0;\n",
          "son code : les blocs partis, le commentaire du bloc garde a sa place\n" + (c ? c->body : std::string{}));
    check(c && c->comments == 1 && c->defaults == 2
              && std::any_of(c->notes.begin(), c->notes.end(), [](const mg::Note& n) { return n.attention && n.text.find("RETAIN") != std::string::npos; }),
          "un commentaire, deux valeurs ; RETAIN : un point d'attention (decision D3)");
    const auto* fn = itemOf(plan, "fonction Ponderer");
    check(fn && fn->migrated
              && declsOf(fn->decls) == "parametre A : REAL [execution,entree,privee]\n"
                                       "parametre B : REAL := 0.5 [execution,entree,privee] (le poids)\n"
                                       "parametre Acc : REAL [execution,entree_sortie,privee]\n"
                                       "parametre Ok : BOOL [execution,sortie,privee]\n"
                                       "variable t : REAL [execution,entree,privee]\n"
              && fn->body == "t := A * B;\nAcc := Acc + t;\nOk := TRUE;\nPonderer := t;\n",
          "une fonction : VAR_INPUT, VAR_IN_OUT, VAR_OUTPUT dans l'ordre ; sa VAR : Execution\n" + (fn ? declsOf(fn->decls) + fn->body : std::string{}));
    const auto why = [&](std::string_view label) {
        const auto* it = itemOf(plan, label);
        return it && !it->migrated ? it->why : std::string("(migre)");
    };
    check(why("script Parametres").find("param\xC3\xA8tres") != std::string::npos, "un script a VAR_INPUT : laisse tel quel - " + why("script Parametres"));
    check(why("script Ouvert").find("ligne 1") != std::string::npos && why("script Ouvert").find("sans END_VAR") != std::string::npos,
          "un bloc ouvert : laisse tel quel, a sa ligne - " + why("script Ouvert"));
    check(why("script SansValeur").find("pas de valeur") != std::string::npos, "une constante sans valeur : laissee - " + why("script SansValeur"));
    check(why("script Moitie").find("d\xC3\xA9j\xC3\xA0") != std::string::npos, "un nom deja dans le modele : laisse - " + why("script Moitie"));
    check(why("op\xC3\xA9rateur T_VEC - T_VEC : T_VEC (type T_VEC)").find("A et B") != std::string::npos
              || std::any_of(plan.items.begin(), plan.items.end(), [](const mg::Item& i) { return !i.migrated && i.why.find("A et B") != std::string::npos; }),
          "un operateur a VAR_INPUT : laisse tel quel");
    const auto* inner = itemOf(plan, "script Interne");
    check(inner && inner->migrated && inner->decls.size() == 1 && inner->body.find("FUNCTION Carre(x : REAL) : REAL\nVAR\n    t : REAL;\nEND_VAR") == 0
              && std::any_of(inner->notes.begin(), inner->notes.end(), [](const mg::Note& n) { return n.attention && n.text.find("lot 8") != std::string::npos; }),
          "une fonction interne : gardee dans le code avec ses blocs (lot 8), un point d'attention");
    check(!itemOf(plan, "script SansBloc"), "un script sans bloc : pas dans le plan");

    // Applique : une commande ; deux fois de suite ; Ctrl+Z a l'octet pres.
    auto doc = std::make_shared<Document>();
    doc->project = p;
    core::CommandStack stack;
    auto cmd = changeProject(doc, "Migrer les d\xC3\xA9" "clarations", [&](Project& q) { (void)mg::apply(q, mg::plan(q)); });
    check(cmd != nullptr && static_cast<bool>(stack.push(std::move(cmd))), "migrer : une commande");
    const auto* compter = doc->project.programs.scripts.data();
    check(compter->decls.size() == 5 && compter->body == c->body
              && std::all_of(compter->decls.begin(), compter->decls.end(), [&](const Declaration& d) { return d.id != kNoId && d.id < doc->project.nextId; }),
          "applique : les declarations, des identifiants neufs, le code nettoye");
    const auto again = mg::plan(doc->project);
    check(again.migrated() == 0 && again.items.size() == plan.skipped(), "deux fois : rien de plus (seuls les codes laisses restent)");
    (void)stack.undo();
    bool same = doc->project.programs.scripts.size() == p.programs.scripts.size();
    for (std::size_t i = 0; same && i < p.programs.scripts.size(); ++i)
        same = doc->project.programs.scripts[i].body == p.programs.scripts[i].body && doc->project.programs.scripts[i].decls == p.programs.scripts[i].decls;
    check(same && doc->project.programs.functions[0].body == f.body && doc->project.programs.functions[0].decls.empty(),
          "Ctrl+Z : les corps d'avant, a l'octet pres, sans declaration du modele");
    const std::string text = mg::report(plan);
    check(text.find("Migration des d\xC3\xA9" "clarations : 4 codes migr\xC3\xA9s, 12 d\xC3\xA9" "clarations ; 5 laiss\xC3\xA9s tels quels") == 0
              && text.find("Laiss\xC3\xA9s tels quels :") != std::string::npos && text.find("  ! ") != std::string::npos,
          "le rapport : le resume, le detail, les points d'attention, ceux qui restent\n" + text);
}

// ---------------------------------------------- une fonction de symbole et ses redefinitions
void symboles() {
    std::printf("-- une fonction de symbole et ses redefinitions\n");
    const auto project = [](const std::string& baseBody, const std::string& overBody) {
        Project p;
        View sym = makeView(p, "Vanne");
        sym.role = "symbole";
        HmiFunction f;
        f.id = p.allocate();
        f.name = "Ouvrir";
        f.isVirtual = true;
        f.body = baseBody;
        sym.functions.push_back(f);
        p.views.push_back(sym);
        View use = makeView(p, "Synoptique");
        Object inst;
        inst.id = p.allocate();
        inst.kind = Kind::SymbolInstance;
        inst.name = "V1";
        inst.props.push_back({"symbol", "Vanne", ""});
        inst.functionOverrides.push_back({"Ouvrir", overBody, {}});
        use.objects.push_back(inst);
        p.views.push_back(use);
        return p;
    };
    const std::string base = "VAR_INPUT\n    Pct : INT := 100;\nEND_VAR\nOuvert := Pct > 0;\n";
    {
        const Project p = project(base, "VAR_INPUT\n    Pct : INT := 100;\nEND_VAR\nVAR\n    k : INT;\nEND_VAR\nOuvert := FALSE;\n");
        const auto plan = mg::plan(p);
        const auto* b = itemOf(plan, "fonction Vanne.Ouvrir");
        const auto* o = itemOf(plan, "Synoptique/V1.Ouvrir (red\xC3\xA9" "finition)");
        check(b && b->migrated && o && o->migrated && declsOf(o->decls) == "variable k : INT [execution,entree,privee]\n"
                  && o->body == "Ouvert := FALSE;\n",
              "les memes parametres : la fonction et sa redefinition migrent ; la redefinition garde ses seules locales");
        Project q = p;
        (void)mg::apply(q, plan);
        uniqueDeclarationIds(q);
        const auto& fo = q.views[1].objects[0].functionOverrides[0];
        check(dc::codeOf(fo, &q.views[0].functions[0]).rfind("VAR_INPUT Pct : INT := 100; END_VAR VAR k : INT; END_VAR ", 0) == 0,
              "apres : la redefinition lit les parametres de sa fonction, puis ses locales");
        // Seule la redefinition demandee : sa fonction et la famille suivent.
        const auto only = mg::plan(p, [](const mg::Place& at) { return at.kind == mg::Place::Kind::Override; });
        check(only.migrated() == 2, "Migrer la redefinition : sa fonction suit (" + std::to_string(only.migrated()) + ")");
    }
    {
        const Project p = project(base, "VAR_INPUT\n    Pct : REAL;\nEND_VAR\nOuvert := FALSE;\n");
        const auto plan = mg::plan(p);
        const auto* b = itemOf(plan, "fonction Vanne.Ouvrir");
        const auto* o = itemOf(plan, "Synoptique/V1.Ouvrir (red\xC3\xA9" "finition)");
        check(b && !b->migrated && o && !o->migrated && o->why.find("diff\xC3\xA8rent") != std::string::npos
                  && b->why.find("ne peut pas suivre") != std::string::npos,
              "des parametres differents : ni la fonction ni la redefinition (sinon declares deux fois) - " + (b ? b->why : std::string{}));
    }
}

// ---------------------------------------------- la meme execution -------------
class FakePlc final : public sim::Environment {
public:
    bool read(std::string_view, sim::Value&) override { return false; }
    bool write(std::string_view, const sim::Value&) override { return false; }
    bool exists(std::string_view) override { return false; }
    bool call(std::string_view, std::string_view, const std::vector<std::pair<std::string, sim::Value>>&, sim::Value&) override { return false; }
    void report(sim::Diagnostic) override {}
};

std::string run(const Project& p) {
    FakePlc plc;
    Runtime rt;
    rt.bind(&p, &plc);
    rt.start(0.0);
    std::string out;
    for (int k = 0; k < 3; ++k) {
        std::string why;
        const bool ok = rt.callScript("Compter", 0.1 * k, &why);
        out += (ok ? std::string("ok") : "erreur " + why);
        for (const char* v : {"Total", "Fois", "Moy"}) out += std::string(" ") + v + "=" + (rt.variable(v) ? rt.variable(v)->display() : "?");
        out += "\n";
    }
    return out;
}

void execution() {
    std::printf("-- la meme execution avant et apres\n");
    Project p;
    for (const char* n : {"Total", "Fois", "Moy"}) {
        Variable v;
        v.id = p.allocate();
        v.name = n;
        v.type = std::string(n) == "Moy" ? "REAL" : "INT";
        p.programs.variables.push_back(v);
    }
    HmiFunction f;
    f.id = p.allocate();
    f.name = "Moyenne";
    f.returnType = "REAL";
    f.body = "VAR_INPUT\n    a : REAL;\n    b : REAL := 1.0;\nEND_VAR\nVAR\n    s : REAL;\nEND_VAR\ns := a + b;\nMoyenne := s / 2.0;\n";
    p.programs.functions.push_back(f);
    p.programs.scripts.push_back(script(p, "Compter",
                                        "VAR\n    Compteur : INT := 0;\nEND_VAR\nVAR_TEMP\n    Tmp : INT := 10;\nEND_VAR\n"
                                        "Compteur := Compteur + 1;\nTmp := Tmp + 1;\nTotal := Compteur;\nFois := Tmp;\nMoy := Moyenne(5.0);\n"));
    const std::string before = run(p);
    Project q = p;
    check(mg::apply(q, mg::plan(q)) == 2, "deux codes migres");
    const std::string after = run(q);
    check(before == after && before.find("Total=3 Fois=11 Moy=3") != std::string::npos,
          "trois appels : la meme execution avant et apres la migration\n" + before + after);
}

// ---------------------------------------------- le projet de demonstration -----
void demonstration(const std::string& folder) {
    std::printf("-- le projet de demonstration : %s\n", folder.c_str());
    auto loaded = load(folder);
    check(loaded.has_value(), "le projet s'ouvre");
    if (!loaded.has_value()) return;
    Project p = std::move(loaded.value());
    const auto plan = mg::plan(p);
    std::printf("  %s\n", mg::summary(plan).c_str());
    check(plan.items.size() == 4 && plan.declarations() == 14 && plan.skipped() == 0, "chaque code a blocs migre (" + mg::summary(plan) + ")\n" + mg::report(plan));
    // Aucune declaration perdue : celles du modele, recomposees en blocs et relues, sont celles d'avant.
    for (const auto& it : plan.items) {
        const auto& label = it.place.label;
        // Le corps d'origine : retrouve par le libelle (scripts et fonctions generaux).
        const Script* s = nullptr;
        const HmiFunction* fn = nullptr;
        for (const auto& x : p.programs.scripts)
            if ("script " + x.name == label) s = &x;
        for (const auto& x : p.programs.functions)
            if ("fonction " + x.name == label) fn = &x;
        if (!s && !fn) continue;
        const auto original = dc::extract(s ? s->body : fn->body);
        std::string want, got;
        for (const auto& d : original.decls) want += d.name + ":" + d.type + ":" + d.initial + ":" + d.comment + ";";
        for (const auto& d : it.decls) got += d.name + ":" + d.type + ":" + d.value + ":" + d.description + ";";
        check(want == got, label + " : aucune declaration perdue\n  avant : " + want + "\n  apres : " + got);
    }
    Project migrated = p;
    (void)mg::apply(migrated, plan);
    check(!mg::needed(migrated), "migre : plus aucun bloc VAR dans le code");
    // Enregistre (format 23) et relu : le meme projet.
    const auto files = serializeProject(migrated);
    std::string index(files.front().data->begin(), files.front().data->end());
    check(index.find("ihm format=23") != std::string::npos, "enregistre au format 23");
    const FileReader reader = [&](const std::string& path, std::string& content) {
        for (const auto& f : files)
            if (f.path == path) {
                content.assign(f.data->begin(), f.data->end());
                return true;
            }
        return false;
    };
    const auto back = parseProject(reader);
    check(back.has_value() && back.value().programs.scripts == migrated.programs.scripts
              && back.value().programs.functions == migrated.programs.functions,
          "relu : les memes scripts et fonctions (corps et declarations)");
    // Compiler : aucune faute nouvelle.
    std::size_t before = 0, after = 0;
    for (const auto& s : p.programs.scripts) before += checkScript(s).size();
    for (const auto& s : migrated.programs.scripts) after += checkScript(s).size();
    for (const auto& f : p.programs.functions) before += checkFunction(f).size();
    for (const auto& f : migrated.programs.functions) after += checkFunction(f).size();
    check(before == after, "compiler : autant de constats avant et apres (" + std::to_string(before) + " / " + std::to_string(after) + ")");
}

} // namespace

int main(int argc, char** argv) {
    std::printf("-- 1.11.18 (refonte des scripts, lot 4) : la migration des blocs VAR\n");
    formes();
    symboles();
    execution();
    if (argc > 1) demonstration(argv[1]);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
