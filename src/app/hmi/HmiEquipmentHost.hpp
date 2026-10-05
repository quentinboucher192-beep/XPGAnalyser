// =============================================================================
//  app/hmi/HmiEquipmentHost.hpp - les equipements du reseau appartiennent a
//                     l'application (lot 15)
// -----------------------------------------------------------------------------
//  Comme la liaison avec l'automate (HmiCommHost) : rien ici ne depend d'un
//  ecran. App le fait avancer a chaque image :
//
//    * UNE LIAISON PAR EQUIPEMENT Modbus TCP/IP actif (comm::Link, son fil),
//      au plan de ses variables IHM liees ; un reglage ou une variable liee
//      change, elle se refait.
//
//    * UN EQUIPEMENT SIMULE est un serveur Modbus local (127.0.0.1, un port
//      libre) : sa memoire part des valeurs initiales de ses variables, l'IHM
//      et l'outil Modbus y lisent et y ecrivent comme dans le vrai.
//
//    * LE VEILLEUR (un fil) : le ping de chaque equipement toutes les N s
//      (Ethernet TCP/IP : et son port TCP s'il ne repond pas au ping), les ports
//      du PC (a la demande), l'adresse libre ou prise. Le changement d'adresse
//      d'un port a son propre fil (Windows demande l'autorisation).
//
//    * L'ETAT DE CHAQUE EQUIPEMENT (hmi::EquipmentStatus) est refait a chaque
//      image, sur le fil de l'ecran : SYS.Equip*, IHM_EQUIPEMENT_OK, le
//      schema du reseau le lisent sans attendre.
// =============================================================================
#pragma once

#include "../../hmi/HmiComm.hpp"
#include "../../hmi/HmiModbus.hpp"
#include "../../hmi/HmiModel.hpp"
#include "../../hmi/HmiNetInfo.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiTwin.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace app {

class EquipmentHost {
public:
    EquipmentHost();
    ~EquipmentHost();
    EquipmentHost(const EquipmentHost&) = delete;
    EquipmentHost& operator=(const EquipmentHost&) = delete;

    // Chaque image : les liaisons et les equipements simules suivent le projet ;
    // les pings a faire partent ; l'etat de chacun est refait.
    void tick(const hmi::Project* hmi, double dt);
    void shutdown();

    // La liaison d'un equipement Modbus (nulle : inconnu, desactive, Ethernet).
    [[nodiscard]] hmi::comm::Link* link(std::string_view equipment) const;
    [[nodiscard]] const std::vector<hmi::EquipmentStatus>& statuses() const noexcept { return statuses_; }
    [[nodiscard]] std::optional<hmi::EquipmentStatus> status(std::string_view equipment) const;
    // Le jumeau d'un equipement (lot 17 ; lot 15 : l'equipement simule) : le port
    // de son serveur local (0 : pas en marche), sa memoire, ses chiffres.
    [[nodiscard]] int simulatedPort(std::string_view equipment) const;
    [[nodiscard]] std::shared_ptr<hmi::twin::TwinBank> twinBank(std::string_view equipment) const;
    [[nodiscard]] hmi::modbus::Server::Stats simulatedStats(std::string_view equipment) const;
    // Lot 17 : l'IHM parle-t-elle (ici) au jumeau de l'equipement ?
    [[nodiscard]] bool viaTwin(std::string_view equipment) const;
    // Le jumeau qui sert cette adresse locale ("127.0.0.1:50123") : son nom ; vide : aucun.
    [[nodiscard]] std::string twinOfEndpoint(const std::string& endpoint) const;
    // La memoire du jumeau repart de son depart (zeros, valeurs initiales, memoire gardee).
    void restartTwin(std::string_view equipment);
    // Sur le poste d'exploitation, l'IHM lit les vrais appareils (sauf ceux qui ne
    // sont que simules) - 1.9 : sauf si la fiche dit "automatique" ou "au choix
    // d'un administrateur" (la page Simulation) ; dans l'application, ce que dit
    // la fiche : le vrai, l'esclave simule lie, ou automatique.
    void setStation(bool on) noexcept { station_ = on; }
    [[nodiscard]] bool station() const noexcept { return station_; }

    // ---- 1.9 : l'esclave simule lie, ce que lit l'IHM, la page Simulation --------
    //  AUTOMATIQUE : l'IHM lit le vrai ; quand il ne repond plus depuis "basculer
    //  apres" secondes (sa liaison ne se fait plus), elle lit son esclave simule
    //  (un evenement le dit) ; pendant ce temps, un essai Modbus du vrai toutes
    //  les 5 s ; deux reussis de suite et "revenir au vrai" coche : le vrai a
    //  nouveau. LA PAGE SIMULATION (Parametres systeme) choisit la source, met un
    //  esclave en marche ou en panne, anime et force ses valeurs : jusqu'au
    //  redemarrage de l'IHM (clearRuntimeChoices), jamais dans le projet.
    [[nodiscard]] std::vector<hmi::SimSlave> simSlaves(const hmi::Project& p, bool withValues) const;
    bool simSlaveCommand(const hmi::Project& p, const hmi::SimSlaveCommand& c, std::string* why = nullptr);
    void clearRuntimeChoices();
    // La source choisie sur la page Simulation (rien : celle de la fiche).
    [[nodiscard]] std::optional<hmi::ReadSource> chosenSource(std::string_view equipment) const;
    // La source en vigueur : la page, sinon la fiche (et le poste) ; un
    // equipement seulement simule : l'esclave.
    [[nodiscard]] hmi::ReadSource sourceOf(const hmi::Equipment& e) const;
    // L'equipement tel que l'esclave le joue : le projet et les choix de la page
    // (en marche, en panne, exception, mouvements, forcages).
    [[nodiscard]] hmi::Equipment effective(const hmi::Equipment& e) const;
    // L'essai Modbus du vrai appareil pendant la bascule (tous les 5 s) : pour les essais.
    void setProbeInterval(double seconds) noexcept { probeEvery_ = std::max(0.2, seconds); }
    // UNE BASCULE (le vrai -> l'esclave simule, ou l'inverse), apres la premiere
    // image de l'equipement : l'avis de la cloche, le message du volet
    // Equipements. Numerotees : chacun lit celles d'apres la derniere qu'il a vue
    // (switchesAfter(vu) ; lastSwitch() : le numero de la derniere). Un equipement
    // seulement simule ne bascule jamais (il n'y a que son esclave).
    struct SourceSwitch {
        std::uint64_t seq{0};
        std::string   equipment;
        bool          toSlave{false};
        bool          fallback{false};      // par la bascule automatique (le vrai ne repond plus)
        bool          chosen{false};        // choisi sur la page Simulation
        double        at{0};                // heure murale de la bascule
        double        silentSince{0};       // heure murale : le vrai ne repond plus depuis (0 : inconnu)
        int           afterS{0};            // "basculer apres" de la fiche
        std::string   text;                 // "Il ne repond plus depuis 10 s : l'IHM lit maintenant son esclave simule..."
    };
    [[nodiscard]] std::vector<SourceSwitch> switchesAfter(std::uint64_t seq) const;
    [[nodiscard]] std::uint64_t lastSwitch() const noexcept { return switchSeq_; }
    // Suit l'automate : la valeur d'une variable du simulateur.
    void setPlcReader(hmi::twin::PlcReader reader) { plcReader_ = std::move(reader); }

    // Le dernier ping d'un equipement (ou de l'automate du projet : kPlc).
    struct Probe {
        bool        done{false};
        bool        ok{false};          // le ping a repondu
        double      ms{-1};
        std::string why;
        bool        portTried{false};   // Ethernet TCP/IP : le port essaye (le ping n'a pas repondu)
        bool        portOk{false};
        double      portMs{-1};
        std::string portWhy;
        double      at{0};              // heure murale (s depuis 1970)
        std::string host;
    };
    static constexpr const char* kPlc = "\x01" "automate";
    [[nodiscard]] std::optional<Probe> probe(std::string_view equipment) const;

    // Tester maintenant : le ping (puis le port), sur le veilleur ; vide : tous
    // (et l'automate du projet).
    void test(std::string_view equipment = {});
    [[nodiscard]] bool testing() const;
    // L'heure du dernier essai termine (0 : aucun).
    [[nodiscard]] double testedAt() const;
    // Un ping a une adresse tapee (Ping...) : le resultat par pingResult.
    void pingAddress(const std::string& host, int port, int timeoutMs);
    [[nodiscard]] std::optional<Probe> pingResult() const;
    void reconnect(std::string_view equipment);

    // ---- le reseau du PC ------------------------------------------------------
    [[nodiscard]] std::vector<hmi::netinfo::Adapter> adapters() const;
    [[nodiscard]] bool   adaptersKnown() const;
    [[nodiscard]] double adaptersAt() const;          // l'heure de la derniere lecture
    void refreshAdapters();                           // sur le veilleur
    [[nodiscard]] std::string computerName() const;
    [[nodiscard]] std::string systemName() const;

    // L'adresse est-elle prise sur le reseau ? (ARP, ping) - sur le veilleur.
    struct AddressCheck {
        std::uint32_t ip{0};
        bool          done{false};
        bool          inUse{false};
        std::string   mac;
    };
    void checkAddress(std::uint32_t ip);
    [[nodiscard]] AddressCheck addressCheck() const;

    // Changer l'adresse d'un port : sur son fil ; l'etat par applyState.
    struct ApplyState {
        bool                       busy{false};
        bool                       done{false};
        bool                       ok{false};
        std::string                adapter;
        std::string                why, log;
        hmi::netinfo::PortConfig   config, before;
        double                     at{0};
    };
    bool applyPort(const hmi::netinfo::Adapter& adapter, const hmi::netinfo::PortConfig& config, std::string* why = nullptr);
    [[nodiscard]] ApplyState applyState() const;
    // Le reglage d'un port avant son dernier changement (Remettre l'adresse d'avant).
    [[nodiscard]] std::optional<hmi::netinfo::PortConfig> previous(std::string_view adapter) const;
    [[nodiscard]] std::string lastChangedAdapter() const;

    // Ce qui merite une ligne du journal (liaisons, pings, adresses).
    [[nodiscard]] std::vector<std::string> takeEvents();

private:
    // 1.11.7 (le blocage du 05/10) : une liaison remplacee ou arretee ne s'attend plus sur la
    // boucle principale - elle finit sa requete en cours sur son fil, puis tick() la libere.
    void retire(std::shared_ptr<hmi::comm::Link> link);
    std::vector<std::shared_ptr<hmi::comm::Link>> retiring_;

    struct Running {
        hmi::Equipment                            equipment;
        hmi::Equipment                            effective;    // 1.9 : avec les choix de la page Simulation
        std::shared_ptr<hmi::comm::Link>          link;
        std::string                               linkKey;
        std::unique_ptr<hmi::modbus::Server>      server;       // le jumeau (lot 17)
        std::unique_ptr<hmi::modbus::Server>      exposed;      // le jumeau, visible sur le vrai reseau
        int                                       exposedPort{0};
        std::string                               exposeWhy;    // pourquoi il n'ecoute pas
        std::shared_ptr<hmi::twin::TwinBank>      bank;
        hmi::twin::Behaviors                      behaviors;
        std::map<std::string, std::string>        primed;       // variable -> adresse deja mise en memoire
        std::vector<hmi::twin::TwinBank::ForcedCell> forced;    // lot 18 : les cases forcees posees dans `bank`
        bool                                      forcedSet{false};
        bool                                      restart{false};
        double                                    nextPing{0};
        bool                                      lastReachable{false};
        bool                                      reported{false};
        // 1.9 : ce que lit l'IHM.
        bool                                      readSlave{false};     // en ce moment : l'esclave simule
        bool                                      fallback{false};      // ... par la bascule automatique
        bool                                      linkToSlave{false};   // la liaison en cours va a l'esclave
        double                                    readSince{0};         // heure murale de la derniere bascule
        double                                    silentSince{-1};      // clock_ : le vrai ne repond plus depuis (-1 : il repond)
        double                                    silentWall{0};        // ... en heure murale
        double                                    nextProbe{0};         // clock_ : le prochain essai du vrai (bascule en cours)
        double                                    probeAsked{0};        // heure murale de la demande
        int                                       probeOk{0};           // essais reussis de suite
        bool                                      realOnline{false};
        bool                                      sourceKnown{false};   // la premiere source est posee (sans avis)
        std::string                               watchName;            // automatique : la variable lue en veille sur le vrai
    };
    // 1.9 : les choix de la page Simulation, par equipement.
    struct Choice {
        std::optional<hmi::ReadSource>            source;
        std::optional<bool>                       running, responds;
        std::optional<int>                        exception;
        std::optional<std::vector<hmi::Behavior>> behaviors;
        std::optional<std::vector<hmi::Forcing>>  forcings;
    };
    // L'essai Modbus du vrai (connexion, une lecture) ; une exception compte : il repond.
    struct ModbusProbe {
        bool        done{false};
        bool        ok{false};
        std::string why;
        double      at{0};          // heure murale
    };
    struct Job {
        enum class Kind : std::uint8_t { Ping, Adapters, Address, Free, Modbus };
        Kind          kind{Kind::Ping};
        std::string   key;              // l'equipement (ou kPlc)
        std::string   host;
        int           port{0};
        int           timeoutMs{1000};
        bool          tryPort{false};
        std::uint32_t ip{0};
        // Modbus (1.9) : l'esclave, la table et l'adresse lues.
        int           unit{1};
        int           area{2};          // comm::Area : 0 bobines, 1 entrees TOR, 2 maintien, 3 entrees
        int           offset{0};
    };

    void event(std::string text);
    void loop();
    void runPings(std::vector<Job> jobs);
    void schedule(Job job);
    void primeMemory(Running& r, const hmi::Project& p);
    void runTwin(Running& r, const hmi::Equipment& e, const hmi::Project& p);
    [[nodiscard]] bool usesTwin(const hmi::Equipment& e) const noexcept;
    // 1.9 : la source de l'IHM (le vrai, l'esclave, la bascule) ; vrai : l'esclave.
    bool updateSource(Running& r, const hmi::Equipment& e, const hmi::Project& p, double now);
    void switchSource(Running& r, const hmi::Equipment& e, bool slave, bool fallback, const std::string& why);
    void scheduleProbe(Running& r, const hmi::Equipment& e, const hmi::Project& p);
    [[nodiscard]] Choice* choice(std::string_view equipment);
    [[nodiscard]] const Choice* choice(std::string_view equipment) const;
    [[nodiscard]] const Running* runningOf(std::string_view equipment) const;
    [[nodiscard]] Running* runningOf(std::string_view equipment);
    void rebuildStatuses(const hmi::Project* p);

    std::map<hmi::Id, Running>              running_;
    std::vector<hmi::EquipmentStatus>       statuses_;
    std::vector<std::string>                events_;
    hmi::Communication                      plc_;
    double                                  plcNextPing_{0};
    double                                  clock_{0};
    bool                                    station_{false};      // lot 17
    hmi::twin::PlcReader                    plcReader_;
    std::map<std::string, Choice>           choices_;             // 1.9 : la page Simulation (nom en majuscules)
    double                                  probeEvery_{5.0};     // 1.9 : l'essai du vrai pendant la bascule
    std::deque<SourceSwitch>                switches_;            // 1.9 : les dernieres bascules (64 au plus)
    std::uint64_t                           switchSeq_{0};

    // Le veilleur.
    mutable std::mutex                      mutex_;
    std::condition_variable                 wake_;
    std::deque<Job>                         jobs_;
    std::thread                             worker_;
    std::atomic<bool>                       stop_{false};
    int                                     busy_{0};           // pings en cours (sous mutex_)
    std::map<std::string, Probe>            probes_;
    std::map<std::string, ModbusProbe>      modbusProbes_;        // 1.9 : l'essai Modbus du vrai (bascule)
    std::optional<Probe>                    manualPing_;
    double                                  testedAt_{0};
    std::vector<hmi::netinfo::Adapter>      adapters_;
    bool                                    adaptersKnown_{false};
    double                                  adaptersAt_{0};
    std::string                             computer_, system_;
    AddressCheck                            addressCheck_;
    std::vector<std::string>                workerEvents_;

    // Le changement d'adresse.
    std::thread                             applyThread_;
    ApplyState                              apply_;
    std::map<std::string, hmi::netinfo::PortConfig> previous_;
    std::string                             lastChanged_;
};

} // namespace app
