// =============================================================================
//  tools/sessions/preparer-projet-1113.cpp - 1.11.3 : le projet des captures du
//  carre de legende et du selecteur de valeur (session-1113-carre-selecteur.txt).
// -----------------------------------------------------------------------------
//  Ajoute a l'IHM d'une copie du projet (ex. bac/Armoire_Gaz) ce que montrent
//  les captures du client : le symbole STEST (Name : STRING, Value : ARRAY[0..9]
//  OF UINT), la vue Vue_STEST et son instance STEST_1 (Name := 'Voiture' ; Value
//  := UINTS), les variables IHM UINTS, AUTRES, gCoef, gNom, et les scripts de
//  la popup Popup_vanne (OnOpen, OnCycle).
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-1113.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-1113
//    /tmp/preparer-1113 <dossier du projet>
// =============================================================================
#include "hmi/HmiEdit.hpp"
#include "hmi/HmiModel.hpp"
#include "hmi/HmiStore.hpp"

#include <cstdio>
#include <string>

using namespace hmi;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage : preparer-1113 <dossier du projet>\n");
        return 2;
    }
    const std::string folder = argv[1];
    auto loaded = load(folder);
    Project p = loaded ? std::move(*loaded) : Project{};
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
    var("UINTS", "ARRAY[0..9] OF UINT", "0", "Tableau de travail de l'IHM");
    var("AUTRES", "ARRAY[0..9] OF UINT", "0", "Un autre tableau du meme type");
    var("gCoef", "REAL", "1.0", "Coefficient d'affichage");
    var("gNom", "STRING", "'Site Nord'", "Le nom du site");
    var("gAlarme", "BOOL", "FALSE", "Une alarme est active");

    const auto view = [&p](const char* name, const char* role) -> View& {
        if (View* v = p.viewByName(name)) return *v;
        View v;
        v.id = p.allocate();
        v.name = name;
        v.role = role;
        v.width = std::string(role) == "symbole" ? 240 : std::string(role) == "popup" ? 640 : 1920;
        v.height = std::string(role) == "symbole" ? 120 : std::string(role) == "popup" ? 400 : 1080;
        Layer l;
        l.id = p.allocate();
        l.name = "Calque 1";
        v.layers.push_back(l);
        v.activeLayer = l.id;
        p.views.push_back(v);
        return p.views.back();
    };
    {
        View& s = view("STEST", "symbole");
        if (s.params.empty()) {
            s.params.push_back({"Name", "'Sans nom'", "Le nom affich\xC3\xA9", "STRING", ParamMode::Reference});
            s.params.push_back({"Value", "", "", "ARRAY[0..9] OF UINT", ParamMode::Reference});
            const Id t = edit::add(p, s, Kind::Text, 10, 10);
            if (auto* o = s.object(t)) o->set("text", "{Name} : {Value[0]}");
        }
    }
    {
        View& v = view("Vue_STEST", "vue");
        if (!v.objectByName("STEST_1")) {
            const Id inst = edit::add(p, v, Kind::SymbolInstance, 200, 200);
            if (auto* o = v.object(inst)) {
                o->name = "STEST_1";
                o->set("symbol", "STEST");
                o->set("params", "Name := 'Voiture'; Value := UINTS");
            }
        }
    }
    {
        View& pop = view("Popup_vanne", "popup");
        if (pop.scripts.empty()) {
            Script a;
            a.id = p.allocate();
            a.name = "Popup_vanne.OnOpen";
            a.event = "OnOpen";
            a.body = "(* A l'ouverture : le titre *)\ngNom := 'Vanne V1';\n";
            pop.scripts.push_back(a);
            Script c;
            c.id = p.allocate();
            c.name = "Popup_vanne.OnCycle";
            c.event = "OnCycle";
            c.body = "gAlarme := UINTS[0] > 100;\n";
            pop.scripts.push_back(c);
        }
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt : %s\n", folder.c_str());
    return 0;
}
