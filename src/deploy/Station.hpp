// =============================================================================
//  deploy/Station.hpp - 1.29.0 : le poste d'exploitation lance par l'agent
// -----------------------------------------------------------------------------
//  Process : un programme lance par l'agent (le poste), suivi par son pid ET son
//  heure de creation (un pid repris par un autre programme n'est pas pris pour
//  lui) ; l'agent qui redemarre le retrouve (attach). Le poste est lance dans
//  sa propre session / son propre groupe : un Ctrl+C dans la console de l'agent
//  ne l'arrete pas. Aucun descripteur n'est herite (ni la socket d'ecoute).
//
//  findStations : les postes qui tournent sans que l'agent les ait lances (la
//  session les a demarres : la cle Run, le .desktop) - le programme du poste
//  avec --ihm sur un dossier donne (Linux : /proc ; Windows : la liste des
//  processus et leur ligne de commande).
//
//  Demarrer avec la session (sans dependre de src/app : la meme logique que
//  app/hmi/HmiStationLaunch, reprise au plus court) :
//    - le poste : Windows, la valeur << XPG Poste d'exploitation >> de
//      HKCU\Software\Microsoft\Windows\CurrentVersion\Run ("programme" --ihm
//      "dossier", le meme nom que le Studio : un seul poste par PC) ; Linux,
//      ~/.config/autostart/xpg-poste.desktop ($XDG_CONFIG_HOME/autostart) ;
//    - l'agent : la valeur << XPG Deployment Agent >> ("agent" --servir) ;
//      Linux, xpg-deployment-agent.desktop.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace deploy::station {

class Process {
public:
    Process();
    ~Process();   // ne termine pas le programme : l'oublie seulement
    Process(Process&&) noexcept;
    Process& operator=(Process&&) noexcept;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // Lancer `program` avec `args`, l'environnement de l'agent plus `env`, dans `workDir`.
    // stdout et stderr vont dans `outputFile` (vide : nulle part). Faux : `why`.
    bool launch(const std::string& program, const std::vector<std::string>& args,
                const std::vector<std::pair<std::string, std::string>>& env, const std::string& workDir,
                const std::string& outputFile, std::string* why);
    // Retrouver un programme deja lance : vrai s'il tourne et que son heure de creation est `created`.
    bool attach(int pid, std::int64_t created);
    // Tourne-t-il encore ? (recolte le fils sorti, et retient son code)
    [[nodiscard]] bool running();
    // Attendre sa sortie `ms` au plus ; `cancel` interrompt l'attente. Vrai : il est sorti.
    bool waitExit(int ms, const std::atomic<bool>* cancel = nullptr);
    // Le terminer sans lui demander (Windows : TerminateProcess ; ailleurs SIGTERM, puis SIGKILL apres 3 s).
    bool terminate(std::string* why);
    // Oublier le programme (sans rien lui faire).
    void release();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int pid() const noexcept;
    [[nodiscard]] std::int64_t created() const noexcept;
    // Son code de sortie (-1 : inconnu, ou il tourne).
    [[nodiscard]] int exitCode() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// L'heure de creation d'un processus (une valeur propre au systeme, a comparer telle quelle) ; 0 : il ne tourne pas.
[[nodiscard]] std::int64_t processCreated(int pid);

// Un poste qui tourne : `program` (chemin complet) lance avec --ihm sur un dossier sous `under`.
struct Found {
    int pid{0};
    std::int64_t created{0};
    std::string folder;
};
[[nodiscard]] std::vector<Found> findStations(const std::string& program, const std::string& under);

// Le programme qui tourne (son chemin complet, UTF-8 ; vide si inconnu).
[[nodiscard]] std::string currentExecutable();

// ---- demarrer avec la session ----
inline constexpr const char* kStationAutostartName = "XPG Poste d'exploitation";
inline constexpr const char* kStationDesktopFile = "xpg-poste.desktop";
inline constexpr const char* kAgentAutostartName = "XPG Deployment Agent";
inline constexpr const char* kAgentDesktopFile = "xpg-deployment-agent.desktop";

struct Autostart {
    bool present{false};
    bool machine{false};   // Windows : la valeur est dans HKLM (posee par l'installateur, pour tous les comptes)
    std::string command;   // la ligne lancee (Windows : la valeur ; Linux : la ligne Exec)
    std::string where;     // la cle et la valeur, ou le fichier
};
// L'entree du poste (agent = faux) ou de l'agent (agent = vrai ; Windows : HKCU, sinon HKLM).
[[nodiscard]] Autostart readAutostart(bool agent);
// La ligne de commande du poste : "programme" --ihm "dossier".
[[nodiscard]] std::string stationCommand(const std::string& program, const std::string& folder);
// Le dossier que lance une entree (... --ihm "dossier") ; vide : aucun.
[[nodiscard]] std::string folderOfCommand(const std::string& command);
// Le poste : on -> l'entree lance `program` --ihm `folder` (elle remplace celle d'un autre
// projet : un seul poste par PC) ; off -> l'entree s'efface si elle lance un dossier sous
// `ownedUnder` (celle d'un autre reste).
bool setStationAutostart(bool on, const std::string& program, const std::string& folder, const std::string& ownedUnder,
                         std::string* where, std::string* why);
// L'agent : on -> "agent" --servir dans HKCU, SAUF si l'installateur a deja pose la valeur dans
// HKLM (deux agents au login : le second trouverait le port pris) - la valeur HKCU est alors
// retiree et `note` le dit ; off -> la valeur HKCU s'efface (celle de HKLM reste : `note`).
bool setAgentAutostart(bool on, const std::string& agentExe, std::string* where, std::string* why, std::string* note = nullptr);

} // namespace deploy::station
