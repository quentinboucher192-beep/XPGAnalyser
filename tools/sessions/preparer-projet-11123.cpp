// =============================================================================
//  tools/sessions/preparer-projet-11123.cpp - 1.11.23 : le projet des captures de la
//  1.11.23 (session-11123-clavier-explorateur.txt), sur une copie d'Armoire_Gaz deja
//  preparee par preparer-projet-11120 puis preparer-projet-11121 :
//    - les variables IHM Compteur_F5 (INT), Lampe_F6 (BOOL), Niveau (INT) ;
//    - la vue Vue_Clavier : des textes qui montrent la souris et le clavier en marche
//      (SYS.MouseX, SYS.MouseY, SYS.MouseObject, SYS.KeyLast, SYS.KeysDown,
//      SYS.KeyHoldTime, SYS.Key.F6, SYS.ShortcutLast...), une zone a cliquer, et trois
//      raccourcis : F5 (front montant : Compteur_F5 + 1), F6 (duree : tenue une seconde,
//      Lampe_F6 bascule), Haut (repetition : Niveau + 1 toutes les 200 ms).
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-11123.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-11123
//    /tmp/preparer-11123 <dossier du projet>
// =============================================================================
#include "hmi/HmiEdit.hpp"
#include "hmi/HmiModel.hpp"
#include "hmi/HmiStore.hpp"

#include <cstdio>
#include <string>

using namespace hmi;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage : preparer-11123 <dossier du projet>\n");
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
    var("Compteur_F5", "INT", "0", "Compt\xC3\xA9 par le raccourci F5 (front montant)");
    var("Lampe_F6", "BOOL", "FALSE", "Bascul\xC3\xA9" "e par F6 tenue une seconde (dur\xC3\xA9" "e)");
    var("Niveau", "INT", "0", "Mont\xC3\xA9 par Haut tenue (r\xC3\xA9p\xC3\xA9tition, toutes les 200 ms)");
    if (!p.viewByName("Vue_Clavier")) {
        View v;
        v.id = p.allocate();
        v.name = "Vue_Clavier";
        v.role = "vue";
        v.width = 900;
        v.height = 520;
        v.description = "La souris et le clavier en marche, et trois raccourcis (1.11.23)";
        Layer l;
        l.id = p.allocate();
        l.name = "Calque 1";
        v.layers.push_back(l);
        v.activeLayer = l.id;
        p.views.push_back(v);
        View& view = p.views.back();
        const auto label = [&p, &view](const char* name, const char* text, Box box) {
            const Id id = edit::add(p, view, Kind::Text, box.x, box.y);
            if (auto* o = view.object(id)) {
                o->setBox(box);
                o->name = name;
                o->set("text", text);
            }
        };
        label("Txt_Titre", "Souris et clavier (1.11.23)", {30, 20, 500, 34});
        label("Txt_Souris", "Souris : X = {SYS.MouseX:0}   Y = {SYS.MouseY:0}", {30, 80, 420, 28});
        label("Txt_Objet", "Sous la souris : {SYS.MouseObject}", {30, 112, 420, 28});
        label("Txt_Boutons", "Boutons : {SYS.MouseButtons}   sur l'IHM : {SYS.MouseInside}", {30, 144, 420, 28});
        label("Txt_Derniere", "Derni\xC3\xA8re touche : {SYS.KeyLast}", {30, 200, 420, 28});
        label("Txt_Tenues", "Touches tenues : {SYS.KeysDown}   ({SYS.KeyDownCount})", {30, 232, 420, 28});
        label("Txt_Duree", "Tenue depuis : {SYS.KeyHoldTime}", {30, 264, 420, 28});
        label("Txt_F6", "SYS.Key.F6 : {SYS.Key.F6}", {30, 296, 420, 28});
        label("Txt_Appuis", "Touches enfonc\xC3\xA9" "es : {SYS.KeyPresses}", {30, 328, 420, 28});
        label("Txt_Raccourci", "Dernier raccourci : {SYS.ShortcutLast}   ({SYS.ShortcutCount})", {30, 384, 520, 28});
        label("Txt_Compteur", "F5 (front montant) : Compteur_F5 = {Compteur_F5}", {480, 80, 400, 28});
        label("Txt_Lampe", "F6 tenue 1 s (dur\xC3\xA9" "e) : Lampe_F6 = {Lampe_F6}", {480, 112, 400, 28});
        label("Txt_Niveau", "Haut tenue (r\xC3\xA9p\xC3\xA9tition) : Niveau = {Niveau}", {480, 144, 400, 28});
        const Id zone = edit::add(p, view, Kind::Button, 480, 220);
        if (auto* o = view.object(zone)) {
            o->setBox({480, 220, 300, 80});
            o->name = "Zone_Clic";
            o->set("text", "Cliquer ici, puis F5, F6, Haut");
        }
        Action f5;
        f5.trigger = Trigger::KeyPress;
        f5.key = "F5";
        f5.operation = Operation::Increment;
        f5.target = "Compteur_F5";
        view.actions.push_back(f5);
        Action f6;
        f6.trigger = Trigger::KeyHold;
        f6.key = "F6";
        f6.delayMs = 1000;
        f6.operation = Operation::Toggle;
        f6.target = "Lampe_F6";
        view.actions.push_back(f6);
        Action up;
        up.trigger = Trigger::KeyRepeat;
        up.key = "Up";
        up.delayMs = 200;
        up.operation = Operation::Increment;
        up.target = "Niveau";
        view.actions.push_back(up);
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt (1.11.23) : %s\n", folder.c_str());
    return 0;
}
