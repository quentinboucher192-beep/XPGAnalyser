// =============================================================================
//  hmi/HmiSpy.hpp - l'espion Modbus (lot 15) : voir les trames qui passent
// -----------------------------------------------------------------------------
//  TROIS FACONS DE VOIR, du plus simple au plus reel :
//
//  LA TRACE      les trames de ce PC : les liaisons de l'IHM (l'automate, les
//                equipements) et l'outil Modbus. Rien a installer.
//  LE RELAIS     l'espion se place entre un autre maitre (une supervision, un
//                automate) et l'equipement : le maitre vise ce PC (un port),
//                le PC relaie vers l'equipement et montre chaque trame. Rien a
//                installer ; il faut changer l'adresse que vise le maitre.
//  LA CAPTURE    ce que voit la carte reseau, comme Wireshark : les echanges
//                des autres maitres aussi, si le switch recopie leur port vers
//                le PC (port miroir). Windows : Npcap (installe a part, gratuit
//                jusqu'a 5 postes, sinon sous licence) ; Linux : libpcap, sinon
//                un socket AF_PACKET (root). Rien n'est lie au programme :
//                Npcap est charge s'il est la. Compilee sauf avec
//                XPG_SANS_NPCAP (une option de construction).
//
//  LE JOURNAL garde les trames (les 20 000 dernieres), en clair, chaque
//  reponse avec le temps depuis sa requete ; les compteurs ; l'export CSV.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hmi::spy {

struct FrameEvent {
    std::uint64_t             seq{0};           // le rang dans le journal (1, 2...)
    double                    t{0};             // l'heure (secondes depuis 1970)
    std::string               source;           // "trace", "relais", "capture"
    std::string               from, to;         // "192.168.1.20:50712", "192.168.1.30:502"
    bool                      request{true};    // du maitre vers l'equipement
    std::vector<std::uint8_t> adu;
    std::string               text;             // en clair
    double                    replyMs{-1};      // une reponse : le temps depuis sa requete (-1 : pas trouvee)
    int                       transaction{0}, unit{0}, function{0}, exception{0};
};

class Journal {
public:
    explicit Journal(std::size_t capacity = 20000) : capacity_(capacity) {}
    // Une trame vue : decodee, et appariee (une reponse a sa requete, par la
    // connexion et la transaction).
    void add(FrameEvent e);
    // Les trames de rang > `after` (au plus `max`), pour l'ecran qui suit le journal.
    [[nodiscard]] std::vector<FrameEvent> since(std::uint64_t after, std::size_t max = 5000) const;
    [[nodiscard]] std::uint64_t lastSeq() const;
    void clear();

    struct Stats {
        std::uint64_t frames{0}, requests{0}, responses{0}, exceptions{0}, unanswered{0};
        double        avgReplyMs{0}, maxReplyMs{0};
    };
    [[nodiscard]] Stats stats() const;
    // Le journal en CSV (l'heure, la source, de, vers, le sens, la trame en clair, l'hexa, le temps).
    [[nodiscard]] std::string csv() const;

private:
    mutable std::mutex       mutex_;
    std::deque<FrameEvent>   events_;
    std::size_t              capacity_;
    std::uint64_t            next_{1};
    Stats                    stats_;
    double                   sumReply_{0};
    std::uint64_t            replies_{0};
    // Les requetes en attente de leur reponse : (connexion, transaction) -> (heure, fonction).
    std::map<std::string, std::pair<double, int>> pending_;
};

// Les trames Modbus TCP d'un flux d'octets (une connexion, un sens) : les
// morceaux s'ajoutent, les trames completes sortent. Une suite qui n'est pas
// du Modbus (protocole non nul, longueur impossible) est jetee.
class Reassembler {
public:
    void feed(const std::uint8_t* data, std::size_t n);
    [[nodiscard]] bool next(std::vector<std::uint8_t>& adu);
    void reset() { buffer_.clear(); }
private:
    std::vector<std::uint8_t> buffer_;
};

// ------------------------------------------------------------------ la trace ---
// Branche le journal sur tous les clients Modbus de l'application (nul : arret).
void traceTo(std::shared_ptr<Journal> journal);
[[nodiscard]] bool tracing() noexcept;

// ----------------------------------------------------------------- le relais ---
class Relay {
public:
    Relay();
    ~Relay();
    Relay(const Relay&) = delete;
    Relay& operator=(const Relay&) = delete;
    // Ecoute sur bind:listenPort ; chaque client est relaye vers targetHost:targetPort.
    bool start(const std::string& bind, int listenPort, const std::string& targetHost, int targetPort,
               std::shared_ptr<Journal> journal, std::string* why = nullptr);
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] int  port() const noexcept { return port_; }
    struct Stats {
        std::size_t   clients{0};
        std::uint64_t frames{0};
        std::string   lastClient;
        std::string   lastError;
    };
    [[nodiscard]] Stats stats() const;
private:
    void loop();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::thread           thread_;
    std::atomic<bool>     stop_{false};
    std::atomic<bool>     running_{false};
    int                   port_{0};
    mutable std::mutex    statsMutex_;
    Stats                 stats_;
};

// ------------------------------------------------------ decodage des paquets ---
// Les trames Modbus d'un paquet capture (Ethernet, boucle locale, Linux SLL,
// IP brut) : TCP vers ou depuis `port`. Les morceaux d'une trame sur plusieurs
// paquets se recollent par connexion (`flows`).
struct Flows {
    std::map<std::string, Reassembler> byFlow;
};
enum LinkType : int { kLinkNull = 0, kLinkEthernet = 1, kLinkRaw = 12, kLinkRaw2 = 101, kLinkLinuxSll = 113 };
[[nodiscard]] std::vector<FrameEvent> decodePacket(const std::uint8_t* data, std::size_t n, int linkType, int port, double t,
                                                   Flows& flows);

// ----------------------------------------------------------------- la capture ---
struct CaptureSupport {
    bool        compiled{false};    // faux : construit avec XPG_SANS_NPCAP
    bool        available{false};   // Npcap / libpcap / AF_PACKET utilisable maintenant
    std::string backend;            // "Npcap", "libpcap", "AF_PACKET"
    std::string why;                // pourquoi pas
};
[[nodiscard]] CaptureSupport captureSupport();

struct CaptureInterface {
    std::string              name;          // "\\Device\\NPF_{...}", "eth1"
    std::string              description;   // "Intel(R) Ethernet I219-LM", "eth1"
    std::vector<std::string> addresses;     // "192.168.1.20"
};
[[nodiscard]] std::vector<CaptureInterface> captureInterfaces(std::string* why = nullptr);

class Capture {
public:
    Capture();
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    // Ecoute l'interface ; garde les trames Modbus TCP (vers ou depuis `port`).
    // `promiscuous` : aussi ce qui n'est pas adresse au PC (port miroir).
    bool start(const std::string& interfaceName, int port, bool promiscuous, std::shared_ptr<Journal> journal,
               std::string* why = nullptr);
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    struct Stats {
        std::uint64_t packets{0}, modbus{0};
        std::string   backend, interfaceName, lastError;
    };
    [[nodiscard]] Stats stats() const;
private:
    void loop();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::thread           thread_;
    std::atomic<bool>     stop_{false};
    std::atomic<bool>     running_{false};
    mutable std::mutex    statsMutex_;
    Stats                 stats_;
};

} // namespace hmi::spy
