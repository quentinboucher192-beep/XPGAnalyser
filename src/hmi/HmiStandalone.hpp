// =============================================================================
//  hmi/HmiStandalone.hpp - 1.12.0 : UNE IHM DE LA 1.11, RENDUE AUTONOME
// -----------------------------------------------------------------------------
//  Jusqu'a la 1.11, l'IHM lisait les variables de l'automate du projet par leur
//  nom (Armoires[0].ana.PT1.mes). XPGAnalyser IHM n'a pas d'automate : ces noms
//  y sont inconnus. fromPlc() les rend a l'IHM, une fois, depuis le programme
//  du dossier d'origine :
//
//    - chaque globale de l'automate que l'IHM nomme (vues, actions, scripts,
//      fonctions, alarmes, recettes, historiques, la table des adresses) devient
//      une VARIABLE IHM de meme nom, de meme type et de meme valeur initiale ;
//    - son type suit : un DDT devient un TYPE IHM (ses membres, recursivement) ;
//      un tableau, ARRAY[a..b] OF ... ; une instance de bloc DFB, un type IHM de
//      ses entrees et sorties ; EBOOL, BOOL ; STRING[n], STRING ;
//    - l'automate etait relie par Modbus TCP (Configuration > Communication) :
//      un EQUIPEMENT « Automate » (son adresse, son port, son esclave) ; chaque
//      variable situee (%MW, %M...) ou presente dans la table des adresses y est
//      liee a son adresse - l'IHM parle toujours a l'automate, en equipement ;
//    - les noms deja a l'IHM (une variable IHM, une vue, un parametre) ne sont
//      pas touches ; rien n'est retire.
//
//  Sans ecran : edition_test l'essaie sur bac/Armoire_Gaz.
// =============================================================================
#pragma once

#include "HmiModel.hpp"

#include <string>
#include <vector>

namespace domain { class Project; }

namespace hmi::standalone {

struct Report {
    std::vector<std::string> variables;    // les variables IHM creees
    std::vector<std::string> types;        // les types IHM crees (DDT, blocs)
    std::vector<std::string> bound;        // les variables liees a l'equipement Automate
    std::vector<std::string> skipped;      // "Nom : raison"
    std::string              equipment;    // l'equipement cree ("" : aucun)
    [[nodiscard]] bool changed() const noexcept { return !variables.empty() || !types.empty() || !equipment.empty(); }
};

// Le nom de l'equipement cree pour l'automate.
inline constexpr const char* kPlcEquipment = "Automate";

Report fromPlc(Project& hmi, const domain::Project& plc);

// Une ligne par chose faite (pour le rapport de la migration, le journal).
[[nodiscard]] std::string describe(const Report& r);

} // namespace hmi::standalone
