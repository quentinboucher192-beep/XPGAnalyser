// =============================================================================
//  app/hmi/HmiCommHost.hpp - la communication avec l'automate reel appartient a
//                     l'application (lot 14)
// -----------------------------------------------------------------------------
//  Comme la simulation (SimulationHost.hpp) : la liaison Modbus TCP ne depend
//  d'aucun ecran. App la fait avancer a chaque image, quel que soit l'ecran :
//
//    * LA LIAISON suit la configuration du projet IHM (Configuration >
//      Communication) : en mode Modbus TCP elle existe, sa tache de
//      communication tourne sur son propre fil ; un reglage ou le plan
//      d'adressage change (une variable localisee, la table des adresses),
//      elle se refait. En mode simulateur, il n'y en a pas.
//
//    * LE SERVEUR DE DEMONSTRATION, s'il est demande, expose le simulateur en
//      Modbus TCP au meme plan d'adressage : les valeurs du simulateur passent
//      dans sa memoire a chaque image (au plus toutes les 50 ms), les ecritures
//      de ses clients passent dans le simulateur.
//
//    * TESTER : une connexion d'essai a des reglages (enregistres ou non), sur
//      le fil de l'appelant, avec un compte rendu ligne par ligne.
// =============================================================================
#pragma once

#include "../../hmi/HmiComm.hpp"
#include "../../hmi/HmiModbus.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace domain { class Project; }
namespace sim { class Runtime; }

namespace app {

class CommHost {
public:
    CommHost() = default;
    ~CommHost();
    CommHost(const CommHost&) = delete;
    CommHost& operator=(const CommHost&) = delete;

    // Chaque image : la liaison et le serveur suivent la configuration du projet
    // IHM ; le simulateur publie ses valeurs dans la memoire du serveur.
    void tick(const hmi::Project* hmi, const std::shared_ptr<const domain::Project>& plc, sim::Runtime* rt, double dt);
    void shutdown();

    // La liaison (nulle : mode simulateur).
    [[nodiscard]] hmi::comm::Link* link() const noexcept { return link_.get(); }

    // Le serveur de demonstration.
    [[nodiscard]] bool running() const noexcept { return demo_ && demo_->running(); }
    [[nodiscard]] int  demoPort() const noexcept { return demo_ && demo_->running() ? demo_->port() : 0; }
    [[nodiscard]] const std::string& demoError() const noexcept { return demoError_; }
    [[nodiscard]] hmi::modbus::Server::Stats demoStats() const;
    // Muet : il ne repond plus (le cable debranche de la demonstration).
    void setDemoMute(bool on);
    [[nodiscard]] bool demoMute() const noexcept { return demo_ && demo_->mute(); }

    // Le plan d'adressage de ces reglages, pour ce programme (le simulateur dit
    // le type des variables de la table qui n'en donnent pas).
    [[nodiscard]] static hmi::comm::Plan planFor(const hmi::Communication& comm, const domain::Project* plc, sim::Runtime* rt);

    // Tester : une connexion a ces reglages, l'identification, la lecture des
    // variables du plan (60 au plus) - sur le fil de l'appelant.
    struct TestLine {
        std::string text;
        int         tone{0};   // 0 neutre, 1 bon, 2 attention, 3 mauvais
    };
    [[nodiscard]] static std::vector<TestLine> test(const hmi::Communication& comm, const domain::Project* plc, sim::Runtime* rt);
    // Lot 15 : le meme essai pour un plan et des reglages (un equipement) ;
    // `planLine` : la premiere ligne (vide : celle de l'automate) ; `show` : le
    // texte d'une valeur lue (vide : la valeur brute).
    using ShowValue = std::function<std::string(const hmi::comm::Point&, const sim::Value&)>;
    [[nodiscard]] static std::vector<TestLine> testLink(const hmi::comm::Plan& plan, const hmi::comm::Settings& settings, std::string planLine = {},
                                                       const ShowValue& show = {});

    // Ce qui merite une ligne du journal : liaison etablie, perdue, retrouvee,
    // ecriture refusee ; le serveur de demonstration demarre, arrete.
    [[nodiscard]] std::vector<std::string> takeEvents();

private:
    void event(std::string text);

    hmi::Communication                      comm_;
    const domain::Project*                  plc_{nullptr};
    const sim::Runtime*                     rt_{nullptr};
    bool                                    seen_{false};
    std::string                             linkKey_;
    std::shared_ptr<hmi::comm::Link>        link_;
    std::unique_ptr<hmi::modbus::Server>    demo_;
    std::shared_ptr<hmi::comm::SimBank>     bank_;
    std::string                             demoKey_;
    std::string                             demoError_;
    double                                  publishAcc_{0};
    std::vector<std::string>                events_;
};

} // namespace app
