// =============================================================================
//  tools/sessions/preparer-projet-11111.cpp - 1.11.11 : le projet des captures de la
//  1.11.11 (session-11111-fonctions-expressions.txt), apres preparer-projet-11110 :
//  comme la capture du client, une fonction GetActiveCount(n : INT) : INT dans le
//  symbole S_Vanne, et des expressions qui l'appellent :
//    - dans le symbole : le texte Txt_Score, =GetActiveCount(30) ;
//    - dans la vue Vue_Vannes : le texte Txt_Compte, =Vanne_4.GetActiveCount(30).
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-11111.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-11111
//    /tmp/preparer-11111 <dossier du projet>
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
        std::fprintf(stderr, "usage : preparer-11111 <dossier du projet>\n");
        return 2;
    }
    const std::string folder = argv[1];
    auto loaded = load(folder);
    if (!loaded) {
        std::fprintf(stderr, "projet illisible : %s\n", folder.c_str());
        return 1;
    }
    Project p = std::move(*loaded);
    View* s = p.viewByName("S_Vanne");
    View* v = p.viewByName("Vue_Vannes");
    if (!s || !v) {
        std::fprintf(stderr, "S_Vanne ou Vue_Vannes absent : lancer preparer-projet-11110 d'abord\n");
        return 1;
    }
    if (!symbolFunction(*s, "GetActiveCount")) {
        HmiFunction f;
        f.id = p.allocate();
        f.name = "GetActiveCount";
        f.returnType = "INT";
        f.description = "Le compte : n, plus la position de la vanne";
        f.body = "VAR_INPUT\n    n : INT;\nEND_VAR\nGetActiveCount := n + Vanne.POSITION;";
        s->functions.push_back(f);
        s->height = 180;
        const Id t = edit::add(p, *s, Kind::Text, 10, 150);
        if (auto* o = s->object(t)) {
            o->setBox({10, 150, 180, 26});
            o->name = "Txt_Score";
            o->setExpr("text", "GetActiveCount(30)");
        }
    }
    if (!v->objectByName("Txt_Compte")) {
        const Id t = edit::add(p, *v, Kind::Text, 40, 360);
        if (auto* o = v->object(t)) {
            o->setBox({40, 360, 600, 30});
            o->name = "Txt_Compte";
            o->setExpr("text", "Vanne_4.GetActiveCount(30)");
        }
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt (1.11.11) : %s\n", folder.c_str());
    return 0;
}
