// =============================================================================
//  tools/sessions/preparer-projet-11121.cpp - 1.11.21 : le projet des captures de la
//  1.11.21 (session-11121-editeurs-explorateurs.txt), sur une copie d'Armoire_Gaz deja
//  preparee par preparer-projet-11120 (Random, les trois Convertir, Essai_Signatures) :
//    - le script Essai_Explorateur (appele) : une constante Seuil_Haut (documentee), deux
//      variables (Compteur, conservee ; Ecart_Max), une fonction interne Ecart(a, b) et sa
//      locale - ce que l'arbre deplie ;
//    - la variable IHM Ecart_Releve (REAL), qu'il ecrit.
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-11121.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-11121
//    /tmp/preparer-11121 <dossier du projet>
// =============================================================================
#include "hmi/HmiModel.hpp"
#include "hmi/HmiStore.hpp"

#include <cstdio>
#include <string>

using namespace hmi;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage : preparer-11121 <dossier du projet>\n");
        return 2;
    }
    const std::string folder = argv[1];
    auto loaded = load(folder);
    if (!loaded) {
        std::fprintf(stderr, "projet illisible : %s\n", folder.c_str());
        return 1;
    }
    Project p = std::move(*loaded);
    if (!p.variable("Ecart_Releve")) {
        Variable v;
        v.id = p.allocate();
        v.name = "Ecart_Releve";
        v.type = "REAL";
        v.description = "L'ecart calcule par Essai_Explorateur";
        p.programs.variables.push_back(v);
    }
    if (!p.generalScript("Essai_Explorateur")) {
        const auto decl = [&p](DeclKind kind, const char* name, const char* type, const char* value, const char* text,
                               Storage storage = Storage::Execution) {
            Declaration d;
            d.id = p.allocate();
            d.kind = kind;
            d.name = name;
            d.type = type;
            d.value = value;
            d.description = text;
            d.storage = storage;
            return d;
        };
        Script sc;
        sc.id = p.allocate();
        sc.name = "Essai_Explorateur";
        sc.event = "Appel";
        sc.description = "Ce que l'arbre deplie : une constante, des variables, une fonction interne";
        sc.decls = {decl(DeclKind::Constant, "Seuil_Haut", "REAL", "80.0", "le seuil d'alarme (bar)"),
                    decl(DeclKind::Variable, "Compteur", "INT", "0", "les appels", Storage::Kept),
                    decl(DeclKind::Variable, "Ecart_Max", "REAL", "", "le plus grand ecart vu")};
        sc.body = "FUNCTION Ecart(a : REAL; b : REAL) : REAL\n"
                  "    VAR d : REAL; END_VAR\n"
                  "    d := a - b;\n"
                  "    IF d < 0.0 THEN d := -d; END_IF;\n"
                  "    Ecart := d;\n"
                  "END_FUNCTION\n"
                  "Compteur := Compteur + 1;\n"
                  "Ecart_Max := Ecart(Seuil_Haut, 75.0);\n"
                  "Ecart_Releve := Ecart_Max;\n";
        p.programs.scripts.push_back(sc);
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt (1.11.21) : %s\n", folder.c_str());
    return 0;
}
