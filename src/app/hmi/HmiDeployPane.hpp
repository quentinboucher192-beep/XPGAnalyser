// =============================================================================
//  app/hmi/HmiDeployPane.hpp - 1.28.0 : IHM > Deploiement reseau
// -----------------------------------------------------------------------------
//  ENVOYER LE POSTE D'EXPLOITATION SUR UN AUTRE PC (docs/DEPLOIEMENT.md). Ce
//  volet montre les PC de maintenance du reseau et le paquet du projet :
//
//    les outils   Creer un package (le .xpgdeploy signe du projet FINISH) ;
//                 Rechercher (la decouverte UDP) ; Ajouter par adresse ;
//                 Appairer (le code a 6 chiffres affiche par l'agent) ;
//                 Etat ; Journal ; Oublier
//    au milieu    Machines (appairees, puis decouvertes) ; Journal de la machine
//    a droite     la machine choisie, ce PC (l'empreinte de son certificat), ta
//                 cle d'editeur, le dernier paquet cree
//
//  LE RESEAU NE BLOQUE JAMAIS L'ECRAN : chaque geste part sur un fil de travail
//  (un seul, les gestes a la file) ; son resultat est repris a l'image suivante.
//  Ce que fait chaque geste est dans Backend (app/hmi/HmiDeployService.cpp le
//  branche sur deploy/) : le volet ne parle pas au reseau lui-meme.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ui { class StatusBar; }

namespace app {

class HmiDeployPane final : public ui::Widget {
public:
    // Une machine du tableau.
    struct Machine {
        std::string name;           // PC-ATELIER-1
        std::string address;        // 192.168.1.20
        int         port{47861};
        std::string id;             // M-7F21
        std::string version;        // la version de l'agent
        std::string fingerprint;    // l'empreinte de son certificat (lisible : 4F:9A:...:C1)
        bool        paired{false};
        std::string state;          // "Appairee", "Decouverte", "Injoignable", "Incompatible"...
        std::string seen;           // quand on l'a vue (ou appairee)
    };
    // Ce qu'un geste rend (sur le fil de travail).
    struct Result {
        std::string message;
        bool        error{false};
        bool        hasMachines{false};
        std::vector<Machine> machines;                                   // la liste entiere, refaite
        bool        hasDetails{false};
        std::vector<std::pair<std::string, std::string>> details;        // Etat : nom, valeur
        bool        hasLogs{false};
        std::vector<std::string> logs;                                   // Journal : les lignes
        std::string package;                                             // le paquet cree (son chemin)
        // 1.29.0 : les applications de la machine choisie, son historique (des lignes de tableau).
        bool        hasApps{false};
        std::vector<std::vector<std::string>> apps;                      // Application, Version active, Gardees, Poste, Avec la session
        bool        hasHistory{false};
        std::vector<std::vector<std::string>> history;                   // Quand, Depuis, Application, Version, Resultat
    };
    // 1.29.0 : les reglages d'un deploiement.
    struct DeployOptions {
        bool autostart{true};          // le poste demarre avec la session du PC de maintenance
        int  stopDelay{60};            // le poste en marche est prevenu, puis se ferme apres ce delai (s)
        bool force{false};             // un poste qui ne se ferme pas est arrete quand meme
    };
    // La progression d'un geste long (le fil de travail l'ecrit, l'ecran la lit) : une phrase, une part de 0 a 1 (< 0 : sans part).
    using Progress = std::function<bool(const std::string& step, double fraction)>;   // faux : annuler
    // Ce que le volet demande ; chaque fonction tourne sur le fil de travail (bloquante, avec ses delais).
    struct Backend {
        std::function<Result()> machines;                                              // les appairees (sans reseau)
        std::function<Result(bool withNetwork)> discover;                              // Rechercher
        std::function<Result(const std::string& address, int port)> add;               // Ajouter par adresse
        std::function<Result(const Machine&, const std::string& code)> pair;           // Appairer
        std::function<Result(const Machine&)> status;                                  // Etat
        std::function<Result(const Machine&)> logs;                                    // Journal
        std::function<Result(const Machine&)> forget;                                  // Oublier
        std::function<Result(const std::string& projectFolder)> createPackage;         // Creer un package
        std::function<std::vector<std::pair<std::string, std::string>>()> identity;    // ce PC, la cle d'editeur
        // 1.29.0 : deployer le projet (le package cree puis envoye, installe, lance), revenir a la version precedente,
        // les applications et l'historique de la machine.
        std::function<Result(const Machine&, const std::string& projectFolder, const DeployOptions&, const Progress&)> deploy;
        std::function<Result(const Machine&, const Progress&)> rollback;
        std::function<Result(const Machine&)> apps;
        std::function<Result(const Machine&)> history;
    };
    // Ce que l'ecran fournit (les dialogues, le projet).
    struct Hosts {
        std::function<std::string()> projectFolder;                      // vide : pas enregistre
        std::function<bool(std::string*)> finished;                      // le projet est-il FINISH
        std::function<void()> askAddress;                                // Ajouter par adresse... (puis addAddress)
        std::function<void(const Machine&)> askCode;                     // Appairer... (puis pairWith)
        std::function<void()> confirmPackage;                            // Creer un package... (puis createPackage)
        std::function<void(const Machine&)> confirmDeploy;               // 1.29.0 : Deployer... (puis deploy)
        std::function<void(const Machine&)> confirmRollback;             // 1.29.0 : Revenir a la precedente... (puis rollback)
        std::function<void(const std::string&)> help;                    // F1
    };

    HmiDeployPane(std::string id, Backend backend);
    ~HmiDeployPane() override;
    void setHosts(Hosts h);

    // Les gestes (les boutons, les dialogues, les scripts de session et les essais). Faux : refuse tout de suite
    // (rien n'est parti) ; vrai : le geste est parti sur le fil de travail.
    bool refreshMachines();
    bool discover();
    bool addAddress(const std::string& address, int port = 47861);
    bool pairWith(const std::string& machineId, const std::string& code);
    bool askStatus();
    bool askLogs();
    bool forget();
    bool createPackage();
    // 1.29.0 : deployer le projet sur la machine choisie (appairee), revenir a la version precedente, ses applications,
    // son historique, annuler le geste en cours (l'envoi s'arrete au bloc suivant ; il reprendra la ou il en etait).
    bool deploy(const DeployOptions& options);
    bool deploy() { return deploy(DeployOptions{}); }
    bool rollback();
    bool askApps();
    bool askHistory();
    void cancel();
    [[nodiscard]] std::string progressText() const;
    [[nodiscard]] double progressFraction() const;
    [[nodiscard]] const std::vector<std::vector<std::string>>& appRows() const noexcept { return appRows_; }
    [[nodiscard]] const std::vector<std::vector<std::string>>& historyRows() const noexcept { return historyRows_; }
    // Choisir une machine (par son identifiant, son nom ou son adresse).
    bool select(const std::string& machine);
    [[nodiscard]] const Machine* selected() const;

    // Les resultats arrives (l'ecran l'appelle a chaque image ; onPaint aussi).
    void poll();
    [[nodiscard]] bool busy() const;
    // Attendre la fin des gestes en cours (les essais) ; faux : delai depasse.
    bool waitIdle(double seconds);

    [[nodiscard]] const std::vector<Machine>& machines() const noexcept { return machines_; }
    [[nodiscard]] const std::vector<std::string>& logLines() const noexcept { return logLines_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] bool lastError() const noexcept { return lastError_; }
    [[nodiscard]] const std::string& lastPackage() const noexcept { return lastPackage_; }
    [[nodiscard]] HmiToolStrip& tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] ui::TabControl& tabs() noexcept { return *tabs_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    using Job = std::function<Result()>;
    bool start(std::string what, Job job);
    void apply(Result r);
    void rebuildTable();
    void rebuildProperties();
    void say(std::string text, bool error = false);
    void worker();

    Backend backend_;
    Hosts   hosts_;
    HmiToolStrip*   tools_{nullptr};
    ui::TabControl* tabs_{nullptr};
    ui::TableView*  table_{nullptr};
    ui::TableView*  logs_{nullptr};
    ui::TableView*  apps_{nullptr};       // 1.29.0
    ui::TableView*  history_{nullptr};    // 1.29.0
    ui::PropertyGrid* grid_{nullptr};
    ui::StatusBar*  status_{nullptr};
    std::shared_ptr<ui::ITableModel> tableModel_, logsModel_, appsModel_, historyModel_;
    std::vector<std::vector<std::string>> appRows_, historyRows_;
    // 1.29.0 : la progression du geste en cours (ecrite par le fil de travail) et l'annulation.
    std::string progressText_;
    double      progressFraction_{-1};
    bool        cancel_{false};
    [[nodiscard]] Progress progressSink();
    std::string shownProgress_;       // la derniere ligne de progression montree

    std::vector<Machine> machines_;
    std::vector<std::pair<std::string, std::string>> details_;   // l'Etat de la machine choisie
    std::string detailsOf_;                                       // de quelle machine (son identifiant)
    std::vector<std::string> logLines_;
    std::vector<std::pair<std::string, std::string>> identity_;
    std::string selectedId_;
    std::string message_;
    bool        lastError_{false};
    std::string lastPackage_;
    std::string running_;                                         // le geste en cours ("Rechercher"...)

    // le fil de travail
    std::thread             thread_;
    mutable std::mutex      mutex_;
    std::condition_variable wake_;
    std::deque<std::pair<std::string, Job>> jobs_;
    std::deque<Result>      done_;
    int                     pending_{0};
    bool                    stop_{false};
    core::ConnectionScope   links_;
};

} // namespace app
