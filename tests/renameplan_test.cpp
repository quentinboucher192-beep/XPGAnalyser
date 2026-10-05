// tests/renameplan_test.cpp - lot 7 : renommer en voyant tout ce qui suit.
//
//   renameplan_test [MAST.XPG]
//
//  Sans ecran. Les outils du plan (la difference en ligne, les noms) ; un petit
//  programme fait a la main, avec une IHM : le plan d'une globale (sa
//  declaration, ses lignes de code, sa table, ce que l'IHM en cite, la
//  variable IHM liee par son nom), les noms refuses des deux cotes, UNE
//  commande faite puis defaite (le programme ET l'IHM reviennent) ; une
//  variable IHM (ses lignes de table), une vue (qui l'ouvre, qui la cite). Sur
//  le projet d'essai : generalites (declaration, code, table ARM), un nom pris,
//  le type config_armoire, et Ctrl+Z qui rend le texte des sections touchees.
//  Lot API 8 : un champ de DDT (les chemins de ce type seulement, un acces qui
//  ne dit pas son type refuse), les scripts C et C++ de l'IHM, les chaines
//  comparees a SYS.CurrentView ; config_gaz.Nom_gaz sur le projet d'essai ; le
//  nom tape dans la case Nom d'une variable IHM (requestRename, le crochet de
//  l'ecran -> le plan -> une commande : les expressions des objets suivent) ;
//  les forcages enregistres de la simulation (une ligne "nom = valeur").
#include "../src/app/RenameDialog.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/RenamePlan.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace pr = project::rename;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

std::size_t countIf(const pr::Plan& plan, const std::function<bool(const pr::Change&)>& f) {
    return static_cast<std::size_t>(std::count_if(plan.changes.begin(), plan.changes.end(), f));
}
const pr::Change* findChange(const pr::Plan& plan, const std::function<bool(const pr::Change&)>& f) {
    for (const auto& c : plan.changes)
        if (f(c)) return &c;
    return nullptr;
}
bool pathHas(const pr::Change& c, const std::string& part) {
    return std::find(c.path.begin(), c.path.end(), part) != c.path.end();
}

// Un petit programme : deux globales, une unite avec sa variable, deux
// sections, une table d'animation (une ligne API, une ligne IHM).
std::shared_ptr<domain::Project> miniProject() {
    auto p = std::make_shared<domain::Project>();
    const auto sym = [&](const char* s) { return p->strings.intern(s); };
    const auto global = [&](const char* name, const char* type, const char* address) {
        domain::Variable v;
        v.name = sym(name);
        v.type.name = sym(type);
        v.type.klass = domain::TypeClass::Elementary;
        v.scope = domain::VariableScope::Global;
        if (address) v.address = domain::Address::parse(address);
        p->variables.push_back(std::move(v));
    };
    global("Vitesse", "INT", "%MW100");
    global("Debit", "REAL", nullptr);
    domain::Pou unit;
    unit.name = sym("Ligne");
    unit.kind = domain::PouKind::ProgramUnit;
    p->pous.push_back(unit);
    domain::Variable local;
    local.name = sym("compteur");
    local.type.name = sym("INT");
    local.scope = domain::VariableScope::Local;
    local.owner = 0;
    p->variables.push_back(local);
    p->pous[0].locals.push_back(2);
    domain::Section a;
    a.name = sym("Calcul");
    a.language = domain::PouLanguage::ST;
    a.owner = 0;
    a.body = "(* Vitesse en tours *)\nIF Vitesse > 10 THEN\n    Debit := Vitesse * 2.5;\nEND_IF;\ncompteur := compteur + 1;\nmsg := 'Vitesse';\n";
    p->sections.push_back(a);
    p->pous[0].sections.push_back(0);
    domain::Section b;
    b.name = sym("Autre");
    b.language = domain::PouLanguage::ST;
    b.body = "x := Vitesse2 + Vitesse;\n";
    p->sections.push_back(b);
    domain::AnimationTable t;
    t.name = sym("Suivi");
    t.owner = sym("Ligne");
    t.entries.push_back({sym("Vitesse"), false});
    t.entries.push_back({sym("Marche_IHM"), true});
    p->animationTables.push_back(t);
    p->buildIndices();
    return p;
}

// Une IHM qui lit Vitesse : un texte a trous, une visibilite, une action, un
// script general (et un script qui a SA variable Vitesse), une alarme, une
// popup dont un parametre s'appelle Vitesse, une variable IHM liee par son nom.
std::shared_ptr<hmi::Document> miniHmi() {
    auto doc = std::make_shared<hmi::Document>();
    auto& p = doc->project;
    hmi::View v = hmi::makeView(p, "Vue_Ligne");
    auto text = hmi::makeObject(hmi::Kind::Text, p.allocate(), "Texte_1", 10, 10, v.activeLayer);
    text.set("text", "Vitesse : {Vitesse:0} tr/min, {Vue_Ligne.Texte_1.Visible}");
    text.setExpr("visible", "Vitesse > 0 AND Marche_IHM");
    v.objects.push_back(text);
    auto button = hmi::makeObject(hmi::Kind::Button, p.allocate(), "Bouton_1", 10, 60, v.activeLayer);
    hmi::Action a;
    a.trigger = hmi::Trigger::Click;
    a.operation = hmi::Operation::Assign;
    a.target = "Vitesse";
    a.value = "Vitesse + 1";
    button.actions.push_back(a);
    v.objects.push_back(button);
    p.views.push_back(std::move(v));

    hmi::View popup = hmi::makeView(p, "Popup_Moteur");
    popup.role = "popup";
    popup.params.push_back({"Vitesse", "0", "la vitesse montree"});
    auto shown = hmi::makeObject(hmi::Kind::Text, p.allocate(), "Texte_P", 10, 10, popup.activeLayer);
    shown.setExpr("visible", "Vitesse > 5");
    popup.objects.push_back(shown);
    auto nav = hmi::makeObject(hmi::Kind::Button, p.allocate(), "Vers_Ligne", 10, 60, popup.activeLayer);
    hmi::Action go;
    go.trigger = hmi::Trigger::Click;
    go.operation = hmi::Operation::Navigate;
    go.target = "Vue_Ligne";
    nav.actions.push_back(go);
    popup.objects.push_back(nav);
    p.views.push_back(std::move(popup));

    hmi::Script s;
    s.id = p.allocate();
    s.name = "Demarrage";
    s.event = "Demarrage";
    s.body = "IF Vitesse > 100 THEN\n    IHM_JOURNAL('Trop vite : {Vitesse}');\n    IHM_NAVIGUER('Vue_Ligne');\nEND_IF;\n";
    p.programs.scripts.push_back(s);
    hmi::Script own;
    own.id = p.allocate();
    own.name = "Local";
    own.event = "Appel";
    own.body = "VAR\n    Vitesse : INT;\nEND_VAR\nVitesse := 3;\n";
    p.programs.scripts.push_back(own);

    hmi::AlarmDef al;
    al.id = p.allocate();
    al.name = "Survitesse";
    al.condition = "Vitesse > 1500";
    al.message = "Survitesse ({Vitesse} tr/min)";
    p.alarms.push_back(al);

    hmi::Variable linked;
    linked.id = p.allocate();
    linked.name = "Vitesse_IHM";
    linked.type = "INT";
    linked.equipment = "Automate";
    linked.address = "Vitesse";
    p.programs.variables.push_back(linked);
    hmi::Variable marche;
    marche.id = p.allocate();
    marche.name = "Marche_IHM";
    marche.type = "BOOL";
    p.programs.variables.push_back(marche);
    // Une fonction IHM du nom d'une globale : Debit(...) et Debit se confondraient.
    hmi::HmiFunction fn;
    fn.id = p.allocate();
    fn.name = "Debit";
    fn.returnType = "REAL";
    fn.body = "Debit := 1.0;\n";
    p.programs.functions.push_back(fn);
    return doc;
}

std::string textOf(const hmi::Project& p, const char* view, const char* object, const char* key, bool expr) {
    const auto* v = p.viewByName(view);
    const auto* o = v ? v->objectByName(object) : nullptr;
    if (!o) return "?";
    return expr ? o->expr(key) : o->text(key);
}

// ---- Lot API 8 ---------------------------------------------------------------
// Trois types : T_Moteur (Vitesse, Etat), T_Pompe (Etat aussi), T_Ligne (mot :
// T_Moteur) ; des globales de ces types (un tableau), une globale Vitesse (le nom
// du champ), une unite qui recoit un T_Moteur (m) et un parametre effectif
// M1.Vitesse ; une table (une ligne API, une ligne IHM).
std::shared_ptr<domain::Project> fieldProject() {
    using S = domain::VariableScope;
    auto p = std::make_shared<domain::Project>();
    const auto sym = [&](const char* s) { return p->strings.intern(s); };
    const auto var = [&](const char* name, const char* type, S scope, domain::Index owner) {
        domain::Variable v;
        v.name = sym(name);
        v.type.name = sym(type);
        v.scope = scope;
        v.owner = owner;
        p->variables.push_back(std::move(v));
        return static_cast<domain::Index>(p->variables.size() - 1);
    };
    for (const char* t : {"T_Moteur", "T_Pompe", "T_Ligne"}) {
        domain::DerivedType dt;
        dt.name = sym(t);
        p->derivedTypes.push_back(dt);
    }
    p->derivedTypes[0].fields = {var("Vitesse", "INT", S::DerivedMember, 0), var("Etat", "BOOL", S::DerivedMember, 0)};
    p->derivedTypes[1].fields = {var("Etat", "INT", S::DerivedMember, 1)};
    p->derivedTypes[2].fields = {var("mot", "T_Moteur", S::DerivedMember, 2)};
    (void)var("M1", "T_Moteur", S::Global, domain::kNoIndex);
    (void)var("Moteurs", "ARRAY[0..3] OF T_Moteur", S::Global, domain::kNoIndex);
    (void)var("P1", "T_Pompe", S::Global, domain::kNoIndex);
    (void)var("L1", "T_Ligne", S::Global, domain::kNoIndex);
    (void)var("Vitesse", "INT", S::Global, domain::kNoIndex);
    domain::Pou unit;
    unit.name = sym("Ligne");
    unit.kind = domain::PouKind::ProgramUnit;
    p->pous.push_back(unit);
    p->pous[0].parameters.push_back(var("m", "T_Moteur", S::InOut, 0));
    const auto vin = var("v_in", "INT", S::Input, 0);
    p->variables[vin].attributes.emplace_back("EffectiveParameter", "M1.Vitesse");
    p->pous[0].parameters.push_back(vin);
    domain::Section a;
    a.name = sym("Calcul");
    a.language = domain::PouLanguage::ST;
    a.owner = 0;
    a.body = "M1.Vitesse := 1;\nMoteurs[i].Vitesse := Moteurs[i + 1].Vitesse;\nL1.mot.Vitesse := Vitesse;\nP1.Etat := 3;\n"
             "m.Vitesse := 2; (* M1.Vitesse *)\nmsg := 'M1.Vitesse';\n";
    p->sections.push_back(a);
    p->pous[0].sections.push_back(0);
    domain::Section b;
    b.name = sym("Divers");
    b.language = domain::PouLanguage::ST;
    b.body = "x.Vitesse := 5;\ny.Etat := TRUE;\nM1.Etat := P1.Etat > 0;\n";
    p->sections.push_back(b);
    domain::AnimationTable t;
    t.name = sym("Moteurs_suivis");
    t.entries.push_back({sym("Moteurs[2].Vitesse"), false});
    t.entries.push_back({sym("Four.Vitesse"), true});
    p->animationTables.push_back(t);
    p->buildIndices();
    return p;
}

// L'IHM qui va avec : un texte et une visibilite qui lisent M1.Vitesse et
// Moteurs[2].Vitesse ; une variable IHM Four d'un type IHM qui a aussi un membre
// Vitesse ; une variable liee par le chemin L1.mot.Vitesse ; un script C ; deux
// vues, et ce qui compare SYS.CurrentView (ou IHM_VUE()) au nom de la premiere.
std::shared_ptr<hmi::Document> fieldHmi() {
    auto doc = std::make_shared<hmi::Document>();
    auto& p = doc->project;
    hmi::HmiType four;
    four.id = p.allocate();
    four.name = "T_Four";
    four.members.push_back({"Vitesse", "REAL"});
    p.programs.types.push_back(four);
    hmi::Variable fv;
    fv.id = p.allocate();
    fv.name = "Four";
    fv.type = "T_Four";
    p.programs.variables.push_back(fv);
    hmi::Variable bound;
    bound.id = p.allocate();
    bound.name = "Vitesse_Ligne";
    bound.type = "INT";
    bound.equipment = "Automate";
    bound.address = "L1.mot.Vitesse";
    p.programs.variables.push_back(bound);
    hmi::Variable marche;
    marche.id = p.allocate();
    marche.name = "Marche_IHM";
    marche.type = "BOOL";
    p.programs.variables.push_back(marche);

    hmi::View a = hmi::makeView(p, "Vue_A");
    auto texte = hmi::makeObject(hmi::Kind::Text, p.allocate(), "Texte_M1", 10, 10, a.activeLayer);
    texte.set("text", "M1 : {M1.Vitesse} tr/min");
    texte.setExpr("visible", "Moteurs[2].Vitesse > 0 AND Four.Vitesse > 0");
    a.objects.push_back(texte);
    p.views.push_back(std::move(a));
    hmi::View b = hmi::makeView(p, "Vue_B");
    auto voyant = hmi::makeObject(hmi::Kind::Text, p.allocate(), "Voyant_A", 10, 10, b.activeLayer);
    voyant.setExpr("visible", "SYS.CurrentView = 'Vue_A' OR IHM_VUE() <> 'Vue_A'");
    b.objects.push_back(voyant);
    hmi::Script vs;
    vs.id = p.allocate();
    vs.name = "Retour";
    vs.event = "OnOpen";
    vs.body = "IF 'Vue_A' = SYS.PreviousView THEN\n    IHM_POPUP('Vue_A');\n    IHM_JOURNAL('Vue_A');\nEND_IF;\n";
    b.scripts.push_back(vs);
    p.views.push_back(std::move(b));

    hmi::Script c;
    c.id = p.allocate();
    c.name = "Cible";
    c.lang = hmi::ScriptLang::C;
    c.event = "Appel";
    c.body = "/* M1.Vitesse, Marche_IHM */\nint Vitesse = 0;\nfloat v = M1.Vitesse + Moteurs[1].Vitesse;\n"
             "const char* chemin = \"M1.Vitesse\";\nif (Marche_IHM) { lire(\"Marche_IHM\"); x.Marche_IHM = 1; Marche_IHM(); }\n"
             "naviguer(\"Vue_A\");\n";
    p.programs.scripts.push_back(c);

    hmi::AlarmDef al;
    al.id = p.allocate();
    al.name = "Hors_vue";
    al.condition = "SYS.CurrentView <> 'Vue_A' AND M1.Vitesse > 1500";
    al.message = "Vitesse {M1.Vitesse}";
    p.alarms.push_back(al);
    return doc;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("1. Les outils\n");
    {
        const auto d = pr::inlineDiff("IF Vitesse > 10 THEN", "IF Debit > 10 THEN");
        check(d.size() == 4 && d[0].text == "IF " && d[1].kind == 1 && d[1].text == "Vitesse" && d[2].kind == 2 && d[2].text == "Debit"
                  && d[3].text == " > 10 THEN",
              "la difference mot a mot : IF [Vitesse->Debit] > 10 THEN");
        const auto e = pr::inlineDiff("a.b", "a.b.c");
        check(!e.empty() && e.front().text == "a.b" && e.back().kind == 2, "des textes de longueurs differentes : le debut commun");
        check(pr::kindFromKey("IHM-Vue") == pr::Kind::HmiView && pr::kindFromKey("unite") == pr::Kind::Unit && !pr::kindFromKey("champ"),
              "les genres par leur cle (sans casse) ; un genre inconnu");
        check(pr::isIdentifier("Vitesse_2") && !pr::isIdentifier("2Vitesse") && !pr::isIdentifier("Vi tesse"), "un identifiant");
        check(pr::isReservedWord("end_if") && pr::isReservedWord("REAL") && !pr::isReservedWord("Vitesse"), "les mots reserves");
        check(pr::replaceInsensitive("VITESSE_IHM", "Vitesse", "Debit") == "Debit_IHM", "le nom propose a une liee");
    }

    std::printf("2. Un petit programme, avec son IHM\n");
    {
        auto p = miniProject();
        auto doc = miniHmi();
        auto side = app::makeHmiRenameSide(doc);
        const auto preview = pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Vitesse", "", true);
        check(preview.found() && preview.verdict == pr::Verdict::Unchanged && !preview.changes.empty(),
              "sans nouveau nom : les endroits ou Vitesse est citee (" + std::to_string(preview.count()) + ")");
        check(preview.links.size() == 1 && preview.links[0].name == "Vitesse_IHM" && preview.links[0].proposed.empty(),
              "la variable IHM liee par son nom, sans nom propose tant qu'il n'y a pas de nouveau nom");

        const auto plan = pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Vitesse", "Regime", true);
        check(plan.ok(), "Vitesse -> Regime : libre (" + plan.problem + ")");
        check(plan.links.size() == 1 && plan.links[0].proposed == "Regime_IHM" && plan.links[0].usable() && plan.renames.size() == 2,
              "la liee suit : Vitesse_IHM -> Regime_IHM");
        const auto* decl = findChange(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Api && pathHas(c, "D\xC3\xA9" "claration"); });
        check(decl && decl->before == "Vitesse : INT AT %MW100" && decl->after == "Regime : INT AT %MW100", "la declaration");
        const auto code = countIf(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Api && c.line > 0; });
        // La section d'une unite qui porte un autre nom : "Ligne > Calcul".
        const auto* l2 = findChange(plan, [](const pr::Change& c) { return c.line == 2 && pathHas(c, "Ligne \xE2\x80\xBA Calcul"); });
        check(code == 3 && l2 && l2->before == "IF Vitesse > 10 THEN" && l2->after == "IF Regime > 10 THEN",
              "trois lignes de code : pas le commentaire, pas la chaine, pas Vitesse2 (" + std::to_string(code) + ")");
        const auto* table = findChange(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Tables; });
        check(table && table->before == "Vitesse" && table->after == "Regime" && pathHas(*table, "Suivi"), "la ligne API de la table Suivi");
        const auto ihm = plan.count(pr::Tab::Hmi);
        const auto* visible = findChange(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Hmi && pathHas(c, "Texte_1") && c.label == "visible (expression)"; });
        check(visible && visible->after == "Regime > 0 AND Marche_IHM", "IHM : la visibilite de Texte_1");
        check(findChange(plan, [](const pr::Change& c) { return pathHas(c, "Texte_P"); }) == nullptr,
              "IHM : la popup dont le parametre s'appelle Vitesse n'est pas touchee");
        check(findChange(plan, [](const pr::Change& c) { return pathHas(c, "Local"); }) == nullptr,
              "IHM : le script qui a sa propre variable Vitesse non plus");
        check(findChange(plan, [](const pr::Change& c) { return pathHas(c, "Demarrage") && c.after.find("{Regime}") != std::string::npos; }) != nullptr,
              "IHM : le trou d'une chaine d'un script ({Vitesse} dans IHM_JOURNAL)");
        check(findChange(plan, [](const pr::Change& c) { return pathHas(c, "Survitesse") && c.label == "message"; }) != nullptr,
              "IHM : le message de l'alarme");
        check(findChange(plan, [](const pr::Change& c) { return pathHas(c, "Vitesse_IHM") && c.after == "Regime"; }) != nullptr,
              "IHM : l'adresse de la variable liee par son nom");
        check(findChange(plan, [](const pr::Change& c) { return c.label == "nom" && c.before == "Vitesse_IHM" && c.after == "Regime_IHM"; }) != nullptr,
              "IHM : la variable liee renommee (la case cochee)");
        check(ihm >= 8, "IHM : " + std::to_string(ihm) + " changements");

        // Les noms refuses.
        check(pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Vitesse", "Debit", true).verdict == pr::Verdict::Taken,
              "Debit : deja une globale");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Vitesse", "Marche_IHM", true).verdict == pr::Verdict::Taken,
              "Marche_IHM : une variable IHM (l'IHM lirait la sienne)");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Vitesse", "Ligne", true).verdict == pr::Verdict::Taken,
              "Ligne : le nom d'une unite");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Vitesse", "2x", true).verdict == pr::Verdict::Invalid, "2x : invalide");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Vitesse", "THEN", true).verdict == pr::Verdict::Invalid, "THEN : un mot du langage");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Vitesse", "TON", true).verdict == pr::Verdict::Taken,
              "TON : un bloc standard");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Ligne.compteur", "Debit", true).verdict == pr::Verdict::Taken,
              "une variable d'unite ne prend pas le nom d'une globale que son code nomme");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Absente", "X", true).verdict == pr::Verdict::NotFound, "introuvable");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::Variable, "Debit", "Flux", true).verdict == pr::Verdict::Taken,
              "Debit : une fonction IHM porte son nom - refuse, et dit pourquoi");

        // UNE commande, faite puis defaite.
        core::CommandStack stack;
        const auto bodyA = p->sections[0].body, bodyB = p->sections[1].body;
        const auto hmiBefore = doc->project;
        auto cmd = pr::makeCommand(p, side.get(), plan);
        check(cmd && static_cast<bool>(stack.push(std::move(cmd))), "la commande passe");
        check(p->strings.text(p->variables[0].name) == "Regime" && p->sections[0].body.find("IF Regime > 10") != std::string::npos
                  && p->sections[0].body.find("'Vitesse'") != std::string::npos && p->sections[1].body == "x := Vitesse2 + Regime;\n",
              "le programme : la variable, son code (la chaine et Vitesse2 intactes)");
        check(p->strings.text(p->animationTables[0].entries[0].name) == "Regime", "la table suit");
        check(textOf(doc->project, "Vue_Ligne", "Texte_1", "visible", true) == "Regime > 0 AND Marche_IHM"
                  && textOf(doc->project, "Popup_Moteur", "Texte_P", "visible", true) == "Vitesse > 5",
              "l'IHM : la vue suit, la popup garde son parametre");
        check(doc->project.variable("Regime_IHM") && doc->project.variable("Regime_IHM")->address == "Regime", "la variable liee : son nom et son adresse");
        check(static_cast<bool>(stack.undo()), "Ctrl+Z");
        check(p->sections[0].body == bodyA && p->sections[1].body == bodyB && p->strings.text(p->variables[0].name) == "Vitesse",
              "le programme revient, texte des sections compris");
        check(doc->project == hmiBefore, "l'IHM revient telle qu'elle etait");
        check(static_cast<bool>(stack.redo()) && p->strings.text(p->variables[0].name) == "Regime" && doc->project.variable("Regime_IHM") != nullptr,
              "Ctrl+Y refait les deux cotes");
        (void)stack.undo();

        // Une variable IHM : ses lignes de table (cote programme), ce qui la cite.
        const auto hv = pr::makePlan(p.get(), side.get(), pr::Kind::HmiVariable, "Marche_IHM", "EnMarche_IHM", false);
        check(hv.ok() && findChange(hv, [](const pr::Change& c) { return c.tab == pr::Tab::Tables && c.after == "EnMarche_IHM"; }) != nullptr
                  && findChange(hv, [](const pr::Change& c) { return c.tab == pr::Tab::Hmi && c.after == "Vitesse > 0 AND EnMarche_IHM"; }) != nullptr,
              "Marche_IHM -> EnMarche_IHM : la ligne IHM de la table, la visibilite");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::HmiVariable, "Marche_IHM", "Debit", false).verdict == pr::Verdict::Taken,
              "une variable IHM ne prend pas le nom d'une globale");
        const auto hv2 = pr::makePlan(p.get(), side.get(), pr::Kind::HmiVariable, "Vitesse_IHM", "Allure_IHM", true);
        check(hv2.ok() && hv2.links.size() == 1 && hv2.links[0].proposed == "Allure" && hv2.renames.size() == 2,
              "Vitesse_IHM -> Allure_IHM : la globale liee est proposee (Vitesse -> Allure)");

        // Une vue : qui l'ouvre, qui la cite (Vue.Objet), le script qui y va.
        const auto view = pr::makePlan(p.get(), side.get(), pr::Kind::HmiView, "Vue_Ligne", "Vue_Production", false);
        check(view.ok(), "Vue_Ligne -> Vue_Production : libre");
        check(findChange(view, [](const pr::Change& c) { return pathHas(c, "Vers_Ligne") && c.after == "Vue_Production"; }) != nullptr,
              "le bouton de la popup qui y navigue");
        check(findChange(view, [](const pr::Change& c) { return c.after.find("{Vue_Production.Texte_1.Visible}") != std::string::npos; }) != nullptr,
              "une variable d'instance (Vue.Objet.Propriete)");
        check(findChange(view, [](const pr::Change& c) { return c.after.find("IHM_NAVIGUER('Vue_Production')") != std::string::npos; }) != nullptr,
              "IHM_NAVIGUER('Vue_Ligne') dans un script");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::HmiView, "Vue_Ligne", "Popup_Moteur", false).verdict == pr::Verdict::Taken,
              "le nom d'une autre vue");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::HmiView, "Vue_Ligne", "Vue Ligne", false).verdict == pr::Verdict::Invalid,
              "une vue citee dans des expressions : son nouveau nom est un identifiant");

        // Les autres genres, sans IHM.
        const auto unit = pr::makePlan(p.get(), nullptr, pr::Kind::Unit, "Ligne", "Ligne_A", false);
        check(unit.ok() && findChange(unit, [](const pr::Change& c) { return c.tab == pr::Tab::Tables && c.after == "Ligne_A"; }) != nullptr,
              "une unite : la table qu'elle porte suit");
        const auto section = pr::makePlan(p.get(), nullptr, pr::Kind::Section, "Autre", "Calcul", false);
        check(section.verdict == pr::Verdict::Taken, "une section ne prend pas le nom d'une autre");
        const auto renamedTable = pr::makePlan(p.get(), nullptr, pr::Kind::Table, "Suivi", "Suivi_2", false);
        check(renamedTable.ok() && renamedTable.count() == 1, "une table d'animation : son nom");
    }

    std::printf("3. Sur le projet d'essai\n");
    if (argc < 2) {
        std::printf("       (sans MAST.XPG : rien d'essaye ici)\n");
    } else {
        core::EventBus bus;
        importer::ProjectImporter importer(bus);
        auto imported = importer.importFile(argv[1]);
        check(static_cast<bool>(imported), "le projet d'essai se lit");
        if (imported) {
            auto p = std::make_shared<domain::Project>(*imported->project);
            core::CommandStack stack;
            // Une variable d'unite : generalites, recue par Gestion_armoires (le
            // code de ses 13 sections, et la table ARM qu'elle porte).
            const auto plan = pr::makePlan(p.get(), nullptr, pr::Kind::Variable, "Gestion_armoires.generalites", "generalites_2", false);
            check(plan.ok(), "Gestion_armoires.generalites -> generalites_2 : libre (" + plan.problem + ")");
            const auto* decl = findChange(plan, [](const pr::Change& c) { return pathHas(c, "D\xC3\xA9" "claration"); });
            check(decl && decl->before.rfind("generalites : config_armoire", 0) == 0 && decl->after.rfind("generalites_2 : config_armoire", 0) == 0,
                  "sa declaration");
            const auto lines = countIf(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Api && c.line > 0; });
            bool allLines = lines > 0;
            for (const auto& c : plan.changes)
                if (c.line > 0 && (c.after.find("generalites_2") == std::string::npos || c.before.find("generalites") == std::string::npos))
                    allLines = false;
            check(allLines, "ses lignes de code (" + std::to_string(lines) + "), chacune avec son numero, avant et apres");
            check(findChange(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Tables && pathHas(c, "ARM") && c.after == "generalites_2"; }) != nullptr,
                  "la table d'animation ARM");
            check(pr::makePlan(p.get(), nullptr, pr::Kind::Variable, "Gestion_armoires.generalites", "config_utilisee", false).verdict == pr::Verdict::Taken,
                  "config_utilisee : deja pris dans l'unite");

            // Faire, defaire : le texte des sections touchees revient.
            std::map<domain::Index, std::string> before;
            for (domain::Index s = 0; s < p->sections.size(); ++s) before[s] = p->sections[s].body;
            auto cmd = pr::makeCommand(p, nullptr, plan);
            check(cmd && static_cast<bool>(stack.push(std::move(cmd))), "la commande passe");
            std::size_t touched = 0;
            for (domain::Index s = 0; s < p->sections.size(); ++s) touched += p->sections[s].body != before[s] ? 1 : 0;
            std::set<std::string> planned;
            for (const auto& c : plan.changes)
                if (c.line > 0) planned.insert(c.path.back());
            check(touched > 0 && touched == planned.size(), std::to_string(touched) + " sections reecrites, celles du plan");
            check(p->strings.text(p->variables[plan.target.index].name) == "generalites_2", "la variable s'appelle generalites_2");
            (void)stack.undo();
            bool same = true;
            for (domain::Index s = 0; s < p->sections.size(); ++s) same = same && p->sections[s].body == before[s];
            check(same && p->strings.text(p->variables[plan.target.index].name) == "generalites", "Ctrl+Z : le texte de chaque section revient");

            // Une globale : son code, et les parametres des unites qui la recoivent.
            const auto global = pr::makePlan(p.get(), nullptr, pr::Kind::Variable, "ConfigArmoireUtilisee", "ConfigArmoireActive", false);
            const auto received = countIf(global, [](const pr::Change& c) { return c.label.rfind("generalites", 0) == 0; });
            check(global.ok() && received >= 1 && global.count() > received,
                  "ConfigArmoireUtilisee : son code, et " + std::to_string(received) + " unite(s) qui la recoivent (generalites)");

            // Un type derive : les variables de ce type suivent, resolues.
            const auto ddt = pr::makePlan(p.get(), nullptr, pr::Kind::Ddt, "config_armoire", "config_armoire2", false);
            check(ddt.ok() && findChange(ddt, [](const pr::Change& c) { return c.label == "generalites"; }) != nullptr,
                  "config_armoire -> config_armoire2 : generalites en est une");
            auto ddtCmd = pr::makeCommand(p, nullptr, ddt);
            check(ddtCmd && static_cast<bool>(stack.push(std::move(ddtCmd))), "la commande passe");
            const auto& g = p->variables[plan.target.index];
            check(p->strings.text(g.type.name) == "config_armoire2" && g.type.derivedIndex != domain::kNoIndex, "generalites : config_armoire2, resolu");
            (void)stack.undo();
            check(p->strings.text(p->variables[plan.target.index].type.name) == "config_armoire", "Ctrl+Z : son type revient");
        }
    }

    std::printf("4. Lot API 8 : un champ de DDT, les scripts C, les vues comparees\n");
    {
        auto p = fieldProject();
        auto doc = fieldHmi();
        auto side = app::makeHmiRenameSide(doc);
        check(pr::kindFromKey("ddt-champ") == pr::Kind::DdtField && pr::kindKey(pr::Kind::DdtField) == "ddt-champ", "le genre ddt-champ");
        const auto none = pr::makePlan(p.get(), side.get(), pr::Kind::DdtField, "T_Moteur.Absent", "X", false);
        check(none.verdict == pr::Verdict::NotFound && none.problem.find("Absent") != std::string::npos, "un champ introuvable : " + none.problem);
        const auto preview = pr::makePlan(p.get(), side.get(), pr::Kind::DdtField, "t_moteur.vitesse", "", false);
        check(preview.found() && preview.target.display == "T_Moteur.Vitesse" && preview.count() > 0,
              "sans nouveau nom : ou T_Moteur.Vitesse est lu (" + std::to_string(preview.count()) + ")");
        check(pr::makePlan(p.get(), side.get(), pr::Kind::DdtField, "T_Moteur.Vitesse", "Etat", false).verdict == pr::Verdict::Taken,
              "Etat : un autre champ du type");
        const auto plan = pr::makePlan(p.get(), side.get(), pr::Kind::DdtField, "T_Moteur.Vitesse", "Regime", false);
        check(plan.ok(), "T_Moteur.Vitesse -> Regime : libre (" + plan.problem + ")");
        const auto line = [&](int n, const char* section) {
            return findChange(plan, [&](const pr::Change& c) { return c.line == n && pathHas(c, section); });
        };
        const auto* l1 = line(1, "Ligne \xE2\x80\xBA Calcul");
        const auto* l2 = line(2, "Ligne \xE2\x80\xBA Calcul");
        const auto* l3 = line(3, "Ligne \xE2\x80\xBA Calcul");
        const auto* l5 = line(5, "Ligne \xE2\x80\xBA Calcul");
        check(l1 && l1->after == "M1.Regime := 1;", "une globale de ce type");
        check(l2 && l2->after == "Moteurs[i].Regime := Moteurs[i + 1].Regime;", "les elements d'un tableau de ce type");
        check(l3 && l3->after == "L1.mot.Regime := Vitesse;", "un chemin imbrique (T_Ligne.mot) ; la globale Vitesse ne bouge pas");
        check(l5 && l5->after == "m.Regime := 2; (* M1.Vitesse *)", "le parametre d'une unite de ce type ; pas le commentaire");
        check(!line(4, "Ligne \xE2\x80\xBA Calcul") && !line(6, "Ligne \xE2\x80\xBA Calcul"), "P1.Etat (un autre type) et la chaine ne changent pas");
        const auto* x = line(1, "Divers");
        check(x && x->after == "x.Regime := 5;", "x inconnu : Vitesse n'est le membre que de T_Moteur, il suit (comme l'onglet Types)");
        check(findChange(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Tables && c.after == "Moteurs[2].Regime"; }) != nullptr
                  && findChange(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Tables && c.before == "Four.Vitesse"; }) == nullptr,
              "la ligne API de la table suit, pas la ligne IHM");
        check(findChange(plan, [](const pr::Change& c) { return c.before == "M1.Vitesse" && c.after == "M1.Regime" && pathHas(c, "Ligne"); }) != nullptr,
              "le parametre effectif M1.Vitesse de l'unite");
        check(findChange(plan, [](const pr::Change& c) {
                  return c.tab == pr::Tab::Hmi && c.after == "Moteurs[2].Regime > 0 AND Four.Vitesse > 0";
              }) != nullptr,
              "IHM : la visibilite (Four, une variable IHM, garde son Vitesse)");
        check(findChange(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Hmi && c.after == "M1 : {M1.Regime} tr/min"; }) != nullptr,
              "IHM : le texte a trous");
        check(findChange(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Hmi && c.after == "L1.mot.Regime" && pathHas(c, "Vitesse_Ligne"); }) != nullptr,
              "IHM : la variable liee par un chemin");
        const auto* cl = findChange(plan, [](const pr::Change& c) { return pathHas(c, "Cible") && c.line == 3; });
        check(cl && cl->after == "float v = M1.Regime + Moteurs[1].Regime;", "le script C : les acces au champ");
        check(findChange(plan, [](const pr::Change& c) { return pathHas(c, "Cible") && c.after.find("\"M1.Regime\"") != std::string::npos; }) != nullptr
                  && findChange(plan, [](const pr::Change& c) { return pathHas(c, "Cible") && (c.line == 1 || c.line == 2); }) == nullptr,
              "le script C : le chemin en chaine suit ; le commentaire et sa variable Vitesse non");
        check(findChange(plan, [](const pr::Change& c) { return pathHas(c, "Hors_vue") && c.label == "condition"; }) != nullptr,
              "IHM : la condition de l'alarme");
        // Faire, defaire : le programme et l'IHM.
        core::CommandStack stack;
        const auto bodyA = p->sections[0].body;
        const auto hmiBefore = doc->project;
        auto cmd = pr::makeCommand(p, side.get(), plan);
        check(cmd && static_cast<bool>(stack.push(std::move(cmd))), "la commande passe");
        check(p->strings.text(p->variables[plan.target.index].name) == "Regime" && p->sections[1].body.rfind("x.Regime := 5;", 0) == 0
                  && p->strings.text(p->animationTables[0].entries[0].name) == "Moteurs[2].Regime",
              "le champ, le code, la table");
        check(textOf(doc->project, "Vue_A", "Texte_M1", "visible", true) == "Moteurs[2].Regime > 0 AND Four.Vitesse > 0"
                  && doc->project.variable("Vitesse_Ligne")->address == "L1.mot.Regime",
              "l'IHM suit");
        check(static_cast<bool>(stack.undo()) && p->sections[0].body == bodyA && doc->project == hmiBefore
                  && p->strings.text(p->variables[plan.target.index].name) == "Vitesse",
              "Ctrl+Z : les deux cotes reviennent");

        // Etat : aussi un champ de T_Pompe. y.Etat (y inconnu) ne dit pas lequel.
        const auto etat = pr::makePlan(p.get(), side.get(), pr::Kind::DdtField, "T_Moteur.Etat", "Marche", false);
        check(etat.verdict == pr::Verdict::Ambiguous && etat.problem.find("Divers") != std::string::npos
                  && etat.problem.find("T_Pompe") != std::string::npos,
              "T_Moteur.Etat : y.Etat ne dit pas son type - refuse (" + etat.problem + ")");
        const auto* doubt = findChange(etat, [](const pr::Change& c) { return c.doubt; });
        check(doubt && doubt->info && doubt->line == 2 && doubt->before == "y.Etat := TRUE;", "l'acces a verifier est montre (non compte)");
        const auto* m1 = findChange(etat, [](const pr::Change& c) { return pathHas(c, "Divers") && c.line == 3; });
        check(m1 && m1->after == "M1.Marche := P1.Etat > 0;", "M1.Etat suit, P1.Etat (T_Pompe) non");
        check(!pr::makeCommand(p, side.get(), etat), "refuse : pas de commande");

        // Une variable IHM : le script C (identifiant entier, la chaine qui la nomme).
        const auto hv = pr::makePlan(p.get(), side.get(), pr::Kind::HmiVariable, "Marche_IHM", "EnMarche_IHM", false);
        const auto* ch = findChange(hv, [](const pr::Change& c) { return pathHas(c, "Cible") && c.line == 5; });
        check(hv.ok() && ch && ch->after == "if (EnMarche_IHM) { lire(\"EnMarche_IHM\"); x.Marche_IHM = 1; Marche_IHM(); }",
              "le script C : Marche_IHM et \"Marche_IHM\" suivent ; x.Marche_IHM et l'appel Marche_IHM() non");
        check(findChange(hv, [](const pr::Change& c) { return pathHas(c, "Cible") && c.line == 1; }) == nullptr, "pas le commentaire");

        // Une vue : les chaines comparees a SYS.CurrentView, IHM_VUE(), la navigation.
        const auto vue = pr::makePlan(p.get(), side.get(), pr::Kind::HmiView, "Vue_A", "Vue_Accueil", false);
        check(vue.ok(), "Vue_A -> Vue_Accueil : libre (" + vue.problem + ")");
        check(findChange(vue, [](const pr::Change& c) { return c.after == "SYS.CurrentView = 'Vue_Accueil' OR IHM_VUE() <> 'Vue_Accueil'"; }) != nullptr,
              "une animation : SYS.CurrentView = 'Vue_A', IHM_VUE() <> 'Vue_A'");
        check(findChange(vue, [](const pr::Change& c) { return c.after == "IF 'Vue_Accueil' = SYS.PreviousView THEN"; }) != nullptr
                  && findChange(vue, [](const pr::Change& c) { return c.after == "    IHM_POPUP('Vue_Accueil');"; }) != nullptr
                  && findChange(vue, [](const pr::Change& c) { return c.before.find("IHM_JOURNAL('Vue_A')") != std::string::npos; }) == nullptr,
              "un script : 'Vue_A' = SYS.PreviousView, IHM_POPUP('Vue_A') ; IHM_JOURNAL('Vue_A') reste un texte");
        check(findChange(vue, [](const pr::Change& c) { return pathHas(c, "Hors_vue") && c.after.rfind("SYS.CurrentView <> 'Vue_Accueil'", 0) == 0; }) != nullptr,
              "la condition d'une alarme");
        check(findChange(vue, [](const pr::Change& c) { return pathHas(c, "Cible") && c.after == "naviguer(\"Vue_Accueil\");"; }) != nullptr,
              "le script C : la chaine qui nomme la vue");
        check(vue.count(pr::Tab::Hmi) == 6, "comptees dans l'onglet IHM : le nom, l'animation, 2 lignes du script, l'alarme, le C ("
                                                + std::to_string(vue.count(pr::Tab::Hmi)) + ")");
    }

    std::printf("5. Lot API 8 : config_gaz.Nom_gaz sur le projet d'essai\n");
    if (argc < 2) {
        std::printf("       (sans MAST.XPG : rien d'essaye ici)\n");
    } else {
        core::EventBus bus;
        importer::ProjectImporter importer(bus);
        auto imported = importer.importFile(argv[1]);
        if (imported) {
            auto p = std::make_shared<domain::Project>(*imported->project);
            auto doc = std::make_shared<hmi::Document>();
            {
                auto& hp = doc->project;
                hmi::View v = hmi::makeView(hp, "Gaz");
                auto nom = hmi::makeObject(hmi::Kind::Text, hp.allocate(), "Nom", 10, 10, v.activeLayer);
                nom.set("text", "Gaz : {ConfigGazUtilisee.Nom_gaz}");
                v.objects.push_back(nom);
                hp.views.push_back(std::move(v));
                hmi::Script c;
                c.id = hp.allocate();
                c.name = "Etiquette";
                c.lang = hmi::ScriptLang::Cpp;
                c.event = "Appel";
                c.body = "std::string nom = ConfigsGaz[2].Nom_gaz; // Nom_gaz\n";
                hp.programs.scripts.push_back(c);
            }
            auto side = app::makeHmiRenameSide(doc);
            const auto plan = pr::makePlan(p.get(), side.get(), pr::Kind::DdtField, "config_gaz.Nom_gaz", "Nom_du_gaz", false);
            check(plan.ok(), "config_gaz.Nom_gaz -> Nom_du_gaz : libre (" + plan.problem + ")");
            const auto* decl = findChange(plan, [](const pr::Change& c) { return pathHas(c, "D\xC3\xA9" "claration"); });
            check(decl && decl->before == "Nom_gaz : string[8]" && decl->after == "Nom_du_gaz : string[8]", "sa declaration dans le type");
            const auto lines = countIf(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Api && c.line > 0; });
            check(lines == 7, "7 lignes de code : ConfigsGaz[i], ConfigsGazModifications[i], les DFB (" + std::to_string(lines) + ")");
            const auto* both = findChange(plan, [](const pr::Change& c) { return c.before.find("ConfigsGazModifications[i].nom_gaz = nom_gaz") != std::string::npos; });
            check(both && both->after.find("ConfigsGazModifications[i].Nom_du_gaz = nom_gaz") != std::string::npos,
                  "le champ suit, la globale nom_gaz du meme nom non");
            check(findChange(plan, [](const pr::Change& c) { return c.after.find("stockage_public.Nom_du_gaz") != std::string::npos; }) != nullptr
                      && findChange(plan, [](const pr::Change& c) { return c.after.find("STRING_TO_ASCII(config.Nom_du_gaz)") != std::string::npos; }) != nullptr,
                  "dans les DFB : une publique et une entree de ce type");
            check(findChange(plan, [](const pr::Change& c) { return c.tab == pr::Tab::Hmi && c.after == "Gaz : {ConfigGazUtilisee.Nom_du_gaz}"; }) != nullptr
                      && findChange(plan, [](const pr::Change& c) {
                             return pathHas(c, "Etiquette") && c.after == "std::string nom = ConfigsGaz[2].Nom_du_gaz; // Nom_gaz";
                         }) != nullptr,
                  "l'IHM : un texte a trous, un script C++ (pas son commentaire)");
            check(pr::makePlan(p.get(), nullptr, pr::Kind::DdtField, "config_gaz.Nom_gaz", "Unite", false).verdict == pr::Verdict::Taken,
                  "Unite : un autre champ de config_gaz");
            core::CommandStack stack;
            std::map<domain::Index, std::string> before;
            for (domain::Index s = 0; s < p->sections.size(); ++s) before[s] = p->sections[s].body;
            auto cmd = pr::makeCommand(p, side.get(), plan);
            check(cmd && static_cast<bool>(stack.push(std::move(cmd))), "la commande passe");
            std::size_t touched = 0;
            for (domain::Index s = 0; s < p->sections.size(); ++s) touched += p->sections[s].body != before[s] ? 1 : 0;
            std::set<std::string> planned;
            for (const auto& c : plan.changes)
                if (c.tab == pr::Tab::Api && c.line > 0) planned.insert(c.path.back());
            check(p->strings.text(p->variables[plan.target.index].name) == "Nom_du_gaz" && touched == 6 && planned.size() == 6,
                  "le champ s'appelle Nom_du_gaz ; 6 sections reecrites, celles du plan (" + std::to_string(touched) + ")");
            (void)stack.undo();
            bool same = true;
            for (domain::Index s = 0; s < p->sections.size(); ++s) same = same && p->sections[s].body == before[s];
            check(same && p->strings.text(p->variables[plan.target.index].name) == "Nom_gaz", "Ctrl+Z : le champ et le code reviennent");
            // Lot API 8 : un forcage enregistre de la simulation suit aussi (section 7).
            check(!plan.renames.empty() && pr::citedByForcings(plan.renames[0])
                      && pr::renameForcingLine("ConfigsGaz[2].Nom_gaz = 'Argon'", plan.renames[0]) == "ConfigsGaz[2].Nom_du_gaz = 'Argon'"
                      && pr::renameForcingLine("nom_gaz = 'Argon'", plan.renames[0]) == "nom_gaz = 'Argon'",
                  "un forcage enregistre : ConfigsGaz[2].Nom_gaz suit, la globale nom_gaz non");
        }
    }

    // ---- Lot API 8 : la case Nom d'une variable IHM (le nom tape -> le dialogue) ----
    std::printf("6. Lot API 8 : un nom tape dans la case Nom d'une variable IHM\n");
    {
        // La case Nom (HmiVariablesPane::commitCell -> renameInDialog) demande
        // requestRename("ihm-variable", ancien, tape). Le crochet de l'ecran
        // (RenameWorkspace.cpp) ouvre le dialogue, le nom tape deja ecrit ;
        // Confirmer refait le plan montre et passe UNE commande. Ici, le crochet
        // fait de meme : le plan, puis la commande sur la pile.
        auto p = fieldProject();
        auto doc = std::make_shared<hmi::Document>();
        {
            auto& hp = doc->project;
            for (const char* n : {"Pompe_Marche", "Pompe_Marche2"}) {
                hmi::Variable v;
                v.id = hp.allocate();
                v.name = n;
                v.type = "BOOL";
                hp.programs.variables.push_back(v);
            }
            hmi::View v = hmi::makeView(hp, "Synoptique");
            auto etat = hmi::makeObject(hmi::Kind::Text, hp.allocate(), "Etat", 10, 10, v.activeLayer);
            etat.set("text", "Pompe : {Pompe_Marche}");
            etat.setExpr("visible", "Pompe_Marche AND NOT Pompe_Marche2");
            etat.setExpr("blink", "NOT Pompe_Marche");
            v.objects.push_back(etat);
            auto bouton = hmi::makeObject(hmi::Kind::Button, hp.allocate(), "Bouton", 10, 60, v.activeLayer);
            hmi::Action toggle;
            toggle.operation = hmi::Operation::Toggle;
            toggle.target = "Pompe_Marche";
            bouton.actions.push_back(toggle);
            hmi::Action assign;
            assign.operation = hmi::Operation::Assign;
            assign.target = "Pompe_Marche2";
            assign.value = "Pompe_Marche";
            bouton.actions.push_back(assign);
            v.objects.push_back(bouton);
            hmi::Script s;
            s.id = hp.allocate();
            s.name = "Ouverture";
            s.event = "OnOpen";
            s.body = "IF Pompe_Marche THEN\n    Pompe_Marche2 := FALSE;\nEND_IF;\n";
            v.scripts.push_back(s);
            hp.views.push_back(std::move(v));
            hmi::Script c;
            c.id = hp.allocate();
            c.name = "Journal";
            c.lang = hmi::ScriptLang::C;
            c.event = "Appel";
            c.body = "if (Pompe_Marche) lire(\"Pompe_Marche\"); /* Pompe_Marche */\n";
            hp.programs.scripts.push_back(c);
        }
        auto side = app::makeHmiRenameSide(doc);
        core::CommandStack stack;
        std::string asked;
        pr::Plan shown;
        app::renameRequestHook() = [&](const std::string& kind, const std::string& name, const std::string& newName) {
            asked = kind + " " + name + " -> " + newName;
            const auto k = pr::kindFromKey(kind);
            if (!k) return false;
            shown = pr::makePlan(p.get(), side.get(), *k, name, newName, false);   // le dialogue, le nom tape deja ecrit
            if (!shown.ok()) return true;                                           // refuse : le dialogue le dit
            auto cmd = pr::makeCommand(p, side.get(), shown);                      // Confirmer
            if (cmd) (void)stack.push(std::move(cmd));
            return true;
        };
        const auto before = doc->project;
        const bool opened = app::requestRename("ihm-variable", "Pompe_Marche", "Pompe_En_Marche");
        check(opened && asked == "ihm-variable Pompe_Marche -> Pompe_En_Marche", "la case Nom : le dialogue s'ouvre, le nom tape deja ecrit (" + asked + ")");
        check(shown.ok() && shown.count(pr::Tab::Hmi) >= 7,
              "le plan montre l'IHM : " + std::to_string(shown.count(pr::Tab::Hmi)) + " changements (" + shown.problem + ")");
        const auto& hp = doc->project;
        check(hp.variable("Pompe_En_Marche") && !hp.variable("Pompe_Marche") && hp.variable("Pompe_Marche2"), "la variable s'appelle Pompe_En_Marche");
        check(textOf(hp, "Synoptique", "Etat", "visible", true) == "Pompe_En_Marche AND NOT Pompe_Marche2",
              "l'expression de visibilite de l'objet (Pompe_Marche2 ne bouge pas) : " + textOf(hp, "Synoptique", "Etat", "visible", true));
        check(textOf(hp, "Synoptique", "Etat", "blink", true) == "NOT Pompe_En_Marche", "l'expression du clignotement");
        check(textOf(hp, "Synoptique", "Etat", "text", false) == "Pompe : {Pompe_En_Marche}", "le texte a trous");
        const auto* vw = hp.viewByName("Synoptique");
        const auto* bt = vw ? vw->objectByName("Bouton") : nullptr;
        check(bt && bt->actions.size() == 2 && bt->actions[0].target == "Pompe_En_Marche" && bt->actions[1].target == "Pompe_Marche2"
                  && bt->actions[1].value == "Pompe_En_Marche",
              "les actions du bouton : la variable basculee, l'expression affectee");
        check(vw && !vw->scripts.empty() && vw->scripts[0].body == "IF Pompe_En_Marche THEN\n    Pompe_Marche2 := FALSE;\nEND_IF;\n",
              "le script ST de la vue");
        check(!hp.programs.scripts.empty() && hp.programs.scripts[0].body == "if (Pompe_En_Marche) lire(\"Pompe_En_Marche\"); /* Pompe_Marche */\n",
              "le script C : l'identifiant et la chaine qui la nomme, pas le commentaire");
        check(stack.canUndo() && static_cast<bool>(stack.undo()) && doc->project == before, "un seul Ctrl+Z : tout revient");
        // Sans programme ouvert (l'IHM seule) : le meme plan de l'IHM.
        const auto alone = pr::makePlan(nullptr, side.get(), pr::Kind::HmiVariable, "Pompe_Marche", "Pompe_En_Marche", false);
        check(alone.ok() && alone.count(pr::Tab::Hmi) == shown.count(pr::Tab::Hmi) && pr::makeCommand(nullptr, side.get(), alone),
              "sans programme ouvert : le meme plan, une commande");
        // Sans l'ecran d'analyse (les essais sans ecran) : faux - la case renomme sur place.
        app::renameRequestHook() = nullptr;
        check(!app::requestRename("ihm-variable", "Pompe_Marche", "Pompe_En_Marche"), "sans l'ecran : requestRename rend faux (sur place, comme avant)");
    }
    // ---- fin Lot API 8 : la case Nom d'une variable IHM ----

    // ---- Lot API 8 : les forcages enregistres de la simulation (nom = valeur) ----
    std::printf("7. Lot API 8 : les forcages enregistres de la simulation\n");
    {
        pr::Rename g;
        g.target.kind = pr::Kind::Variable;
        g.target.global = true;
        g.target.name = "Pompe";
        g.to = "Pompe_En_Marche";
        check(pr::renameForcingLine("Pompe = TRUE", g) == "Pompe_En_Marche = TRUE" && pr::renameForcingLine("  pompe.Etat = 1", g) == "  Pompe_En_Marche.Etat = 1"
                  && pr::renameForcingLine("Pompe[2]=3", g) == "Pompe_En_Marche[2]=3",
              "une globale : sa racine, sans casse (un membre, un element)");
        check(pr::renameForcingLine("Pompe2 = 3", g) == "Pompe2 = 3" && pr::renameForcingLine("# Pompe = 3", g) == "# Pompe = 3"
                  && pr::renameForcingLine("Autre = Pompe", g) == "Autre = Pompe" && pr::renameForcingLine("Ligne.Pompe = 1", g) == "Ligne.Pompe = 1",
              "pas un autre nom, un commentaire, la valeur, une variable d'unite du meme nom");
        pr::Rename l;
        l.target.kind = pr::Kind::Variable;
        l.target.name = "compteur";
        l.target.display = "Ligne.compteur";
        l.to = "cpt";
        check(pr::renameForcingLine("LIGNE.Compteur[1] = 3", l) == "LIGNE.cpt[1] = 3" && pr::renameForcingLine("compteur = 3", l) == "compteur = 3",
              "une variable d'unite : Unite.var (pas la globale du meme nom)");
        pr::Rename u;
        u.target.kind = pr::Kind::Unit;
        u.target.name = "Ligne";
        u.to = "Ligne_2";
        check(pr::renameForcingLine("Ligne.compteur = 3", u) == "Ligne_2.compteur = 3" && pr::renameForcingLine("Lignes.x = 1", u) == "Lignes.x = 1",
              "une unite : la racine Unite");
        pr::Rename v;
        v.target.kind = pr::Kind::HmiView;
        v.target.name = "Pompe";
        v.to = "X";
        check(pr::citedByForcings(g) && pr::citedByForcings(l) && pr::citedByForcings(u) && !pr::citedByForcings(v), "une vue : rien dans les forcages");
    }
    // ---- fin Lot API 8 : les forcages enregistres ----

    std::printf("%s\n", failures ? "ECHEC" : "OK");
    return failures ? 1 : 0;
}
