// =============================================================================
//  hmi/HmiScenarios.hpp - les essais de reception (lot 13) : des scenarios
//                         rejoues sur l'IHM en marche, un verdict par pas,
//                         un rapport
// -----------------------------------------------------------------------------
//  UN ESSAI EST UNE SUITE DE PAS (Project::scenarios, IHM > Essais). Chaque
//  pas fait ce que ferait l'operateur - ouvrir une vue, cliquer un objet (ou
//  une de ses parties), saisir dans un champ, se connecter, signer, acquitter -
//  ou ce que ferait le procede (ecrire une variable), attend, ou VERIFIE : une
//  expression vraie, une valeur attendue. Chaque pas recoit son verdict :
//  reussi, echec (l'IHM a refuse, la verification est fausse, l'attente a
//  expire), erreur (l'objet n'est pas a l'ecran, l'expression est illisible).
//
//  LE MEME MOTEUR PARTOUT : ScenarioRun avance pas a pas sur un Runtime qui
//  tourne - celui de la simulation (on voit l'IHM rejouer l'essai, au rythme
//  choisi), ou un Runtime sans ecran (runScenario : tout d'un coup, le temps
//  simule). Le rapport (reportTable) s'exporte en CSV, Excel ou PDF.
//
//  Les mots de passe des pas Se connecter et Signer ne sortent jamais dans le
//  rapport ni dans le journal : ils y sont masques.
// =============================================================================
#pragma once

#include "HmiExport.hpp"
#include "HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sim { class Environment; }

namespace hmi {

class Runtime;

// ---- les pas ------------------------------------------------------------------------
enum class StepKind : std::uint8_t {
    OpenView, Click, Type, Write, Wait, WaitUntil, Check, Login, Logout, Sign, Acknowledge, Comment, Unknown
};
// Les actions, dans l'ordre de la liste : "Ouvrir la vue", "Cliquer", "Saisir"...
[[nodiscard]] const std::vector<std::string>& stepActions();
[[nodiscard]] StepKind      stepKind(std::string_view action);        // sans casse, avec ou sans accents
[[nodiscard]] std::string   stepLabel(StepKind);                      // "Cliquer"
// Ce que veulent dire la cible, la valeur et l'attendu pour cette action
// ("" : le champ ne sert pas) - l'aide de la fiche du pas.
struct StepFields { std::string target, value, expected; };
[[nodiscard]] StepFields    stepFields(StepKind);
// "Cliquer Btn_Purge", "Verifier Vanne_Purge = TRUE", "Se connecter chef (****)".
[[nodiscard]] std::string   describeStep(const TestStep&);

// ---- les verdicts ------------------------------------------------------------------
enum class Verdict : std::uint8_t { Pending, Passed, Failed, Error, Skipped, Info };
[[nodiscard]] std::string_view verdictLabel(Verdict) noexcept;   // "en attente", "reussi" (accentue)...

struct StepResult {
    Verdict     verdict{Verdict::Pending};
    std::string detail;          // ce qui s'est passe : "Vanne_Purge = TRUE", "permission Piloter requise"
    double      at{0};           // le debut du pas, en secondes depuis le debut de l'essai
    double      seconds{0};      // sa duree (les attentes)
};

struct ScenarioReport {
    Id                      scenario{kNoId};
    std::string             name;
    std::string             started;      // "2026-09-25 10:12:03"
    double                  seconds{0};   // la duree de l'essai (temps de l'IHM)
    std::vector<StepResult> steps;
    bool                    done{false};
    bool                    stopped{false};   // arrete avant la fin
    [[nodiscard]] std::size_t count(Verdict) const;
    [[nodiscard]] bool        ok() const;     // fini, sans echec ni erreur
    // "12 pas : 11 reussis, 1 echec" ; "en cours : pas 3 / 12" (accentue).
    [[nodiscard]] std::string summary() const;
};

// ---- le moteur -----------------------------------------------------------------------
class ScenarioRun {
public:
    // `pace` : les secondes entre deux pas (pour qu'on voie l'IHM les faire).
    // `stopOnFailure` : apres un echec ou une erreur, les pas suivants ne
    // se jouent pas ("non joue").
    ScenarioRun(const TestScenario&, Runtime&, double now, double pace = 0.4, bool stopOnFailure = false);
    // Jusqu'a `now` : le pas dont c'est l'heure se joue (une attente en cours
    // se verifie). Vrai quand l'essai est fini.
    bool advance(double now);
    void stop(double now);                         // arrete : le reste "non joue"
    [[nodiscard]] bool                  done() const noexcept { return report_.done; }
    [[nodiscard]] const ScenarioReport& report() const noexcept { return report_; }
    [[nodiscard]] const TestScenario&   scenario() const noexcept { return scenario_; }
    [[nodiscard]] std::size_t           current() const noexcept { return index_; }   // le pas en cours
    // L'objet que le dernier pas a touche (kNoId : aucun) - l'ecran le montre.
    [[nodiscard]] Id                    touched() const noexcept { return touched_; }
    [[nodiscard]] double                touchedAt() const noexcept { return touchedAt_; }
private:
    void execute(double now);
    void finish(Verdict, std::string detail, double now);
    TestScenario   scenario_;
    Runtime&       rt_;
    ScenarioReport report_;
    std::size_t    index_{0};
    double         start_{0}, next_{0};
    double         pace_{0.4};
    bool           stopOnFailure_{false};
    bool           waiting_{false};      // "Attendre que" en cours
    double         deadline_{0};
    Id             touched_{kNoId};
    double         touchedAt_{-1};
};

// Tout d'un coup, sans ecran : un Runtime neuf (relie a `plc` s'il y en a un),
// demarre, l'essai joue au temps simule. Au plus `limitS` secondes d'IHM.
[[nodiscard]] ScenarioReport runScenario(const Project&, const TestScenario&, sim::Environment* plc = nullptr,
                                         double pace = 0.1, double limitS = 600);

// Le rapport en tableau : No, Pas, Verdict, Detail, Instant (s) ; le titre et
// le sous-titre disent l'essai, la date, le bilan.
[[nodiscard]] ExportTable reportTable(const TestScenario&, const ScenarioReport&);
// Plusieurs essais dans un tableau (un rapport de campagne) : Essai, No, Pas, Verdict, Detail.
[[nodiscard]] ExportTable campaignTable(const std::vector<std::pair<const TestScenario*, const ScenarioReport*>>&);

// Les controles de Generer : une action inconnue, une vue, un objet, une
// variable, un compte ou une alarme qui n'existent pas ; un essai vide.
struct Issue;
void checkScenarios(const Project&, std::vector<Issue>& out);

} // namespace hmi
