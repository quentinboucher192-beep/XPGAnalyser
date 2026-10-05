// =============================================================================
//  hmi/HmiIpScan.hpp - le scanner IP (lot 15) : qui repond sur un reseau
// -----------------------------------------------------------------------------
//  POUR TROUVER LES EQUIPEMENTS dont on ne connait pas l'adresse : chaque
//  adresse d'une plage (le reseau d'un port du PC, ou "192.168.1.1-254") est
//  essayee, 64 a la fois :
//
//    le ping          (ICMP) ;
//    l'ARP            sur le reseau d'un port du PC, un appareil qui bloque le
//                     ping repond quand meme a l'ARP : sa MAC le trahit ;
//    les ports TCP    502 (Modbus), 80 / 443 (web), 102 (S7), 44818
//                     (EtherNet/IP), 4840 (OPC UA), 9100 (imprimante)... ;
//                     un port qui refuse la connexion dit aussi que l'appareil
//                     est la ;
//    la MAC           et le fabricant qu'elle designe (ses 3 premiers octets) ;
//    le nom           (DNS inverse) ;
//    Modbus           le 502 ouvert : l'identification de l'appareil (fonction
//                     43, esclave 255 puis 1).
//
//  Rien d'autre n'est envoye : pas d'ecriture, pas de balayage UDP.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace hmi::ipscan {

struct Options {
    std::uint32_t    first{0}, last{0};
    std::vector<int> ports{502, 80, 443, 102, 44818, 4840, 9100};
    int              timeoutMs{400};
    int              threads{64};
    bool             names{true};         // le DNS inverse
    bool             modbusId{true};      // l'identification Modbus des 502 ouverts
    bool             onLink{true};        // la plage est dans le reseau d'un port du PC (l'ARP y repond)
};

struct Host {
    std::uint32_t    ip{0};
    std::string      address;            // "192.168.1.30"
    bool             ping{false};
    double           pingMs{-1};
    std::string      mac;                // "00-80-F4-12-34-56"
    std::string      vendor;             // d'apres la MAC
    std::string      name;               // DNS inverse
    std::vector<int> open;               // les ports TCP ouverts
    std::string      modbus;             // l'identification (fonction 43) ; vide : pas lue
    int              modbusUnit{-1};
    std::string      how;                // comment on l'a vu : "ping", "ARP", "port 502"
    [[nodiscard]] bool hasPort(int port) const;
    // "502 Modbus, 80 web"
    [[nodiscard]] std::string portsText() const;
};

// "192.168.1.0/24", "192.168.1.0 / 255.255.255.0", "192.168.1.1-254",
// "192.168.1.1-192.168.1.50", "192.168.1.30" : la premiere et la derniere
// adresse (sans celle du reseau ni la diffusion d'un /n) ; 4096 au plus.
bool parseRange(std::string_view text, std::uint32_t& first, std::uint32_t& last, std::string* why = nullptr);
// "192.168.1.1 - 192.168.1.254 (254 adresses)"
[[nodiscard]] std::string rangeText(std::uint32_t first, std::uint32_t last);
// "502, 80, 102" -> les ports (1..65535, 32 au plus).
bool parsePorts(std::string_view text, std::vector<int>& out, std::string* why = nullptr);
[[nodiscard]] std::string portsText(const std::vector<int>& ports);
// "Modbus", "web", "S7 (Siemens)", "EtherNet/IP", "OPC UA", "imprimante", "" (inconnu).
[[nodiscard]] std::string serviceName(int port);
// Le fabricant d'une carte d'apres sa MAC ("Schneider Electric", "Siemens"...) ;
// une adresse administree localement : "adresse locale (machine virtuelle,
// conteneur)" ; inconnu : vide.
[[nodiscard]] std::string vendorOf(std::string_view mac);

class Scanner {
public:
    Scanner() = default;
    ~Scanner();
    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;

    bool start(const Options& options, std::string* why = nullptr);
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }

    struct Progress {
        std::size_t done{0}, total{0}, found{0};
        double      seconds{0};
        bool        finished{false};    // toute la plage essayee
        bool        stopped{false};     // arrete avant la fin
    };
    [[nodiscard]] Progress progress() const;
    // Les appareils trouves, par adresse croissante.
    [[nodiscard]] std::vector<Host> results() const;
    [[nodiscard]] const Options& options() const noexcept { return options_; }
    // Le releve : adresse, MAC, fabricant, nom, ping, ports, Modbus.
    [[nodiscard]] std::string csv() const;

private:
    void work();
    Host probe(std::uint32_t ip) const;

    Options                   options_;
    std::vector<std::thread>  threads_;
    std::thread               watcher_;
    std::atomic<bool>         running_{false};
    std::atomic<bool>         stop_{false};
    std::atomic<std::uint64_t> next_{0};
    std::atomic<std::size_t>  done_{0};
    mutable std::mutex        mutex_;
    std::vector<Host>         found_;
    double                    t0_{0}, t1_{0};
    bool                      stopped_{false};
};

} // namespace hmi::ipscan
