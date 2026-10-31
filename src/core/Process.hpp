// =============================================================================
//  core/Process.hpp - 1.21.0 : lancer un programme A PART, borne et observe
// -----------------------------------------------------------------------------
//  Le bac a sable de l'IA (executer et tester du code genere) ne tourne JAMAIS
//  dans le processus principal : un processus enfant, qui ne partage ni la
//  memoire ni les fichiers ouverts de l'application, avec
//    - un TEMPS borne (au-dela : tue, et dit) ;
//    - une MEMOIRE bornee (Windows : un Job Object ; Linux : RLIMIT_AS) ;
//    - une SORTIE bornee (au-dela : coupee, et dit) ;
//    - un environnement VIDE (rien de l'utilisateur : pas de cle, pas de chemin) ;
//    - pas de reseau quand le systeme le permet sans droits (Linux : un espace de
//      noms reseau prive ; Windows : rien de tel sans droits d'administrateur -
//      le programme lance n'a de toute facon pas de fonction reseau, et c'est
//      dit dans le resultat : networkIsolated).
//  L'appel ATTEND la fin de l'enfant : il se fait sur un fil de fond (jamais sur
//  celui de l'interface). `cancel` l'arrete (l'enfant est tue).
// =============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace core::process {

struct Limits {
    double      seconds{10.0};                       // le temps du mur, au plus
    std::size_t memoryBytes{512u * 1024u * 1024u};   // la memoire de l'enfant, au plus
    std::size_t outputBytes{4u * 1024u * 1024u};     // la sortie (stdout + stderr) gardee, au plus
};

struct Request {
    std::string              program;    // le chemin du programme (pas de recherche dans le PATH)
    std::vector<std::string> args;       // sans le programme lui-meme
    std::string              workDir;    // vide : le dossier courant
    std::string              input;      // ecrit sur l'entree standard, puis fermee
    Limits                   limits;
    bool                     isolateNetwork{true};
};

struct Result {
    bool        started{false};
    int         exitCode{-1};          // le code de sortie (tue : -1)
    bool        timedOut{false};
    bool        memoryExceeded{false}; // la limite de memoire a ete atteinte (au mieux de ce que le systeme dit)
    bool        outputCut{false};
    bool        cancelled{false};
    bool        crashed{false};        // termine par un signal / une exception
    bool        networkIsolated{false};
    std::string out, err;
    double      seconds{0.0};
    std::string error;                 // pourquoi il n'a pas demarre
    [[nodiscard]] bool ok() const noexcept { return started && !timedOut && !cancelled && !crashed && exitCode == 0; }
    // En francais, pour un compte rendu : "termine (code 0) en 0,4 s", "arrete : plus de 10 s"...
    [[nodiscard]] std::string summary() const;
};

// Lance `req.program` et attend sa fin (ou la limite, ou `cancel`).
[[nodiscard]] Result run(const Request& req, const std::atomic<bool>* cancel = nullptr);

// Le chemin du programme en cours (pour se relancer en mode bac a sable).
[[nodiscard]] std::string selfPath();

} // namespace core::process
