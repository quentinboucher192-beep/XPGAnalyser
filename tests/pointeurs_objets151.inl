// =============================================================================
//  tests/pointeurs_objets151.inl - 1.51.0 : UN POINTEUR VERS UN OBJET DE LA VUE
//  (inclus par pointeurs_test.cpp)
// -----------------------------------------------------------------------------
//  Le cas de l'utilisateur : une vue Vue_Accueil, un objet Carte_1 ; un script de
//  cycle << Cycle >> (OnCycle, apres l'etape 4, ST), son onglet Variables tient
//  ObjPtr : POINTER TO Object ; son corps : ObjPtr := Vue_Accueil.Carte_1;
//  << pouvoir y acceder comme ca devrait fonctionner (le pointeur), ou alors mettre
//  une methode dans les objets Get() qui renvoie un pointeur >>.
// =============================================================================
#include "../src/app/hmi/HmiScriptPanes.hpp"
#include "../src/domain/ProjectModel.hpp"
#include "../src/hmi/HmiCheck.hpp"

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

// Le projet de l'utilisateur : Vue_Accueil (Carte_1, Carte_2), une popup Pop_Info (Bouton_OK), une variable R (DINT) ; le
// script de cycle Cycle de Vue_Accueil, ses declarations `decls`, son corps `body`.
struct Case {
    std::shared_ptr<Document> doc = std::make_shared<Document>();
    Id view{kNoId}, script{kNoId}, carte{kNoId};
    Case(const std::string& body, const std::vector<std::pair<std::string, std::string>>& decls, const std::string& event = "OnCycle") {
        Project& p = doc->project;
        addVar(p, "R", "DINT");
        addVar(p, "S", "STRING");
        View v = makeView(p, "Vue_Accueil");
        const Id layer = v.layers.empty() ? kNoId : v.layers.front().id;
        for (const char* n : {"Carte_1", "Carte_2"}) {
            Object o = makeObject(Kind::Rectangle, p.allocate(), n, 10, 20, layer);
            o.setNumber("w", n[6] == '1' ? 50 : 70);
            o.setNumber("h", 30);
            v.objects.push_back(std::move(o));
        }
        carte = v.objects.front().id;
        Script s;
        s.id = p.allocate();
        s.name = "Cycle";
        s.event = event;
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
};

std::string errorsOf(const std::vector<ScriptDiagnostic>& d) {
    std::string out;
    for (const auto& x : d)
        if (x.severity == ScriptDiagnostic::Severity::Error) out += " | l" + std::to_string(x.line) + " " + x.message;
    return out;
}
std::string errorsOf(const std::vector<Issue>& d, Id script = kNoId) {
    std::string out;
    for (const auto& x : d)
        if (x.severity == Issue::Severity::Error && (script == kNoId || x.script == script || x.message.find("Carte") != std::string::npos))
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

} // namespace obj151

void pointeurVersObjet151() {
    using namespace obj151;
    std::printf("-- 1.51.0 : un pointeur vers un objet de la vue (Vue.Objet, Get(), ADR)\n");
    {
        Case c("ObjPtr := Vue_Accueil.Carte_1;\n", {{"ObjPtr", "POINTER TO Object"}});
        const std::string l = live(c), k = compiled(c), g = generated(c);
        check(l.empty(), "le cas de l'utilisateur : la verification en direct ne dit rien" + l);
        check(k.empty(), "le cas de l'utilisateur : Compiler ne dit rien" + k);
        check(g.empty(), "le cas de l'utilisateur : Generer (la validation) ne dit rien" + g);
    }
    // Le volet des scripts de la vue, comme l'utilisateur : le corps tape d'abord, puis ObjPtr declaree dans l'onglet
    // Variables (la grille) - la verification en direct ne dit plus rien.
    for (const char* type : {"POINTER TO Object", "Object", "REF_TO Object", "INT", "pointer to object"}) {
        Case c("ObjPtr := Vue_Accueil.Carte_1;\n", {});
        core::CommandStack stack;
        app::HmiScriptsPane pane("obj151", c.doc, [&](core::CommandPtr cmd) { (void)stack.push(std::move(cmd)); }, c.view);
        pane.setBounds({0, 0, 1500, 860});
        pane.layout();
        pane.selectScript(c.script);
        const std::string before = errorsOf(pane.diagnostics());
        auto& grid = pane.variablesGrid();
        const bool added = grid.add() && grid.setCell(0, hmi::decledit::Column::Name, "ObjPtr")
                        && grid.setCell(0, hmi::decledit::Column::Type, type);
        const std::string after = errorsOf(pane.diagnostics());
        std::printf("   [%s] avant :%s\n   apres :%s\n   decl : %s\n", type, before.c_str(), after.c_str(),
                    c.sc().decls.empty() ? "(aucune)" : (c.sc().decls[0].name + " : " + c.sc().decls[0].type).c_str());
        check(added, std::string("la grille Variables : ObjPtr : ") + type + " (" + grid.lastMessage() + ")");
    }
}

} // namespace
