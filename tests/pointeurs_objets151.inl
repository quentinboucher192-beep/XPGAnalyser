// =============================================================================
//  tests/pointeurs_objets151.inl - 1.51.0 : UN POINTEUR VERS UN OBJET DE LA VUE
//  (inclus par pointeurs_test.cpp)
// -----------------------------------------------------------------------------
//  Le cas de l'utilisateur : une vue Vue_Accueil, un objet Carte_1 ; un script de
//  cycle << Cycle >> (OnCycle, apres l'etape 4, ST), son onglet Variables tient
//  ObjPtr : POINTER TO Object ; son corps : ObjPtr := Vue_Accueil.Carte_1;
//  << pouvoir y acceder comme ca devrait fonctionner (le pointeur), ou alors mettre
//  une methode dans les objets Get() qui renvoie un pointeur >>. Et << afficher
//  l'onglet des cycles que si je suis dans le onCycle sinon ca prend de l'espace >>.
//
//  - la verification en direct, Compiler (F7), Generer (la validation) et le moteur
//    acceptent Vue.Objet (un nom court de la vue, l'objet d'une popup) dans une
//    variable Object, REF_TO Object, POINTER TO Object ; Get() et ADR(Vue.Objet) ;
//    NULL, p^.X, p^.Visible := FALSE, Hide(p^), un pointeur passe a une fonction ;
//  - la ou une valeur simple est attendue, Vue.Objet seul reste une erreur
//    (<< chemin incomplet >>) ;
//  - la barre sous l'editeur suit une declaration ajoutee dans l'onglet Variables ;
//  - la section Execution (les etapes du cycle) n'est montree que pour un script de
//    cycle : OnOpen, OnClose, rien de choisi - cachee, sa largeur va a l'editeur.
// =============================================================================
#include "../src/app/hmi/HmiAssist.hpp"
#include "../src/app/hmi/HmiScriptPanes.hpp"
#include "../src/domain/ProjectModel.hpp"
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiNatives.hpp"

#include <algorithm>
#include <cmath>

namespace {

namespace obj151 {

Declaration variable(Project& p, const std::string& name, const std::string& type) {
    Declaration d;
    d.id = p.allocate();
    d.kind = DeclKind::Variable;
    d.name = name;
    d.type = type;
    return d;
}

// Le projet de l'utilisateur : Vue_Accueil (Carte_1, Carte_2 ; un OnOpen), une popup Pop_Info (Bouton_OK), des variables R
// (DINT), S (STRING), B (BOOL) ; le script de cycle Cycle de Vue_Accueil (apres l'etape 4), ses declarations, son corps.
struct Case {
    std::shared_ptr<Document> doc = std::make_shared<Document>();
    Id view{kNoId}, script{kNoId}, open{kNoId}, carte{kNoId};
    Case(const std::string& body, const std::vector<std::pair<std::string, std::string>>& decls) {
        Project& p = doc->project;
        addVar(p, "R", "DINT");
        addVar(p, "S", "STRING");
        addVar(p, "B", "BOOL");
        View v = makeView(p, "Vue_Accueil");
        const Id layer = v.layers.empty() ? kNoId : v.layers.front().id;
        for (const char* n : {"Carte_1", "Carte_2"}) {
            Object o = makeObject(Kind::Rectangle, p.allocate(), n, n[6] == '1' ? 10 : 200, 20, layer);
            o.setNumber("w", n[6] == '1' ? 50 : 70);
            o.setNumber("h", 30);
            v.objects.push_back(std::move(o));
        }
        carte = v.objects.front().id;
        Script o;
        o.id = p.allocate();
        o.name = "Vue_Accueil_OnOpen";
        o.event = "OnOpen";
        o.body = "R := 0;\n";
        open = o.id;
        v.scripts.push_back(o);
        Script s;
        s.id = p.allocate();
        s.name = "Cycle";
        s.event = "OnCycle";
        s.stage = 4;
        s.body = body;
        for (const auto& [n, t] : decls) s.decls.push_back(variable(p, n, t));
        script = s.id;
        v.scripts.push_back(s);
        p.views.push_back(v);
        view = p.views.back().id;
        View pop = makeView(p, "Pop_Info");
        pop.role = "popup";
        Object ok = makeObject(Kind::Rectangle, p.allocate(), "Bouton_OK", 0, 0, pop.layers.empty() ? kNoId : pop.layers.front().id);
        pop.objects.push_back(std::move(ok));
        p.views.push_back(pop);
        p.config.startView = view;
    }
    [[nodiscard]] const Script& sc() const { return *doc->project.script(script); }
    [[nodiscard]] const View& v() const { return *doc->project.view(view); }
    // Le moteur : la vue s'ouvre, un cycle tourne (le script de cycle avec lui) ; la valeur d'un chemin.
    std::string runCycle(const std::string& readPath) {
        Runtime rt;
        rt.bind(&doc->project, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
        rt.tick(0.2);
        sim::Value out;
        return rt.environment().read(readPath, out) ? shown(out) : std::string("#");
    }
};

std::string errorsOf(const std::vector<ScriptDiagnostic>& d) {
    std::string out;
    for (const auto& x : d)
        if (x.severity == ScriptDiagnostic::Severity::Error) out += " | l" + std::to_string(x.line) + " " + x.message;
    return out;
}
std::string errorsOf(const std::vector<Issue>& d, Id script) {
    std::string out;
    for (const auto& x : d)
        if (x.severity == Issue::Severity::Error && (x.script == script || x.message.find("Carte") != std::string::npos))
            out += " | " + x.category + " l" + std::to_string(x.line) + " " + x.message;
    return out;
}

// La verification en direct (celle de l'editeur), comme IHM Studio : un automate vide (un nom inconnu se dit).
std::string live(const Case& c) {
    const auto plc = std::make_shared<domain::Project>();
    return errorsOf(app::placedScriptDiagnostics(c.doc->project, c.sc().body, &c.v(), nullptr, plc.get(), nullptr, &c.sc().decls));
}
std::string compiled(const Case& c) { return errorsOf(compileWith(c.doc->project, [](std::string_view) { return false; }), c.script); }
std::string generated(const Case& c) { return errorsOf(generate(c.doc->project, [](std::string_view) { return false; }), c.script); }
// Les trois a la fois : rien a dire.
void accepted(const std::string& body, const std::vector<std::pair<std::string, std::string>>& decls, const std::string& what) {
    Case c(body, decls);
    const std::string l = live(c), k = compiled(c), g = generated(c);
    check(l.empty() && k.empty() && g.empty(), what + " : ni la verification en direct, ni Compiler, ni Generer" + l + k + g);
}
// ... ou une erreur qui dit `said`, partout.
void refused(const std::string& body, const std::vector<std::pair<std::string, std::string>>& decls, const std::string& said,
             const std::string& what) {
    Case c(body, decls);
    const std::string l = live(c), k = compiled(c), g = generated(c);
    check(l.find(said) != std::string::npos, what + " : la verification en direct le dit (" + l + ")");
    check(k.find(said) != std::string::npos, what + " : Compiler le dit (" + k + ")");
    check(g.find(said) != std::string::npos, what + " : Generer le dit (" + g + ")");
}

// Un script general appele (le moteur, ses erreurs) : la vue Vue_Accueil montree.
std::string called(const std::string& body, std::string* err, const std::string& readPath) {
    Case c("R := 0;\n", {});
    Script s;
    s.id = c.doc->project.allocate();
    s.name = "Essai";
    s.event = "Appel";
    s.body = body;
    c.doc->project.programs.scripts.push_back(s);
    Runtime rt;
    rt.bind(&c.doc->project, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    std::string e;
    const bool ok = rt.callScript("Essai", 0.2, &e);
    if (err) *err = ok ? std::string{} : e;
    sim::Value out;
    return rt.environment().read(readPath, out) ? shown(out) : std::string("#");
}

} // namespace obj151

// ---- 1 : le cas de l'utilisateur, et Object / REF_TO Object / POINTER TO Object ------------------------------------------
void objetsCasUtilisateur151() {
    using namespace obj151;
    std::printf("-- 1.51.0 : ObjPtr := Vue_Accueil.Carte_1 (le cas de l'utilisateur)\n");
    {
        Case c("(* Cycle *)\nObjPtr := Vue_Accueil.Carte_1;\n", {{"ObjPtr", "POINTER TO Object"}});
        const std::string l = live(c), k = compiled(c), g = generated(c);
        check(l.empty(), "le cas de l'utilisateur : la verification en direct ne dit rien" + l);
        check(k.empty(), "le cas de l'utilisateur : Compiler ne dit rien" + k);
        check(g.empty(), "le cas de l'utilisateur : Generer (la validation, << chemin incomplet >> avant) ne dit rien" + g);
        // F7 dans le volet de la vue : le script seul
        core::CommandStack stack;
        app::HmiScriptsPane pane("obj151.f7", c.doc, [&](core::CommandPtr cmd) { (void)stack.push(std::move(cmd)); }, c.view);
        pane.selectScript(c.script);
        check(pane.compileHere() == 0 && errorsOf(pane.diagnostics()).empty(), "le cas de l'utilisateur : F7 (le script actuel) ne dit rien"
                                                                                   + errorsOf(pane.diagnostics()));
    }
    // le moteur : ObjPtr^.Visible := FALSE cache l'objet ; ObjPtr^.X se lit
    {
        Case c("(* Cycle *)\nObjPtr := Vue_Accueil.Carte_1;\nObjPtr^.Visible := FALSE;\nR := TO_DINT(ObjPtr^.X) * 100 + TO_DINT(ObjPtr^.Width);\n",
               {{"ObjPtr", "POINTER TO Object"}});
        check(c.runCycle("Vue_Accueil.Carte_1.Visible") == "FALSE", "le moteur : ObjPtr^.Visible := FALSE cache Carte_1 ("
                                                                       + c.runCycle("Vue_Accueil.Carte_1.Visible") + ")");
        check(c.runCycle("R") == "1050", "le moteur : ObjPtr^.X et ObjPtr^.Width se lisent (R = " + c.runCycle("R") + ")");
        check(c.runCycle("Vue_Accueil.Carte_2.Visible") == "TRUE", "le moteur : Carte_2 reste montree");
    }
    // le script de la capture (151_P3, 151_P4) : sans erreur, et le moteur cache Carte_1 une fois, le dit a la Console
    {
        Case c("(* Cycle *)\nObjPtr := Vue_Accueil.Carte_1;\nIF ObjPtr <> NULL AND ObjPtr^.Visible THEN\n"
               "    ObjPtr^.Visible := FALSE;            (* Carte_1 se cache, par le pointeur *)\n"
               "    IHM_LOG(INFO, 'Carte_1 cach\xC3\xA9" "e par ObjPtr^.Visible := FALSE');\nEND_IF;\n",
               {{"ObjPtr", "POINTER TO Object"}});
        const std::string l = live(c), k = compiled(c), g = generated(c);
        check(l.empty() && k.empty() && g.empty(), "le script de la capture : sans erreur" + l + k + g);
        Runtime rt;
        rt.bind(&c.doc->project, nullptr);
        rt.start(0.0);
        for (int i = 1; i <= 4; ++i) rt.tick(0.1 * i);
        std::size_t said = 0;
        for (const auto& j : rt.journal()) said += j.message.find("Carte_1 cach\xC3\xA9" "e par ObjPtr") != std::string::npos;
        sim::Value vis;
        check(said == 1 && rt.environment().read("Vue_Accueil.Carte_1.Visible", vis) && !vis.isTruthy(),
              "le script de la capture : Carte_1 cachee, une ligne a la Console (" + std::to_string(said) + ")");
    }
    // les autres declarations : Object, REF_TO Object ; un nom court, une popup, ObjPtr d'un autre type d'objet
    for (const char* type : {"Object", "REF_TO Object", "POINTER TO Object"}) {
        accepted("x := Vue_Accueil.Carte_1;\n", {{"x", type}}, std::string("x : ") + type + " := Vue_Accueil.Carte_1");
        accepted("x := Carte_1;\n", {{"x", type}}, std::string("x : ") + type + " := Carte_1 (le nom court, dans sa vue)");
        accepted("x := Pop_Info.Bouton_OK;\n", {{"x", type}}, std::string("x : ") + type + " := Pop_Info.Bouton_OK (une popup)");
    }
    {
        Case c("o := Vue_Accueil.Carte_2;\nr := Carte_1;\no.Fill := '#FF0000';\nr.Visible := FALSE;\n", {{"o", "Object"}, {"r", "REF_TO Object"}});
        check(c.runCycle("Vue_Accueil.Carte_2.Fill") == "#FF0000" && c.runCycle("Vue_Accueil.Carte_1.Visible") == "FALSE",
              "le moteur : Object et REF_TO Object recoivent Vue.Objet et Carte_1 (" + c.runCycle("Vue_Accueil.Carte_2.Fill") + ")");
    }
}

// ---- 2 : Get(), ADR, NULL, les fonctions des objets, un pointeur passe a une fonction ------------------------------------
void objetsGetAdr151() {
    using namespace obj151;
    std::printf("-- 1.51.0 : Get(), ADR(Vue.Objet), NULL, Hide(p^), une fonction qui prend le pointeur\n");
    const std::vector<std::pair<std::string, std::string>> ptr{{"ObjPtr", "POINTER TO Object"}};
    accepted("ObjPtr := Vue_Accueil.Carte_1.Get();\n", ptr, "ObjPtr := Vue_Accueil.Carte_1.Get()");
    accepted("ObjPtr := Carte_1.Get();\nVue_Accueil.Carte_2.Get();\n", ptr, "Carte_1.Get() dans sa vue ; un appel seul");
    accepted("ObjPtr := ADR(Vue_Accueil.Carte_1);\nObjPtr := ADR(Carte_2);\n", ptr, "ADR(Vue_Accueil.Carte_1), ADR(Carte_2)");
    accepted("o := Carte_1;\nObjPtr := o.Get();\nObjPtr := ObjPtr^.Get();\n", {{"ObjPtr", "POINTER TO Object"}, {"o", "Object"}}, "o.Get(), p^.Get()");
    accepted("ObjPtr := Vue_Accueil.Carte_1;\nIF ObjPtr <> NULL THEN\n  ObjPtr^.Visible := FALSE;\n  R := TO_DINT(ObjPtr^.X);\n  Hide(ObjPtr^);\n"
             "  MoveBy(ObjPtr^, 10, 0);\nEND_IF;\nObjPtr := NULL;\n",
             ptr, "NULL, p^.Visible, p^.X, Hide(p^), MoveBy(p^, ...)");
    {
        std::string err;
        std::string got = called("VAR p : POINTER TO Object; END_VAR\np := Vue_Accueil.Carte_1.Get();\np^.Visible := FALSE;\n", &err,
                                 "Vue_Accueil.Carte_1.Visible");
        check(got == "FALSE" && err.empty(), "le moteur : Vue_Accueil.Carte_1.Get() rend un pointeur, p^.Visible := FALSE (" + got + " " + err + ")");
        got = called("VAR p : POINTER TO Object; END_VAR\np := ADR(Vue_Accueil.Carte_1);\nR := TO_DINT(p^.Width);\n", &err, "R");
        check(got == "50" && err.empty(), "le moteur : ADR(Vue_Accueil.Carte_1), p^.Width (" + got + " " + err + ")");
        got = called("VAR p : POINTER TO Object; END_VAR\np := ADR(Carte_2);\nR := TO_DINT(p^.Width);\n", &err, "R");
        check(got == "70" && err.empty(), "le moteur : ADR(Carte_2), un nom court (" + got + " " + err + ")");
        got = called("VAR p, q : POINTER TO Object; END_VAR\np := Vue_Accueil.Carte_1.Get();\nq := ADR(Vue_Accueil.Carte_1);\n"
                     "IF p = q THEN R := 1; END_IF;\nq := Carte_2.Get();\nIF p <> q THEN R := R + 10; END_IF;\nIF p <> NULL THEN R := R + 100; END_IF;\n"
                     "p := NULL;\nIF p = NULL THEN R := R + 1000; END_IF;\n",
                     &err, "R");
        check(got == "1111" && err.empty(), "le moteur : Get() = ADR() (le meme objet), <> un autre, NULL (R = " + got + " " + err + ")");
        got = called("VAR p : POINTER TO Object; END_VAR\np := Vue_Accueil.Carte_1;\nHide(p^);\n", &err, "Vue_Accueil.Carte_1.Visible");
        check(got == "FALSE" && err.empty(), "le moteur : Hide(p^) - une fonction des objets par le pointeur (" + got + " " + err + ")");
        got = called("VAR p : POINTER TO Object; END_VAR\np := Carte_1.Get();\nMoveBy(p^, 5, 0);\nR := TO_DINT(Vue_Accueil.Carte_1.X);\n", &err, "R");
        check(got == "15" && err.empty(), "le moteur : MoveBy(p^, 5, 0) (X = " + got + " " + err + ")");
        got = called("FUNCTION Cacher(c : POINTER TO Object)\n  IF c <> NULL THEN c^.Visible := FALSE; END_IF;\nEND_FUNCTION\n"
                     "Cacher(Vue_Accueil.Carte_2.Get());\n",
                     &err, "Vue_Accueil.Carte_2.Visible");
        check(got == "FALSE" && err.empty(), "le moteur : le pointeur passe a une fonction (" + got + " " + err + ")");
        got = called("VAR o : Object; p : POINTER TO Object; END_VAR\np := o.Get();\nIF p = NULL THEN R := 7; END_IF;\n", &err, "R");
        check(got == "7" && err.empty(), "le moteur : Get() d'un Object NULL rend NULL (" + got + " " + err + ")");
        (void)called("VAR p : POINTER TO Object; END_VAR\np := Vue_Accueil.Carte_1.Get(3);\n", &err, "R");
        check(err.find("Get()") != std::string::npos, "le moteur : Get(3) refuse - il ne prend pas d'argument (" + err + ")");
    }
    refused("ObjPtr := Vue_Accueil.Carte_1.Get(1);\n", ptr, "ne prend pas d'argument", "Get(1)");
    // une fonction Get du projet ne gene pas : Vue.Objet.Get() reste la methode de l'objet
    {
        Case c("ObjPtr := Vue_Accueil.Carte_1.Get();\n", ptr);
        HmiFunction f;
        f.id = c.doc->project.allocate();
        f.name = "Get";
        f.returnType = "INT";
        f.body = "Get := 3;";
        c.doc->project.programs.functions.push_back(f);
        const std::string l = live(c), k = compiled(c);
        check(l.empty() && k.empty(), "une fonction Get du projet : Vue.Objet.Get() reste la methode de l'objet" + l + k);
    }
}

// ---- 3 : la ou une valeur simple est attendue, Vue.Objet seul reste une erreur --------------------------------------------
void objetsValeurSimple151() {
    using namespace obj151;
    std::printf("-- 1.51.0 : une valeur simple attendue - << chemin incomplet >> reste\n");
    refused("R := Vue_Accueil.Carte_1;\n", {}, "chemin incomplet", "R (DINT) := Vue_Accueil.Carte_1");
    refused("S := Carte_1;\n", {}, "chemin incomplet", "S (STRING) := Carte_1");
    refused("x := Vue_Accueil.Carte_1;\n", {{"x", "BOOL"}}, "chemin incomplet", "x : BOOL := Vue_Accueil.Carte_1");
    refused("Vue_Accueil.Carte_2.Width := Vue_Accueil.Carte_1;\n", {}, "chemin incomplet", "une propriete := Vue.Objet");
    refused("IF Vue_Accueil.Carte_1 THEN R := 1; END_IF;\n", {}, "chemin incomplet", "IF Vue.Objet THEN");
    refused("R := Vue_Accueil.Carte_1.Get();\n", {}, "POINTER TO Object", "R (DINT) := Vue.Objet.Get()");
    // une formule d'objet (fx) : une valeur simple, toujours
    {
        Case c("R := 0;\n", {});
        auto& v = c.doc->project.views.front();
        Object t = makeObject(Kind::Text, c.doc->project.allocate(), "Etiquette", 0, 0, v.layers.empty() ? kNoId : v.layers.front().id);
        t.set("text", "{Vue_Accueil.Carte_1}");
        v.objects.push_back(std::move(t));
        bool said = false;
        for (const auto& i : generate(c.doc->project, [](std::string_view) { return false; }))
            said = said || (i.message.find("chemin incomplet") != std::string::npos && i.message.find("Carte_1") != std::string::npos);
        check(said, "un texte a trous {Vue_Accueil.Carte_1} : Generer dit << chemin incomplet >>");
    }
    // ecrire l'objet lui-meme : non
    refused("Vue_Accueil.Carte_1 := Vue_Accueil.Carte_2;\n", {}, "chemin incomplet", "Vue.Objet := ...");
}

// ---- 4 : l'onglet Variables - la barre sous l'editeur suit (tous les types) ----------------------------------------------
void objetsOngletVariables151() {
    using namespace obj151;
    std::printf("-- 1.51.0 : une variable de l'onglet Variables n'est plus dite inexistante (la barre sous l'editeur suit)\n");
    // IHM Studio : un programme vide (un nom inconnu est dit)
    app::assist::installProgram([] { return std::make_shared<const domain::Project>(); }, {});
    struct T {
        const char* type;
        const char* body;
    };
    const T cases[] = {{"POINTER TO Object", "(* Cycle *)\nObjPtr := Vue_Accueil.Carte_1;\n"},
                       {"Object", "(* Cycle *)\nObjPtr := Vue_Accueil.Carte_1;\n"},
                       {"REF_TO Object", "(* Cycle *)\nObjPtr := Carte_1;\n"},
                       {"pointer to object", "(* Cycle *)\nObjPtr := Vue_Accueil.Carte_1.Get();\n"},
                       {"INT", "(* Cycle *)\nObjPtr := ObjPtr + 1;\n"},
                       {"STRING", "(* Cycle *)\nObjPtr := CONCAT(ObjPtr, 'a');\n"},
                       {"BOOL", "(* Cycle *)\nObjPtr := NOT ObjPtr;\n"},
                       {"REAL", "(* Cycle *)\nObjPtr := ObjPtr * 2.0;\n"},
                       {"ARRAY[0..3] OF INT", "(* Cycle *)\nObjPtr[1] := 2;\n"},
                       {"LIST OF Object", "(* Cycle *)\nLIST_ADD(ObjPtr, Vue_Accueil.Carte_1);\n"}};
    for (const auto& t : cases) {
        Case c(t.body, {});
        core::CommandStack stack;
        app::HmiScriptsPane pane("obj151.vars", c.doc, [&](core::CommandPtr cmd) { (void)stack.push(std::move(cmd)); }, c.view);
        pane.setAssist([] { return std::make_shared<const domain::Project>(); });
        pane.setBounds({0, 0, 1500, 860});
        pane.layout();
        pane.selectScript(c.script);
        {                                             // le curseur dans ObjPtr (ligne 2)
            const std::string body = t.body;
            const std::size_t line2 = body.find('\n') + 1;
            pane.editor().selectRange(1, static_cast<std::uint32_t>(body.find("ObjPtr", line2) - line2 + 1), 0);
        }
        pane.updateSymbolLine();
        const std::string before = errorsOf(pane.diagnostics());
        const std::string barBefore = pane.symbolBar().message();
        auto& grid = pane.variablesGrid();
        const bool added = grid.add() && grid.setCell(0, hmi::decledit::Column::Name, "ObjPtr") && grid.setCell(0, hmi::decledit::Column::Type, t.type);
        const std::string after = errorsOf(pane.diagnostics());
        const std::string barAfter = pane.symbolBar().message();
        check(before.find("ObjPtr n'existe pas") != std::string::npos && barBefore.find("Erreur : ObjPtr n'existe pas") == 0,
              std::string("[") + t.type + "] avant la declaration : ObjPtr n'existe pas (" + before + " ; barre : " + barBefore + ")");
        check(added && !c.sc().decls.empty(), std::string("[") + t.type + "] la grille Variables : ObjPtr declaree (" + grid.lastMessage() + ")");
        check(after.empty(), std::string("[") + t.type + "] apres : plus d'erreur" + after);
        check(barAfter.find("n'existe pas") == std::string::npos && barAfter.rfind("Erreur", 0) != 0,
              std::string("[") + t.type + "] la barre sous l'editeur suit (" + barAfter + ")");
        check(pane.statusText().find("aucune erreur") != std::string::npos, std::string("[") + t.type + "] la barre du volet : " + pane.statusText());
    }
    app::assist::installProgram({}, {});
}

// ---- 5 : l'aide a la saisie et les fiches : Get() ---------------------------------------------------------------------------
void objetsAideGet151() {
    using namespace obj151;
    std::printf("-- 1.51.0 : Get() dans l'aide a la saisie (apres Objet.) et dans la fiche du type Object\n");
    Case c("R := 0;\n", {});
    const auto has = [](const std::vector<app::assist::Item>& items, const std::string& text, const std::string& insert) {
        return std::any_of(items.begin(), items.end(), [&](const app::assist::Item& i) {
            return i.text == text && (i.insert.empty() ? i.text : i.insert) == insert && i.detail.find("POINTER TO Object") != std::string::npos;
        });
    };
    check(has(app::assist::suggest(c.doc->project, nullptr, "ObjPtr := Vue_Accueil.Carte_1.", ""), "Get", "Get()"),
          "apres Vue_Accueil.Carte_1. : Get() (POINTER TO Object)");
    check(has(app::assist::suggest(c.doc->project, nullptr, "ObjPtr := Vue_Accueil.Carte_1.", "Ge"), "Get", "Get()"), "... en tapant Ge");
    check(has(app::assist::suggest(c.doc->project, nullptr, "VAR o : Object; END_VAR\nx := o.", ""), "Get", "Get()"), "apres o. (un Object) : Get()");
    check(has(app::assist::suggest(c.doc->project, nullptr, "VAR p : POINTER TO Object; END_VAR\nx := p^.", ""), "Get", "Get()"),
          "apres p^. (un POINTER TO Object) : Get()");
    // la barre sous l'editeur (le nom ou est le curseur) : Vue.Objet seul est un Object, Vue.Objet.Get sa methode
    const auto d1 = app::assist::describe(c.doc->project, nullptr, "Vue_Accueil.Carte_1");
    check(d1.found && d1.type == "Object" && d1.line.find("chemin incomplet") == std::string::npos && d1.line.find("Get()") != std::string::npos,
          "la barre : Vue_Accueil.Carte_1 est un Object (" + d1.line + ")");
    const auto d2 = app::assist::describe(c.doc->project, nullptr, "Vue_Accueil.Carte_1.Get");
    check(d2.found && d2.type == "POINTER TO Object", "la barre : Vue_Accueil.Carte_1.Get() rend un POINTER TO Object (" + d2.line + ")");
    const auto card = natives::typeCard("Object");
    bool noted = false;
    if (card)
        for (const auto& n : card->notes) noted = noted || (n.rfind("Get()", 0) == 0 && n.find("POINTER TO Object") != std::string::npos);
    check(noted, "la fiche du type Object (Natives) : Get() : POINTER TO Object");
}

// ---- 6 : la section Execution, seulement pour un script de cycle ----------------------------------------------------------
void objetsSectionCycle151() {
    using namespace obj151;
    std::printf("-- 1.51.0 : la section Execution (les etapes du cycle) seulement pour un script de cycle\n");
    Case c("(* Cycle *)\nR := R + 1;\n", {});
    core::CommandStack stack;
    app::HmiScriptsPane pane("obj151.cycle", c.doc, [&](core::CommandPtr cmd) { (void)stack.push(std::move(cmd)); }, c.view);
    pane.setBounds({0, 0, 1500, 860});
    pane.layout();
    auto* stages = pane.cycleStages();
    check(stages != nullptr, "la section Execution existe (une vue)");
    if (!stages) return;
    const auto width = [&] {
        pane.layout();
        return pane.editor().bounds().w;
    };
    // la colonne : le panneau EXECUTION qui tient la section (cache : sa largeur est nulle)
    const auto column = [&] {
        ui::Widget* w = stages;
        while (w && w->parent() && !dynamic_cast<ui::Splitter*>(w->parent())) w = w->parent();
        return w && w->parent() ? w->bounds().w : -1.f;
    };
    const auto shownNow = [&] { return pane.cycleColumnShown() && column() > 100.f; };
    const auto hiddenNow = [&] { return !pane.cycleColumnShown() && column() < 1.f; };
    const std::string hint = "Glisse un script de cycle entre deux";
    // OnOpen : cachee, l'editeur prend sa largeur, la phrase du cycle part
    pane.selectScript(c.open);
    const float wOpen = width();
    check(hiddenNow(), "OnOpen choisi : la section Execution est cachee (" + std::to_string(column()) + ")");
    pane.refresh();
    check(hiddenNow() && pane.selectedScript() == c.open, "... et le reste apres un rafraichissement");
    // Cycle (OnCycle) : montree
    pane.selectScript(c.script);
    const float wCycle = width();
    check(shownNow(), "un script de cycle choisi : la section Execution est montree (" + std::to_string(column()) + ")");
    check(wOpen > wCycle + 150.f, "sa largeur va a l'editeur quand elle est cachee (" + std::to_string(wOpen) + " contre " + std::to_string(wCycle) + ")");
    pane.refresh();
    width();
    check(shownNow() && pane.selectedScript() == c.script, "... et le reste apres un rafraichissement");
    pane.refresh();
    check(std::fabs(width() - wCycle) < 1.f, "un rafraichissement ne change pas la place");
    // OnClose (vide) : cachee ; le titre OnCycle : montree ; rien de choisi : cachee
    pane.selectEvent("OnClose");
    width();
    check(hiddenNow() && pane.selectedScript() == kNoId, "OnClose (vide) choisi : cachee (montree " + std::to_string(pane.cycleColumnShown())
                                                             + ", largeur " + std::to_string(column()) + ", script "
                                                             + std::to_string(pane.selectedScript()) + ", evenement " + pane.selectedEvent() + ")");
    pane.selectEvent("OnOpen");
    width();
    check(hiddenNow(), "OnOpen : cachee");
    pane.refresh();
    check(pane.properties().categories().size() > 0, "les proprietes suivent");
    // la largeur revient : OnOpen -> Cycle -> OnOpen donne la meme largeur
    pane.selectScript(c.script);
    width();
    pane.selectScript(c.open);
    check(std::fabs(width() - wOpen) < 1.f, "OnOpen, Cycle, OnOpen : la meme largeur d'editeur (" + std::to_string(width()) + ")");
    pane.selectScript(c.script);
    check(std::fabs(width() - wCycle) < 1.f, "... et Cycle retrouve la sienne (" + std::to_string(width()) + ")");
    // glisser la pastille de Cycle apres 8 : marche toujours quand la section est montree
    {
        stages->layout();
        gfx::Rect chip, slot8;
        check(stages->chipRect(c.script, chip) && stages->slotRect(8, slot8), "la pastille de Cycle, la place apres 8");
        const gfx::Point from{chip.x + chip.w * 0.5f, chip.y + chip.h * 0.5f};
        const auto send = [&](const ui::InputEvent& ev) {
            if (!ui::Widget::routeCapturedMouse(pane, ev)) (void)pane.dispatch(ev);
        };
        send(ui::MouseDown{from, ui::MouseButton::Left, 1, {}});
        send(ui::MouseMove{{from.x + 8.f, from.y + 8.f}, {8.f, 8.f}, {}});
        stages->slotRect(8, slot8);
        const gfx::Point to{slot8.x + slot8.w * 0.5f, slot8.y + slot8.h * 0.5f};
        send(ui::MouseMove{to, {0.f, 0.f}, {}});
        send(ui::MouseUp{to, ui::MouseButton::Left, {}});
        check(c.sc().stage == 8, "glisser Cycle apres l'etape 8 marche (etape " + std::to_string(c.sc().stage) + ")");
        check(shownNow(), "... la section reste montree (Cycle choisi)");
    }
    // OnOpen choisi, puis + Script de cycle tire : la section se montre, le depot marche
    {
        pane.selectScript(c.open);
        width();
        check(hiddenNow(), "OnOpen de nouveau : cachee");
        auto* add = pane.cycleAdd();
        check(add != nullptr, "le bouton + Script de cycle");
        if (add) {
            const gfx::Rect b = add->buttonRect();
            const gfx::Point at{b.x + b.w * 0.5f, b.y + b.h * 0.5f};
            const auto send = [&](const ui::InputEvent& ev) {
                if (!ui::Widget::routeCapturedMouse(pane, ev)) (void)pane.dispatch(ev);
            };
            const std::size_t count = c.v().scripts.size();
            send(ui::MouseDown{at, ui::MouseButton::Left, 1, {}});
            send(ui::MouseMove{{at.x + 12.f, at.y}, {12.f, 0.f}, {}});
            check(shownNow(), "+ Script de cycle tire avec OnOpen choisi : la section se montre pour le depot");
            stages->layout();
            gfx::Rect slot6;
            (void)stages->slotRect(6, slot6);
            const gfx::Point to{slot6.x + slot6.w * 0.5f, slot6.y + slot6.h * 0.5f};
            send(ui::MouseMove{to, {0.f, 0.f}, {}});
            send(ui::MouseUp{to, ui::MouseButton::Left, {}});
            check(c.v().scripts.size() == count + 1 && hmi::viewcycle::atMoment(c.v(), 6).size() == 1,
                  "... le depot apres 6 cree le script de cycle");
            check(shownNow(), "... choisi : la section reste montree");
        }
    }
    // la phrase du cycle dans la barre du volet : seulement avec la section
    {
        auto* bar = dynamic_cast<ui::StatusBar*>(pane.findById("obj151.cycle.status"));
        check(bar != nullptr, "la barre du volet");
        pane.selectScript(c.script);
        pane.refresh();
        if (bar) bar->dismissTransient();
        check(pane.cycleColumnShown() && bar && bar->message().find(hint) == 0, "Cycle choisi : la phrase du cycle dans la barre ("
                                                                                     + (bar ? bar->message() : std::string("?")) + ")");
        pane.selectScript(c.open);
        check(!pane.cycleColumnShown() && bar && bar->message().find(hint) == std::string::npos,
              "OnOpen choisi : la phrase part avec la section (" + (bar ? bar->message() : std::string("?")) + ")");
        pane.refresh();
        check(!pane.cycleColumnShown() && bar && bar->message().find(hint) == std::string::npos,
              "OnOpen choisi, rafraichi : cachee, sans la phrase (" + (bar ? bar->message() : std::string("?")) + ")");
        pane.selectEvent("OnClose");
        check(bar && bar->message().find(hint) == std::string::npos, "OnClose (vide) : sans la phrase");
    }
}

// ---- 7 : une instance de symbole (l'objet pose) ------------------------------------------------------------------------------
void objetsSymbole151() {
    using namespace obj151;
    std::printf("-- 1.51.0 : une instance de symbole - ObjPtr := Vue_Accueil.Carte_3\n");
    const auto withSymbol = [](Case& c) {
        Project& p = c.doc->project;
        View sym = makeView(p, "S_Carte");
        sym.role = "symbole";
        Object fond = makeObject(Kind::Rectangle, p.allocate(), "Fond", 0, 0, sym.layers.empty() ? kNoId : sym.layers.front().id);
        fond.setNumber("w", 80);
        fond.setNumber("h", 40);
        sym.objects.push_back(std::move(fond));
        p.views.push_back(sym);
        View& v = *p.view(c.view);
        Object inst = makeObject(Kind::SymbolInstance, p.allocate(), "Carte_3", 400, 20, v.layers.empty() ? kNoId : v.layers.front().id);
        inst.set("symbol", "S_Carte");
        inst.setNumber("w", 80);
        inst.setNumber("h", 40);
        v.objects.push_back(std::move(inst));
    };
    {
        Case c("ObjPtr := Vue_Accueil.Carte_3;\nObjPtr^.Visible := FALSE;\n", {{"ObjPtr", "POINTER TO Object"}});
        withSymbol(c);
        const std::string l = live(c), k = compiled(c), g = generated(c);
        check(l.empty() && k.empty() && g.empty(), "une instance de symbole : ObjPtr := Vue_Accueil.Carte_3 accepte" + l + k + g);
        check(c.runCycle("Vue_Accueil.Carte_3.Visible") == "FALSE", "le moteur : l'instance se cache par le pointeur ("
                                                                       + c.runCycle("Vue_Accueil.Carte_3.Visible") + ")");
    }
    // (Vue_Accueil.Carte_3.Fond, un objet DANS l'instance : pas un chemin de l'IHM - ni Vue.Instance.Objet.Propriete,
    // ni en pointeur ; l'instance elle-meme, oui.)
}

void pointeurVersObjet151() {
    std::printf("-- 1.51.0 : un pointeur vers un objet de la vue (Vue.Objet, Get(), ADR) ; la section Execution\n");
    objetsCasUtilisateur151();
    objetsGetAdr151();
    objetsValeurSimple151();
    objetsOngletVariables151();
    objetsAideGet151();
    objetsSectionCycle151();
    objetsSymbole151();
}

} // namespace
