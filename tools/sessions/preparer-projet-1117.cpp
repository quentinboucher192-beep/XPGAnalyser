// =============================================================================
//  tools/sessions/preparer-projet-1117.cpp - 1.11.7 : le projet des captures de la
//  1.11.7 (session-1117-forcage-commun.txt).
// -----------------------------------------------------------------------------
//  Sur la copie d'Armoire_Gaz des sessions 1115 et 1116 (preparer-projet-1113,
//  -1114, -1115 passes) :
//    - Four3 (T_Four), liee a la Centrale PM5560 (l'esclave simule) a %MW3200 :
//      une variable IHM commune a Variables IHM et a Esclaves simules (la zone
//      3200-3220 ajoutee a la memoire de la centrale, si elle a des zones) ;
//    - la popup Pop_Vanne et son parametre IN_V (T_Vanne, Reference) : sa position,
//      son etat, Ouvrir (Mettre a 1 IN_V.Ouverte) et Fermer ;
//    - la vue Vue_Vannes : trois boutons qui l'ouvrent avec un repere
//      (IN_V := $Four2.Vannes[1]$, comme Dupliquer... les pose).
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-1117.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-1117
//    /tmp/preparer-1117 <dossier du projet>
// =============================================================================
#include "hmi/HmiEdit.hpp"
#include "hmi/HmiModel.hpp"
#include "hmi/HmiStore.hpp"

#include <cstdio>
#include <string>

using namespace hmi;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage : preparer-1117 <dossier du projet>\n");
        return 2;
    }
    const std::string folder = argv[1];
    auto loaded = load(folder);
    if (!loaded) {
        std::fprintf(stderr, "projet illisible : %s\n", folder.c_str());
        return 1;
    }
    Project p = std::move(*loaded);
    if (!p.equipmentByName("Centrale PM5560")) {
        std::fprintf(stderr, "pas de Centrale PM5560 : passer d'abord preparer-projet-1115\n");
        return 1;
    }
    // Ses registres sont dans la memoire de la centrale simulee : la zone 3200-3220 (sinon
    // l'esclave refuse, exception 2 : adresse illegale - comme le vrai appareil).
    for (auto& c : p.equipments) {
        if (c.name != "Centrale PM5560" || !c.zones.declared) continue;
        auto& holding = c.zones.of(MemTable::Holding);
        bool covered = false;
        for (const auto& r : holding) covered = covered || (r.first <= 3200 && r.last >= 3220);
        if (!covered) holding.push_back({3200, 3220});
    }
    if (!p.variable("Four3")) {
        Variable v;
        v.id = p.allocate();
        v.name = "Four3";
        v.type = "T_Four";
        v.equipment = "Centrale PM5560";
        v.address = "%MW3200";
        v.description = "Le four 3, lu sur la centrale (l'esclave simul\xC3\xA9)";
        p.programs.variables.push_back(v);
    }
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
    const auto act = [](Trigger t, Operation o, std::string target, std::string value) {
        Action a;
        a.trigger = t;
        a.operation = o;
        a.target = std::move(target);
        a.value = std::move(value);
        return a;
    };
    {
        View& pop = view("Pop_Vanne", "popup", 420, 200);
        if (pop.params.empty()) {
            pop.params.push_back({"IN_V", "", "La vanne montr\xC3\xA9" "e", "T_Vanne", ParamMode::Reference});
            pop.popup.title = "Vanne";
            const Id t = edit::add(p, pop, Kind::Text, 20, 20);
            if (auto* o = pop.object(t)) {
                o->setBox({20, 20, 380, 40});
                o->name = "Etat";
                o->set("text", "Ouverte : {IN_V.Ouverte}   Position : {IN_V.Position} %");
            }
            const Id b1 = edit::add(p, pop, Kind::Button, 20, 90);
            if (auto* o = pop.object(b1)) {
                o->setBox({20, 100, 170, 60});
                o->name = "Btn_Ouvrir";
                o->set("text", "Ouvrir");
                o->actions.push_back(act(Trigger::Click, Operation::Set, "IN_V.Ouverte", ""));
            }
            const Id b2 = edit::add(p, pop, Kind::Button, 200, 90);
            if (auto* o = pop.object(b2)) {
                o->setBox({230, 100, 170, 60});
                o->name = "Btn_Fermer";
                o->set("text", "Fermer");
                o->actions.push_back(act(Trigger::Click, Operation::Reset, "IN_V.Ouverte", ""));
            }
        }
    }
    {
        View& v = view("Vue_Vannes", "vue", 640, 360);
        if (!v.objectByName("Valve_1")) {
            for (int k = 1; k <= 3; ++k) {
                const std::string n = std::to_string(k);
                const Id b = edit::add(p, v, Kind::Button, 60 + (k - 1) * 220, 80);
                if (auto* o = v.object(b)) {
                    o->setBox({20.0 + (k - 1) * 210, 40, 180, 60});
                    o->name = "Valve_" + n;
                    o->set("text", "Vanne " + n);
                    o->actions.push_back(act(Trigger::Click, Operation::Popup, "Pop_Vanne", "IN_V := $Four2.Vannes[" + n + "]$"));
                }
                const Id t = edit::add(p, v, Kind::Text, 60 + (k - 1) * 220, 160);
                if (auto* o = v.object(t)) {
                    o->setBox({20.0 + (k - 1) * 210, 120, 200, 30});
                    o->name = "Etat_" + n;
                    o->set("text", "V" + n + " ouverte : {Four2.Vannes[" + n + "].Ouverte}");
                }
            }
        }
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt : %s\n", folder.c_str());
    return 0;
}
