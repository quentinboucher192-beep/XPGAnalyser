// =============================================================================
//  tools/sessions/preparer-projet-1118.cpp - 1.11.8 : le projet des captures de la
//  1.11.8 (session-1118-membres-internes.txt), comme la capture du client :
//    - le type IHM Vanne (POSITION INT, OUV BOOL, NOM STRING, CMD_OUV BOOL,
//      CMD_FERM BOOL) ;
//    - l'equipement Esclave virtuel 1 (Modbus TCP) ;
//    - V : ARRAY[0..63] OF Vanne, liee a l'Esclave virtuel 1 a partir de %MW17
//      (19 mots par vanne : mots 17 a 1232).
//
//    g++ -std=c++20 -Isrc tools/sessions/preparer-projet-1118.cpp \
//        build-linux/libxpg_hmi.a build-linux/libxpg_import.a build-linux/libxpg_xls.a \
//        build-linux/libxpg_core.a -lpthread -ldl -o /tmp/preparer-1118
//    /tmp/preparer-1118 <dossier du projet>
// =============================================================================
#include "hmi/HmiModel.hpp"
#include "hmi/HmiStore.hpp"

#include <cstdio>
#include <string>

using namespace hmi;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage : preparer-1118 <dossier du projet>\n");
        return 2;
    }
    const std::string folder = argv[1];
    auto loaded = load(folder);
    if (!loaded) {
        std::fprintf(stderr, "projet illisible : %s\n", folder.c_str());
        return 1;
    }
    Project p = std::move(*loaded);
    if (!p.hmiTypeByName("Vanne")) {
        HmiType t;
        t.id = p.allocate();
        t.name = "Vanne";
        t.description = "Une vanne de la ligne";
        t.members = {{"POSITION", "INT", "", "Position (%)"},
                     {"OUV", "BOOL", "", "Fin de course ouverte"},
                     {"NOM", "STRING", "", "Le nom affich\xC3\xA9"},
                     {"CMD_OUV", "BOOL", "", "Commande ouvrir"},
                     {"CMD_FERM", "BOOL", "", "Commande fermer"}};
        p.programs.types.push_back(std::move(t));
    }
    if (!p.equipmentByName("Esclave virtuel 1")) {
        Equipment e;
        e.id = p.allocate();
        e.name = "Esclave virtuel 1";
        e.type = EquipmentType::ModbusTcp;
        e.host = "192.168.1.50";
        e.port = 502;
        p.equipments.push_back(std::move(e));
    }
    if (!p.variable("V")) {
        Variable v;
        v.id = p.allocate();
        v.name = "V";
        v.type = "ARRAY[0..63] OF Vanne";
        v.equipment = "Esclave virtuel 1";
        v.address = "%MW17";
        v.description = "Les vannes de la ligne";
        p.programs.variables.push_back(std::move(v));
    }
    if (const auto st = save(p, folder); !st) {
        std::fprintf(stderr, "enregistrement impossible : %s\n", st.error().message().c_str());
        return 1;
    }
    std::printf("projet pr\xC3\xAAt (1.11.8) : %s\n", folder.c_str());
    return 0;
}
