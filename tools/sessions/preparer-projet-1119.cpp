// =============================================================================
//  tools/sessions/preparer-projet-1119.cpp - 1.11.9 : le projet des captures de la
//  1.11.9 (session-1119-actions.txt) - les actions Maths, Clavier virtuel et
//  Executer un script.
//    - les variables IHM M (12,5), C (10), Sortie, Code, Compteur ;
//    - la vue Vue_Actions : Btn_Calcul (Maths : Sortie := (Mesure - Consigne) * 2,
//      Mesure := M, Consigne := C), Btn_Code (Clavier virtuel sur Code : numerique,
//      0 a 9999, un titre), Btn_Compter (Executer un script) et leurs valeurs ;
//    - la popup Pop_Reglage et son parametre Four (T_Four, Reference) : Btn_Remise
//      (Executer un script qui ecrit Four.Consigne - la reference Four).
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-1119.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-1119
//    /tmp/preparer-1119 <dossier du projet>
// =============================================================================
#include "hmi/HmiActionKinds.hpp"
#include "hmi/HmiEdit.hpp"
#include "hmi/HmiModel.hpp"
#include "hmi/HmiStore.hpp"

#include <cstdio>
#include <string>

using namespace hmi;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage : preparer-1119 <dossier du projet>\n");
        return 2;
    }
    const std::string folder = argv[1];
    auto loaded = load(folder);
    if (!loaded) {
        std::fprintf(stderr, "projet illisible : %s\n", folder.c_str());
        return 1;
    }
    Project p = std::move(*loaded);
    const auto var = [&p](const char* name, const char* type, const char* initial, const char* text) {
        if (p.variable(name)) return;
        Variable v;
        v.id = p.allocate();
        v.name = name;
        v.type = type;
        v.initial = initial;
        v.description = text;
        p.programs.variables.push_back(v);
    };
    var("M", "REAL", "12.5", "La mesure");
    var("C", "REAL", "10", "La consigne");
    var("Sortie", "REAL", "0", "Le r\xC3\xA9sultat de Maths");
    var("Code", "INT", "0", "Le code tap\xC3\xA9 au clavier virtuel");
    var("Compteur", "INT", "0", "Compt\xC3\xA9 par un script");
    const auto view = [&p](const char* name, const char* role, double w, double h) -> View& {
        if (View* v = p.viewByName(name)) return *v;
        View v;
        v.id = p.allocate();
        v.name = name;
        v.role = role;
        v.width = w;
        v.height = h;
        Layer l;
        l.id = p.allocate();
        l.name = "Calque 1";
        v.layers.push_back(l);
        v.activeLayer = l.id;
        p.views.push_back(v);
        return p.views.back();
    };
    const auto button = [&p](View& v, const char* name, const char* text, Box box, Action a) {
        const Id id = edit::add(p, v, Kind::Button, box.x, box.y);
        if (auto* o = v.object(id)) {
            o->setBox(box);
            o->name = name;
            o->set("text", text);
            o->actions.push_back(std::move(a));
        }
    };
    const auto label = [&p](View& v, const char* name, const char* text, Box box) {
        const Id id = edit::add(p, v, Kind::Text, box.x, box.y);
        if (auto* o = v.object(id)) {
            o->setBox(box);
            o->name = name;
            o->set("text", text);
        }
    };
    {
        View& v = view("Vue_Actions", "vue", 640, 360);
        if (!v.objectByName("Btn_Calcul")) {
            Action maths;
            maths.trigger = Trigger::Click;
            maths.operation = Operation::Maths;
            maths.target = "Sortie";
            maths.value = "(Mesure - Consigne) * 2";
            maths.params = "Mesure := M; Consigne := C";
            button(v, "Btn_Calcul", "Calculer", {20, 30, 180, 60}, maths);
            label(v, "Txt_Sortie", "Sortie = {Sortie:0.0}", {20, 110, 200, 30});
            Action clavier;
            clavier.trigger = Trigger::Click;
            clavier.operation = Operation::Keyboard;
            clavier.target = "Code";
            actionkinds::KeyboardSpec k;
            k.title = "Code du badge";
            k.keyboard = "numerique";
            k.min = "0";
            k.max = "9999";
            actionkinds::setKeyboardSpec(clavier, k);
            button(v, "Btn_Code", "Saisir le code", {230, 30, 180, 60}, clavier);
            label(v, "Txt_Code", "Code = {Code}", {230, 110, 200, 30});
            Action script;
            script.trigger = Trigger::Click;
            script.operation = Operation::RunScript;
            script.value = "Compteur := Compteur + 1;";
            button(v, "Btn_Compter", "Compter", {440, 30, 180, 60}, script);
            label(v, "Txt_Compteur", "Compteur = {Compteur}", {440, 110, 200, 30});
        }
    }
    {
        View& pop = view("Pop_Reglage", "popup", 420, 200);
        if (pop.params.empty()) {
            pop.params.push_back({"Four", "", "Le four r\xC3\xA9gl\xC3\xA9", "T_Four", ParamMode::Reference});
            Action remise;
            remise.trigger = Trigger::Click;
            remise.operation = Operation::RunScript;
            remise.value = "Four.Consigne := 850;";
            button(pop, "Btn_Remise", "Remettre la consigne", {20, 60, 240, 60}, remise);
        }
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt (1.11.9) : %s\n", folder.c_str());
    return 0;
}
