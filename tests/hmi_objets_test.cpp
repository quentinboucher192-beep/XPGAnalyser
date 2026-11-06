// =============================================================================
//  tests/hmi_objets_test.cpp - 1.27.0 : LES TYPES DE L'IHM DANS LES SCRIPTS
// -----------------------------------------------------------------------------
//  Object, Font, Texture, Sound, Style : declares, affectes, ranges dans des
//  listes, compares a NULL ; leurs membres lus et ecrits ; les natives des
//  objets (GetObject, ObjectsOf, AlignTop, JustifyH, ShowIf, CopyObjectTo...)
//  et des ressources (GetSound, GetStyle, GetFont, TextWidth, Color) ; @Alarme ;
//  { } ; un nom court dans un script de vue ; This dans une action ; une
//  expression (fx) lit mais n'agit pas ; Generer previent d'un nom ecrit ; le
//  registre des types et Compiler les connaissent.
//
//  Argument facultatif : le dossier des polices (resources/polices) - une police
//  du projet pour Font, TextWidth et HasGlyph.
// =============================================================================
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiExpr.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiNatives.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiScript.hpp"
#include "../src/hmi/HmiTypeRegistry.hpp"
#include "../src/sim/Rich.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}

std::string shown(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }

std::string g_fonts;     // le dossier des polices (argument)

// Un projet : une vue Vue_A (trois rectangles, un texte, un calque Process), une vue Vue_B (un rectangle), des
// variables R (STRING), N (REAL), B (BOOL), un script appele Essai.
struct Bench {
    Project p;
    Runtime rt;
    Id viewA{kNoId}, viewB{kNoId}, pompe1{kNoId}, titre{kNoId};
    explicit Bench(const std::string& body, const std::string& onOpen = {}, const std::function<void(Project&)>& extra = {}) {
        const auto var = [this](const char* n, const char* t) {
            Variable v;
            v.id = p.allocate();
            v.name = n;
            v.type = t;
            p.programs.variables.push_back(v);
        };
        var("R", "STRING");
        var("N", "REAL");
        var("B", "BOOL");
        var("Mode_Auto", "BOOL");
        View a = makeView(p, "Vue_A");
        const Id layer = a.layers.empty() ? kNoId : a.layers.front().id;
        const auto rect = [&](View& v, const char* n, double x, double y, double w, double h) {
            Object o = makeObject(Kind::Rectangle, p.allocate(), n, x, y, layer);
            o.setNumber("w", w);
            o.setNumber("h", h);
            v.objects.push_back(std::move(o));
            return v.objects.back().id;
        };
        pompe1 = rect(a, "Pompe_1", 10, 20, 50, 30);
        rect(a, "Pompe_2", 100, 60, 50, 40);
        rect(a, "Pompe_3", 200, 10, 60, 30);
        Object t = makeObject(Kind::Text, p.allocate(), "Titre", 300, 300, layer);
        titre = t.id;
        a.objects.push_back(std::move(t));
        Layer process;
        process.id = p.allocate();
        process.name = "Process";
        a.layers.push_back(process);
        if (!onOpen.empty()) {
            Script s;
            s.id = p.allocate();
            s.name = "Ouverture";
            s.event = "OnOpen";
            s.body = onOpen;
            a.scripts.push_back(s);
        }
        p.views.push_back(a);
        viewA = p.views.back().id;
        View b = makeView(p, "Vue_B");
        Object other = makeObject(Kind::Rectangle, p.allocate(), "Cadre", 0, 0, b.layers.empty() ? kNoId : b.layers.front().id);
        b.objects.push_back(std::move(other));
        p.views.push_back(b);
        viewB = p.views.back().id;
        p.config.startView = viewA;
        Script s;
        s.id = p.allocate();
        s.name = "Essai";
        s.event = "Appel";
        s.body = body;
        p.programs.scripts.push_back(s);
        if (extra) extra(p);
        rt.bind(&p, nullptr);
        rt.start(0.0);
        rt.tick(0.1);
    }
    std::string run(std::string* error = nullptr) {
        std::string err;
        const bool ok = rt.callScript("Essai", 0.2, &err);
        if (error) *error = ok ? std::string{} : err;
        return ok ? std::string{} : err;
    }
    std::string read(const std::string& path) {
        sim::Value v;
        return rt.environment().read(path, v) ? shown(v) : std::string("#");
    }
    double number(const std::string& path) {
        sim::Value v;
        if (!rt.environment().read(path, v)) return -9999;
        return v.type() == sim::Type::Real ? v.asReal() : static_cast<double>(v.asInteger());
    }
};

// Un script, puis la valeur de R (ou l'erreur).
std::string scriptR(const std::string& body, std::string* error = nullptr) {
    Bench b(body);
    std::string err;
    b.run(&err);
    if (error) *error = err;
    return b.read("R");
}

// ============================================================== les types ===
void typesIhm() {
    std::printf("-- les types : declares, affectes, NULL, listes, pointeurs\n");
    std::string err;
    check(scriptR("VAR o : Object; END_VAR\nIF o = NULL THEN R := 'nul'; END_IF", &err) == "nul" && err.empty(),
          "un Object neuf vaut NULL [" + err + "]");
    check(scriptR("VAR o : Object; END_VAR\no := Pompe_1;\nR := o.Kind;", &err) == "Rectangle", "o := Pompe_1 ; o.Kind [" + err + "]");
    check(scriptR("VAR o : Object; END_VAR\no := Vue_A.Pompe_2;\nR := TO_STRING(o);", &err) == "Vue_A.Pompe_2", "TO_STRING d'un objet : son chemin [" + err + "]");
    check(scriptR("VAR o : Object; END_VAR\no := 'Pompe_3';\nR := o.View;", &err) == "Vue_A", "un nom vers un Object, sa vue [" + err + "]");
    check(scriptR("VAR o : Object; END_VAR\no := 'Absent';\nR := 'non';", &err) == "#" || !err.empty(), "un nom inconnu vers un Object : refuse");
    check(scriptR("VAR p : POINTER TO Object := GetObject('Pompe_1'); END_VAR\nR := TO_STRING(p^.Width);", &err) == "50",
          "GetObject, p^.Width [" + err + "]");
    check(scriptR("VAR p : POINTER TO Object := GetObject('Absent'); END_VAR\nIF p = NULL THEN R := 'nul'; END_IF", &err) == "nul",
          "GetObject d'un nom absent : NULL [" + err + "]");
    check(scriptR("VAR L : LIST OF Object; END_VAR\nL := ObjectsOf('Pompe_*');\nR := TO_STRING(L.Count);", &err) == "3", "ObjectsOf, LIST OF Object [" + err + "]");
    check(scriptR("VAR L : LIST OF Object; n : INT; END_VAR\nL := ObjectsOf('type:Rectangle');\nFOR EACH x IN L DO n := n + 1; END_FOR;\nR := TO_STRING(n);", &err) == "3",
          "FOR EACH sur une liste d'objets, type: [" + err + "]");
    check(scriptR("VAR a, b : Object; END_VAR\na := Pompe_1; b := Vue_A.Pompe_1;\nIF a = b THEN R := 'meme'; END_IF", &err) == "meme", "deux poignees du meme objet sont egales [" + err + "]");
    check(scriptR("VAR o : Object; END_VAR\no := Pompe_1;\no := NULL;\nIF o = NULL THEN R := 'nul'; END_IF", &err) == "nul", "o := NULL [" + err + "]");
    check(scriptR("VAR L : LIST OF Object; END_VAR\nL := ObjectsOf('calque:Process');\nR := TO_STRING(L.Count);", &err) == "0", "calque: (aucun objet) [" + err + "]");
    check(scriptR("VAR L : LIST OF Object; END_VAR\nL := ObjectsOf('Vue_B.*');\nR := L[0].Name;", &err) == "Cadre", "'Vue_B.*' : une autre vue [" + err + "]");
    (void)scriptR("VAR o : Object; END_VAR\nR := o.Name;", &err);
    check(err.find("NULL") != std::string::npos, "un membre d'un Object NULL : refuse (" + err + ")");
}

// ========================================================== les membres ===
void membres() {
    std::printf("-- les membres : lus et ecrits (la vue montree)\n");
    {
        Bench b("VAR o : Object; END_VAR\no := Pompe_1;\no.Fill := '#FF0000';\no.X := 77;");
        std::string err;
        b.run(&err);
        check(err.empty(), "o.Fill := ... s'ecrit [" + err + "]");
        check(b.read("Vue_A.Pompe_1.Fill") == "#FF0000", "Vue_A.Pompe_1.Fill = #FF0000 (" + b.read("Vue_A.Pompe_1.Fill") + ")");
        check(b.number("Vue_A.Pompe_1.X") == 77, "X = 77");
        const auto* v = b.p.view(b.viewA);
        const auto* o = v ? v->objectByName("Pompe_1") : nullptr;
        check(o && o->text("fill") == "#3A4556", "le projet ne change pas (fill " + (o ? o->text("fill") : std::string("?")) + ")");
    }
    {
        Bench b("Pompe_2.Width := 120;\nR := TO_STRING(Pompe_2.Width);");
        std::string err;
        b.run(&err);
        check(b.read("R") == "120", "un nom court dans un script general : la vue montree (" + b.read("R") + ") [" + err + "]");
    }
    {
        Bench b("VAR L : LIST OF Object; i : INT; END_VAR\nL := ObjectsOf('Pompe_*');\nFOR i := 0 TO L.Count - 1 DO L[i].Visible := FALSE; END_FOR;");
        std::string err;
        b.run(&err);
        check(b.read("Vue_A.Pompe_3.Visible") == "FALSE", "L[i].Visible := FALSE [" + err + "]");
    }
    {
        Bench b("VAR p : POINTER TO Object := GetObject('Titre'); END_VAR\np^.Text := CONCAT('Pompes : ', INT_TO_STRING(ObjectsOf('Pompe_*').Count));");
        std::string err;
        b.run(&err);
        check(b.read("Vue_A.Titre.Text") == "Pompes : 3", "p^.Text := ... (" + b.read("Vue_A.Titre.Text") + ") [" + err + "]");
    }
    {
        Bench b("Pompe_1.Layer := 'Process';\nR := Pompe_1.Layer;");
        std::string err;
        b.run(&err);
        check(b.read("R") == "Process", "Objet.Layer := 'Process' le change de calque (" + b.read("R") + ") [" + err + "]");
    }
    {
        Bench b("VAR L : LIST OF Object; END_VAR\nL := Pompe_1.Children;\nR := TO_STRING(L.Count);");
        std::string err;
        b.run(&err);
        check(b.read("R") == "0", "Children : une liste (vide) [" + err + "]");
    }
}

// ========================================================== les natives ===
void nativesObjets() {
    std::printf("-- les natives des objets\n");
    {
        Bench b("AlignTop(ObjectsOf('Pompe_*'));\nJustifyH(ObjectsOf('Pompe_*'), 16);");
        std::string err;
        b.run(&err);
        check(err.empty(), "AlignTop puis JustifyH [" + err + "]");
        check(b.number("Vue_A.Pompe_2.Y") == 10 && b.number("Vue_A.Pompe_3.Y") == 10, "AlignTop : le haut du plus haut (10)");
        check(b.number("Vue_A.Pompe_2.X") == 76 && b.number("Vue_A.Pompe_3.X") == 142, "JustifyH 16 : 10, 76, 142");
    }
    {
        Bench b("AlignLeft(ObjectsOf('Pompe_*'), Pompe_3);\nSameSize(ObjectsOf('Pompe_*'), Pompe_2);");
        b.run();
        check(b.number("Vue_A.Pompe_1.X") == 200, "AlignLeft sur une reference (Pompe_3 : 200)");
        check(b.number("Vue_A.Pompe_1.Height") == 40 && b.number("Vue_A.Pompe_3.Width") == 50, "SameSize sur Pompe_2 (50 x 40)");
    }
    {
        Bench b("JustifyV(ObjectsOf('Pompe_*'));");       // des ecarts egaux entre le premier et le dernier
        b.run();
        // tries par Y : Pompe_3 (10, h30), Pompe_1 (20, h30), Pompe_2 (60, h40) : de 10 a 100, 100 de tailles -> ecart -5
        check(std::fabs(b.number("Vue_A.Pompe_1.Y") - 35) < 1e-6, "JustifyV sans ecart : des ecarts egaux (" + std::to_string(b.number("Vue_A.Pompe_1.Y")) + ")");
    }
    {
        Bench b("EditObject(Pompe_1.Width, 200);\nCopyObjectTo(Pompe_1, Pompe_2, {Pompe_1.Width});\nCopyObjectTo(Pompe_1, Pompe_3, {});");
        std::string err;
        b.run(&err);
        check(err.empty(), "EditObject, CopyObjectTo [" + err + "]");
        check(b.number("Vue_A.Pompe_2.Width") == 200 && b.number("Vue_A.Pompe_2.X") == 100, "CopyObjectTo {Width} : la largeur seule");
        check(b.number("Vue_A.Pompe_3.X") == 10 && b.number("Vue_A.Pompe_3.Height") == 30, "CopyObjectTo {} : tout sauf le nom");
        check(b.read("Vue_A.Pompe_3.Name") == "Pompe_3", "... le nom reste");
    }
    {
        Bench b("MoveObject(Pompe_1, 400, 300);\nResizeBy(Pompe_1, 10, 10);\nResetObject(Pompe_1);");
        b.run();
        check(b.number("Vue_A.Pompe_1.X") == 10 && b.number("Vue_A.Pompe_1.Width") == 50, "ResetObject : les valeurs de conception");
    }
    {
        Bench b("B := ShowIf(Pompe_1, FALSE);\nHideIf(ObjectsOf('Pompe_2'), FALSE);\nDisable(Pompe_3);\nBlinkIf(Pompe_2, TRUE);");
        std::string err;
        b.run(&err);
        check(b.read("Vue_A.Pompe_1.Visible") == "FALSE" && b.read("B") == "FALSE", "ShowIf(FALSE) cache, rend la condition [" + err + "]");
        check(b.read("Vue_A.Pompe_2.Visible") == "TRUE", "HideIf(FALSE) montre");
        check(b.read("Vue_A.Pompe_3.Enabled") == "FALSE", "Disable : Enabled vaut FALSE");
        check(b.read("Vue_A.Pompe_2.Blink") == "TRUE", "BlinkIf(TRUE) : Blink (meme sans la propriete a la conception)");
    }
    {
        Bench b("BringToFront(Pompe_1);");
        b.run();
        const View* v = b.rt.composedView(b.viewA);
        const auto order = v ? v->paintOrder() : std::vector<const Object*>{};
        check(!order.empty() && order.back() && order.back()->name == "Pompe_1", "BringToFront : dessine en dernier");
        Bench c("SendToBack(Titre);");
        c.run();
        const View* w = c.rt.composedView(c.viewA);
        const auto o2 = w ? w->paintOrder() : std::vector<const Object*>{};
        check(!o2.empty() && o2.front() && o2.front()->name == "Titre", "SendToBack : dessine en premier");
    }
    {
        Bench b("ArrangeGrid(ObjectsOf('Pompe_*'), 2, 8);\nR := TO_STRING(ObjectsIn(0, 0, 90, 90).Count);");
        b.run();
        check(b.number("Vue_A.Pompe_3.Y") == 68 && b.number("Vue_A.Pompe_2.X") == 78, "ArrangeGrid 2 colonnes");
        check(b.read("R") == "3", "ObjectsIn apres la grille (" + b.read("R") + ")");
    }
    {
        std::string err;
        check(scriptR("AlignTop(Pompe_9);", &err) == "#" || !err.empty(), "un objet inconnu : refuse (" + err + ")");
        check(err.find("Pompe_9") != std::string::npos || err.find("declared") != std::string::npos, "... et le message le nomme");
    }
}

// ========================================================== un script de vue, This ===
void vueEtThis() {
    std::printf("-- un script de vue (noms courts), This dans une action\n");
    {
        Bench b("R := 'x';", "Pompe_1.Fill := '#00FF00';\nHide(Titre);");
        check(b.read("Vue_A.Pompe_1.Fill") == "#00FF00" && b.read("Vue_A.Titre.Visible") == "FALSE", "OnOpen : les noms courts de sa vue");
    }
    {
        Bench b("R := 'x';", {}, [](Project& p) {
            View& a = p.views.front();
            Object* o = a.objectByName("Pompe_1");
            Action act;
            act.trigger = Trigger::Click;
            act.operation = Operation::RunScript;
            act.value = "MoveObject(5, 6);";
            o->actions.push_back(act);
        });
        b.rt.press(b.pompe1, 0.3);
        b.rt.release(b.pompe1, 0.35, true);
        b.rt.tick(0.4);
        check(b.number("Vue_A.Pompe_1.X") == 5 && b.number("Vue_A.Pompe_1.Y") == 6, "MoveObject(X, Y) dans une action : This (" +
                                                                                     std::to_string(b.number("Vue_A.Pompe_1.X")) + ")");
    }
}

// ========================================================== les ressources ===
void ressources() {
    std::printf("-- les ressources : Sound, Style, Font, Color, @Alarme\n");
    std::string err;
    check(scriptR("VAR s : POINTER TO Sound := GetSound('Bip court'); END_VAR\nR := s^.Name;", &err) == "Bip court", "GetSound (bibliotheque) [" + err + "]");
    check(scriptR("VAR s : Sound; END_VAR\ns := 'Bip court';\nIF s.Duration > 0.0 THEN R := 'dure'; END_IF", &err) == "dure", "Sound.Duration [" + err + "]");
    check(scriptR("VAR s : POINTER TO Sound := GetSound('Inconnu'); END_VAR\nIF s = NULL THEN R := 'nul'; END_IF", &err) == "nul", "GetSound d'un son absent : NULL");
    check(scriptR("VAR st : POINTER TO Style := GetStyle('ISA-101'); END_VAR\nIF st^.Builtin THEN R := 'fourni'; END_IF", &err) == "fourni", "GetStyle, Builtin [" + err + "]");
    {
        Bench b("ApplyStyle(Pompe_1, GetStyle('ISA-101')^);\nR := Pompe_1.Style;");
        std::string e2;
        b.run(&e2);
        check(b.read("R") == "ISA-101", "ApplyStyle : l'objet cite le style (" + b.read("R") + ") [" + e2 + "]");
    }
    check(scriptR("R := @Alarme;", &err) == "@Alarme", "@Alarme s'ecrit seul : le texte '@Alarme' [" + err + "]");
    check(scriptR("R := Color(@Alarme);", &err).rfind("#", 0) == 0, "Color(@Alarme) : #RRGGBB [" + err + "]");
    check(scriptR("VAR L : LIST OF Style; END_VAR\nL := Styles('ISA*');\nR := TO_STRING(L.Count);", &err) == "1", "Styles('ISA*') [" + err + "]");
    {
        Bench b("Pompe_1.Fill := @Alarme;\nR := Pompe_1.Fill;");
        b.run();
        check(b.read("R") == "@Alarme", "Objet.Fill := @Alarme (la couleur suit le theme au dessin)");
    }
    if (g_fonts.empty()) {
        std::printf("   (pas de dossier de polices : Font saute)\n");
        return;
    }
    const std::string file = g_fonts + "/LiberationSans-Regular.ttf";
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::printf("   (%s introuvable : Font saute)\n", file.c_str());
        return;
    }
    auto data = std::make_shared<Bytes>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto withFont = [&](Project& p) {
        Resource r;
        r.id = p.allocate();
        r.name = "LiberationSans-Regular.ttf";
        r.format = "TTF";
        r.bytes = data->size();
        r.data = data;
        p.assets.resources.push_back(r);
    };
    {
        Bench b("VAR f : POINTER TO Font := GetFont('LiberationSans-Regular'); END_VAR\nR := f^.Family;\nN := TextWidth(f^, 'AB', 20);\nB := HasGlyph(f^, '\xC3\xA9');", {}, withFont);
        std::string e2;
        b.run(&e2);
        check(b.read("R") == "Liberation Sans", "Font.Family (" + b.read("R") + ") [" + e2 + "]");
        check(b.number("N") > 10 && b.number("N") < 40, "TextWidth('AB', 20) (" + std::to_string(b.number("N")) + ")");
        check(b.read("B") == "TRUE", "HasGlyph(e accent)");
        Bench c("VAR f : POINTER TO Font := GetFont('LiberationSans-Regular'); END_VAR\nTitre.Font := f;\nR := Titre.Font;", {}, withFont);
        c.run(&e2);
        check(c.read("R") == "LiberationSans-Regular.ttf", "Objet.Font := un POINTER TO Font : son nom (" + c.read("R") + ") [" + e2 + "]");
    }
}

// ========================================================== fx, Generer, registre, Compiler ===
void ailleurs() {
    std::printf("-- une expression lit sans agir ; Generer ; le registre ; Compiler\n");
    Bench b("R := 'x';");
    {
        const auto e = Expression::compile("ObjectsOf('Pompe_*').Count");
        const auto r = e.valid() ? e.evaluate(b.rt.environment()) : core::Result<sim::Value>(core::fail(core::ErrorCode::InvalidArgument, e.error()));
        check(r && r->asInteger() == 3, "une expression (fx) : ObjectsOf(...).Count = 3");
    }
    {
        const auto e = Expression::compile("AlignTop(ObjectsOf('Pompe_*'))");
        const auto r = e.valid() ? e.evaluate(b.rt.environment()) : core::Result<sim::Value>(core::fail(core::ErrorCode::InvalidArgument, e.error()));
        check(!r, "une expression (fx) n'agit pas : AlignTop refuse");
        check(b.number("Vue_A.Pompe_2.Y") == 60, "... et rien n'a bouge");
    }
    {
        Project p = b.p;
        p.programs.scripts.front().body = "VAR s : POINTER TO Sound := GetSound('Bip cour'); END_VAR\no := GetObject('Pompe_9');";
        const auto issues = generate(p, [](std::string_view) { return false; });
        bool sound = false, object = false;
        for (const auto& i : issues) {
            sound = sound || i.message.find("GetSound('Bip cour')") != std::string::npos;
            object = object || i.message.find("GetObject('Pompe_9')") != std::string::npos;
        }
        check(sound, "Generer : GetSound('Bip cour') rendra NULL");
        check(!object, "Generer : GetObject dans un script general ne se verifie qu'en marche (la vue montree)");
        p.views.front().scripts.push_back(Script{});
        auto& vs = p.views.front().scripts.back();
        vs.id = p.allocate();
        vs.event = "OnOpen";
        vs.body = "x := GetObject('Pompe_9');\ny := GetObject('Pompe_1');";
        const auto again = generate(p, [](std::string_view) { return false; });
        std::size_t n = 0;
        for (const auto& i : again) n += i.message.find("GetObject('Pompe_9')") != std::string::npos;
        check(n == 1, "Generer : GetObject('Pompe_9') dans un script de Vue_A (" + std::to_string(n) + ")");
    }
    {
        const auto& reg = typereg::baseRegistry();
        check(reg.resolve("Object", typereg::UseDeclaration).ok, "le registre : Object dans une declaration");
        check(reg.resolve("LIST OF Font", typereg::UseDeclaration).ok, "le registre : LIST OF Font");
        check(reg.resolve("POINTER TO Sound", typereg::UseDeclaration).ok, "le registre : POINTER TO Sound");
        check(!reg.resolve("Style", typereg::UseVariable).ok, "le registre : pas une variable IHM (pas de place Modbus)");
        const auto* e = reg.byName("Texture");
        check(e && e->category == typereg::Category::Ihm, "le registre : Texture est un type de l'IHM");
    }
    // Compiler comme le build (l'automate connu : un nom inconnu n'est pas a lui) - vu sous Wine : << fonction
    // inconnue : ObjectsOf >> bloquait la simulation, alors que le moteur les connaissait.
    {
        Bench b("R := 'x';");
        Project p = b.p;
        p.programs.scripts.front().body =
            "AlignTop(ObjectsOf('Vue_A.Pompe_*'));\n"
            "JustifyH(ObjectsOf('Vue_A.Pompe_*'), 24);\n"
            "MoveBy(ObjectsOf('Vue_A.Pompe_*'), 0, -80);\n"
            "EditObject(Vue_A.Titre.Text, CONCAT('Pompes : ', INT_TO_STRING(ObjectsOf('Vue_A.Pompe_*').Count)));\n"
            "HideIf({Vue_A.Pompe_1, Vue_A.Pompe_2}, NOT Mode_Auto);\n"
            "N := TextWidth(GetFont('Carlito'), 'Pompe', 12.0);\n"
            "Play(GetSound('Bip court'));\n"
            "R := Color(@Alarme);";
        p.views.front().scripts.push_back(Script{});
        auto& vs = p.views.front().scripts.back();
        vs.id = p.allocate();
        vs.event = "OnOpen";
        vs.body = "VAR\n  L : LIST OF Object;\n  o : POINTER TO Object;\nEND_VAR\nL := ObjectsOf('Pompe_*');\nAlignTop(L);\n"
                  "o := GetObject('Titre');\nIF o <> NULL THEN o^.Text := 'Pompes'; END_IF;\nShowIf(Pompe_1, Mode_Auto);\nBringToFront(Pompe_2);";
        const auto issues = compileWith(p, [](std::string_view) { return false; });
        std::string errors;
        for (const auto& i : issues)
            if (i.severity == Issue::Severity::Error) errors += " | " + i.message;
        check(errors.empty(), "Compiler (comme le build) accepte les fonctions des objets et des ressources" + errors);
    }
    {
        const auto diags = checkScript(ScriptLang::ST, "VAR\n  p : POINTER TO Object;\n  L : LIST OF Texture;\n  s : Sound;\nEND_VAR\nAlignTop(L);");
        std::string errors;
        for (const auto& d : diags)
            if (d.severity == ScriptDiagnostic::Severity::Error) errors += " | " + d.message;
        check(errors.empty(), "Compiler accepte les types de l'IHM" + errors);
    }
    {
        Bench c("R := 'x';", {}, [](Project& p) {
            HmiFunction f;
            f.id = p.allocate();
            f.name = "Show";
            f.returnType = "INT";
            f.body = "Show := 42;";
            p.programs.functions.push_back(f);
        });
        check(!c.rt.isHandleNative("SHOW") && c.rt.isHandleNative("HIDE"), "une fonction du projet nommee Show passe avant la native");
    }
    {
        std::size_t objets = 0, ressources = 0;
        for (const auto& f : natives::functions()) {
            objets += f.category == "objets";
            ressources += f.category == "ressources";
        }
        check(objets == 38 && ressources == 16, "le catalogue : 38 fonctions d'objets, 16 de ressources (" + std::to_string(objets) + ", " +
                                                     std::to_string(ressources) + ")");
        check(Runtime::handleNativeNames().size() == 54, "le moteur connait les 54");
        for (const auto& f : natives::functions())
            if (f.category == "objets" || f.category == "ressources") {
                std::string u(f.name);
                for (auto& ch : u) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                const auto& names = Runtime::handleNativeNames();
                check(std::find(names.begin(), names.end(), u) != names.end(), "le moteur fait " + std::string(f.name));
            }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) g_fonts = argv[1];
    typesIhm();
    membres();
    nativesObjets();
    vueEtThis();
    ressources();
    ailleurs();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
