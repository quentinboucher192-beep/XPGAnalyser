// =============================================================================
//  tools/sessions/preparer-projet-1115.cpp - 1.11.5 : le projet des captures des
//  esclaves simules en arbre et des onglets Variables IHM / Variables API
//  (session-1115-esclaves-arbre.txt).
// -----------------------------------------------------------------------------
//  Sur une copie d'Armoire_Gaz (celle des sessions, ou celle de la 1.11.4) :
//    - les types IHM T_Vanne et T_Four, s'ils manquent (Armoire_Gaz les a deja :
//      T_Four = Temperature, Consigne, Marche, Defaut, Vannes : ARRAY[1..3] OF T_Vanne) ;
//    - Four1 (T_Four), s'il manque (Armoire_Gaz l'a deja, lu sur la Balance B) :
//      ses valeurs se rangent sous Four1 > Vannes > [1] dans les valeurs simulees ;
//    - Four2 (T_Four), locale : l'arbre de l'onglet Variables IHM.
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-1115.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-1115
//    /tmp/preparer-1115 <dossier du projet>
// =============================================================================
#include "hmi/HmiModel.hpp"
#include "hmi/HmiStore.hpp"

#include <cstdio>
#include <string>

using namespace hmi;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage : preparer-1115 <dossier du projet>\n");
        return 2;
    }
    const std::string folder = argv[1];
    auto loaded = load(folder);
    if (!loaded) {
        std::fprintf(stderr, "projet illisible : %s\n", folder.c_str());
        return 1;
    }
    Project p = std::move(*loaded);
    const auto type = [&p](const char* name, const char* text, std::vector<TypeMember> members) {
        for (const auto& t : p.programs.types)
            if (t.name == name) return;
        HmiType t;
        t.id = p.allocate();
        t.name = name;
        t.description = text;
        t.members = std::move(members);
        p.programs.types.push_back(std::move(t));
    };
    type("T_Vanne", "Une vanne du four", {{"Ouverte", "BOOL", "", "Fin de course ouverte"}, {"Position", "INT", "", "Position (%)"}});
    type("T_Four", "Un four de l'armoire",
         {{"Temperature", "REAL", "", "Temperature (degres C)"},
          {"Pression", "REAL", "", "Pression (mbar)"},
          {"Vannes", "ARRAY[1..2] OF T_Vanne", "", "Les vannes d'entree"}});
    const auto var = [&p](const char* name, const char* t, const char* equipment, const char* address, const char* text) {
        if (p.variable(name)) return;
        Variable v;
        v.id = p.allocate();
        v.name = name;
        v.type = t;
        v.description = text;
        v.equipment = equipment;
        v.address = address;
        p.programs.variables.push_back(v);
    };
    var("Four1", "T_Four", "Centrale PM5560", "%MW3030", "Le four 1, lu sur la centrale");
    var("Four2", "T_Four", "", "", "Le four 2, local a l'IHM");
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt (1.11.5) : %s\n", folder.c_str());
    return 0;
}
