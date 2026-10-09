// =============================================================================
//  tools/sessions/preparer-projet-11120.cpp - 1.11.20 : le projet des captures de la
//  1.11.20 (session-11120-signatures.txt), sur une copie d'Armoire_Gaz :
//    - les variables IHM Graine (REAL := 1.0), Tirage, Resultat_Tirage (REAL),
//      Texte_Entier, Texte_Reel (STRING), Code_Hexa (INT) ;
//    - la fonction Random de la capture du 09/10 : Min, Max (entrees), RandomSeed
//      (E/S, VAR_IN_OUT), test (sortie, VAR_OUTPUT), dans ses onglets ;
//    - trois surcharges de Convertir : (valeur : INT), (valeur : REAL),
//      (texte : STRING; base : INT) ;
//    - le script general Essai_Signatures (au demarrage) qui les appelle et dit ce
//      que chaque appel a pris (IHM_LOG).
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-11120.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-11120
//    /tmp/preparer-11120 <dossier ihm du projet>
// =============================================================================
#include "hmi/HmiModel.hpp"
#include "hmi/HmiStore.hpp"

#include <cstdio>
#include <string>

using namespace hmi;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage : preparer-11120 <dossier ihm du projet>\n");
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
    var("Graine", "REAL", "1.0", "La graine du tirage (E/S de Random)");
    var("Tirage", "REAL", "", "Le tirage (sortie de Random)");
    var("Resultat_Tirage", "REAL", "", "Ce que Random rend");
    var("Texte_Entier", "STRING", "", "Convertir(5)");
    var("Texte_Reel", "STRING", "", "Convertir(2.5)");
    var("Code_Hexa", "INT", "", "Convertir('FF', 16)");
    const auto param = [&p](const char* name, const char* type, PassMode mode) {
        Declaration d;
        d.id = p.allocate();
        d.kind = DeclKind::Parameter;
        d.name = name;
        d.type = type;
        d.mode = mode;
        return d;
    };
    if (!p.functionByName("Random")) {
        HmiFunction f;
        f.id = p.allocate();
        f.name = "Random";
        f.returnType = "REAL";
        f.description = "Un tirage entre Min et Max ; la graine avance (E/S), le tirage sort aussi (sortie)";
        f.decls = {param("Min", "REAL", PassMode::In), param("Max", "REAL", PassMode::In), param("RandomSeed", "REAL", PassMode::InOut),
                   param("test", "REAL", PassMode::Out)};
        f.body = "RandomSeed := RandomSeed * 1.5 + 0.25;\ntest := Min + (Max - Min) * 0.5;\nRandom := test;";
        p.programs.functions.push_back(f);
    }
    if (p.functionsNamed("Convertir").empty()) {
        const auto conv = [&](const char* ret, std::vector<Declaration> decls, const char* body, const char* text) {
            HmiFunction f;
            f.id = p.allocate();
            f.name = "Convertir";
            f.returnType = ret;
            f.description = text;
            f.decls = std::move(decls);
            f.body = body;
            p.programs.functions.push_back(f);
        };
        conv("STRING", {param("valeur", "INT", PassMode::In)}, "Convertir := 'entier';", "Un entier en texte");
        conv("STRING", {param("valeur", "REAL", PassMode::In)}, "Convertir := 'reel';", "Un reel en texte");
        conv("INT", {param("texte", "STRING", PassMode::In), param("base", "INT", PassMode::In)}, "Convertir := base;",
             "Un texte dans une base (ici : la base)");
    }
    if (!p.generalScript("Essai_Signatures")) {
        Script sc;
        sc.id = p.allocate();
        sc.name = "Essai_Signatures";
        sc.event = "Demarrage";
        sc.description = "Les surcharges et les E/S, appelees au demarrage";
        sc.body = "(* 1.11.20 : chaque appel prend sa surcharge ; Random ecrit Graine (E/S) et Tirage (sortie) *)\n"
                  "Resultat_Tirage := Random(0.0, 10.0, Graine, Tirage);\n"
                  "Texte_Entier := Convertir(5);\n"
                  "Texte_Reel := Convertir(2.5);\n"
                  "Code_Hexa := Convertir('FF', 16);\n"
                  "IHM_LOG(INFO, 'Signatures 1.11.20 - Convertir(5) = {Texte_Entier}, Convertir(2.5) = {Texte_Reel}, Convertir(FF, 16) = {Code_Hexa}');\n"
                  "IHM_LOG(INFO, 'Signatures 1.11.20 - Random(0.0, 10.0, Graine, Tirage) : Graine {Graine}, Tirage {Tirage}, rendu {Resultat_Tirage}');";
        p.programs.scripts.push_back(sc);
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt (1.11.20) : %s\n", folder.c_str());
    return 0;
}
