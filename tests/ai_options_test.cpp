// =============================================================================
//  tests/ai_options_test.cpp - 1.16.0 : l'Assistant IA, les nouvelles operations (chantier OPTIONS)
// -----------------------------------------------------------------------------
//  Sans ecran ni reseau :
//    - creer_vue : la vue neuve et ses objets, UNE commande, Ctrl+Z la retire ; refusee (nom pris, pas en
//      premier, un reglage inconnu) ; l'apercu ; l'empreinte (absente -> appliquee -> annulee) ;
//    - creer_variables : les regles du logiciel (nom, type, valeur initiale), puis associer et une formule
//      dans la meme proposition ; une proposition sans vue ;
//    - creer_type : les membres, l'ordre des dependances ;
//    - les formules (fx) : couleur, visibilite ; refusees (nom absent, ecriture, vide) ;
//    - dupliquer avec reperes : 3 copies = le moteur de Ctrl+Maj+D ; copies et decalage ;
//    - le JSON (une conversation relue), la lecture stricte de l'outil, son schema ;
//    - l'automate relie (un faux Env : globales, DDT) -> les operations proposees ;
//    - le contexte (les DDT, les types IHM) ; l'Assistant : la vue creee s'ouvre, appliquee, annulee.
// =============================================================================
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiAutomate.hpp"
#include "../src/app/ai/AiContext.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiOperations.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiPrompt.hpp"
#include "../src/core/Command.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiDuplicate.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
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

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Un projet IHM : la vue Vue_Station (ouverte), la vue Accueil, des variables, un type IHM ; la pile.
struct Fixture {
    hmi::DocumentPtr   doc = std::make_shared<hmi::Document>();
    core::CommandStack stack;
    hmi::Id            view{hmi::kNoId};

    Fixture() {
        auto& p = doc->project;
        p.config.width = 1280;
        p.config.height = 800;
        p.views = {hmi::makeView(p, "Vue_Station"), hmi::makeView(p, "Accueil")};
        view = p.views[0].id;
        const auto var = [&](const char* name, const char* type) {
            hmi::Variable v;
            v.id = p.allocate();
            v.name = name;
            v.type = type;
            p.programs.variables.push_back(v);
        };
        var("Pompe_1_Marche", "BOOL");
        var("Pompe_1_Defaut", "BOOL");
        var("Cuves", "ARRAY[0..9] OF REAL");
        hmi::HmiType t;
        t.id = p.allocate();
        t.name = "T_Moteur";
        t.members = {{"Marche", "BOOL"}, {"Vitesse", "REAL"}};
        p.programs.types.push_back(t);
    }
    [[nodiscard]] hmi::View& v() { return *doc->project.view(view); }
    [[nodiscard]] const hmi::Object* object(const hmi::View& in, const std::string& name) const {
        for (const auto& o : in.objects)
            if (o.name == name) return &o;
        return nullptr;
    }
    // Une proposition sur la vue ouverte (ou sans vue), son empreinte prise comme a la reception.
    [[nodiscard]] ai::Proposal proposal(std::vector<ai::Operation> ops, std::string request = "essai") const {
        ai::Proposal p;
        p.id = "op1";
        p.request = std::move(request);
        p.summary = "Essai";
        p.ops = std::move(ops);
        // 1.19.0 : une proposition peut creer une vue ET toucher la vue ouverte - la vue ouverte d'abord.
        if (ai::usesOpenView(p)) {
            p.view = doc->project.view(view)->name;
            p.viewId = view;
        } else if (ai::createsView(p)) {
            p.view = ai::newViewName(p);
        }
        p.fingerprint = ai::proposalPrint(doc->project, p);
        return p;
    }
    bool apply(const ai::Proposal& p, std::string* why = nullptr, const ai::Env& env = {}) {
        auto cmd = ai::makeCommand(doc, p, why, env);
        if (!cmd) return false;
        return static_cast<bool>(stack.push(std::move(cmd)));
    }
};

ai::Operation op(A a, std::string name = {}, std::vector<std::pair<std::string, std::string>> props = {}) {
    ai::Operation o;
    o.action = a;
    o.name = std::move(name);
    o.props = std::move(props);
    return o;
}
ai::Operation create(std::string kind, std::string name, std::vector<std::pair<std::string, std::string>> props = {}) {
    ai::Operation o = op(A::Create, std::move(name), std::move(props));
    o.kind = std::move(kind);
    return o;
}
ai::Operation vars(std::vector<ai::Operation::VariableSpec> list) {
    ai::Operation o = op(A::Variables);
    o.variables = std::move(list);
    return o;
}
ai::Operation type(std::string name, std::vector<ai::Operation::MemberSpec> members) {
    ai::Operation o = op(A::Type, std::move(name));
    o.members = std::move(members);
    return o;
}

// ---- creer_vue ---------------------------------------------------------------------------
void createView() {
    std::printf("creer_vue\n");
    Fixture f;
    const auto p = f.proposal({op(A::View, "Vue_Pompes", {{"largeur", "1024"}, {"hauteur", "600"}, {"fond", "#101820"}, {"description", "Les pompes"}}),
                               create("Rectangle", "Cadre", {{"x", "20"}, {"y", "20"}, {"w", "200"}, {"h", "100"}}),
                               create("Text", "Titre", {{"x", "40"}, {"y", "30"}, {"text", "Pompes"}})});
    check(ai::createsView(p) && !ai::usesOpenView(p), "creer_vue en premier : la proposition cree une vue (pas la vue ouverte)");
    const auto ck = ai::validate(f.doc->project, p);
    check(ck.ok() && ck.newView == "Vue_Pompes" && ck.create == 2, "valide : la vue neuve et ses deux objets (" + errorsOf(ck) + ")");
    check(ai::creationSummary(p) == "la vue Vue_Pompes", "le resume : la vue Vue_Pompes");
    const auto pv = ai::preview(f.doc->project, p);
    check(pv.ok && pv.view.name == "Vue_Pompes" && pv.view.width == 1024 && pv.view.objects.size() == 2 && pv.touched.created.size() == 2,
          "l'apercu : la vue neuve (1024 de large), ses deux objets");
    const std::string before = ai::proposalPrint(f.doc->project, p);
    check(before == p.fingerprint && has(before, "absente"), "l'empreinte avant : la vue est absente");
    const std::size_t views = f.doc->project.views.size(), steps = f.stack.done().size();
    std::string why;
    check(f.apply(p, &why) && f.stack.done().size() == steps + 1, "appliquee en UNE commande (" + why + ")");
    const auto* nv = f.doc->project.viewByName("Vue_Pompes");
    check(f.doc->project.views.size() == views + 1 && nv, "la vue Vue_Pompes est dans le projet");
    check(nv && nv->width == 1024 && nv->height == 600 && nv->background == "#101820" && nv->description == "Les pompes" && nv->role == "vue",
          "sa taille, son fond, sa description, son role");
    check(nv && nv->objects.size() == 2 && f.object(*nv, "Cadre") && f.object(*nv, "Titre") && !nv->layers.empty(),
          "ses objets sont DANS la vue neuve (et un calque)");
    check(f.v().objects.empty(), "la vue ouverte n'a pas bouge");
    ai::Proposal applied = p;
    applied.viewId = nv ? nv->id : hmi::kNoId;
    const std::string after = ai::proposalPrint(f.doc->project, applied);
    check(after != before, "l'empreinte apres : la vue existe");
    (void)f.stack.undo();
    check(f.doc->project.views.size() == views && !f.doc->project.viewByName("Vue_Pompes"), "Ctrl+Z : la vue et ses objets partent ensemble");
    check(ai::proposalPrint(f.doc->project, applied) == before, "apres Ctrl+Z : l'empreinte d'avant (la carte dit annulee)");

    // Refusees.
    auto taken = f.proposal({op(A::View, "accueil")});
    check(anyHas(ai::validate(f.doc->project, taken).errors, "une vue porte d\xC3\xA9j\xC3\xA0 ce nom"), "un nom de vue pris (sans la casse) : refuse");
    auto second = f.proposal({create("Rectangle", "R"), op(A::View, "Vue_B")});
    // 1.19.0 : l'ordre d'ecriture ne decide plus : un objet ecrit AVANT creer_vue, sans "vue", va sur la vue ouverte ;
    // avec "vue", il va sur la vue creee, meme ecrit avant elle.
    const auto sc = ai::validate(f.doc->project, second);
    check(sc.ok() && ai::opTarget(second, 0).empty() && ai::opTarget(second, 1).empty(),
          "creer_vue apres un objet : accepte, l'objet ecrit avant reste sur la vue ouverte (" + errorsOf(sc) + ")");
    auto r1 = create("Rectangle", "R", {{"x", "10"}, {"y", "10"}});
    r1.target = "Vue_B";
    auto before1 = f.proposal({r1, op(A::View, "Vue_B")});
    const auto sb = ai::validate(f.doc->project, before1);
    check(sb.ok() && sb.order.size() == 2 && sb.order.front() == 1 && sb.newView == "Vue_B" && sb.create == 1,
          "un objet de Vue_B ecrit avant creer_vue : la vue est creee d'abord (l'ordre du plan) (" + errorsOf(sb) + ")");
    auto badKey = f.proposal({op(A::View, "Vue_C", {{"couleur", "#FFFFFF"}})});
    check(anyHas(ai::validate(f.doc->project, badKey).errors, "n'est pas un r\xC3\xA9glage d'une vue"), "un reglage inconnu (couleur) : refuse");
    auto badSize = f.proposal({op(A::View, "Vue_D", {{"largeur", "grand"}, {"fond", "bleu"}})});
    const auto bs = ai::validate(f.doc->project, badSize);
    check(bs.errors.size() == 2 && anyHas(bs.errors, "nombre entier") && anyHas(bs.errors, "#RRGGBB"), "une largeur et un fond illisibles : deux erreurs");
    auto badName = f.proposal({op(A::View, "Vue;X")});
    check(anyHas(ai::validate(f.doc->project, badName).errors, "n'est pas permis dans un nom de vue"), "un ; dans le nom : refuse");
    auto varName = f.proposal({op(A::View, "Pompe_1_Marche")});
    check(anyHas(ai::validate(f.doc->project, varName).errors, "une variable IHM porte ce nom"), "le nom d'une variable IHM : refuse");
    std::string why2;
    check(!f.apply(taken, &why2) && has(why2, "Vue"), "une proposition refusee ne s'applique pas");
}

// ---- creer_variables --------------------------------------------------------------------------
void createVariables() {
    std::printf("creer_variables\n");
    Fixture f;
    auto p = f.proposal({vars({{"Marche_P2", "BOOL", "Marche pompe 2", "vrai"},
                               {"Consigne_Niveau", "real", "", "75"},
                               {"Message", "STRING", "", "Bonjour"},
                               {"Mesures", "ARRAY[0..9] OF REAL", "", ""},
                               {"Moteur_1", "T_Moteur", "", ""},
                               {"Compteur", "INT", "", "12"}})});
    check(!ai::usesOpenView(p) && !ai::createsView(p) && p.view.empty(), "des variables seules : ni vue ouverte ni vue neuve");
    auto ck = ai::validate(f.doc->project, p);
    check(ck.ok() && ck.newVariables.size() == 6, "six variables valides (" + errorsOf(ck) + ")");
    const auto at = [&](std::size_t i) { return i < ck.newVariables.size() ? ck.newVariables[i] : ai::Operation::VariableSpec{}; };
    check(at(0).initial == "TRUE" && at(1).type == "REAL" && at(1).initial == "75" && at(2).initial == "'Bonjour'" && at(3).initial.empty()
              && at(5).initial == "12",
          "les valeurs initiales lues selon le type (vrai -> TRUE, real -> REAL, 'Bonjour')");
    check(at(0).description == "Marche pompe 2" && has(at(1).description, "Assistant IA"), "la description donnee, sinon celle de l'Assistant");
    check(ai::creationSummary(p) == "6 variables IHM", "le resume : 6 variables IHM");
    const auto pv = ai::preview(f.doc->project, p);
    check(!pv.ok && has(pv.why, "6 variables IHM"), "l'apercu sans vue : il dit ce qui sera cree");
    const std::size_t before = f.doc->project.programs.variables.size(), steps = f.stack.done().size();
    std::string why;
    check(f.apply(p, &why) && f.stack.done().size() == steps + 1, "appliquee en UNE commande (" + why + ")");
    const auto* m = f.doc->project.variable("Marche_P2");
    check(f.doc->project.programs.variables.size() == before + 6 && m && m->type == "BOOL" && m->initial == "TRUE", "les six variables existent");
    check(f.doc->project.variable("Moteur_1") && f.doc->project.variable("Moteur_1")->type == "T_Moteur", "une variable d'un type IHM du projet");
    (void)f.stack.undo();
    check(f.doc->project.programs.variables.size() == before && !f.doc->project.variable("Marche_P2"), "Ctrl+Z : toutes reparties");

    // Refusees, une raison par variable.
    auto bad = f.proposal({vars({{"Pompe_1_Marche", "INT", "", ""},
                                 {"1abc", "BOOL", "", ""},
                                 {"Niveau", "REEL", "", ""},
                                 {"Etat", "BOOL", "", "peut-etre"},
                                 {"Grand", "INT", "", "70000"},
                                 {"Double", "INT", "", ""},
                                 {"double", "INT", "", ""},
                                 {"SYS", "INT", "", ""},
                                 {"SansType", "", "", ""},
                                 {"Tableau", "ARRAY[0..3] OF REAL", "", "5"}})});
    const auto bk = ai::validate(f.doc->project, bad);
    check(anyHas(bk.errors, "Pompe_1_Marche") && anyHas(bk.errors, "porte d\xC3\xA9j\xC3\xA0 ce nom avec le type BOOL"), "un nom pris avec un autre type : refuse");
    {
        // 1.19.0 : le meme nom, le meme type : reutilisee (rien de cree, rien de remplace).
        auto same = f.proposal({vars({{"Pompe_1_Marche", "BOOL", "", ""}})});
        const auto ss = ai::validate(f.doc->project, same);
        check(ss.ok() && ss.newVariables.empty() && ss.reused.size() == 1, "le meme nom et le meme type : reutilisee (" + errorsOf(ss) + ")");
    }
    check(anyHas(bk.errors, "1abc") && anyHas(bk.errors, "nom invalide"), "un nom invalide : refuse");
    check(anyHas(bk.errors, "REEL") && anyHas(bk.errors, "les plus proches : REAL"), "un type inconnu : refuse, avec les plus proches (REAL)");
    check(anyHas(bk.errors, "TRUE ou FALSE"), "une valeur BOOL illisible : refusee");
    check(anyHas(bk.errors, "ne tient pas dans un INT"), "70000 dans un INT : refuse");
    check(anyHas(bk.errors, "plus haut dans la proposition"), "deux fois le meme nom dans la proposition : refuse");
    check(anyHas(bk.errors, "mot r\xC3\xA9serv\xC3\xA9"), "SYS : un mot reserve");
    check(anyHas(bk.errors, "son type manque"), "sans type : refuse");
    check(anyHas(bk.errors, "pas de valeur initiale"), "une valeur initiale sur un tableau : refusee");
    check(bk.errors.size() == 9, "neuf erreurs, toutes dites (" + std::to_string(bk.errors.size()) + ")");

    // creer_variables puis associer et une formule, dans la meme proposition.
    auto both = f.proposal({vars({{"Defaut_P2", "BOOL", "Defaut pompe 2", ""}, {"Marche_P2", "BOOL", "", ""}}),
                            create("Indicator", "Voyant_P2", {{"x", "100"}, {"y", "100"}, {"variable", "Marche_P2"}}),
                            create("Rectangle", "Fond_P2", {{"x", "100"}, {"y", "200"}, {"fill", "=SEL(Defaut_P2, '#2ECC71', '#E5534B')"}}),
                            op(A::Bind, "Voyant_P2")});
    both.ops.back().variable = "Defaut_P2";
    both.fingerprint = ai::proposalPrint(f.doc->project, both);
    check(ai::usesOpenView(both) && has(both.fingerprint, "+"), "variables et objets : la vue ouverte, + l'etat des variables dans l'empreinte");
    const auto bc = ai::validate(f.doc->project, both);
    check(bc.ok() && bc.newVariables.size() == 2 && bc.create == 2, "les variables creees sont connues des operations suivantes (" + errorsOf(bc) + ")");
    check(anyHas(bc.bindings, "cr\xC3\xA9\xC3\xA9" "e par la proposition"), "l'association dit que la variable est creee par la proposition");
    const std::size_t s2 = f.stack.done().size();
    check(f.apply(both, &why) && f.stack.done().size() == s2 + 1, "appliquee en UNE commande (" + why + ")");
    const auto* vo = f.object(f.v(), "Voyant_P2");
    const auto* fo = f.object(f.v(), "Fond_P2");
    check(vo && vo->text("variable") == "Defaut_P2" && f.doc->project.variable("Defaut_P2"), "associee a la variable creee");
    check(fo && fo->expr("fill") == "SEL(Defaut_P2, '#2ECC71', '#E5534B')", "la formule de couleur posee (fx)");
    ai::Proposal done = both;
    const std::string after = ai::proposalPrint(f.doc->project, done);
    (void)f.stack.undo();
    check(!f.doc->project.variable("Defaut_P2") && !f.object(f.v(), "Voyant_P2"), "Ctrl+Z : les variables ET les objets partent ensemble");
    check(ai::proposalPrint(f.doc->project, done) == both.fingerprint && after != both.fingerprint, "l'empreinte revient a celle d'avant");
    // Une variable creee dans le projet entre-temps : la proposition est perimee.
    {
        hmi::Variable v;
        v.id = f.doc->project.allocate();
        v.name = "Marche_P2";
        v.type = "INT";
        f.doc->project.programs.variables.push_back(v);
        const auto st = ai::validate(f.doc->project, both);
        check(st.stale && anyHas(st.errors, "porte d\xC3\xA9j\xC3\xA0 ce nom"), "une variable du meme nom apparue depuis : perimee et refusee");
        f.doc->project.programs.variables.pop_back();
    }
}

// ---- creer_type ------------------------------------------------------------------------------
void createTypes() {
    std::printf("creer_type\n");
    Fixture f;
    auto p = f.proposal({type("T_Vanne", {{"Ouv", "BOOL", "Ouverte"}, {"Position", "INT", ""}}),
                         type("T_Ligne", {{"Vannes", "ARRAY[1..3] OF T_Vanne", ""}, {"Moteur", "T_Moteur", ""}, {"Debit", "REAL", "m3/h"}}),
                         vars({{"Ligne_1", "T_Ligne", "La ligne 1", ""}})});
    p.ops[0].props.emplace_back("description", "Une vanne");
    p.fingerprint = ai::proposalPrint(f.doc->project, p);
    auto ck = ai::validate(f.doc->project, p);
    check(ck.ok() && ck.newTypes.size() == 2 && ck.newVariables.size() == 1, "deux types, puis une variable de ce type (" + errorsOf(ck) + ")");
    check(ck.newTypes.size() == 2 && ck.newTypes[1].second.size() == 3 && ck.newTypes[1].second[0].type == "ARRAY[1..3] OF T_Vanne",
          "les membres montres (le type remis en forme)");
    check(ai::creationSummary(p) == "1 variable IHM, 2 types IHM", "le resume : 1 variable, 2 types");
    const std::size_t types = f.doc->project.programs.types.size(), steps = f.stack.done().size();
    std::string why;
    check(f.apply(p, &why) && f.stack.done().size() == steps + 1, "appliquee en UNE commande (" + why + ")");
    const auto* tv = f.doc->project.hmiTypeByName("T_Vanne");
    check(f.doc->project.programs.types.size() == types + 2 && tv && tv->members.size() == 2 && tv->description == "Une vanne",
          "T_Vanne : ses membres, sa description");
    check(f.doc->project.variable("Ligne_1") && f.doc->project.variable("Ligne_1")->type == "T_Ligne", "la variable Ligne_1 : T_Ligne");
    (void)f.stack.undo();
    check(f.doc->project.programs.types.size() == types && !f.doc->project.variable("Ligne_1"), "Ctrl+Z : les types et la variable partent ensemble");

    // L'ordre des dependances.
    auto order = f.proposal({type("T_Ligne", {{"Vannes", "ARRAY[1..3] OF T_Vanne", ""}}), type("T_Vanne", {{"Ouv", "BOOL", ""}})});
    // 1.19.0 : l'ordre d'ecriture ne compte plus - le plan cree T_Vanne d'abord.
    const auto ok1 = ai::validate(f.doc->project, order);
    check(ok1.ok() && ok1.order.size() == 2 && ok1.order.front() == 1, "un type employe avant d'etre cree : le plan le cree d'abord (" + errorsOf(ok1) + ")");
    auto varFirst = f.proposal({vars({{"Ligne_2", "T_Ligne2", "", ""}}), type("T_Ligne2", {{"A", "BOOL", ""}})});
    const auto ok2 = ai::validate(f.doc->project, varFirst);
    check(ok2.ok() && ok2.order.size() == 2 && ok2.order.front() == 1, "une variable d'un type ecrit plus bas : le type d'abord (" + errorsOf(ok2) + ")");
    auto bad = f.proposal({type("T_Moteur", {{"A", "BOOL", ""}}),
                           type("INT", {{"A", "BOOL", ""}}),
                           type("T_Soi", {{"Moi", "T_Soi", ""}}),
                           type("T_Double", {{"A", "BOOL", ""}, {"a", "INT", ""}}),
                           type("T_Vide", {}),
                           type("T_Inconnu", {{"A", "T_Nulle_Part", ""}})});
    const auto bk = ai::validate(f.doc->project, bad);
    check(anyHas(bk.errors, "existe d\xC3\xA9j\xC3\xA0"), "un type du projet : refuse");
    check(anyHas(bk.errors, "type \xC3\xA9l\xC3\xA9mentaire"), "INT : un type elementaire, refuse");
    check(anyHas(bk.errors, "ne se contient pas lui-m\xC3\xAAme"), "un type qui se contient : refuse");
    check(anyHas(bk.errors, "ce membre existe d\xC3\xA9j\xC3\xA0"), "deux membres du meme nom : refuse");
    check(anyHas(bk.errors, "aucun membre"), "un type sans membre : refuse");
    check(anyHas(bk.errors, "T_Nulle_Part"), "un membre d'un type inconnu : refuse");
}

// ---- les formules (fx) -----------------------------------------------------------------------
void formulas() {
    std::printf("formules\n");
    Fixture f;
    auto p = f.proposal({create("Rectangle", "Etat_P1", {{"x", "40"}, {"y", "40"}, {"fill", "=SEL(Pompe_1_Marche, '#5B6270', '#2ECC71')"},
                                                         {"visible", "=NOT Pompe_1_Defaut"}}),
                         create("Rectangle", "Niveau_1", {{"x", "40"}, {"y", "200"}, {"visible", "=$Cuves[0]$ > 50"}})});
    auto ck = ai::validate(f.doc->project, p);
    check(ck.ok(), "une couleur, une visibilite, un repere : des formules valides (" + errorsOf(ck) + ")");
    bool shown = false;
    for (const auto& it : ck.items)
        for (const auto& c : it.changes)
            if (c.key == "visible" && c.after == "=NOT Pompe_1_Defaut") shown = true;
    check(shown, "l'apercu montre la formule (=...)");
    std::string why;
    check(f.apply(p, &why), "appliquee (" + why + ")");
    const auto* e = f.object(f.v(), "Etat_P1");
    check(e && e->expr("fill") == "SEL(Pompe_1_Marche, '#5B6270', '#2ECC71')" && e->expr("visible") == "NOT Pompe_1_Defaut",
          "posees comme l'inspecteur (Prop::expr, sans le =)");
    check(e && e->text("fill") != "=SEL(Pompe_1_Marche, '#5B6270', '#2ECC71')", "la valeur fixe reste dessous (pas le texte de la formule)");
    const auto* n = f.object(f.v(), "Niveau_1");
    check(n && n->expr("visible") == "$Cuves[0]$ > 50", "le repere garde ses $ (il avancera a la duplication)");
    // Modifier : une formule sur un objet existant.
    auto mod = f.proposal({op(A::Modify, "Etat_P1", {{"opacity", "=SEL(Pompe_1_Defaut, 100, 40)"}})});
    check(ai::validate(f.doc->project, mod).ok() && f.apply(mod, &why), "modifier : une formule posee (" + why + ")");
    check(f.object(f.v(), "Etat_P1") && f.object(f.v(), "Etat_P1")->expr("opacity") == "SEL(Pompe_1_Defaut, 100, 40)", "l'opacite pilotee");
    // Refusees.
    auto bad = f.proposal({create("Rectangle", "R1", {{"visible", "=Vanne_Inconnue"}}),
                           create("Rectangle", "R2", {{"visible", "=Pompe_1_Marche := TRUE"}}),
                           create("Rectangle", "R3", {{"visible", "="}}),
                           create("Indicator", "R4", {{"variable", "=Pompe_1_Marche"}}),
                           create("Rectangle", "R5", {{"visible", "=(Pompe_1_Marche"}}),
                           create("Rectangle", "R6", {{"visible", "=$Vanne$.OUV"}})});
    const auto bk = ai::validate(f.doc->project, bad);
    check(anyHas(bk.errors, "Vanne_Inconnue") && anyHas(bk.errors, "n'invente pas"), "une formule qui lit une variable absente : refusee");
    check(anyHas(bk.errors, "ne fait que lire"), "une formule qui ecrit (:=) : refusee");
    check(anyHas(bk.errors, "formule vide"), "= seul : refuse");
    check(anyHas(bk.errors, "s'\xC3\xA9" "crit par son nom"), "variable = une formule : refuse");
    check(anyHas(bk.errors, "illisible"), "une formule illisible : refusee");
    check(anyHas(bk.errors, "Vanne"), "un repere sur un nom absent ($Vanne$) : refuse");
    check(bk.errors.size() == 6, "six erreurs (" + std::to_string(bk.errors.size()) + ")");
}

// ---- 1.16.1 : un repere associe ; appliquer malgre les erreurs ---------------------------------
void markersAndLenient() {
    std::printf("1.16.1 : repere associe, appliquer malgre les erreurs\n");
    Fixture f;
    {   // une structure Armoires (ARRAY OF T_Armoire) et un voyant associe a $Armoires[0]$.Prete
        auto& p = f.doc->project;
        hmi::HmiType t;
        t.id = p.allocate();
        t.name = "T_Armoire";
        t.members = {{"Prete", "BOOL"}, {"Defaut", "BOOL"}};
        p.programs.types.push_back(t);
        hmi::Variable v;
        v.id = p.allocate();
        v.name = "Armoires";
        v.type = "ARRAY[0..3] OF T_Armoire";
        p.programs.variables.push_back(v);
    }
    ai::Operation bind = op(A::Bind, "Voyant_Pret");
    bind.variable = "$Armoires[0]$.Prete";
    auto p = f.proposal({create("Indicator", "Voyant_Pret", {{"x", "40"}, {"y", "40"}}), bind});
    const auto ck = ai::validate(f.doc->project, p);
    check(ck.ok(), "associer a $Armoires[0]$.Prete (la variable existe) : pas d'erreur (" + errorsOf(ck) + ")");
    // Appliquer malgre les erreurs : ce qui est valide s'applique, l'erreur est rendue (pour le journal).
    auto mixed = f.proposal({create("Rectangle", "Bon_1", {{"x", "40"}, {"y", "300"}}),
                             create("Rectangle", "Mauvais_1", {{"x", "200"}, {"y", "300"}, {"visible", "=Vanne_Inconnue"}}),
                             op(A::Modify, "Objet_Absent", {{"fill", "#FF0000"}})});
    const auto mk = ai::validate(f.doc->project, mixed);
    check(!mk.ok() && mk.errors.size() == 2, "deux erreurs (" + errorsOf(mk) + ")");
    std::string why;
    check(!ai::makeCommand(f.doc, mixed, &why), "sans l'option : une erreur bloque");
    std::vector<std::string> skipped;
    auto cmd = ai::makeCommandLenient(f.doc, mixed, skipped, &why);
    check(cmd && skipped.size() == 2, "malgre les erreurs : la commande, et les deux erreurs rendues (" + why + ")");
    if (cmd) (void)f.stack.push(std::move(cmd));
    check(f.object(f.v(), "Bon_1") != nullptr, "ce qui est valide est applique");
    const auto* m1 = f.object(f.v(), "Mauvais_1");
    check(!m1 || m1->expr("visible").empty(), "la formule en erreur n'est pas posee");
    check(f.stack.canUndo(), "un seul Ctrl+Z la reprend");
    // Une vue qui a change depuis : bloque toujours, meme malgre les erreurs.
    auto stale = f.proposal({create("Rectangle", "Tard_1", {{"x", "40"}, {"y", "500"}})});
    (void)f.apply(f.proposal({create("Rectangle", "Entre_Deux", {{"x", "400"}, {"y", "500"}})}));
    skipped.clear();
    check(!ai::makeCommandLenient(f.doc, stale, skipped, &why), "une vue changee depuis : bloque toujours (" + why + ")");
}

// ---- dupliquer avec reperes ------------------------------------------------------------------
void duplicateMarkers() {
    std::printf("dupliquer avec reperes\n");
    Fixture f;
    // L'original : un rectangle a reperes, pose a la main.
    {
        auto& p = f.doc->project;
        hmi::Object o = hmi::makeObject(hmi::Kind::Rectangle, p.allocate(), "Cuve_1", 40, 100, f.v().activeLayer);
        o.setNumber("w", 120);
        o.setNumber("h", 160);
        o.setExpr("visible", "$Cuves[0]$ > 50");
        o.setExpr("opacity", "SEL($Cuves[0]$ > 90, 60, 100)");
        f.v().objects.push_back(o);
    }
    auto p = f.proposal({op(A::Duplicate)});
    p.ops[0].objects = {"Cuve_1"};
    p.ops[0].count = 3;
    p.ops[0].markers = true;
    p.ops[0].dx = 180;
    p.ops[0].dy = 0;
    p.fingerprint = ai::proposalPrint(f.doc->project, p);
    const auto ck = ai::validate(f.doc->project, p);
    check(ck.ok() && ck.create == 3, "3 copies valides (" + errorsOf(ck) + ")");
    // Ce que ferait Ctrl+Maj+D : le meme moteur, le meme plan, la meme pose (180 px).
    hmi::Project editor = f.doc->project;
    hmi::View& ev = *editor.view(f.view);
    const auto sc = hmi::dup::scan(ev, {ev.objects.front().id}, hmi::dup::projectBounds(editor));
    auto plan = hmi::dup::defaultPlan(ev, sc, 3);
    plan.layout.axis = hmi::dup::Axis::X;
    plan.layout.spacing = 180 - 120;
    (void)hmi::dup::apply(editor, ev, plan);
    std::string why;
    check(f.apply(p, &why), "appliquee (" + why + ")");
    check(f.v().objects == ev.objects, "le meme resultat que la duplication de l'editeur (objets, noms, reperes, ids)");
    const auto* c3 = f.object(f.v(), "Cuve_4");
    check(f.object(f.v(), "Cuve_2") && f.object(f.v(), "Cuve_3") && c3, "les noms en serie : Cuve_2, Cuve_3, Cuve_4");
    check(c3 && c3->expr("visible") == "$Cuves[3]$ > 50" && c3->expr("opacity") == "SEL($Cuves[3]$ > 90, 60, 100)", "le repere avance : $Cuves[3]$");
    check(c3 && c3->number("x") == 40 + 3 * 180 && c3->number("y") == 100, "la copie 3 decalee de 3 fois dx");
    (void)f.stack.undo();
    check(f.v().objects.size() == 1, "Ctrl+Z : les trois copies partent");

    // En colonne (dy), des noms voulus.
    auto col = f.proposal({op(A::Duplicate)});
    col.ops[0].objects = {"Cuve_1"};
    col.ops[0].count = 2;
    col.ops[0].markers = true;
    col.ops[0].dx = 0;
    col.ops[0].dy = 200;
    col.ops[0].names = {"Cuve_Bas", "Cuve_Fond"};
    col.fingerprint = ai::proposalPrint(f.doc->project, col);
    check(f.apply(col, &why), "en colonne : appliquee (" + why + ")");
    const auto* b = f.object(f.v(), "Cuve_Bas");
    const auto* fo = f.object(f.v(), "Cuve_Fond");
    check(b && fo && b->number("x") == 40 && b->number("y") == 300 && fo->number("y") == 500, "en colonne : y + 200, y + 400 ; les noms voulus");
    check(fo && fo->expr("visible") == "$Cuves[2]$ > 50", "le repere de la copie 2 : $Cuves[2]$");
    (void)f.stack.undo();

    // Sans reperes : count copies, le decalage k fois.
    auto plain = f.proposal({op(A::Duplicate)});
    plain.ops[0].objects = {"Cuve_1"};
    plain.ops[0].count = 3;
    plain.ops[0].dx = 50;
    plain.ops[0].dy = 10;
    plain.fingerprint = ai::proposalPrint(f.doc->project, plain);
    check(f.apply(plain, &why) && f.v().objects.size() == 4, "sans reperes : 3 copies (" + why + ")");
    const auto& last = f.v().objects.back();
    check(last.number("x") == 40 + 150 && last.number("y") == 100 + 30 && last.expr("visible") == "$Cuves[0]$ > 50",
          "sans reperes : la copie 3 decalee de 3 fois (dx, dy), ses reperes inchanges");
    (void)f.stack.undo();
    // Le nombre de copies.
    for (const int n : {0, 51}) {
        auto bad = f.proposal({op(A::Duplicate)});
        bad.ops[0].objects = {"Cuve_1"};
        bad.ops[0].count = n;
        check(anyHas(ai::validate(f.doc->project, bad).errors, "de 1 \xC3\xA0 50 copies"), std::to_string(n) + " copies : refuse");
    }
    // Un repere sans indice : il reste, et c'est dit.
    f.v().objects.front().setExpr("text", "$Nom_Cuve$");
    auto keep = f.proposal({op(A::Duplicate)});
    keep.ops[0].objects = {"Cuve_1"};
    keep.ops[0].markers = true;
    check(anyHas(ai::validate(f.doc->project, keep).warnings, "$Nom_Cuve$ n'a pas d'indice"), "un repere sans indice : un avertissement");
}

// ---- le JSON : une conversation relue -------------------------------------------------------
void json() {
    std::printf("le JSON\n");
    Fixture f;
    auto dup = op(A::Duplicate);
    dup.objects = {"Cuve_1"};
    dup.count = 4;
    dup.markers = true;
    dup.dx = 150;
    dup.dy = 0;
    auto t = type("T_Vanne", {{"Ouv", "BOOL", "Ouverte"}, {"Pos", "INT", ""}});
    t.props.emplace_back("description", "Une vanne");
    std::vector<ai::Operation> ops = {op(A::View, "Vue_X", {{"largeur", "800"}}), vars({{"A", "BOOL", "desc", "TRUE"}, {"B", "REAL", "", ""}}), t, dup,
                                      create("Rectangle", "R", {{"fill", "=SEL(A, '#000000', '#FFFFFF')"}})};
    const auto j = ai::operationsToJson(ops);
    std::vector<ai::Operation> back;
    std::string why;
    check(ai::operationsFromJson(j, back, &why) && back.size() == ops.size(), "relu (" + why + ")");
    check(back.size() == 5 && back[0].action == A::View && back[0].name == "Vue_X" && back[0].props == ops[0].props, "creer_vue : nom, reglages");
    check(back.size() == 5 && back[1].variables == ops[1].variables, "creer_variables : nom, type, description, valeur");
    check(back.size() == 5 && back[2].name == "T_Vanne" && back[2].members == ops[2].members && back[2].props == ops[2].props,
          "creer_type : nom, description, membres");
    check(back.size() == 5 && back[3].count == 4 && back[3].markers && back[3].dx == 150, "dupliquer : copies, reperes, decalage");
    check(back.size() == 5 && back[4].props == ops[4].props, "une formule : telle quelle");
    // Une proposition 1.15.0 s'ecrit comme avant (ni copies ni reperes).
    auto old = op(A::Duplicate);
    old.objects = {"X"};
    const std::string text = ai::json::write(ai::operationsToJson({old}), false);
    check(!has(text, "copies") && !has(text, "reperes"), "dupliquer d'avant : le meme JSON");
    // Une proposition entiere : l'empreinte d'apres, la vue neuve.
    auto p = f.proposal(ops);
    p.fingerprintAfter = "vue:abc";
    p.viewId = 77;
    const auto q = ai::proposalFromJson(ai::toJson(p));
    check(q.ops.size() == 5 && q.fingerprint == p.fingerprint && q.fingerprintAfter == "vue:abc" && q.view == "Vue_X" && q.viewId == 77,
          "une proposition relue : ses operations, ses empreintes, la vue neuve");
    check(ai::createsView(q) && ai::creationSummary(q) == "la vue Vue_X, 2 variables IHM, 1 type IHM", "relue : la meme portee, le meme resume");
}

// ---- la lecture stricte de l'outil, son schema ---------------------------------------------
void toolReading() {
    std::printf("la lecture de l'outil\n");
    Fixture f;
    ai::PromptInput in;
    in.project = &f.doc->project;
    in.view = f.doc->project.view(f.view);
    std::string why;
    const auto read = [&](const std::string& ops) {
        why.clear();
        return ai::proposalFromTool("{\"resume\": \"x\", \"operations\": [" + ops + "]}", in, "demande", &why);
    };
    auto p = read("{\"action\": \"creer_vue\", \"nom\": \"Vue_Neuve\", \"proprietes\": {\"largeur\": 800}},"
                  "{\"action\": \"creer\", \"genre\": \"Rectangle\", \"nom\": \"R\", \"proprietes\": {\"visible\": \"=Pompe_1_Marche\"}}");
    check(p && ai::createsView(*p) && p->view == "Vue_Neuve" && p->viewId == hmi::kNoId, "creer_vue lu : la proposition vise la vue neuve (" + why + ")");
    check(p && p->ops[0].props.size() == 1 && p->ops[0].props[0].second == "800", "un nombre dans proprietes : lu en texte");
    p = read("{\"action\": \"creer_variables\", \"variables\": [{\"nom\": \"A\", \"type\": \"BOOL\", \"valeur\": true}, {\"nom\": \"B\", \"type\": \"REAL\", "
             "\"valeur\": 2.5, \"description\": \"b\"}]}");
    check(p && p->ops[0].variables.size() == 2 && p->ops[0].variables[0].initial == "TRUE" && p->ops[0].variables[1].initial == "2.5",
          "creer_variables lu (une valeur booleenne ou un nombre)");
    p = read("{\"action\": \"creer_type\", \"nom\": \"T_X\", \"description\": \"d\", \"membres\": [{\"nom\": \"A\", \"type\": \"BOOL\"}]}");
    check(p && p->ops[0].members.size() == 1 && p->ops[0].props.size() == 1, "creer_type lu (sa description)");
    p = read("{\"action\": \"dupliquer\", \"objets\": [\"R\"], \"copies\": 3, \"reperes\": true}");
    check(p && p->ops[0].count == 3 && p->ops[0].markers, "dupliquer : copies, reperes lus");
    // Refusees a la lecture.
    check(!read("{\"action\": \"creer_variables\", \"variables\": [{\"nom\": \"A\", \"type\": \"BOOL\", \"couleur\": \"rouge\"}]}") && has(why, "couleur"),
          "un champ inconnu dans une variable : refuse");
    check(!read("{\"action\": \"creer_vue\", \"nom\": \"V\", \"taille\": 3}") && has(why, "taille"), "un champ inconnu sur creer_vue : refuse");
    check(!read("{\"action\": \"creer_variables\", \"variables\": [{\"nom\": \"A\"}]}") && has(why, "type"), "une variable sans type : refusee");
    check(!read("{\"action\": \"creer_variables\", \"variables\": []}") && has(why, "vide"), "une liste de variables vide : refusee");
    check(!read("{\"action\": \"creer_type\", \"nom\": \"T\", \"membres\": [{\"nom\": \"A\", \"type\": 3}]}") && has(why, "texte"),
          "un type de membre qui n'est pas un texte : refuse");
    check(!read("{\"action\": \"creer_type\", \"nom\": \"T\"}") && has(why, "membres"), "creer_type sans membres : refuse");
    check(!read("{\"action\": \"dupliquer\", \"objets\": [\"R\"], \"copies\": 2.5}") && has(why, "copies"), "copies non entier : refuse");
    check(!read("{\"action\": \"dupliquer\", \"objets\": [\"R\"], \"copies\": 80}") && has(why, "1 \xC3\xA0 50"), "80 copies : refuse a la lecture");
    check(!read("{\"action\": \"dupliquer\", \"objets\": [\"R\"], \"reperes\": \"oui\"}") && has(why, "reperes"), "reperes non booleen : refuse");
    check(!read("{\"action\": \"creer_vue\", \"nom\": \"\"}") && has(why, "vide"), "creer_vue sans nom : refuse");
    check(!read("{\"action\": \"creer\", \"genre\": \"Rectangle\", \"copies\": 2}") && has(why, "copies"), "copies sur creer : refuse");
    // Sans vue ouverte : une vue neuve, des variables passent ; toucher des objets, non.
    in.view = nullptr;
    p = read("{\"action\": \"creer_vue\", \"nom\": \"Vue_Sans\"}");
    check(p && p->view == "Vue_Sans", "sans vue ouverte : creer_vue passe");
    p = read("{\"action\": \"creer_variables\", \"variables\": [{\"nom\": \"A\", \"type\": \"BOOL\"}]}");
    check(p && p->view.empty() && p->viewId == hmi::kNoId && has(p->fingerprint, "projet"), "sans vue ouverte : des variables passent (le projet seul)");
    check(!read("{\"action\": \"supprimer\", \"objets\": [\"A\"]}") && has(why, "Aucune vue"), "sans vue ouverte : supprimer est refuse");
    // Le schema.
    ai::json::Value tools;
    check(ai::json::parse(ai::operationsTool(), tools), "le schema se lit");
    const auto& props = tools.items().front().at("input_schema").at("properties").at("operations").at("items").at("properties");
    check(props.has("variables") && props.has("membres") && props.has("copies") && props.has("reperes") && props.has("description"),
          "le schema : variables, membres, copies, reperes, description");
    check(props.at("variables").at("items").at("required").items().size() == 2, "une variable : nom et type requis");
    const std::string rules = ai::systemPrompt(in);
    check(has(rules, "creer_vue") && has(rules, "creer_variables") && has(rules, "creer_type") && has(rules, "SEL(") && has(rules, "Ctrl+Maj+D")
              && has(rules, "Jamais pour masquer une variable absente"),
          "les consignes : les trois operations, les formules, les reperes, quand creer");
    // Le retour au modele cite les nouvelles erreurs.
    auto bad = f.proposal({vars({{"Pompe_1_Marche", "REAL", "", ""}})});
    const std::string fb = ai::feedback(ai::validate(f.doc->project, bad), bad);
    check(has(fb, "Pompe_1_Marche") && has(fb, "porte d\xC3\xA9j\xC3\xA0 ce nom") && has(fb, "Corrige"), "le retour au modele : l'erreur de la variable");
}

// ---- l'automate relie -> l'IHM -------------------------------------------------------------
ai::Env plcEnv() {
    ai::Env env;
    env.plcVariables = [] {
        return std::vector<std::pair<std::string, std::string>>{{"API.Niveau", "REAL"},         {"API.Marche", "EBOOL"},
                                                                {"API.Libelle", "STRING[32]"},  {"API.Tempo", "TON"},
                                                                {"API.Vannes", "ARRAY[0..3] OF T_Vanne"}, {"API.Pompe_1_Marche", "BOOL"},
                                                                {"API.Heure", "TOD"}};
    };
    env.plcName = [](std::string_view n) {
        for (const char* x : {"Niveau", "Marche", "Libelle", "Tempo", "Vannes", "Pompe_1_Marche", "Heure"})
            if (n == x) return true;
        return false;
    };
    env.plcComment = [](std::string_view n) { return n == "Niveau" ? std::string("Niveau de la cuve") : std::string{}; };
    env.plcTypes = [] {
        std::vector<ai::Env::PlcType> out;
        out.push_back({"T_Vanne", "Une vanne", {{"Ouv", "EBOOL", "Ouverte"}, {"Pos", "INT", ""}, {"Etat", "T_Etat", ""}, {"Tempo", "TON", ""}}});
        out.push_back({"T_Etat", "", {{"Code", "INT", ""}, {"Texte", "STRING[16]", ""}}});
        out.push_back({"T_Seul", "", {{"X", "REAL", ""}}});
        out.push_back({"T_Moteur", "", {{"Marche", "BOOL", ""}}});   // deja dans l'IHM
        return out;
    };
    return env;
}

void automate() {
    std::printf("l'automate relie\n");
    Fixture f;
    const ai::Env env = plcEnv();
    std::string why;
    const std::vector<std::string> ddts = {"T_Vanne", "T_Etat"};
    check(ai::hmiTypeOfPlc("EBOOL", ddts, f.doc->project) == "BOOL" && ai::hmiTypeOfPlc("STRING[32]", ddts, f.doc->project) == "STRING"
              && ai::hmiTypeOfPlc("dint", ddts, f.doc->project) == "DINT",
          "la conversion : EBOOL -> BOOL, STRING[32] -> STRING, dint -> DINT");
    check(ai::hmiTypeOfPlc("TOD", ddts, f.doc->project, &why).empty() && has(why, "TIME_OF_DAY ne se range pas"),
          "TOD : l'IHM n'en fait pas de variable (dit)");
    check(ai::hmiTypeOfPlc("ARRAY [0 .. 3] OF EBOOL", ddts, f.doc->project) == "ARRAY[0..3] OF BOOL"
              && ai::hmiTypeOfPlc("ARRAY[1..2] OF t_vanne", ddts, f.doc->project) == "ARRAY[1..2] OF T_Vanne",
          "un tableau : ses bornes, son element converti (un DDT garde son nom)");
    check(ai::hmiTypeOfPlc("TON", ddts, f.doc->project, &why).empty() && has(why, "pas d'\xC3\xA9quivalent"), "une instance de bloc (TON) : pas d'equivalent");

    const auto v = ai::variablesFromPlc(f.doc->project, env);
    check(v.size() == 1 && v[0].action == A::Variables, "les globales -> UNE operation creer_variables");
    const auto& list = v.empty() ? std::vector<ai::Operation::VariableSpec>{} : v[0].variables;
    const auto find = [&](const std::string& n) -> const ai::Operation::VariableSpec* {
        for (const auto& x : list)
            if (x.name == n) return &x;
        return nullptr;
    };
    check(list.size() == 4, "quatre variables proposees (" + std::to_string(list.size()) + ")");
    check(find("Niveau") && find("Niveau")->type == "REAL" && find("Niveau")->description == "Niveau de la cuve", "Niveau : REAL, le commentaire de l'automate");
    check(find("Marche") && find("Marche")->type == "BOOL" && has(find("Marche")->description, "API.Marche"), "Marche : EBOOL -> BOOL, sans commentaire : dit d'ou elle vient");
    check(find("Vannes") && find("Vannes")->type == "ARRAY[0..3] OF T_Vanne", "Vannes : un tableau du DDT");
    check(!find("Tempo") && !find("Pompe_1_Marche") && !find("Heure"), "ni l'instance de bloc, ni l'heure (TOD), ni celle que l'IHM a deja");
    const auto only = ai::variablesFromPlc(f.doc->project, env, "niv");
    check(only.size() == 1 && only[0].variables.size() == 1 && only[0].variables[0].name == "Niveau", "le filtre : un morceau de nom (sans la casse)");
    check(ai::variablesFromPlc(f.doc->project, env, "zzz").empty(), "rien ne correspond : rien a proposer");

    const auto t = ai::typesFromPlc(f.doc->project, env);
    std::vector<std::string> order;
    for (const auto& o : t) order.push_back(o.name);
    check(order == std::vector<std::string>{"T_Etat", "T_Vanne", "T_Seul"}, "les DDT : T_Etat avant T_Vanne (les dependances), pas T_Moteur (deja la)");
    check(t.size() == 3 && t[1].members.size() == 3 && t[1].members[0].type == "BOOL" && t[1].members[0].description == "Ouverte",
          "T_Vanne : ses membres convertis, son commentaire ; le membre TON ecarte");
    const auto tv = ai::typesFromPlc(f.doc->project, env, "Vanne");
    check(tv.size() == 2 && tv[0].name == "T_Etat" && tv[1].name == "T_Vanne", "le filtre Vanne : T_Vanne et ce qu'il emploie (T_Etat)");

    // Une proposition entiere : valide, appliquee en un Ctrl+Z.
    auto ops = ai::fromPlc(f.doc->project, env);
    check(ops.size() == 4 && ops.back().action == A::Variables, "fromPlc : les types d'abord, puis les variables");
    auto p = f.proposal(ops, "les variables de l'automate");
    const auto ck = ai::validate(f.doc->project, p, env);
    check(ck.ok() && ck.newVariables.size() == 4 && ck.newTypes.size() == 3, "valide (" + errorsOf(ck) + ")");
    check(anyHas(ck.warnings, "porte le nom d'une variable de l'automate"), "le meme nom qu'une globale : dit, pas refuse");
    const std::size_t steps = f.stack.done().size();
    check(f.apply(p, &why, env) && f.stack.done().size() == steps + 1, "appliquee en UNE commande (" + why + ")");
    check(f.doc->project.variable("Vannes") && f.doc->project.hmiTypeByName("T_Vanne") && f.doc->project.hmiTypeByName("T_Etat"),
          "les variables et les types sont dans l'IHM");
    check(ai::variablesFromPlc(f.doc->project, env).empty() && ai::typesFromPlc(f.doc->project, env).empty(), "deja la : plus rien a proposer");
    (void)f.stack.undo();
    check(!f.doc->project.variable("Vannes") && !f.doc->project.hmiTypeByName("T_Vanne"), "Ctrl+Z : tout repart");
    check(ai::variablesFromPlc(f.doc->project, ai::Env{}).empty() && ai::typesFromPlc(f.doc->project, ai::Env{}).empty(), "sans automate : rien");
}

// ---- le contexte ----------------------------------------------------------------------------
void context() {
    std::printf("le contexte\n");
    Fixture f;
    ai::PromptInput in;
    in.project = &f.doc->project;
    in.view = f.doc->project.view(f.view);
    in.env = plcEnv();
    in.request = "cree les types de l'automate";
    ai::ContextInfo info;
    const std::string text = ai::contextText(in, {}, &info);
    check(has(text, "Types (DDT) de l'automate") && has(text, "T_Vanne : Ouv EBOOL, Pos INT") && info.plcTypes == 4 && info.plcTypesListed == 4,
          "les DDT de l'automate resumes (nom, membres)");
    check(has(text, "Types IHM du projet (1) : T_Moteur") && info.hmiTypes == 1, "les types IHM du projet nommes");
    in.view = nullptr;
    check(has(ai::contextText(in), "creer_vue"), "sans vue ouverte : le modele sait qu'il peut proposer une vue neuve");
}

// ---- l'Assistant : la vue creee s'ouvre, l'etat reel de la carte ---------------------------
struct Script {
    std::deque<std::vector<ai::ProviderEvent>> replies;
};
class FakeProvider final : public ai::Provider {
public:
    explicit FakeProvider(Script& s) : s_(s) {}
    [[nodiscard]] std::string_view key() const noexcept override { return "anthropic"; }
    bool start(ai::ProviderRequest, std::string*) override {
        if (!s_.replies.empty()) {
            pending_ = std::move(s_.replies.front());
            s_.replies.pop_front();
        }
        busy_ = true;
        return true;
    }
    std::vector<ai::ProviderEvent> poll() override {
        busy_ = false;
        return std::exchange(pending_, {});
    }
    void cancel() override { busy_ = false; }
    [[nodiscard]] bool busy() const noexcept override { return busy_; }

private:
    Script&                        s_;
    std::vector<ai::ProviderEvent> pending_;
    bool                           busy_{false};
};

std::vector<ai::ProviderEvent> toolReply(const std::string& input) {
    ai::ProviderEvent t;
    t.type = ai::ProviderEvent::Type::ToolUse;
    t.toolUseId = "toolu_1";
    t.toolName = "proposer_operations";
    t.toolInputJson = input;
    ai::ProviderEvent d;
    d.type = ai::ProviderEvent::Type::Done;
    d.stopReason = "tool_use";
    return {t, d};
}

void assistant() {
    std::printf("l'Assistant\n");
    Fixture f;
    const fs::path dir = fs::temp_directory_path() / "xpg_ai_options";
    fs::remove_all(dir);
    fs::create_directories(dir);
    ai::setRoot(U(dir / "IA"));
    Script script;
    std::vector<hmi::Id> opened;
    hmi::Id active = f.view;
    ai::Assistant::Host host;
    host.document = [&] { return f.doc; };
    host.activeView = [&] { return active; };
    host.selection = [] { return std::vector<hmi::Id>{}; };
    host.apply = [&](core::CommandPtr c) { (void)f.stack.push(std::move(c)); };
    host.projectName = [] { return std::string("Essai"); };
    host.projectPath = [&] { return U(dir / "Essai"); };
    host.openView = [&](hmi::Id id) { opened.push_back(id); };
    ai::Assistant a(host);
    a.load();
    ai::Settings s = a.settings();
    s.provider = "anthropic";
    s.model = "modele-essai";
    s.mode = "previsualisation";
    a.setSettings(s);
    a.setApiKey("cle-essai");
    a.setProviderFactory([&](std::string_view) { return std::make_unique<FakeProvider>(script); });
    double t = 10;
    const auto run = [&](const std::string& text) {
        std::string why;
        const bool ok = a.send(text, &why);
        for (int guard = 0; a.generating() && guard < 200; ++guard) a.tick(t += 0.05);
        return ok;
    };
    const auto last = [&]() -> int {
        const auto* c = a.current();
        if (!c) return -1;
        for (int i = static_cast<int>(c->messages.size()) - 1; i >= 0; --i)
            if (c->messages[static_cast<std::size_t>(i)].proposal) return i;
        return -1;
    };
    script.replies.push_back(toolReply("{\"resume\": \"Vue des pompes\", \"operations\": [{\"action\": \"creer_vue\", \"nom\": \"Vue_Pompes\"}, "
                                       "{\"action\": \"creer\", \"genre\": \"Rectangle\", \"nom\": \"Cadre\", \"proprietes\": {\"x\": \"10\", \"y\": \"10\"}}]}"));
    check(run("cree une vue des pompes"), "envoyee");
    const int m = last();
    const ai::Proposal* p = a.proposal(m);
    check(p && ai::createsView(*p) && p->view == "Vue_Pompes" && p->create == 1, "la carte : la vue neuve, un objet a creer");
    std::string why;
    check(m >= 0 && a.apply(m, &why), "appliquee (" + why + ")");
    const auto* nv = f.doc->project.viewByName("Vue_Pompes");
    check(nv && opened.size() == 1 && opened.front() == nv->id, "la vue creee s'ouvre dans l'editeur");
    check(a.appliedState(m) == ai::Assistant::Applied::Applied, "la carte : appliquee");
    // L'utilisateur travaille ailleurs : la vue ouverte change, la carte reste appliquee.
    f.v().objects.push_back(hmi::makeObject(hmi::Kind::Rectangle, f.doc->project.allocate(), "Ailleurs", 0, 0, f.v().activeLayer));
    check(a.appliedState(m) == ai::Assistant::Applied::Applied, "une autre vue modifiee : toujours appliquee (elle ne depend pas de la vue ouverte)");
    f.v().objects.pop_back();
    (void)f.stack.undo();
    check(!f.doc->project.viewByName("Vue_Pompes") && a.appliedState(m) == ai::Assistant::Applied::Undone, "Ctrl+Z : la carte dit annulee");
    check(a.reapply(m, &why) && f.doc->project.viewByName("Vue_Pompes") && a.appliedState(m) == ai::Assistant::Applied::Applied,
          "Reappliquer la remet (" + why + ")");
    check(opened.size() == 2, "Reappliquer rouvre la vue");
    // Sans vue ouverte : des variables seules.
    active = hmi::kNoId;
    script.replies.push_back(toolReply("{\"resume\": \"Variables\", \"operations\": [{\"action\": \"creer_variables\", \"variables\": "
                                       "[{\"nom\": \"Marche_P9\", \"type\": \"BOOL\"}]}]}"));
    check(run("cree la variable Marche_P9"), "envoyee sans vue ouverte");
    const int m2 = last();
    check(m2 > m && a.check(m2).ok() && a.apply(m2, &why) && f.doc->project.variable("Marche_P9"), "des variables sans vue : appliquees (" + why + ")");
    check(opened.size() == 2, "des variables : aucune vue a ouvrir");
    (void)f.stack.undo();
    check(a.appliedState(m2) == ai::Assistant::Applied::Undone, "Ctrl+Z : annulee");
    // La conversation relue : la proposition garde sa vue et ses empreintes.
    ai::Assistant b(host);
    b.load();
    const ai::Proposal* r = b.proposal(m);
    check(r && ai::createsView(*r) && r->fingerprintAfter == a.proposal(m)->fingerprintAfter && b.appliedState(m) == ai::Assistant::Applied::Applied,
          "relue d'une conversation : appliquee, la meme empreinte");
    // 1.16.0 (captures) : par l'IA integree, la proposition garde le nom de la vue OUVERTE a la demande ; la vue
    // neuve se lit dans creer_vue - elle s'ouvre apres Appliquer, et Ctrl+Z rend la carte annulee.
    active = f.view;
    s = a.settings();
    s.provider = "integree";
    a.setSettings(s);
    const std::size_t before = opened.size();
    check(run("Cr\xC3\xA9" "e une vue Vue_Process avec 2 pompes"), "IA integree : envoyee");
    const int m3 = last();
    const ai::Proposal* p3 = m3 > m2 ? a.proposal(m3) : nullptr;
    check(p3 && ai::createsView(*p3) && ai::newViewName(*p3) == "Vue_Process", "IA integree : une vue neuve, Vue_Process");
    check(p3 && a.apply(m3, &why), "IA integree : appliquee (" + why + ")");
    const auto* pv = f.doc->project.viewByName("Vue_Process");
    check(pv && opened.size() == before + 1 && opened.back() == pv->id, "IA integree : la vue creee s'ouvre (pas la vue ouverte a la demande)");
    check(a.appliedState(m3) == ai::Assistant::Applied::Applied, "IA integree : la carte dit appliquee");
    (void)f.stack.undo();
    check(!f.doc->project.viewByName("Vue_Process") && a.appliedState(m3) == ai::Assistant::Applied::Undone,
          "IA integree : Ctrl+Z, la carte dit annulee (" + (p3 ? a.proposal(m3)->fingerprint + " / " + a.proposal(m3)->fingerprintAfter + " / "
                                                                    + ai::proposalPrint(f.doc->project, *a.proposal(m3)) : std::string{}) + ")");
    check(a.reapply(m3, &why) && f.doc->project.viewByName("Vue_Process"), "IA integree : Reappliquer la remet (" + why + ")");
    fs::remove_all(dir);
}

} // namespace


// ---- 1.18.0 : les outils de l'IA -------------------------------------------------------------------
void tools118() {
    std::printf("1.18.0 : les outils de l'IA\n");
    std::string why;
    // -- un type generique, puis une variable d'une de ses instances
    {
        Fixture f;
        auto t = type("T_Pile", {{"Elements", "LIST OF T", ""}, {"Taille", "INT", ""}});
        t.generics = {"T"};
        auto p = f.proposal({t, vars({{"Pile_1", "T_Pile<REAL>", "", ""}})});
        auto ck = ai::validate(f.doc->project, p);
        check(ck.ok() && ck.newTypes.size() == 1 && ck.newTypes[0].first == "T_Pile<T>", "T_Pile<T> : membres en T, une variable T_Pile<REAL> (" + errorsOf(ck) + ")");
        check(f.apply(p, &why), "appliquee (" + why + ")");
        const auto* tp = f.doc->project.hmiTypeByName("T_Pile");
        check(tp && tp->generics.size() == 1 && tp->generics[0].name == "T", "le type porte son generique T");
        auto two = type("T_Paire", {{"Cle", "K", ""}, {"Valeur", "T", ""}});
        two.generics = {"T", "K : ANY_NUM"};
        check(ai::validate(f.doc->project, f.proposal({two})).ok(), "T_Paire<T, K : ANY_NUM> : deux generiques");
        auto dup = type("T_Mal", {{"A", "T", ""}});
        dup.generics = {"T", "T"};
        check(!ai::validate(f.doc->project, f.proposal({dup})).ok(), "deux generiques du meme nom : refuse");
        auto noGen = type("T_Sans", {{"A", "T", ""}});
        check(!ai::validate(f.doc->project, f.proposal({noGen})).ok(), "un membre de type T sans generique T : refuse");
    }
    // -- un symbole (parametres, generiques), une popup, un en-tete
    {
        Fixture f;
        auto sym = op(A::View, "Sym_Jauge", {{"role", "symbole"}});
        sym.params = {{"Valeur", "T", "la mesure", "0"}, {"Unite", "STRING", "", "'%'"}};
        sym.generics = {"T : ANY_NUM"};
        auto p = f.proposal({sym, create("Rectangle", "Fond", {{"x", "0"}, {"y", "0"}, {"w", "200"}, {"h", "200"}})});
        auto ck = ai::validate(f.doc->project, p);
        check(ck.ok() && std::any_of(ck.made.begin(), ck.made.end(), [](const ai::Check::Made& m) { return m.what == "Symbole" && has(m.detail, "Valeur : T"); }),
              "un symbole : ses parametres montres dans l'apercu (" + errorsOf(ck) + ")");
        const auto start = f.doc->project.config.startView;
        check(f.apply(p, &why), "le symbole cree (" + why + ")");
        const auto* v = f.doc->project.viewByName("Sym_Jauge");
        check(v && v->role == "symbole" && v->params.size() == 2 && v->generics.size() == 1 && v->width == 200 && v->objects.size() == 1,
              "Sym_Jauge : role symbole, 2 parametres, 1 generique, 200 x 200, son objet");
        check(f.doc->project.config.startView == start, "un symbole ne devient jamais la vue de demarrage");
        std::vector<ai::Operation> ops;
        ai::json::Value arr;
        check(ai::json::parse(R"([{"action":"creer_popup","nom":"Pop_Reglage","proprietes":{"titre":"Reglage","modale":"false"},"parametres":[{"nom":"Consigne","type":"REAL"}]}])", arr)
                  && ai::operationsFromJson(arr, ops) && ops.size() == 1 && ops[0].action == A::View,
              "creer_popup se lit comme creer_vue + role popup");
        auto pp = f.proposal(ops);
        check(f.apply(pp, &why), "la popup creee (" + why + ")");
        const auto* pop = f.doc->project.viewByName("Pop_Reglage");
        check(pop && pop->role == "popup" && pop->popup.title == "Reglage" && !pop->popup.modal && pop->params.size() == 1, "Pop_Reglage : popup, titre, non modale, 1 parametre");
        check(f.apply(f.proposal({op(A::View, "Entete_Usine", {{"role", "entete"}})}), &why), "un en-tete cree");
        const auto* head = f.doc->project.viewByName("Entete_Usine");
        check(head && head->role == "entete" && head->height == 100 && head->width == 1280, "l'en-tete : 1280 x 100");
        auto badParam = op(A::View, "Vue_P", {});
        badParam.params = {{"X", "REAL", "", ""}};
        check(!ai::validate(f.doc->project, f.proposal({badParam})).ok(), "des parametres sur une vue ordinaire : refuse");
    }
    // -- un equipement (detection proposee), des adresses, une formule de lien
    {
        Fixture f;
        auto eq = op(A::Equipment, "Centrale_1", {{"adresse", "192.168.1.40"}, {"esclave", "3"}});
        eq.detect = true;
        auto addr = op(A::Address, {}, {{"equipement", "Centrale_1"}, {"adresse", "%M5"}});
        addr.variable = "Pompe_1_Marche";
        auto ex = type("T_Ex", {{"Retour", "INT", ""}, {"Cmd", "INT", ""}});
        auto table = vars({{"Ex", "ARRAY[0..3] OF T_Ex", "", ""}});
        auto addrF = op(A::Address, {}, {{"equipement", "Centrale_1"}, {"adresse", "%MW500"}, {"motif", "[*].Retour"}, {"formule", "%MW(1000 + 10 * i)"}});
        addrF.variable = "Ex";
        auto p = f.proposal({eq, addr, ex, table, addrF});
        auto ck = ai::validate(f.doc->project, p);
        check(ck.ok(), "equipement + adresses + formule de lien : valide (" + errorsOf(ck) + ")");
        check(ck.detections.size() == 1 && ck.detections[0].second == "192.168.1.0/24" && anyHas(ck.warnings, "c'est toi qui la lances"),
              "la detection : PROPOSEE sur 192.168.1.0/24, jamais lancee par l'IA");
        const std::size_t steps = f.stack.done().size();
        check(f.apply(p, &why) && f.stack.done().size() == steps + 1, "tout en UNE commande (" + why + ")");
        const auto* e = f.doc->project.equipmentByName("Centrale_1");
        const auto* v = f.doc->project.variable("Pompe_1_Marche");
        const auto* t = f.doc->project.variable("Ex");
        check(e && e->host == "192.168.1.40" && e->unit == 3 && e->port == 502, "Centrale_1 : 192.168.1.40:502, esclave 3");
        check(v && v->equipment == "Centrale_1" && v->address == "%M5", "Pompe_1_Marche liee a Centrale_1, %M5");
        check(t && t->equipment == "Centrale_1" && !t->places.empty(), "Ex : liee, ses cases placees par la formule");
        (void)f.stack.undo();
        check(!f.doc->project.equipmentByName("Centrale_1") && f.doc->project.variable("Pompe_1_Marche")->equipment.empty() && !f.doc->project.variable("Ex"),
              "Ctrl+Z : l'equipement, les adresses et la variable partent ensemble");
        auto unknown = op(A::Address, {}, {{"equipement", "Centrale_1"}, {"adresse", "%MW1"}});
        unknown.variable = "Pas_De_Variable";
        auto badAddr = op(A::Address, {}, {{"equipement", "Centrale_1"}, {"adresse", "n'importe quoi"}});
        badAddr.variable = "Pompe_1_Defaut";
        auto eth = op(A::Equipment, "Switch_1", {{"type", "ethernet_tcp"}, {"adresse", "192.168.1.2"}});
        auto onEth = op(A::Address, {}, {{"equipement", "Switch_1"}, {"adresse", "%MW1"}});
        onEth.variable = "Pompe_1_Defaut";
        const auto bk = ai::validate(f.doc->project, f.proposal({eq, unknown, badAddr, eth, onEth}));
        check(anyHas(bk.errors, "introuvable"), "une variable inconnue : refusee (jamais inventee)");
        check(anyHas(bk.errors, "illisible"), "une adresse illisible : refusee");
        check(anyHas(bk.errors, "Ethernet"), "adresser un equipement Ethernet : refuse");
    }
    // -- un script, une fonction (ses blocs VAR deviennent ses declarations), une alarme : compiles, jamais executes
    {
        Fixture f;
        auto sc = op(A::Script, "Init_Pompes", {{"langage", "ST"}, {"evenement", "demarrage"}, {"corps", "Pompe_1_Marche := FALSE;"}});
        auto fn = op(A::Function, "Moyenne", {{"retour", "REAL"}, {"corps", "VAR_INPUT\n    a : REAL;\n    b : REAL;\nEND_VAR\nMoyenne := (a + b) / 2.0;"}});
        auto al = op(A::Alarm, "Defaut_P1", {{"condition", "Pompe_1_Defaut"}, {"message", "Pompe 1 en defaut"}, {"priorite", "2"}});
        auto p = f.proposal({sc, fn, al});
        auto ck = ai::validate(f.doc->project, p);
        check(ck.ok() && ck.made.size() == 3, "script, fonction, alarme : valides (" + errorsOf(ck) + ")");
        check(f.apply(p, &why), "appliques (" + why + ")");
        const auto* s1 = f.doc->project.generalScript("Init_Pompes");
        const auto* f1 = f.doc->project.functionByName("Moyenne");
        const auto* a1 = f.doc->project.alarmByName("Defaut_P1");
        check(s1 && s1->event == "Demarrage" && s1->lang == hmi::ScriptLang::ST, "le script : Demarrage, ST");
        check(f1 && f1->returnType == "REAL" && f1->decls.size() == 2 && !has(f1->body, "VAR_INPUT"), "la fonction : ses parametres devenus ses declarations");
        check(a1 && a1->priority == 2 && a1->condition == "Pompe_1_Defaut", "l'alarme : priorite 2");
        auto badSc = op(A::Script, "Mauvais", {{"corps", "Inconnue_X := 1;"}});
        auto badAl = op(A::Alarm, "Mauvaise", {{"condition", "Pas_La > 3"}});
        const auto bk = ai::validate(f.doc->project, f.proposal({badSc, badAl}));
        check(anyHas(bk.errors, "Script Mauvais") && anyHas(bk.errors, "Alarme Mauvaise"), "un nom inconnu dans le code ou la condition : refuse (" + errorsOf(bk) + " / avert. " + std::to_string(bk.warnings.size()) + (bk.warnings.empty() ? std::string{} : " : " + bk.warnings.front()) + ")");
        check(anyHas(ai::validate(f.doc->project, f.proposal({op(A::Script, "Vide", {})})).errors, "corps"), "un script sans code : refuse");
    }
    // -- une ressource depuis une piece jointe ; des dossiers, un rangement
    {
        Fixture f;
        const fs::path dir = fs::temp_directory_path() / "xpg_ai_outils118";
        fs::remove_all(dir);
        fs::create_directories(dir);
        {
            std::ofstream out(dir / "logo.png", std::ios::binary);
            out << "\x89PNG\r\n\x1a\n-- essai --";
        }
        ai::Env env;
        env.attachmentPath = [&](std::string_view n) { return n == "logo.png" ? U(dir / "logo.png") : std::string{}; };
        auto res = op(A::Resource, {}, {{"piece_jointe", "logo.png"}, {"dossier", "Logos"}});
        auto folder = op(A::Folder, {}, {{"liste", "vues"}, {"dossier", "Ligne 1"}});
        auto store = op(A::Store, {}, {{"liste", "vues"}, {"dossier", "Ligne 1"}});
        store.objects = {"Accueil"};
        auto p = f.proposal({res, folder, store});
        auto ck = ai::validate(f.doc->project, p, env);
        check(ck.ok() && ck.made.size() == 3, "ressource, dossier, rangement : valides (" + errorsOf(ck) + ")");
        check(f.apply(p, &why, env), "appliques (" + why + ")");
        const auto* r = f.doc->project.resourceByName("logo.png");
        check(r && r->data && r->data->size() > 8 && r->folder == "Logos", "logo.png : copiee dans le projet, dans son dossier");
        const auto* acc = f.doc->project.viewByName("Accueil");
        check(acc && acc->folder == "Ligne 1", "Accueil rangee dans Ligne 1");
        auto missing = op(A::Resource, {}, {{"piece_jointe", "absent.png"}});
        check(anyHas(ai::validate(f.doc->project, f.proposal({missing}), env).errors, "pi\xC3\xA8" "ce jointe"), "une piece jointe absente : refusee");
        auto badStore = op(A::Store, {}, {{"liste", "vues"}, {"dossier", "Ligne 1"}});
        badStore.objects = {"Vue_Inexistante"};
        check(!ai::validate(f.doc->project, f.proposal({badStore})).ok(), "ranger un element absent : refuse");
        fs::remove_all(dir);
    }
    // -- le JSON des outils, aller-retour
    {
        ai::json::Value arr;
        const bool parsed = ai::json::parse(R"([{"action":"creer_symbole","nom":"Sym_V","parametres":[{"nom":"Ouv","type":"BOOL"}],"generiques":["T"]},
            {"action":"creer_equipement","nom":"Var_1","adresse":"10.0.0.5","port":502,"detecter":true},
            {"action":"adresser","variable":"Pompe_1_Marche","equipement":"Var_1","adresse":"%MW10"},
            {"action":"creer_script","nom":"S1","langage":"ST","evenement":"Cyclique","periode":500,"corps":"x := 1;"},
            {"action":"ranger","liste":"scripts","dossier":"Outils","objets":["S1"]}])", arr);
        std::vector<ai::Operation> ops, back;
        check(parsed && ai::operationsFromJson(arr, ops) && ops.size() == 5, "le JSON des outils se lit");
        check(ops.size() == 5 && ops[0].params.size() == 1 && ops[0].generics.size() == 1 && ops[1].detect && ops[3].action == A::Script && ops[4].objects.size() == 1,
              "parametres, generiques, detecter, script, ranger");
        check(ai::operationsFromJson(ai::operationsToJson(ops), back) && back == ops, "l'aller-retour JSON garde tout");
    }
}

int main() {
    // Le dossier IA des essais (les connaissances relues par les consignes, les conversations).
    const fs::path root = fs::temp_directory_path() / "xpg_ai_options_racine";
    fs::remove_all(root);
    fs::create_directories(root);
    ai::setRoot(U(root / "IA"));
    createView();
    createVariables();
    createTypes();
    formulas();
    markersAndLenient();
    duplicateMarkers();
    json();
    toolReading();
    automate();
    context();
    assistant();
    tools118();
    fs::remove_all(root);
    std::printf("ai_options_test : %d verifications, %d echecs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
