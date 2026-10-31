// =============================================================================
//  app/ai/AiSandbox.hpp - 1.21.0 : LE BAC A SABLE D'EXECUTION de l'Assistant IA
// -----------------------------------------------------------------------------
//  L'IA ecrit une fonction ST, puis la fait VERIFIER ET ESSAYER ici avant de la
//  proposer : jamais dans le processus de l'application. Un processus ENFANT
//  (IHM-Studio --bac-a-sable, core::process) recoit le travail sur son entree :
//  une copie du projet, le code, les cas d'essai ; il rend un compte rendu JSON.
//    - temps, memoire et sortie bornes ; environnement vide ; dossier de travail
//      temporaire, efface apres ; reseau coupe quand le systeme le permet ;
//    - les fonctions de fichiers des scripts confinees au dossier de travail
//      (hmi::confineFiles) ; aucun automate : aucune ecriture possible vers un
//      equipement (le banc d'essai n'a aucune connexion) ;
//    - des ETAPES DISTINCTES, chacune avec son etat REEL : analyse syntaxique,
//      compilation (le Compiler du logiciel : noms, types, appels), execution
//      des cas sur un banc neuf (une simulation, sans automate), tests.
//  C ET C++ : VERIFICATION STATIQUE SEULEMENT (regle permanente : << ne jamais
//  executer du code C++ arbitraire genere par l'IA >>) - le compte rendu le dit.
//  Rien n'est integre au projet ici : l'integration reste une proposition a part.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hmi { struct Project; }

namespace app::ai::sandbox {

// L'outil de l'IA (repondu sur le fil du fournisseur, jamais sur celui de l'interface).
inline constexpr std::string_view kTool = "executer_code";
inline constexpr int              kMaxRuns = 5;   // les essais d'une demande, au plus (ecrire -> essayer -> corriger)
inline constexpr std::string_view kChildFlag = "--bac-a-sable";

struct Case {
    std::string              name;
    std::vector<std::string> arguments;   // un par parametre, lus comme toute case (2,5 ; VRAI ; 'texte')
    std::string              expected;    // le retour attendu ("" : une procedure)
    std::string              tolerance;   // sur un REAL (vide : egalite exacte)
    std::string              errorCode;   // non vide : le cas attend cette erreur levee
};

struct Job {
    std::string       language{"ST"};   // ST, C, C++
    std::string       name;             // la fonction
    std::string       returnType;       // "" : une procedure (ST)
    std::string       code;             // ST : VAR_INPUT ... END_VAR puis le calcul ; C/C++ : le code a relire
    bool              existing{false};  // essayer la fonction du projet telle qu'elle est (code vide)
    std::vector<Case> cases;
    double            seconds{10.0};    // le temps du processus, au plus
};

enum class State : std::uint8_t { NotRun, Ok, Failed, Skipped };
[[nodiscard]] std::string_view stateText(State) noexcept;   // "non fait", "r\xC3\xA9ussi", "\xC3\xA9" "chec", "non fait (...)"

struct Stage {
    std::string key;      // "analyse", "compilation", "execution"
    std::string label;    // "Analyse syntaxique", ...
    State       state{State::NotRun};
    std::string detail;   // les fautes, ou ce qui a ete fait
};

struct CaseResult {
    std::string name, call, verdict, detail, got;
    bool        passed{false};
};

struct Outcome {
    bool                    started{false};       // le processus enfant a tourne
    bool                    networkIsolated{false};
    std::string             process;              // "termine (code 0) en 0,4 s", "arrete : plus de 10 s"...
    std::string             error;                // pourquoi rien n'a tourne (vide : il a tourne)
    std::string             language;
    std::vector<Stage>      stages;               // dans l'ordre
    std::vector<CaseResult> cases;
    [[nodiscard]] const Stage* stage(std::string_view key) const;
    // Au moins un cas, tous reussis (et le code compile).
    [[nodiscard]] bool testsPassed() const;
    // LE COMPTE RENDU REEL, en francais : chaque etape et son etat, chaque cas ; ce qui n'a PAS ete fait est dit
    // (jamais << ca marche >> parce que ca compile) ; l'integration au projet n'est jamais faite ici.
    [[nodiscard]] std::string report() const;
};

// ---- l'enfant ----------------------------------------------------------------------------------------------------
// Le mode --bac-a-sable : lit le travail sur l'entree standard (une ligne JSON, puis la copie du projet), ecrit le
// resultat (une ligne JSON) sur la sortie standard. Rend le code de sortie du processus.
int childMain();
// Le meme travail dans CE processus (l'enfant ; les essais). Ne confine rien lui-meme.
[[nodiscard]] Outcome runHere(const Job&, const hmi::Project&);

// ---- le parent ---------------------------------------------------------------------------------------------------
// Lance l'enfant (`program` vide : ce programme-ci, core::process::selfPath) et attend sa fin : a appeler sur un fil
// de fond. `cancel` l'arrete (l'enfant est tue).
[[nodiscard]] Outcome run(const Job&, const hmi::Project&, const std::atomic<bool>* cancel = nullptr, std::string program = {});
// L'entree de l'enfant (pour les essais) et la lecture de sa sortie.
[[nodiscard]] std::string childInput(const Job&, const hmi::Project&);
[[nodiscard]] bool        readChildOutput(std::string_view out, Outcome& o, std::string* why = nullptr);

// ---- l'outil de l'IA ---------------------------------------------------------------------------------------------
// La definition de l'outil (format Anthropic : name, description, input_schema).
[[nodiscard]] std::string toolJson();
// L'entree JSON de l'outil -> le travail (faux : illisible, `why` dit pourquoi).
[[nodiscard]] bool jobFromJson(std::string_view inputJson, Job& out, std::string* why = nullptr);
// Une ligne au journal des executions (IA/journaux/bac-a-sable.jsonl) : quand, quoi, le resultat. Jamais le code.
void journal(const Job&, const Outcome&);

} // namespace app::ai::sandbox
