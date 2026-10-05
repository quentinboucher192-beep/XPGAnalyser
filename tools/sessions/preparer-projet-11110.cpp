// =============================================================================
//  tools/sessions/preparer-projet-11110.cpp - 1.11.10 : le projet des captures de la
//  1.11.10 (session-11110-fonctions-symbole.txt), apres preparer-projet-1118 (le type
//  Vanne, V : ARRAY[0..63] OF Vanne sur l'Esclave virtuel 1) :
//    - les variables IHM Ouvertures (INT) et Purge_OK (BOOL) ;
//    - la fonction IHM Journaliser(Texte) ;
//    - le symbole S_Vanne : ses parametres Vanne (Vanne) et Nom (STRING), un dessin
//      (cadre, nom, position, bouton Detail), trois fonctions - Ouvrir et Fermer
//      (virtuelles), Etat : INT - et sa popup Pop_Vanne (nom, position, etat, Ouvrir,
//      Fermer) ;
//    - la vue Vue_Vannes : Vanne_3 (V[3], Ouvrir redefinie : la purge d'abord, puis
//      SUPER.Ouvrir) et Vanne_4 (V[4]) ; un bouton qui appelle Vanne_4.Ouvrir ;
//    - le script general Sequence_Matin (Vue_Vannes.Vanne_3.Ouvrir...) ;
//    - le type T_VEC et deux operateurs (* emploie + et la fonction Bonus).
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-11110.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-11110
//    /tmp/preparer-11110 <dossier du projet>
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
        std::fprintf(stderr, "usage : preparer-11110 <dossier du projet>\n");
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
    // L'Esclave virtuel 1 (preparer-projet-1118) : seulement simule, comme chez le client.
    for (auto& e : p.equipments)
        if (e.name == "Esclave virtuel 1") e.simulated = true;
    var("Ouvertures", "INT", "0", "Le nombre d'ouvertures de vannes");
    var("Purge_OK", "BOOL", "FALSE", "La purge de la ligne gaz est faite");
    var("V0", "T_VEC", "", "Un vecteur");
    var("R", "T_VEC", "", "Un vecteur calcul\xC3\xA9");
    if (!p.functionByName("Journaliser")) {
        HmiFunction f;
        f.id = p.allocate();
        f.name = "Journaliser";
        f.description = "\xC3\x89" "crit une ligne au journal";
        f.body = "VAR_INPUT\n    Texte : STRING;\nEND_VAR\nIHM_JOURNAL(Texte);";
        p.programs.functions.push_back(f);
    }
    if (!p.functionByName("Bonus")) {
        HmiFunction f;
        f.id = p.allocate();
        f.name = "Bonus";
        f.returnType = "REAL";
        f.body = "VAR_INPUT\n    x : REAL;\nEND_VAR\nBonus := x + 0.5;";
        p.programs.functions.push_back(f);
    }
    // Le type T_VEC et ses operateurs : * emploie + (un operateur) et Bonus (une fonction).
    if (!p.hmiTypeByName("T_VEC")) {
        HmiType t;
        t.id = p.allocate();
        t.name = "T_VEC";
        t.description = "Un vecteur";
        t.members = {{"X", "REAL", "", ""}, {"Y", "REAL", "", ""}};
        HmiOperator plus;
        plus.id = p.allocate();
        plus.op = "+";
        plus.left = "T_VEC";
        plus.right = "T_VEC";
        plus.result = "T_VEC";
        plus.body = "Resultat.X := a.X + b.X;\nResultat.Y := a.Y + b.Y;";
        HmiOperator fois;
        fois.id = p.allocate();
        fois.op = "*";
        fois.left = "T_VEC";
        fois.right = "REAL";
        fois.result = "T_VEC";
        fois.body = "(* a * b : un vecteur fois un nombre, en s'appuyant sur + et sur une fonction *)\n"
                    "IF b = 2.0 THEN\n    Resultat := a + a;          (* l'op\xC3\xA9rateur T_VEC + T_VEC *)\n"
                    "ELSE\n    Resultat.X := a.X * b;\n    Resultat.Y := a.Y * b;\nEND_IF;\n"
                    "Resultat.X := Bonus(Resultat.X);  (* une fonction IHM *)";
        t.operators = {plus, fois};
        p.programs.types.push_back(t);
    }
    // ---- le symbole S_Vanne
    if (!p.viewByName("S_Vanne")) {
        View s = makeView(p, "S_Vanne");
        s.role = "symbole";
        s.width = 200;
        s.height = 150;
        s.params.push_back({"Vanne", "V[0]", "La vanne", "Vanne", ParamMode::Reference});
        s.params.push_back({"Nom", "'Vanne'", "Son nom", "STRING", ParamMode::Reference});
        const auto add = [&](Kind k, const char* name, Box b) -> Object* {
            const Id id = edit::add(p, s, k, b.x, b.y);
            Object* o = s.object(id);
            if (o) {
                o->setBox(b);
                o->name = name;
            }
            return o;
        };
        if (auto* o = add(Kind::Rectangle, "Cadre", {0, 0, 200, 150})) o->set("fill", "#2B3440");
        if (auto* o = add(Kind::Valve, "Corps", {70, 20, 60, 40})) o->setExpr("fill", "SEL(Vanne.OUV, '#7F8C8D', '#2ECC71')");
        if (auto* o = add(Kind::Text, "Titre", {10, 70, 180, 24})) o->set("text", "{Nom}");
        if (auto* o = add(Kind::Text, "Pos", {10, 94, 180, 22})) o->set("text", "{Vanne.POSITION} %  \xC2\xB7  \xC3\xA9tat {Etat()}");
        if (auto* o = add(Kind::Button, "Btn_Detail", {50, 118, 100, 28})) {
            o->set("text", "D\xC3\xA9tail");
            Action a;
            a.trigger = Trigger::Click;
            a.operation = Operation::Popup;
            a.target = "Pop_Vanne";
            o->actions.push_back(a);
        }
        const auto fn = [&](const char* name, const char* ret, const char* description, const char* body, bool virt) {
            HmiFunction f;
            f.id = p.allocate();
            f.name = name;
            f.returnType = ret;
            f.description = description;
            f.body = body;
            f.isVirtual = virt;
            s.functions.push_back(f);
        };
        fn("Ouvrir", "", "Ouvre la vanne et le note",
           "VAR_INPUT\n    Motif : STRING := 'op\xC3\xA9rateur';\nEND_VAR\n"
           "(* Vanne : le param\xC3\xA8tre du symbole - V[3] pour l'instance Vanne_3 *)\n"
           "IF Etat() = 2 THEN\n    Journaliser(CONCAT(Nom, ' d\xC3\xA9j\xC3\xA0 ouverte'));\n    RETURN;\nEND_IF;\n"
           "Vanne.CMD_FERM := FALSE;\nVanne.CMD_OUV := TRUE;\nVanne.POSITION := 100;\n"
           "Ouvertures := Ouvertures + 1;\nJournaliser(CONCAT(Nom, ' ouverte : ', Motif));",
           true);
        fn("Fermer", "", "Ferme la vanne",
           "Vanne.CMD_OUV := FALSE;\nVanne.CMD_FERM := TRUE;\nVanne.POSITION := 0;\nJournaliser(CONCAT(Nom, ' ferm\xC3\xA9" "e'));", true);
        fn("Etat", "INT", "0 : ferm\xC3\xA9" "e, 1 : en mouvement, 2 : ouverte",
           "IF Vanne.POSITION >= 100 THEN\n    Etat := 2;\nELSIF Vanne.POSITION > 0 THEN\n    Etat := 1;\nELSE\n    Etat := 0;\nEND_IF;", false);
        const Id symId = s.id;
        p.views.push_back(s);
        // Sa popup.
        View pop = makeView(p, "Pop_Vanne");
        pop.role = "popup";
        pop.ownerSymbol = symId;
        pop.width = 420;
        pop.height = 240;
        pop.popup.title = "D\xC3\xA9tail de la vanne";
        const auto addp = [&](Kind k, const char* name, Box b) -> Object* {
            const Id id = edit::add(p, pop, k, b.x, b.y);
            Object* o = pop.object(id);
            if (o) {
                o->setBox(b);
                o->name = name;
            }
            return o;
        };
        if (auto* o = addp(Kind::Text, "Txt_Nom", {20, 20, 380, 30})) o->set("text", "{Nom}");
        if (auto* o = addp(Kind::Text, "Txt_Pos", {20, 60, 380, 26})) o->set("text", "Position : {Vanne.POSITION} %");
        if (auto* o = addp(Kind::Text, "Txt_Ouv", {20, 90, 380, 26})) o->set("text", "Ouverte : {Vanne.OUV}");
        if (auto* o = addp(Kind::Text, "Txt_Etat", {20, 120, 380, 26})) o->set("text", "\xC3\x89tat : {Etat()}  (0 ferm\xC3\xA9" "e, 1 en mouvement, 2 ouverte)");
        const auto button = [&](const char* name, const char* text, Box b, const char* code) {
            if (auto* o = addp(Kind::Button, name, b)) {
                o->set("text", text);
                Action a;
                a.trigger = Trigger::Click;
                a.operation = Operation::RunScript;
                a.value = code;
                o->actions.push_back(a);
            }
        };
        button("Btn_Ouvrir", "Ouvrir", {20, 170, 170, 44}, "Ouvrir('popup');");
        button("Btn_Fermer", "Fermer", {220, 170, 170, 44}, "Fermer();");
        p.views.push_back(pop);
    }
    // ---- la vue Vue_Vannes : deux instances, l'une redefinit Ouvrir
    if (!p.viewByName("Vue_Vannes")) {
        View v = makeView(p, "Vue_Vannes");
        v.width = 1000;
        v.height = 560;
        const Id i3 = placeSymbol(p, v, "S_Vanne", 40, 40);
        if (auto* o = v.object(i3)) {
            o->name = "Vanne_3";
            o->set("params", "Vanne := V[3]; Nom := 'Vanne entr\xC3\xA9" "e gaz'");
            o->functionOverrides.push_back({"Ouvrir",
                "VAR_INPUT\n    Motif : STRING := 'op\xC3\xA9rateur';\nEND_VAR\n"
                "(* Vanne_3 : l'entr\xC3\xA9" "e gaz s'ouvre en deux temps - la purge d'abord *)\n"
                "IF NOT Purge_OK THEN\n    Journaliser('Vanne_3 : purge non faite');\n    RETURN;\nEND_IF;\n"
                "SUPER.Ouvrir(Motif);   (* le corps du symbole *)"});
        }
        const Id i4 = placeSymbol(p, v, "S_Vanne", 300, 40);
        if (auto* o = v.object(i4)) {
            o->name = "Vanne_4";
            o->set("params", "Vanne := V[4]; Nom := 'Vanne de sortie'");
        }
        const Id b = edit::add(p, v, Kind::Button, 40, 240);
        if (auto* o = v.object(b)) {
            o->setBox({40, 240, 260, 50});
            o->name = "Btn_Sortie";
            o->set("text", "Ouvrir la sortie");
            Action a;
            a.trigger = Trigger::Click;
            a.operation = Operation::RunScript;
            a.value = "Vanne_4.Ouvrir('bouton de la vue');";
            o->actions.push_back(a);
        }
        const Id pg = edit::add(p, v, Kind::Button, 320, 240);
        if (auto* o = v.object(pg)) {
            o->setBox({320, 240, 260, 50});
            o->name = "Btn_Purge";
            o->set("text", "Purge faite");
            Action a;
            a.trigger = Trigger::Click;
            a.operation = Operation::Set;
            a.target = "Purge_OK";
            o->actions.push_back(a);
        }
        const Id t = edit::add(p, v, Kind::Text, 40, 310);
        if (auto* o = v.object(t)) {
            o->setBox({40, 310, 600, 30});
            o->name = "Txt_Ouvertures";
            o->set("text", "Ouvertures : {Ouvertures}   \xC2\xB7   Vanne_4 : \xC3\xA9tat {Vanne_4.Etat()}");
        }
        p.views.push_back(v);
    }
    // ---- le script general
    bool has = false;
    for (const auto& sc : p.programs.scripts) has = has || sc.name == "Sequence_Matin";
    if (!has) {
        Script sc;
        sc.id = p.allocate();
        sc.name = "Sequence_Matin";
        sc.event = "Appel";
        sc.description = "Ouvre les vannes d'entr\xC3\xA9" "e et de sortie";
        sc.body = "(* Les fonctions des symboles, depuis un script g\xC3\xA9n\xC3\xA9ral *)\n"
                  "Vue_Vannes.Vanne_3.Ouvrir('s\xC3\xA9quence');\nVue_Vannes.Vanne_4.Ouvrir('s\xC3\xA9quence');\n"
                  "IF Vue_Vannes.Vanne_4.Etat() = 2 THEN\n    Journaliser('s\xC3\xA9quence : sortie ouverte');\nEND_IF;\n"
                  "R := V0 * 2.0;   (* l'op\xC3\xA9rateur T_VEC * REAL *)";
        p.programs.scripts.push_back(sc);
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt (1.11.10) : %s\n", folder.c_str());
    return 0;
}
