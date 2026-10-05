// =============================================================================
//  tools/sessions/preparer-projet-1114.cpp - 1.11.4 : le projet des captures de
//  la geometrie calculee en marche (session-1114-geometrie.txt).
// -----------------------------------------------------------------------------
//  A lancer APRES preparer-projet-1113 (STEST, Vue_STEST, STEST_1, UINTS, gCoef) :
//    - STEST_1 : X = $UINTS[0]$ (la capture du client), Y = gCoef * 100 ;
//    - STEST_2 : une autre instance, tournee de gAngle (30) et retournee (gMiroir) ;
//    - Groupe_geo : deux rectangles groupes, X = 1300 + gDecal (150), rotation gAngle ;
//    - les variables IHM gAngle (REAL), gMiroir (BOOL), gDecal (INT).
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-1114.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-1114
//    /tmp/preparer-1114 <dossier du projet>
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
        std::fprintf(stderr, "usage : preparer-1114 <dossier du projet>\n");
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
    var("gAngle", "REAL", "30.0", "L'angle des objets tournes");
    var("gMiroir", "BOOL", "TRUE", "Les objets retournes");
    var("gDecal", "INT", "150", "Le decalage du groupe");
    View* v = p.viewByName("Vue_STEST");
    if (!v) {
        std::fprintf(stderr, "lancer d'abord preparer-projet-1113\n");
        return 1;
    }
    if (auto* o = v->objectByName("STEST_1")) {
        o->setExpr("x", "$UINTS[0]$");
        o->setExpr("y", "gCoef * 100");
    }
    if (!v->objectByName("STEST_2")) {
        const Id i = placeSymbol(p, *v, "STEST", 900, 300);
        if (auto* o = v->object(i)) {
            o->name = "STEST_2";
            o->set("params", "Name := 'Retourne'; Value := UINTS");
            o->setExpr("rot", "gAngle");
            o->setExpr("flipH", "gMiroir");
        }
    }
    if (!v->objectByName("Groupe_geo")) {
        const Id a = edit::add(p, *v, Kind::Rectangle, 1300, 600);
        const Id b = edit::add(p, *v, Kind::Ellipse, 1420, 600);
        if (auto* o = v->object(a)) { o->name = "Rect_A"; o->set("fill", "#2F6FD6"); }
        if (auto* o = v->object(b)) { o->name = "Rond_B"; o->set("fill", "#D7A824"); }
        const Id g = edit::group(p, *v, {a, b});
        if (auto* o = v->object(g)) {
            o->name = "Groupe_geo";
            o->setExpr("x", "1300 + gDecal");
            o->setExpr("rot", "gAngle");
        }
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt (1.11.4) : %s\n", folder.c_str());
    return 0;
}
