#pragma once
// =============================================================================
//  project/ActivationConditions.hpp - 1.11 (R111, decision 6 du 03/10) : les
//  conditions d'activation manquantes des projets importes avant la 1.8.0
// -----------------------------------------------------------------------------
//  Avant la 1.8.0, l'import d'un .XPG perdait la condition d'activation des
//  sections de tache (Control Expert la met sur <sectionDesc>) ; celles des
//  unites de programme restaient. En simulation, ces sections tournent alors a
//  chaque cycle (Armoire_Gaz : Reset_all, Reset, Modifications) - c'est ce qui
//  a fait croire le programme de XFAB en faute sur le pupitre.
//
//  LE SIGNAL. Le projet ne garde ni le chemin de son .XPG d'origine, ni la
//  version qui l'a importe. Il garde son manifeste - `product` (le Control
//  Expert du fichier d'origine : le projet vient d'un import) et `created` (le
//  jour ou son dossier a ete cree, c'est-a-dire l'import) - et ses sections :
//    - un projet importe d'un .XPG (product ne dit pas XpgAnalyzer) ;
//    - cree avant le 30/09/2026, la livraison de la 1.8.0 ;
//    - des sections de tache, et AUCUNE n'a de condition.
//  Des sections d'unite AVEC condition (Armoire_Gaz : « configuree ») disent
//  que le .XPG en portait : `unitConditions` le compte.
// =============================================================================
#include "../domain/ProjectModel.hpp"

#include <string>
#include <vector>

namespace project {

struct Manifest;

struct MissingConditions {
    bool                     suspected{false};
    std::vector<std::string> sections;         // les sections de tache, sans condition
    std::size_t              unitConditions{0};  // les sections d'unite qui en ont une
    std::string              message;          // en clair, avec le remede ("" : rien a dire)
};

// La livraison de la 1.8.0 : un projet cree avant a ete importe par une version
// qui perdait les conditions des sections de tache.
inline constexpr const char* kConditionsSince = "2026-09-30";

[[nodiscard]] MissingConditions missingTaskConditions(const domain::Project& p, const Manifest& m);

} // namespace project
