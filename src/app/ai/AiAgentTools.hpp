// =============================================================================
//  app/ai/AiAgentTools.hpp - 1.21.0 : LES OUTILS DE L'AGENT (repondus sur ce PC)
// -----------------------------------------------------------------------------
//  L'IA connectee enchaine seule les etapes NON DESTRUCTIVES d'un travail :
//    analyser_projet   le graphe des relations du projet (qui utilise quoi, ce
//                      qu'une suppression toucherait, les elements orphelins),
//                      la verification globale, les conventions - LECTURE SEULE ;
//    executer_code     le bac a sable (AiSandbox.hpp) : analyse, compilation,
//                      execution des cas d'une fonction, dans un processus a part ;
//    detecter_equipements  prepare une detection reseau (hmi::discovery) et OUVRE LA
//                      FENETRE DE CONFIRMATION, preremplie : c'est l'utilisateur
//                      qui lance (ou non) - l'IA ne lance jamais un scan ;
//    resultat_detection    le compte rendu du dernier scan termine (lecture seule).
//  Chacun travaille sur une COPIE du projet prise au depart de la demande (sur le
//  fil de l'interface) : le fil du fournisseur ne lit jamais le projet vivant.
//  Ce qui MODIFIE le projet reste une proposition (proposer_operations) que
//  l'utilisateur valide : un outil de cette liste ne change jamais rien.
// =============================================================================
#pragma once

#include "AiProvider.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace hmi { struct Project; }

namespace app::ai::agent {

inline constexpr std::string_view kAnalyseTool = "analyser_projet";
inline constexpr int              kMaxAnalyses = 8;   // par demande

inline constexpr std::string_view kScanTool = "detecter_equipements";
inline constexpr std::string_view kScanResultTool = "resultat_detection";

// LE PONT entre le fil du fournisseur (les outils) et celui de l'interface (l'Assistant, a chaque image) : les demandes
// d'ouvrir la confirmation du scanner, et le dernier compte rendu de scan. Tout passe sous le verrou.
struct Bridge {
    struct ScanAsk {
        std::string encoded;   // hmi::discovery::encodeRequest
        std::string reason;
    };
    std::mutex           m;
    bool                 scanAvailable{false};   // l'interface sait ouvrir la confirmation (un projet IHM ouvert)
    std::vector<ScanAsk> scanAsks;               // posees par detecter_equipements, servies par l'interface
    std::uint64_t        scanSerial{0};          // le numero du dernier compte rendu (0 : aucun)
    std::string          scanText;               // hmi::discovery::reportText
};

// detecter_equipements et resultat_detection, branches sur ce pont.
[[nodiscard]] LocalTool scanTool(std::shared_ptr<Bridge> bridge);
[[nodiscard]] LocalTool scanResultTool(std::shared_ptr<Bridge> bridge);
// La reponse de detecter_equipements (pour les essais ; l'outil l'appelle) : la demande verifiee, posee sur le pont.
[[nodiscard]] std::string askScan(Bridge&, std::string_view inputJson);

// L'outil analyser_projet sur cette copie du projet.
[[nodiscard]] LocalTool analyseTool(std::shared_ptr<const hmi::Project> snapshot);
// L'outil executer_code (le bac a sable) sur cette copie ; `program` : le programme relance (vide : celui-ci).
[[nodiscard]] LocalTool sandboxTool(std::shared_ptr<const hmi::Project> snapshot, std::string program = {});

// La reponse de analyser_projet a une entree JSON (pour les essais ; l'outil l'appelle).
[[nodiscard]] std::string analyse(const hmi::Project&, std::string_view inputJson);

} // namespace app::ai::agent
