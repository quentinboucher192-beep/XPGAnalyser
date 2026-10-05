// =============================================================================
//  app/hmi/HmiModbusToolPane.hpp - IHM > Outil Modbus (lot 15)
// -----------------------------------------------------------------------------
//  A LA FACON DE MODBUS DOCTOR, pour mettre au point une liaison sans l'IHM :
//
//    Lecture / ecriture   une requete (fonctions 1 a 6, 15, 16, 43) vers la
//                         cible choisie a droite (un equipement du projet,
//                         l'automate, une adresse tapee) ; les registres lus,
//                         dans le format choisi (decimal, hexa, binaire, ASCII,
//                         32 bits, flottant) ;
//    Lecture cyclique     la meme requete toutes les N ms, sur son fil ; le
//                         graphique des valeurs lues, les temps de reponse, le
//                         releve en CSV ;
//    Trames               une trame tapee en hexa, envoyee telle quelle ; la
//                         reponse, octet par octet et en clair ;
//    Espion               les trames qui passent : la trace (ce PC), le relais
//                         (un autre maitre passe par ce PC), la capture (la
//                         carte reseau, Npcap / libpcap - sauf XPG_SANS_NPCAP) ;
//    Ping                 un ping (et un port TCP) repete, avec ses temps.
//
//  Les requetes partent sur un fil : l'ecran ne se fige pas pendant l'attente.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiModbusTool.hpp"
#include "../../hmi/HmiNetInfo.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../hmi/HmiSpy.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ui { class StatusBar; }

namespace app {

namespace mbtool = hmi::mbtool;
namespace spy = hmi::spy;

class EquipmentHost;
class CommHost;
class HmiCyclicPage;
class HmiCyclicSide;

// Le graphique de la lecture cyclique : une courbe par registre, sur les
// dernieres secondes.
class HmiSeriesChart final : public ui::Widget {
public:
    struct Series {
        std::string                                 name;
        std::vector<std::pair<double, double>>      points;    // (heure murale, valeur)
        // Lot 18 : un jumeau - la zone de mouvement de la case (en clair), forcee (en orange, F).
        std::optional<std::pair<double, double>>    band;
        bool                                        forced{false};
        bool                                        animated{false};
    };
    explicit HmiSeriesChart(std::string id = {});
    void setData(std::vector<Series> series, double from, double to, std::string empty = {});
    [[nodiscard]] const std::vector<Series>& series() const noexcept { return series_; }

protected:
    void onPaint(const ui::PaintContext&) override;

private:
    std::vector<Series> series_;
    double              from_{0}, to_{0};
    std::string         empty_;
};

class HmiModbusToolPane final : public ui::Widget {
public:
    explicit HmiModbusToolPane(std::string id);
    ~HmiModbusToolPane() override;

    enum Tab : int { TRead = 0, TCyclic, TFrames, TSpy, TPing };

    struct Hosts {
        std::function<const hmi::Project*()>                            project;
        std::function<EquipmentHost*()>                                 equipments;
        std::function<bool(const hmi::ExportRequest&, std::string*)>    exportFile;
        // 1.9 : les jeux de lecture (une commande sur le projet : Ctrl+Z la defait).
        std::function<hmi::DocumentPtr()>                               document;
        std::function<void(core::CommandPtr)>                           apply;
        // 1.9 : le dossier de l'enregistrement continu (exports/modbus du projet,
        // sinon celui des exports de l'appli) ; vide : aucun.
        std::function<std::string()>                                    recordFolder;
        // 1.9 : Configuration > Equipements > Carte memoire sur cet equipement.
        std::function<void(const std::string& equipment)>              openMemoryMap;
        // 1.9 : les variables localisees du programme de l'API (%MW, %M, %MF, %MD) :
        // nom, type, adresse ("%MW1058") ; vide : pas de programme.
        struct PlcVariable { std::string name, type, address, comment; };
        std::function<std::vector<PlcVariable>()>                      plcVariables;
        // 1.9 : le texte du presse-papiers (Coller depuis Excel, importer un jeu).
        std::function<std::string()>                                   clipboardText;
        // 1.9 : Importer un jeu de lecture : l'ecran demande le fichier CSV (le bouton ...)
        // puis le donne a cyclic().importSet ; absent : le jeu vient du presse-papiers.
        std::function<void()>                                          importSetFile;
    };
    void setHosts(Hosts h);

    // La cible : une adresse, ou un equipement du projet ("Centrale PM5560") -
    // ou l'automate ("automate du projet").
    void setTarget(const std::string& host, int port, int unit);
    bool chooseEquipment(const std::string& name, std::string* why = nullptr);
    // "lecture", "cyclique", "trames", "espion", "ping".
    void showTab(const std::string& tab);
    // Un reglage de la grille : "hote", "port", "esclave", "delai", "fonction",
    // "adresse", "nombre", "format", "ordre", "valeur", "periode", "fenetre",
    // "mode" (trace, relais, capture), "interface", "port_modbus", "promiscuite",
    // "ecoute", "ping_hote", "ping_port", "ping_nombre".
    bool setField(const std::string& key, const std::string& value, std::string* why = nullptr);

    // Les gestes (boutons, scripts, essais). Asynchrones : done() dit la fin.
    bool read(std::string* why = nullptr);
    bool write(std::string* why = nullptr);
    bool identify(std::string* why = nullptr);
    bool sendFrame(const std::string& hexText, std::string* why = nullptr);
    bool startCyclic(std::string* why = nullptr);
    void stopCyclic();
    bool startSpy(std::string* why = nullptr);
    void stopSpy();
    bool ping(std::string* why = nullptr);
    void stopPing();
    void clear();
    bool exportCsv(std::string* where = nullptr);
    [[nodiscard]] bool busy() const;
    // Attendre la fin de la requete en cours (les essais) ; faux : delai depasse.
    bool wait(int ms);

    [[nodiscard]] const mbtool::Target& target() const noexcept { return target_; }
    [[nodiscard]] const mbtool::Query& query() const noexcept { return query_; }
    [[nodiscard]] const std::optional<mbtool::Reply>& lastReply() const noexcept { return last_; }
    [[nodiscard]] mbtool::Poller& poller() noexcept { return poller_; }
    [[nodiscard]] std::shared_ptr<spy::Journal> journal() const noexcept { return journal_; }
    [[nodiscard]] ui::TabControl& tabs() noexcept { return *tabs_; }
    [[nodiscard]] ui::TableView& values() noexcept { return *values_; }
    [[nodiscard]] ui::TableView& frames() noexcept { return *frames_; }
    [[nodiscard]] ui::TableView& spyTable() noexcept { return *spy_; }
    [[nodiscard]] ui::TableView& pings() noexcept { return *pings_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] HmiToolStrip& tools() noexcept { return *tools_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

    // 1.9 : la lecture cyclique a plusieurs requetes (l'onglet Lecture cyclique).
    [[nodiscard]] HmiCyclicPage& cyclic() noexcept { return *cyclicTab_; }
    [[nodiscard]] const Hosts& hosts() const noexcept { return hosts_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    // 1.9 : les touches de la lecture cyclique (Inser, Suppr, Ctrl+S, Ctrl+Z, Ctrl+Y, Ctrl+V).
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    friend class HmiCyclicPage;
    HmiCyclicPage*              cyclicTab_{nullptr};
    HmiCyclicSide*              side_{nullptr};
    std::string                 cyclicStatus_;
    void rebuildProperties();
    void refreshValues();
    void refreshCyclic();
    void refreshFrames();
    void refreshSpy();
    void refreshPings();
    void collect();                  // la requete finie : son resultat
    void say(std::string text, bool error = false);
    bool launch(const mbtool::Query& q, const std::string& label, std::string* why);
    [[nodiscard]] std::vector<std::string> targetChoices() const;
    [[nodiscard]] mbtool::Format format() const;
    [[nodiscard]] std::string placeText(int offset) const;

    Hosts                       hosts_;
    HmiToolStrip*               tools_{nullptr};
    ui::TabControl*             tabs_{nullptr};
    ui::TableView*              values_{nullptr};
    HmiSeriesChart*             chart_{nullptr};
    ui::TableView*              cyclic_{nullptr};
    ui::Widget*                 cyclicPage_{nullptr};
    ui::Widget*                 framesPage_{nullptr};
    ui::InputText*              frameText_{nullptr};
    ui::TableView*              frames_{nullptr};
    ui::TableView*              spy_{nullptr};
    ui::TableView*              pings_{nullptr};
    ui::PropertyGrid*           grid_{nullptr};
    ui::StatusBar*              status_{nullptr};
    std::shared_ptr<ui::ITableModel> valuesModel_, cyclicModel_, framesModel_, spyModel_, pingsModel_;

    mbtool::Target              target_;
    std::string                 equipment_;          // la cible choisie par son nom (vide : une adresse tapee)
    mbtool::Query               query_;
    std::string                 formatLabel_{"D\xC3\xA9" "cimal non sign\xC3\xA9"};
    bool                        lowFirst_{true};
    std::string                 writeText_{"0"};
    int                         periodMs_{500};
    int                         windowS_{60};
    std::unique_ptr<mbtool::Session> session_;
    std::future<mbtool::Reply>  pending_;
    std::string                 pendingLabel_;
    mbtool::Query               pendingQuery_;
    bool                        pendingRaw_{false};
    std::optional<mbtool::Reply> last_;
    mbtool::Query               lastQuery_;
    // Lot 18 : la cible est-elle un jumeau (son equipement ; vide : non) ; ce que
    // fait une case du jumeau ("~ sinus", "F 5100", vide : rien).
    [[nodiscard]] std::string twinTarget() const;
    [[nodiscard]] std::string twinCell(int function, int address, bool* forced = nullptr, std::optional<std::pair<double, double>>* band = nullptr) const;
    mbtool::Poller              poller_;
    struct FrameRow {
        std::string time, direction, hexText, text, ms;
        int         tone{0};
    };
    std::vector<FrameRow>       frameRows_;
    // L'espion.
    std::shared_ptr<spy::Journal> journal_;
    std::string                 spyMode_{"trace"};
    std::string                 spyInterface_;
    int                         spyPort_{502};
    bool                        promiscuous_{false};
    int                         relayPort_{1502};
    std::unique_ptr<spy::Relay>   relay_;
    std::unique_ptr<spy::Capture> capture_;
    bool                        spyRunning_{false};
    std::uint64_t               spyShown_{0};
    // Le ping.
    std::string                 pingHost_;
    int                         pingPort_{0};
    int                         pingCount_{4};
    struct PingRow {
        std::string time, host, result, method;
        int         tone{0};
    };
    std::vector<PingRow>        pingRows_;
    std::future<void>           pingTask_;
    std::shared_ptr<std::atomic<bool>> pingStop_;
    std::shared_ptr<std::mutex> pingMutex_;
    std::shared_ptr<std::vector<PingRow>> pingInbox_;
    double                      lastLive_{-10};
    double                      lastSecond_{-10};
    std::string                 message_;
    core::ConnectionScope       links_;
};

} // namespace app
