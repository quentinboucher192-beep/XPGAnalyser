// =============================================================================
//  app/ai/AiLearningFile.hpp - 1.17.0 : l'apprentissage de l'IA dans UN fichier
// -----------------------------------------------------------------------------
//  "APPRENDRE" ICI = la memoire locale de l'Assistant (AiKnowledge.hpp : le
//  modele distant n'est jamais reentraine). Ce qu'elle a appris sur un PC - les
//  connaissances ajoutees (connaissances/*.json : celles de l'utilisateur, de
//  l'Assistant, des fichiers analyses, ses corrections des modeles metier et du
//  vocabulaire), l'etat des connaissances integrees (memoire.json) - part dans
//  un fichier .xia (du JSON, versionne) et revient sur un autre PC :
//
//    - exportTo   : ecrit le fichier (jamais la cle API, jamais un mot de passe) ;
//    - peek       : ce qu'un import ferait (les comptes, les lignes), sans rien ecrire ;
//    - importFrom : fusionne (une connaissance de meme identifiant : gardee, ou
//                   remplacee si `replaceSame`) ; chaque import ecrit sa ligne dans
//                   activite.jsonl.
//
//  L'installateur (page Assistant IA) peut deposer un .xia et des reglages de
//  l'Assistant : applyInstallation les applique une fois par utilisateur, au
//  premier lancement qui suit l'installation.
//
//  Les chemins sont en UTF-8 ; la racine de l'Assistant : AiPaths.hpp.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace app::ai::learning {

inline constexpr const char* kExtension = ".xia";

struct FileOptions {
    bool knowledge{true};        // les connaissances ajoutees (et les corrections)
    bool states{true};           // l'etat des connaissances integrees (actives, desactivees)
    bool settings{false};        // les reglages de l'Assistant (fournisseur, service, adresse, modele) - JAMAIS la cle
    bool conversations{false};   // les conversations (leurs pieces jointes non comprises)
};

struct ImportReport {
    int added{0}, replaced{0}, kept{0}, skipped{0};
    std::string from;                  // "PC-ATELIER-3, 31/10/2026 08:40" (ce que dit le fichier)
    std::vector<std::string> lines;    // une ligne par connaissance : ajoutee, remplacee, gardee, ignoree (et pourquoi)
};

bool exportTo(const std::string& path, const FileOptions& options, std::string* why = nullptr);
bool peek(const std::string& path, ImportReport& preview, std::string* why = nullptr);
bool importFrom(const std::string& path, ImportReport& report, bool replaceSame = false, std::string* why = nullptr);

// Ce que l'installateur a depose (le dossier commun des donnees du programme) : applique une fois par
// utilisateur. Rend vrai s'il y avait quelque chose a appliquer ; `said` : la phrase a montrer dans le panneau.
bool applyInstallation(std::string* said = nullptr);

} // namespace app::ai::learning
