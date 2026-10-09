// =============================================================================
//  tools/sessions/preparer-projet-11124.cpp - 1.11.24 : le projet des captures de la
//  1.11.24 (session-11124-reperes.txt), sur une copie d'Armoire_Gaz deja preparee par
//  preparer-projet-11120, -11121 et -11123. Le cas de la capture du client (saisie :
//  variable inconnue (V[0]).IN_Percent), et ce qu'un symbole herite :
//    - le type IHM T_Cuve (IN_Percent, Niveau : REAL ; Consigne : INT ; Nom : STRING),
//      la variable IHM Cuves : ARRAY[0..3] OF T_Cuve (V est deja pris : des vannes), Idx (INT = 2) ;
//    - S_Mini (M : T_Cuve) : un champ M.Consigne, un texte ;
//    - S_Cuve (Cuve : T_Cuve ; Titre : STRING ; Haut : REAL) : un texte, InputField_1
//      (Cuve.IN_Percent, de 0 a Haut), Btn_Plus (Cuve.Consigne + 1), Mini (S_Mini avec
//      M := $Cuve$ : le repere herite) ;
//    - la vue Vue_Reperes : Symbole_1 (Cuve := $Cuves[0]$ ; Titre := '$Nom$'), Symbole_2
//      (Cuve := $Cuves[1]$), Symbole_3 (Cuve := Cuves[$Idx$] : un indice calcule).
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-11124.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-11124
//    /tmp/preparer-11124 <dossier du projet>
// =============================================================================
#include "hmi/HmiEdit.hpp"
#include "hmi/HmiModel.hpp"
#include "hmi/HmiStore.hpp"
#include "hmi/HmiSymbols.hpp"

#include <cstdio>
#include <string>

using namespace hmi;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage : preparer-11124 <dossier du projet>\n");
        return 2;
    }
    const std::string folder = argv[1];
    auto loaded = load(folder);
    if (!loaded) {
        std::fprintf(stderr, "projet illisible : %s\n", folder.c_str());
        return 1;
    }
    Project p = std::move(*loaded);
    if (p.viewByName("Vue_Reperes")) {
        std::printf("deja pret : %s\n", folder.c_str());
        return 0;
    }
    HmiType cuve;
    cuve.id = p.allocate();
    cuve.name = "T_Cuve";
    cuve.description = "Une cuve (1.11.24 : les reperes dans les symboles)";
    cuve.members = {{"IN_Percent", "REAL", "", "Le remplissage saisi (%)"}, {"Niveau", "REAL", "", ""},
                    {"Consigne", "INT", "", ""}, {"Nom", "STRING", "", ""}};
    p.programs.types.push_back(cuve);
    const auto var = [&p](const char* name, const char* type, const char* initial, const char* text) {
        Variable v;
        v.id = p.allocate();
        v.name = name;
        v.type = type;
        v.initial = initial;
        v.description = text;
        p.programs.variables.push_back(v);
    };
    var("Cuves", "ARRAY[0..3] OF T_Cuve", "", "Les cuves (1.11.24)");
    var("Idx", "INT", "2", "L'indice de la cuve de Symbole_3");
    const auto symbolView = [&p](const char* name, double w, double h) -> View& {
        View v = makeView(p, name);
        v.role = "symbole";
        v.width = w;
        v.height = h;
        p.views.push_back(v);
        return p.views.back();
    };
    const auto add = [&p](View& v, Kind k, const char* name, Box box) -> Object& {
        const Id id = edit::add(p, v, k, box.x, box.y);
        Object* o = v.object(id);
        o->setBox(box);
        o->name = name;
        return *o;
    };
    {
        View& mini = symbolView("S_Mini", 300, 70);
        mini.params.push_back({"M", "", "La cuve", "T_Cuve", ParamMode::Reference});
        auto& f = add(mini, Kind::InputField, "Saisie_M", {0, 0, 140, 34});
        f.set("variable", "M.Consigne");
        f.set("mode", "numerique");
        auto& t = add(mini, Kind::Text, "Txt_M", {150, 4, 150, 28});
        t.set("text", "consigne {M.Consigne}");
    }
    {
        View& sym = symbolView("S_Cuve", 420, 200);
        sym.params.push_back({"Cuve", "", "La cuve montree", "T_Cuve", ParamMode::Reference});
        sym.params.push_back({"Titre", "'Cuve'", "Son titre", "STRING", ParamMode::Reference});
        sym.params.push_back({"Haut", "100.0", "La borne haute de la saisie", "REAL", ParamMode::Reference});
        auto& t = add(sym, Kind::Text, "Txt", {0, 0, 420, 30});
        t.set("text", "{Titre} : {Cuve.IN_Percent:0.0} % \xC2\xB7 consigne {Cuve.Consigne}");
        auto& f = add(sym, Kind::InputField, "InputField_1", {0, 40, 160, 36});
        f.set("variable", "Cuve.IN_Percent");
        f.set("mode", "numerique");
        f.set("min", "0");
        f.set("max", "Haut");
        auto& b = add(sym, Kind::Button, "Btn_Plus", {180, 40, 120, 36});
        b.set("text", "Consigne + 1");
        Action plus;
        plus.trigger = Trigger::Click;
        plus.operation = Operation::Increment;
        plus.target = "Cuve.Consigne";
        plus.value = "1";
        b.actions.push_back(plus);
        const Id mini = placeSymbol(p, sym, "S_Mini", 0, 100);
        sym.object(mini)->name = "Mini";
        sym.object(mini)->set("params", "M := $Cuve$");
    }
    {
        View v = makeView(p, "Vue_Reperes");
        v.width = 1280;
        v.height = 800;
        v.description = "1.11.24 : des symboles aux arguments a reperes";
        p.views.push_back(v);
        View& vr = p.views.back();
        const auto inst = [&](const char* name, const char* params, double x, double y) {
            const Id id = placeSymbol(p, vr, "S_Cuve", x, y);
            vr.object(id)->name = name;
            vr.object(id)->set("params", params);
        };
        auto& titre = add(vr, Kind::Text, "Txt_Titre", {40, 20, 900, 36});
        titre.set("text", "Des symboles aux arguments \xC3\xA0 rep\xC3\xA8res (1.11.24)");
        inst("Symbole_1", "Cuve := $Cuves[0]$; Titre := '$Nom$'; Haut := 80.0", 40, 80);
        inst("Symbole_2", "Cuve := $Cuves[1]$; Titre := 'Cuve 2'; Haut := 80.0", 40, 320);
        inst("Symbole_3", "Cuve := Cuves[$Idx$]; Titre := 'Cuve Idx'", 40, 560);
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt (1.11.24) : %s\n", folder.c_str());
    return 0;
}
