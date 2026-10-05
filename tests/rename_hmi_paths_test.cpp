// tests/rename_hmi_paths_test.cpp - lot API 8 : renommer dans l'IHM, par tous
// les chemins, et les references suivent (expressions des objets comprises).
//
//   rename_hmi_paths_test
//
//  Sans ecran. Tous les chemins de renommage d'une variable IHM, d'une vue et
//  d'une variable de l'automate passent par requestRename (le crochet que pose
//  l'ecran d'analyse, ici un faux qui fait ce que fait Confirmer : le plan, puis
//  sa commande). On verifie, apres UNE commande, que les expressions de TOUTES
//  les proprietes de TOUS les objets suivent : dans un groupe, sur un autre
//  calque, dans une popup, dans un ecran modele et dans une instance de
//  symbole ; par un membre (Var.Membre), un element (Tab[i].x), un texte a
//  trous ({Var}), une animation, une condition de visibilite, une action (cible,
//  valeur, garde), un titre de popup, une consigne d'alarme, le fichier d'un
//  bouton d'export, "mettre de cote" et "exporter" ; puis Ctrl+Z rend tout.
//  Renommer un objet (hmi::renameObjectReferences, ce qu'appelle l'editeur) :
//  Vue.Objet suit partout, Emprunteuse.Objet (ecran modele), les actions GIF,
//  les pas d'essai "cliquer".
#include "../src/app/RenameDialog.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRenameRefs.hpp"
#include "../src/project/RenamePlan.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace pr = project::rename;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// L'automate : Vitesse (INT), Moteur (lu par Moteur.Etat), Tab (lu par Tab[2].x).
std::shared_ptr<domain::Project> plcProject() {
    auto p = std::make_shared<domain::Project>();
    const auto sym = [&](const char* s) { return p->strings.intern(s); };
    for (const char* name : {"Vitesse", "Moteur", "Tab"}) {
        domain::Variable v;
        v.name = sym(name);
        v.type.name = sym("INT");
        v.type.klass = domain::TypeClass::Elementary;
        v.scope = domain::VariableScope::Global;
        p->variables.push_back(std::move(v));
    }
    p->buildIndices();
    return p;
}

hmi::Action action(hmi::Operation op, std::string target, std::string value = {}) {
    hmi::Action a;
    a.trigger = hmi::Trigger::Click;
    a.operation = op;
    a.target = std::move(target);
    a.value = std::move(value);
    return a;
}

// Une IHM ou le nom `n` (une variable) est cite partout.
std::shared_ptr<hmi::Document> hmiCiting(const std::string& n) {
    auto doc = std::make_shared<hmi::Document>();
    auto& p = doc->project;
    hmi::Variable var;
    var.id = p.allocate();
    var.name = "Pompe";
    var.type = "INT";
    p.programs.variables.push_back(var);

    hmi::View v = hmi::makeView(p, "Vue_A");
    hmi::Layer second;
    second.id = p.allocate();
    second.name = "Calque_2";
    v.layers.push_back(second);
    auto group = hmi::makeObject(hmi::Kind::Group, p.allocate(), "Groupe_1", 0, 0, v.activeLayer);
    v.objects.push_back(group);
    auto inGroup = hmi::makeObject(hmi::Kind::Text, p.allocate(), "Texte_G", 10, 10, v.activeLayer);
    inGroup.parent = group.id;
    inGroup.set("text", "Valeur : {" + n + ":0} / {" + n + ".Membre} / {Tab[2].x}");
    inGroup.setExpr("visible", n + " > 0");
    v.objects.push_back(inGroup);
    auto onLayer = hmi::makeObject(hmi::Kind::Rectangle, p.allocate(), "Rect_C2", 50, 50, second.id);
    onLayer.setExpr("fill", "IF " + n + ".Membre THEN 'red' ELSE 'green'");
    onLayer.setExpr("x", n + "[3].x * 2");
    v.objects.push_back(onLayer);
    auto button = hmi::makeObject(hmi::Kind::Button, p.allocate(), "Bouton_1", 10, 60, v.activeLayer);
    auto assign = action(hmi::Operation::Assign, n, n + " + 1");
    assign.guard = n + " < 10";
    button.actions.push_back(assign);
    button.actions.push_back(action(hmi::Operation::ShelveAlarm, "Alarme_1", n + "; essai {" + n + "}"));
    button.actions.push_back(action(hmi::Operation::Export, "alarmes", "export_{" + n + "}"));
    v.actions.push_back(action(hmi::Operation::SetLanguage, n));     // tranche 2 : la cible, une expression
    v.objects.push_back(button);
    auto exportButton = hmi::makeObject(hmi::Kind::ExportButton, p.allocate(), "Export_1", 10, 120, v.activeLayer);
    exportButton.set("fileName", "releve_{" + n + "}");
    v.objects.push_back(exportButton);
    p.views.push_back(std::move(v));

    hmi::View popup = hmi::makeView(p, "Popup_1");
    popup.role = "popup";
    popup.popup.title = "Popup de {" + n + "}";
    auto inPopup = hmi::makeObject(hmi::Kind::Text, p.allocate(), "Texte_P", 10, 10, popup.activeLayer);
    inPopup.setExpr("text", n + ".Membre");
    popup.objects.push_back(inPopup);
    p.views.push_back(std::move(popup));

    hmi::View model = hmi::makeView(p, "Modele_1");
    model.role = "modele";
    auto inModel = hmi::makeObject(hmi::Kind::Text, p.allocate(), "Texte_M", 10, 10, model.activeLayer);
    inModel.setExpr("visible", "NOT " + n);
    model.objects.push_back(inModel);
    p.views.push_back(std::move(model));

    hmi::AlarmDef al;
    al.id = p.allocate();
    al.name = "Alarme_1";
    al.condition = n + " > 5";
    al.message = "Trop : {" + n + "}";
    al.instruction = "Regarde {" + n + "} avant d'acquitter";
    p.alarms.push_back(al);
    return doc;
}

const hmi::Object* objectOf(const hmi::Project& p, const char* view, const char* object) {
    const auto* v = p.viewByName(view);
    return v ? v->objectByName(object) : nullptr;
}

// Tout ce qui citait `from` cite `to` (et `from` n'est plus cite).
void checkAllFollow(const hmi::Project& p, const std::string& from, const std::string& to, const std::string& what) {
    const auto* g = objectOf(p, "Vue_A", "Texte_G");
    check(g && g->text("text") == "Valeur : {" + to + ":0} / {" + to + ".Membre} / {Tab[2].x}", what + " : texte a trous dans un groupe");
    check(g && g->expr("visible") == to + " > 0", what + " : condition de visibilite");
    const auto* r = objectOf(p, "Vue_A", "Rect_C2");
    check(r && r->expr("fill") == "IF " + to + ".Membre THEN 'red' ELSE 'green'", what + " : animation (Var.Membre) sur un autre calque");
    check(r && r->expr("x") == to + "[3].x * 2", what + " : un element (Var[3].x)");
    const auto* b = objectOf(p, "Vue_A", "Bouton_1");
    check(b && b->actions.size() == 3 && b->actions[0].target == to && b->actions[0].value == to + " + 1" && b->actions[0].guard == to + " < 10",
          what + " : action Affecter (cible, valeur, garde)");
    check(b && b->actions.size() == 3 && b->actions[1].value == to + "; essai {" + to + "}", what + " : mettre de cote (duree ; raison)");
    check(b && b->actions.size() == 3 && b->actions[2].value == "export_{" + to + "}", what + " : exporter (le fichier)");
    const auto* va = p.viewByName("Vue_A");
    check(va && !va->actions.empty() && va->actions.back().target == to, what + " : changer de langue (la cible)");
    const auto* e = objectOf(p, "Vue_A", "Export_1");
    check(e && e->text("fileName") == "releve_{" + to + "}", what + " : le fichier d'un bouton d'export");
    const auto* pop = p.viewByName("Popup_1");
    check(pop && pop->popup.title == "Popup de {" + to + "}", what + " : le titre d'une popup");
    const auto* tp = objectOf(p, "Popup_1", "Texte_P");
    check(tp && tp->expr("text") == to + ".Membre", what + " : un objet de popup");
    const auto* tm = objectOf(p, "Modele_1", "Texte_M");
    check(tm && tm->expr("visible") == "NOT " + to, what + " : un objet d'ecran modele");
    check(!p.alarms.empty() && p.alarms[0].condition == to + " > 5" && p.alarms[0].message == "Trop : {" + to + "}"
              && p.alarms[0].instruction == "Regarde {" + to + "} avant d'acquitter",
          what + " : l'alarme (condition, message, consigne)");
    (void)from;
}

// Le faux ecran d'analyse : requestRename fait le plan et le confirme.
struct FakeScreen {
    std::shared_ptr<domain::Project> plc;
    std::shared_ptr<hmi::Document> doc;
    std::unique_ptr<pr::HmiSide> side;
    core::CommandStack stack;
    int opened = 0;
    std::string lastProblem;
    FakeScreen(std::shared_ptr<domain::Project> p, std::shared_ptr<hmi::Document> d)
        : plc(std::move(p)), doc(std::move(d)), side(app::makeHmiRenameSide(doc)) {
        app::renameRequestHook() = [this](const std::string& kind, const std::string& name, const std::string& newName) {
            ++opened;
            const auto k = pr::kindFromKey(kind);
            if (!k) return false;
            const auto plan = pr::makePlan(plc.get(), side.get(), *k, name, newName, false);
            lastProblem = plan.problem;
            if (!plan.ok()) return true;              // le dialogue reste ouvert, Confirmer grise
            auto cmd = pr::makeCommand(plc, side.get(), plan);
            if (cmd) (void)stack.push(std::move(cmd));
            return true;
        };
    }
    ~FakeScreen() { app::renameRequestHook() = nullptr; }
};

} // namespace

int main() {
    std::printf("1. Une variable IHM renommee (tous les chemins : requestRename(\"ihm-variable\"))\n");
    {
        FakeScreen screen(plcProject(), hmiCiting("Pompe"));
        const hmi::Project before = screen.doc->project;
        // La case Nom (le nom tape), F2 / double-clic / Renommer / l'arbre (le
        // dialogue, puis le nom tape dedans) : le meme appel.
        check(app::requestRename("ihm-variable", "Pompe", "Pompe_2") && screen.opened == 1, "le dialogue s'ouvre (" + screen.lastProblem + ")");
        check(screen.doc->project.variable("Pompe_2") != nullptr && screen.doc->project.variable("Pompe") == nullptr, "la variable est renommee");
        checkAllFollow(screen.doc->project, "Pompe", "Pompe_2", "variable IHM");
        check(static_cast<bool>(screen.stack.undo()) && screen.doc->project == before, "un seul Ctrl+Z rend tout");
    }

    std::printf("2. Une variable de l'automate (globale) : requestRename(\"variable\")\n");
    {
        auto doc = hmiCiting("Moteur");
        // Pas de variable IHM du nom de la globale : l'IHM lit l'automate.
        FakeScreen screen(plcProject(), doc);
        const hmi::Project before = screen.doc->project;
        check(app::requestRename("variable", "Moteur", "Moteur_1") && screen.opened == 1, "le dialogue s'ouvre (" + screen.lastProblem + ")");
        checkAllFollow(screen.doc->project, "Moteur", "Moteur_1", "globale");
        check(static_cast<bool>(screen.stack.undo()) && screen.doc->project == before, "un seul Ctrl+Z rend l'IHM");
    }

    std::printf("3. Une vue renommee : Vue.Objet suit partout\n");
    {
        auto doc = hmiCiting("Vue_A.Texte_G.Visible");
        FakeScreen screen(plcProject(), doc);
        check(app::requestRename("ihm-vue", "Vue_A", "Vue_Accueil") && screen.opened == 1, "le dialogue s'ouvre (" + screen.lastProblem + ")");
        const auto& p = screen.doc->project;
        const auto* tm = objectOf(p, "Modele_1", "Texte_M");
        check(p.viewByName("Vue_Accueil") != nullptr && tm && tm->expr("visible") == "NOT Vue_Accueil.Texte_G.Visible",
              "un objet d'ecran modele lit Vue_Accueil.Texte_G.Visible");
        const auto* pop = p.viewByName("Popup_1");
        check(pop && pop->popup.title == "Popup de {Vue_Accueil.Texte_G.Visible}", "le titre de la popup");
        check(!p.alarms.empty() && p.alarms[0].instruction == "Regarde {Vue_Accueil.Texte_G.Visible} avant d'acquitter", "la consigne de l'alarme");
    }

    std::printf("4. Un objet renomme (l'editeur : liste des objets, case Nom) : Vue.Objet suit\n");
    {
        auto doc = hmiCiting("Vue_A.Rect_C2.Visible");
        auto& p = doc->project;
        // Un objet de l'ecran modele, lu par la vue qui l'emprunte ; une action GIF.
        auto* model = p.viewByName("Modele_1");
        auto* view = p.viewByName("Vue_A");
        view->templateView = model->id;
        auto* b = view->objectByName("Bouton_1");
        b->actions.push_back(action(hmi::Operation::GifPlay, "Rect_C2"));
        b->setExpr("visible", "Vue_A.Texte_M.Visible AND Modele_1.Texte_M.Visible");
        const std::size_t n = hmi::renameObjectReferences(p, "Vue_A", "Rect_C2", "Cadre");
        const auto* g = objectOf(p, "Vue_A", "Texte_G");
        check(g && g->expr("visible") == "Vue_A.Cadre.Visible > 0", "une condition de visibilite");
        b = p.viewByName("Vue_A")->objectByName("Bouton_1");
        check(b && b->actions.back().target == "Cadre", "l'action GIF vise le nouveau nom");
        const auto* pop = p.viewByName("Popup_1");
        check(pop && pop->popup.title == "Popup de {Vue_A.Cadre.Visible}", "le titre de la popup");
        check(!p.alarms.empty() && p.alarms[0].condition == "Vue_A.Cadre.Visible > 5", "la condition d'alarme");
        check(n >= 6, "des endroits ont suivi (" + std::to_string(n) + ")");
        // Tranche 2 : un modele sur un modele (deux niveaux) - Vue_C lit Texte_M de Modele_1.
        {
            const hmi::Id m1 = p.viewByName("Modele_1")->id;
            hmi::View m2 = hmi::makeView(p, "Modele_2");
            m2.role = "modele";
            m2.templateView = m1;
            const hmi::Id m2id = m2.id;
            p.views.push_back(std::move(m2));
            hmi::View vc = hmi::makeView(p, "Vue_C");
            vc.templateView = m2id;
            auto bc = hmi::makeObject(hmi::Kind::Button, p.allocate(), "Bouton_C", 10, 10, vc.activeLayer);
            bc.setExpr("visible", "Vue_C.Texte_M.Visible");
            vc.objects.push_back(bc);
            p.views.push_back(std::move(vc));
        }
        const std::size_t m = hmi::renameObjectReferences(p, "Modele_1", "Texte_M", "Texte_Modele");
        const auto* bc = objectOf(p, "Vue_C", "Bouton_C");
        check(bc && bc->expr("visible") == "Vue_C.Texte_Modele.Visible", "un modele emprunte a deux niveaux : Vue_C.Objet suit");
        b = p.viewByName("Vue_A")->objectByName("Bouton_1");
        check(b && m >= 1 && p.viewByName("Vue_A")->objectByName("Bouton_1")->expr("visible")
                                  == "Vue_A.Texte_Modele.Visible AND Modele_1.Texte_Modele.Visible",
              "un objet d'ecran modele : Modele.Objet et Emprunteuse.Objet suivent");
    }

    // ---- Lot API 8 : finitions (les chaines comparees, les scripts C / C++) ----
    std::printf("5. Les chaines qui designent une alarme ou un objet, les scripts C / C++\n");
    {
        const std::vector<std::string> alarmVars{"AlarmSelected", "AlarmLastName"};
        check(hmi::renameComparedStrings("SYS.AlarmSelected = 'Alarme_1' OR 'Alarme_1' <> sys.alarmlastname", alarmVars, "Alarme_1", "Alarme_Gaz")
                  == "SYS.AlarmSelected = 'Alarme_Gaz' OR 'Alarme_Gaz' <> sys.alarmlastname",
              "une chaine comparee a SYS.AlarmSelected / AlarmLastName suit (des deux cotes, sans la casse)");
        const std::string other = "IHM_JOURNAL('Alarme_1'); x := 'Alarme_1' = Nom; y := SYS.AlarmSelected.Long = 'Alarme_1';"
                                  " (* SYS.AlarmSelected = 'Alarme_1' *) z := SYS.AlarmSelected = 'Alarme_10';";
        check(hmi::renameComparedStrings(other, alarmVars, "Alarme_1", "Alarme_Gaz") == other,
              "ailleurs une chaine reste un texte (journal, autre comparaison, membre, commentaire, autre nom)");
        const auto cf = [](std::string_view s) { return s == "Vue_A.Rect" ? std::string("Vue_A.Cadre") : std::string(s); };
        check(hmi::rewriteCStrings("p = \"Vue_A.Rect\"; /* \"Vue_A.Rect\" */ // \"Vue_A.Rect\"\n#define X \"Vue_A.Rect\"\nc = '\"'; q = \"a\\\"Vue_A.Rect\";", cf)
                  == "p = \"Vue_A.Cadre\"; /* \"Vue_A.Rect\" */ // \"Vue_A.Rect\"\n#define X \"Vue_A.Rect\"\nc = '\"'; q = \"a\\\"Vue_A.Rect\";",
              "C : les chaines seules (ni commentaires, ni preprocesseur, ni caractere '\"', ni une autre chaine)");

        // Un objet renomme : 'Vue_A.Rect_C2' comparee a SYS.FocusedObject / PressedObject, "Vue_A.Rect_C2" en C / C++.
        auto doc = hmiCiting("Pompe");
        auto& p = doc->project;
        auto* view = p.viewByName("Vue_A");
        view->objectByName("Bouton_1")->setExpr("visible", "SYS.FocusedObject <> 'Vue_A.Rect_C2'");
        hmi::Script st;
        st.id = p.allocate();
        st.name = "Sur_Clic";
        st.event = "OnCycle";
        st.body = "IF SYS.PressedObject = 'Vue_A.Rect_C2' THEN IHM_JOURNAL('Vue_A.Rect_C2'); END_IF;";
        view->scripts.push_back(st);
        hmi::Script c = st;
        c.id = p.allocate();
        c.name = "Cible_C";
        c.lang = hmi::ScriptLang::C;
        c.body = "const char* f = \"Vue_A.Rect_C2\"; /* \"Vue_A.Rect_C2\" */ const char* v = \"Vue_A.Rect_C2.Visible\"; const char* w = \"Vue_A.Rect_C20\";";
        view->scripts.push_back(c);
        hmi::Script cpp;
        cpp.id = p.allocate();
        cpp.name = "General_Cpp";
        cpp.lang = hmi::ScriptLang::Cpp;
        cpp.event = "Cyclique";
        cpp.body = "std::string s = \"Vue_A.Rect_C2\";";
        p.programs.scripts.push_back(cpp);
        hmi::Script general = st;
        general.id = p.allocate();
        general.name = "General_ST";
        general.event = "Cyclique";
        general.body = "x := 'Vue_A.Rect_C2' = SYS.FocusedObject;";
        p.programs.scripts.push_back(general);
        const std::size_t n = hmi::renameObjectReferences(p, "Vue_A", "Rect_C2", "Cadre");
        view = p.viewByName("Vue_A");
        check(view->objectByName("Bouton_1")->expr("visible") == "SYS.FocusedObject <> 'Vue_A.Cadre'",
              "objet : 'Vue_A.Objet' compare a SYS.FocusedObject suit (une expression)");
        check(view->scripts.size() == 2 && view->scripts[0].body == "IF SYS.PressedObject = 'Vue_A.Cadre' THEN IHM_JOURNAL('Vue_A.Rect_C2'); END_IF;",
              "objet : compare a SYS.PressedObject dans un script ST de la vue ; le texte du journal reste");
        check(view->scripts.size() == 2
                  && view->scripts[1].body == "const char* f = \"Vue_A.Cadre\"; /* \"Vue_A.Rect_C2\" */ const char* v = \"Vue_A.Cadre.Visible\"; "
                                             "const char* w = \"Vue_A.Rect_C20\";",
              "objet : un script C de la vue - \"Vue.Objet\" et un chemin qui en part suivent (pas le commentaire, pas un autre nom)");
        check(p.programs.scripts.size() == 2 && p.programs.scripts[0].body == "std::string s = \"Vue_A.Cadre\";", "objet : un script C++ general");
        check(p.programs.scripts.size() == 2 && p.programs.scripts[1].body == "x := 'Vue_A.Cadre' = SYS.FocusedObject;", "objet : un script ST general");
        check(n == 5, "les endroits comptes : cinq (" + std::to_string(n) + ")");

        // Une alarme renommee : ce qui la compare a SYS.AlarmSelected / AlarmLastName suit, le reste non.
        auto doc2 = hmiCiting("Pompe");
        auto& q = doc2->project;
        auto* va = q.viewByName("Vue_A");
        va->objectByName("Texte_G")->setExpr("visible", "SYS.AlarmSelected = 'Alarme_1'");
        va->objectByName("Bouton_1")->actions[0].guard = "SYS.AlarmLastName <> 'Alarme_1'";
        va->objectByName("Texte_G")->set("text", "Valeur : {SEL(SYS.AlarmSelected = 'Alarme_1', 'autre', 'celle-ci')}");
        hmi::Script ga = st;
        ga.id = q.allocate();
        ga.name = "Sur_Alarme";
        ga.event = "Cyclique";
        ga.body = "IF SYS.AlarmSelected = 'Alarme_1' THEN IHM_JOURNAL('Alarme_1 choisie'); END_IF;";
        q.programs.scripts.push_back(ga);
        q.alarms[0].condition = "Pompe > 5 AND SYS.AlarmLastName <> 'Alarme_1'";
        // Finitions (suite) : le premier argument de IHM_METTRE_DE_COTE / IHM_REMETTRE designe l'alarme.
        hmi::Script shelve = ga;
        shelve.id = q.allocate();
        shelve.name = "Mise_De_Cote";
        shelve.body = "n := IHM_METTRE_DE_COTE('Alarme_1', 60, 'Alarme_1 en essai'); m := ihm_remettre('Alarme_1'); "
                      "IHM_JOURNAL('Alarme_1'); k := IHM_METTRE_DE_COTE('Alarme_10');";
        q.programs.scripts.push_back(shelve);
        std::vector<std::string> where;
        const std::size_t m = hmi::renameAlarmReferences(q, "Alarme_1", "Alarme_Gaz", &where);
        va = q.viewByName("Vue_A");
        check(va->objectByName("Texte_G")->expr("visible") == "SYS.AlarmSelected = 'Alarme_Gaz'", "alarme : une expression d'objet");
        check(va->objectByName("Bouton_1")->actions[0].guard == "SYS.AlarmLastName <> 'Alarme_Gaz'", "alarme : la garde d'une action");
        check(va->objectByName("Texte_G")->text("text") == "Valeur : {SEL(SYS.AlarmSelected = 'Alarme_Gaz', 'autre', 'celle-ci')}",
              "alarme : un trou d'un texte a trous");
        check(q.programs.scripts.size() >= 2
                  && q.programs.scripts[q.programs.scripts.size() - 2].body
                         == "IF SYS.AlarmSelected = 'Alarme_Gaz' THEN IHM_JOURNAL('Alarme_1 choisie'); END_IF;",
              "alarme : un script ST general (le journal reste un texte)");
        check(q.programs.scripts.back().body
                  == "n := IHM_METTRE_DE_COTE('Alarme_Gaz', 60, 'Alarme_1 en essai'); m := ihm_remettre('Alarme_Gaz'); "
                     "IHM_JOURNAL('Alarme_1'); k := IHM_METTRE_DE_COTE('Alarme_10');",
              "alarme : IHM_METTRE_DE_COTE / IHM_REMETTRE('Alarme_1') suivent (pas la raison, le journal, une autre alarme)");
        check(q.alarms[0].condition == "Pompe > 5 AND SYS.AlarmLastName <> 'Alarme_Gaz'", "alarme : la condition d'une alarme");
        check(va->objectByName("Bouton_1")->actions[1].target == "Alarme_1", "alarme : la cible d'une action (le nom de l'alarme) : l'affaire du volet");
        check(m == 5 && where.size() == 5, "alarme : cinq endroits (" + std::to_string(m) + ")");
        check(hmi::renameAlarmReferences(q, "Alarme_1", "Alarme_Gaz") == 0, "une seconde fois : plus rien");

        // Finitions (suite) : un GIF anime renomme - IHM_GIF_JOUER('Ventilateur') dans un script ST de
        // sa vue suit (comme ses actions) ; un script general (le GIF d'une autre vue ouverte ?) reste,
        // et l'argument d'un objet qui n'est pas un GIF aussi.
        auto doc3 = hmiCiting("Pompe");
        auto& g = doc3->project;
        auto* vg = g.viewByName("Vue_A");
        vg->objects.push_back(hmi::makeObject(hmi::Kind::AnimatedGif, g.allocate(), "Ventilateur", 200, 200, vg->activeLayer));
        hmi::Script play = st;
        play.id = g.allocate();
        play.name = "Jouer";
        play.body = "IHM_GIF_JOUER('Ventilateur'); IHM_GIF_REJOUER('Ventilateur', 2); IHM_JOURNAL('Ventilateur'); IHM_GIF_PAUSE('Rect_C2');";
        vg->scripts.push_back(play);
        hmi::Script elsewhere = play;
        elsewhere.id = g.allocate();
        elsewhere.name = "General_GIF";
        elsewhere.event = "Cyclique";
        elsewhere.body = "IHM_GIF_ARRETER('Ventilateur');";
        g.programs.scripts.push_back(elsewhere);
        (void)hmi::renameObjectReferences(g, "Vue_A", "Ventilateur", "Helice");
        vg = g.viewByName("Vue_A");
        check(!vg->scripts.empty()
                  && vg->scripts.back().body
                         == "IHM_GIF_JOUER('Helice'); IHM_GIF_REJOUER('Helice', 2); IHM_JOURNAL('Ventilateur'); IHM_GIF_PAUSE('Rect_C2');",
              "GIF : IHM_GIF_JOUER / REJOUER('Objet') dans un script de sa vue suivent (pas le journal, pas un autre objet)");
        check(g.programs.scripts.back().body == "IHM_GIF_ARRETER('Ventilateur');", "GIF : un script general reste (un GIF d'une autre vue ?)");
        const std::string notGif = vg->scripts.back().body;
        (void)hmi::renameObjectReferences(g, "Vue_A", "Rect_C2", "Cadre");
        check(g.viewByName("Vue_A")->scripts.back().body == notGif, "un objet qui n'est pas un GIF : IHM_GIF_PAUSE('Rect_C2') reste");
    }
    // ---- fin Lot API 8 : finitions ----

    std::printf("%s (%d echec(s))\n", failures ? "ECHEC" : "OK", failures);
    return failures ? 1 : 0;
}
