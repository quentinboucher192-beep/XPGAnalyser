// =============================================================================
//  tests/pointeurs_test.cpp - 1.50.0 (planche 03) : POINTER TO, un vrai pointeur
// -----------------------------------------------------------------------------
//  La reponse de l'utilisateur : << POINTER TO et un vrai pointeur >>. POINTER TO T
//  vise une variable (ADR(x)), p^ la lit et l'ecrit, NULL ne vise rien, = et <>
//  comparent ce qu'ils visent - pour tout type, Object compris ; une variable
//  disparue (la locale d'une fonction terminee) est dite. << Choisir un type >> a
//  la ligne Acces (Direct, REF_TO, POINTER TO), grisee la ou ce n'est pas permis
//  (une variable IHM, un parametre de popup, une constante, un operande), avec
//  le pointeur sur chaque element (LIST OF POINTER TO Object) ou sur le tout
//  (POINTER TO LIST OF Object), et la recherche comprend un type ecrit
//  (<< pointer to obj >>). << POINTER OF >> tape est lu POINTER TO, et dit.
//
//  PTR_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/app/hmi/HmiTypePicker.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiDeclEdit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiTypeForms.hpp"
#include "../src/hmi/HmiTypeRegistry.hpp"
#include "../src/menu/MenuManager.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("PTR_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

std::string shown(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }

void addVar(Project& p, const std::string& n, const std::string& t) {
    Variable v;
    v.id = p.allocate();
    v.name = n;
    v.type = t;
    p.programs.variables.push_back(v);
}

// Une vue Vue_A (deux rectangles Pompe_1 et Pompe_2), des variables R (DINT), N (DINT), S (STRING), un script Essai.
std::string run(const std::string& body, std::string* err = nullptr, const std::string& readVar = "R") {
    Project p;
    addVar(p, "R", "DINT");
    addVar(p, "N", "DINT");
    addVar(p, "S", "STRING");
    View a = makeView(p, "Vue_A");
    const Id layer = a.layers.empty() ? kNoId : a.layers.front().id;
    for (const char* n : {"Pompe_1", "Pompe_2"}) {
        Object o = makeObject(Kind::Rectangle, p.allocate(), n, 10, 20, layer);
        o.setNumber("w", n[6] == '1' ? 50 : 70);
        o.setNumber("h", 30);
        a.objects.push_back(std::move(o));
    }
    p.views.push_back(a);
    p.config.startView = p.views.back().id;
    Script s;
    s.id = p.allocate();
    s.name = "Essai";
    s.event = "Appel";
    s.body = body;
    p.programs.scripts.push_back(s);
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    std::string e;
    const bool ok = rt.callScript("Essai", 0.2, &e);
    if (err) *err = ok ? std::string{} : e;
    sim::Value out;
    return rt.environment().read(readVar, out) ? shown(out) : std::string("#");
}

// ---- le moteur : un vrai pointeur -------------------------------------------------------------------
void leMoteur() {
    std::printf("-- le moteur : ADR, ^, NULL, = et <>\n");
    std::string err;
    std::string got = run("VAR\n  x : INT;\n  p : POINTER TO INT;\nEND_VAR\nx := 5;\np := ADR(x);\nR := p^;\np^ := 7;\nR := R * 10 + x;\n", &err);
    check(got == "57" && err.empty(), "POINTER TO INT : p^ lit 5, p^ := 7 ecrit x (R = " + got + " " + err + ")");
    got = run("VAR\n  p : POINTER TO DINT;\nEND_VAR\np := ADR(N);\np^ := 42;\nR := N;\n", &err);
    check(got == "42" && err.empty(), "POINTER TO DINT vers une variable IHM : p^ := 42 l'ecrit (R = " + got + " " + err + ")");
    got = run("VAR\n  a, b : INT;\n  p, q : POINTER TO INT;\nEND_VAR\np := ADR(a);\nq := ADR(a);\nIF p = q THEN R := 1; END_IF;\nq := ADR(b);\n"
              "IF p <> q THEN R := R + 10; END_IF;\n",
              &err);
    check(got == "11" && err.empty(), "p = q (la meme variable), p <> q (une autre) (R = " + got + " " + err + ")");
    got = run("VAR\n  p : POINTER TO INT;\nEND_VAR\nIF p = NULL THEN R := 1; END_IF;\np := NULL;\nIF p = NULL THEN R := R + 1; END_IF;\n", &err);
    check(got == "2" && err.empty(), "NULL au depart, p := NULL, p = NULL (R = " + got + " " + err + ")");
    got = run("VAR\n  p : POINTER TO INT;\nEND_VAR\nR := 3;\nR := p^;\n", &err);
    check(err.find("ligne 5") != std::string::npos && err.find("nul") != std::string::npos, "p^ sur NULL : dit, a sa ligne (" + err + ")");
    got = run("FUNCTION Locale() : POINTER TO INT\n  VAR l : INT; END_VAR\n  l := 3;\n  RETURN ADR(l);\nEND_FUNCTION\nVAR\n  p : POINTER TO INT;\nEND_VAR\n"
              "p := Locale();\nR := p^;\n",
              &err);
    check(!err.empty() && err.find("dispar") != std::string::npos, "un pointeur vers la locale d'une fonction terminee : dit (" + err + ")");
    // Object : un pointeur vers une variable qui tient un objet, et le pointeur que rend GetObject
    got = run("VAR\n  o : Object;\n  p : POINTER TO Object;\nEND_VAR\no := GetObject('Pompe_1');\np := ADR(o);\nR := p^.Width;\n"
              "p^ := GetObject('Pompe_2');\nR := R * 1000 + o.Width;\n",
              &err);
    check(got == "50070" && err.empty(), "POINTER TO Object : p := ADR(o), p^.Width, p^ := un autre objet change o (R = " + got + " " + err + ")");
    got = run("VAR\n  p : POINTER TO Object := GetObject('Pompe_2');\nEND_VAR\nIF p <> NULL THEN R := p^.Width; END_IF;\n", &err);
    check(got == "70" && err.empty(), "POINTER TO Object := GetObject(...) (R = " + got + " " + err + ")");
    got = run("VAR\n  l : LIST OF POINTER TO INT;\n  a : INT;\nEND_VAR\na := 4;\nLIST_ADD(l, ADR(a));\nl[0]^ := 9;\nR := a;\n", &err);
    check(got == "9" && err.empty(), "LIST OF POINTER TO INT : chaque element un pointeur (R = " + got + " " + err + ")");
    got = run("VAR\n  l : LIST OF INT;\n  p : POINTER TO LIST OF INT;\nEND_VAR\nLIST_ADD(l, 9);\np := ADR(l);\nLIST_ADD(p^, 4);\nR := p^[0] * 10 + l.Count;\n", &err);
    check(got == "92" && err.empty(), "POINTER TO LIST OF INT : un pointeur vers la liste entiere (R = " + got + " " + err + ")");
    // POINTER OF : lu POINTER TO, et dit
    got = run("VAR\n  p : POINTER OF INT;\n  x : INT;\nEND_VAR\nx := 2;\np := ADR(x);\nR := p^;\n", &err);
    check(got == "2" && err.empty(), "POINTER OF INT : lu comme POINTER TO INT (R = " + got + " " + err + ")");
    bool warned = false;
    for (const auto& d : checkScript(ScriptLang::ST, "VAR\n  p : POINTER OF INT;\nEND_VAR\n", "S"))
        if (d.severity == ScriptDiagnostic::Severity::Warning && d.line == 2 && d.message.find("POINTER TO") != std::string::npos) warned = true;
    check(warned, "POINTER OF : un avertissement a sa ligne (Compiler)");
}

// ---- le registre et les formes ---------------------------------------------------------------------
void lesFormes() {
    std::printf("-- les formes : l'acces sur chaque element ou sur le tout\n");
    namespace tf = typeform;
    tf::Shape s;
    s.form = tf::Form::List;
    s.element = "Object";
    s.setAccess(tf::Access::Pointer);
    check(tf::compose(s) == "LIST OF POINTER TO Object", "chaque element : " + tf::compose(s));
    s.whole = true;
    check(tf::compose(s) == "POINTER TO LIST OF Object", "le tout : " + tf::compose(s));
    const auto back = tf::decompose("POINTER TO LIST OF Object");
    check(back.form == tf::Form::List && back.access() == tf::Access::Pointer && back.whole && back.element == "Object",
          "POINTER TO LIST OF Object relu : Liste, POINTER TO, le tout");
    const auto each = tf::decompose("LIST OF POINTER TO Object");
    check(each.form == tf::Form::List && each.access() == tf::Access::Pointer && !each.whole, "LIST OF POINTER TO Object relu : chaque element");
    const auto simple = tf::decompose("POINTER TO Object");
    check(simple.form == tf::Form::Simple && simple.access() == tf::Access::Pointer && simple.element == "Object", "POINTER TO Object relu");
    check(tf::decompose("REF_TO T_Four").access() == tf::Access::Ref, "REF_TO relu comme avant");
    // ou c'est permis (le tableau de la planche)
    namespace tr = typereg;
    check(tf::accessRefusal(tf::Access::Pointer, tr::UseDeclaration).empty() && tf::accessRefusal(tf::Access::Pointer, tr::UseReturn).empty(),
          "POINTER TO : permis dans une declaration et un retour");
    check(tf::accessRefusal(tf::Access::Pointer, tr::UseVariable).find("\xC3\xA9quipement") != std::string::npos,
          "... pas une variable IHM : " + tf::accessRefusal(tf::Access::Pointer, tr::UseVariable));
    check(!tf::accessRefusal(tf::Access::Pointer, tr::UseParameter).empty() && !tf::accessRefusal(tf::Access::Ref, tr::UseOperand).empty(),
          "... ni un parametre de popup, ni un operande");
    check(tf::accessRefusal(tf::Access::Pointer, tr::UseDeclaration, true).find("constante") != std::string::npos, "... ni une constante");
    Project p;
    const auto reg = tr::Registry::build(p);
    const auto r = reg->resolve("pointer of Object", tr::UseDeclaration);
    check(r.ok && r.text == "POINTER TO Object" && r.warning.find("POINTER OF") != std::string::npos,
          "le registre : POINTER OF Object -> POINTER TO Object, avec un avertissement (" + r.text + " ; " + r.warning + ")");
    const auto v = reg->resolve("POINTER TO INT", tr::UseVariable);
    check(!v.ok, "le registre : POINTER TO INT refuse pour une variable IHM (" + v.why + ")");
}

// ---- le selecteur : la ligne Acces ---------------------------------------------------------------------
void leSelecteur() {
    std::printf("-- le selecteur de types : Acces (Direct, REF_TO, POINTER TO)\n");
    namespace tr = typereg;
    auto doc = std::make_shared<Document>();
    menu::MenuManager mm{menu::MenuFactory{}};
    const auto open = [&](app::HmiTypePicker::Spec spec) -> app::HmiTypePicker* {
        mm.ShowDialog(std::make_unique<app::HmiTypePicker>(std::move(spec)), [](const menu::DialogResult&) {});
        mm.applyPending();
        return dynamic_cast<app::HmiTypePicker*>(mm.top());
    };
    app::HmiTypePicker::Spec spec;
    spec.field = "Type de Cible";
    spec.current = "Object";
    spec.use = tr::UseDeclaration;
    spec.doc = doc;
    auto* picker = open(spec);
    check(picker != nullptr, "le selecteur s'ouvre");
    if (!picker) return;
    check(picker->result() == "Object", "Object choisi d'office : " + picker->result());
    check(picker->setAccess("POINTER TO") && picker->result() == "POINTER TO Object" && picker->resultProblem().empty(),
          "Acces POINTER TO : " + picker->result() + " " + picker->resultProblem());
    check(picker->writtenAs() == "Cible : POINTER TO Object;", "S'ecrira : " + picker->writtenAs());
    check(picker->accessLine().find("ADR(x)") != std::string::npos && picker->accessLine().find("Object*") != std::string::npos,
          "la phrase de l'acces : " + picker->accessLine());
    check(picker->setAccess("REF_TO") && picker->result() == "REF_TO Object", "Acces REF_TO : " + picker->result());
    check(picker->setAccess("Direct") && picker->result() == "Object", "Acces Direct : " + picker->result());
    // avec une forme : chaque element, ou le tout
    check(picker->setForm("Liste") && picker->setAccess("POINTER TO") && picker->result() == "LIST OF POINTER TO Object",
          "Liste + POINTER TO, chaque element : " + picker->result());
    picker->setPointsWhole(true);
    check(picker->result() == "POINTER TO LIST OF Object" && picker->resultProblem().empty(), "... le tout : " + picker->result());
    picker->setPointsWhole(false);
    check(picker->setForm("Simple") && picker->setAccess("Direct"), "retour a Simple, Direct");
    // la recherche comprend un type ecrit
    picker->setSearch("pointer to obj");
    check(picker->result() == "POINTER TO Object" && picker->resultProblem().empty(), "la recherche << pointer to obj >> : " + picker->result());
    picker->setSearch("pointer of Object");
    check(picker->result() == "POINTER TO Object" && picker->typedNote().find("POINTER OF") != std::string::npos,
          "<< pointer of Object >> corrige en POINTER TO, et dit : " + picker->typedNote());
    picker->setSearch("list of real");
    check(picker->result() == "LIST OF REAL", "<< list of real >> : " + picker->result());
    picker->setSearch("");
    // ou c'est grise : une variable IHM, un parametre de popup, une constante, un operande
    struct Grey {
        unsigned    use;
        bool        constant;
        const char* why;
    };
    const Grey greys[] = {{tr::UseVariable, false, "\xC3\xA9quipement"}, {tr::UseParameter, false, "popup"}, {tr::UseDeclaration, true, "constante"}};
    for (const auto& g : greys) {
        auto sp = spec;
        sp.use = g.use;
        sp.constant = g.constant;
        sp.current = "INT";
        auto* p = open(sp);
        if (!p) continue;
        const std::string why = p->accessProblem("POINTER TO");
        check(!p->setAccess("POINTER TO") && why.find(g.why) != std::string::npos && p->result() == "INT",
              "POINTER TO grise (" + std::string(g.why) + ") : " + why + " ; resultat " + p->result());
        check(p->accessProblem("Direct").empty(), "Direct, toujours permis");
        p->setSearch("pointer to obj");
        check(!p->resultProblem().empty(), "... et ecrit dans la recherche : refuse (" + p->resultProblem() + ")");
    }
    // 1.50.0 (les tableaux) : le texte d'un type ecrit dans la recherche - un tableau de tableaux et deux dimensions, pour une
    // variable IHM comme pour une declaration
    for (const unsigned use : {static_cast<unsigned>(tr::UseVariable), static_cast<unsigned>(tr::UseDeclaration)}) {
        auto sp = spec;
        sp.use = use;
        sp.current = "INT";
        auto* p = open(sp);
        if (!p) continue;
        p->setSearch("array[0..3] of array[0..15] of bool");
        check(p->result() == "ARRAY[0..3] OF ARRAY[0..15] OF BOOL" && p->resultProblem().empty(),
              "le selecteur : un tableau de tableaux ecrit (usage " + std::to_string(use) + ") : " + p->result() + " " + p->resultProblem());
        p->setSearch("array[0..3, 0..15] of bool");
        check(p->result() == "ARRAY[0..3, 0..15] OF BOOL" && p->resultProblem().empty(),
              "le selecteur : deux dimensions ecrites (usage " + std::to_string(use) + ") : " + p->result() + " " + p->resultProblem());
    }
    // un retour de fonction : permis
    auto sp = spec;
    sp.use = tr::UseReturn;
    if (auto* p = open(sp)) check(p->setAccess("POINTER TO") && p->result() == "POINTER TO Object", "un retour : POINTER TO Object permis");
    // le type actuel POINTER TO LIST OF Object : rouvert avec sa forme, son acces, le tout
    sp = spec;
    sp.current = "POINTER TO LIST OF Object";
    if (auto* p = open(sp)) check(p->result() == "POINTER TO LIST OF Object", "rouvert : " + p->result());
}

// ---- la grille des declarations : POINTER OF corrige ; une constante refuse POINTER TO ----------------------
void laGrille() {
    std::printf("-- les declarations : POINTER OF corrige ; une constante ne vise rien\n");
    Project p;
    Script sc;
    sc.id = p.allocate();
    sc.name = "Essai";
    sc.event = "Appel";
    Declaration v;
    v.id = p.allocate();
    v.kind = DeclKind::Variable;
    v.name = "Cible";
    v.type = "INT";
    Declaration c = v;
    c.id = p.allocate();
    c.kind = DeclKind::Constant;
    c.name = "Seuil";
    c.value = "0";
    sc.decls = {v, c};
    p.programs.scripts.push_back(sc);
    decledit::Place at;
    at.kind = decledit::Place::Kind::Script;
    at.id = sc.id;
    std::string why, note;
    const bool set = decledit::set(p, at, decledit::Tab::Variables, 0, decledit::Column::Type, "pointer of Object", &why, &note);
    check(set && p.script(sc.id)->decls[0].type == "POINTER TO Object" && note.find("POINTER OF") != std::string::npos,
          "la case Type : << pointer of Object >> -> POINTER TO Object, la note le dit (" + why + note + ")");
    why.clear();
    const bool refused = !decledit::set(p, at, decledit::Tab::Constants, 0, decledit::Column::Type, "POINTER TO INT", &why, &note);
    check(refused && why.find("constante") != std::string::npos, "une constante POINTER TO INT : refusee (" + why + ")");
}

} // namespace

#include "pointeurs_dimensions.inl"   // 1.50.1 : Tableau a N dimensions, + Dimension

int main() {
    std::printf("POINTER TO : un vrai pointeur ; le selecteur de types (Acces)\n");
    leMoteur();
    lesFormes();
    leSelecteur();
    laGrille();
    leSelecteurDimensions();
    std::printf("%d controles, %d echecs\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
