// =============================================================================
//  hmi/HmiIpScan.hpp - le scanner IP (lot 15) : qui repond sur un reseau
// -----------------------------------------------------------------------------
//  POUR TROUVER LES EQUIPEMENTS dont on ne connait pas l'adresse : chaque
//  adresse d'une plage (le reseau d'un port du PC, ou "192.168.1.1-254") est
//  essayee, quelques-unes a la fois :
//
//    le ping          (ICMP) ;
//    l'ARP            sur le reseau d'un port du PC, un appareil qui bloque le
//                     ping repond quand meme a l'ARP : sa MAC le trahit (la
//                     table ARP du systeme est LUE, rien n'est envoye) ;
//    les ports TCP    502 (Modbus), 80 / 443 (web), 102 (S7), 44818
//                     (EtherNet/IP), 4840 (OPC UA), 9100 (imprimante)... ;
//                     une connexion ordinaire, refermee aussitot ; un port qui
//                     refuse la connexion dit aussi que l'appareil est la ;
//    la MAC           et le fabricant qu'elle designe (ses 3 premiers octets) ;
//    le nom           (DNS inverse) ;
//    Modbus           le 502 ouvert : l'identification de l'appareil (fonction
//                     43, MEI 14, esclave 255 puis 1).
//
//  Rien d'autre n'est envoye : pas d'ecriture, pas de balayage UDP.
//
//  1.21.0 : LA DECOUVERTE EST UNE OBSERVATION, PAS UNE COMMANDE.
//    - LA CONFIRMATION : start() exige un Permit, delivre par grant() au clic
//      Lancer du dialogue de confirmation, pour CES reglages exactement (la
//      plage, les ports, la cadence...) et pour un seul scan. Sans lui, ou pour
//      d'autres reglages, rien ne part.
//    - LA CADENCE : `rate` interrogations par seconde au plus (20 par defaut),
//      pour tout le scan, tous fils confondus (un ping, une connexion, une
//      requete Modbus : une interrogation chacun) ; 8 fils par defaut ; 4096
//      adresses au plus. Pas de SYN a moitie ouvert, pas d'astuce : la pile du
//      systeme, une connexion normale.
//    - LA LECTURE SEULE : la seule trame Modbus que le scanner sait ecrire est
//      la lecture de l'identification (identificationRequest). readOnlyFrame la
//      verifie avant chaque envoi (une liste blanche : 43 / 14, rien d'autre) ;
//      la ligne Modbus du vrai reseau la reverifie avant d'ecrire.
//    - L'INJECTION : le reseau (Prober) et le temps (Clock) se remplacent dans
//      les essais : aucun paquet, aucune attente reelle.
// =============================================================================
#pragma once

#include "HmiModbus.hpp"
#include "HmiNetInfo.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace hmi::ipscan {

// 1.21.0 : les bornes, les memes partout (le dialogue, l'IA, le scanner).
inline constexpr std::uint64_t kMaxAddresses = 4096;   // un /20
inline constexpr double        kDefaultRate = 20.0;    // interrogations par seconde
inline constexpr double        kMaxRate = 100.0;
inline constexpr int           kDefaultThreads = 8;
inline constexpr int           kMaxThreads = 32;

struct Options {
    std::uint32_t    first{0}, last{0};
    std::vector<int> ports{502, 80, 443, 102, 44818, 4840, 9100};
    int              timeoutMs{400};
    int              threads{kDefaultThreads};   // 1.21.0 : 8 (64 avant)
    bool             names{true};         // le DNS inverse
    bool             modbusId{true};      // l'identification Modbus des 502 ouverts
    bool             onLink{true};        // la plage est dans le reseau d'un port du PC (l'ARP y repond)
    double           rate{kDefaultRate};  // 1.21.0 : interrogations par seconde au plus (1 a 100)
};

struct Host {
    std::uint32_t    ip{0};
    std::string      address;            // "192.168.1.30"
    bool             ping{false};
    double           pingMs{-1};
    std::string      mac;                // "00-80-F4-12-34-56"
    std::string      vendor;             // d'apres la MAC (une supposition)
    std::string      name;               // DNS inverse
    std::vector<int> open;               // les ports TCP ouverts
    std::string      modbus;             // l'identification (fonction 43) ; vide : pas lue
    int              modbusUnit{-1};
    std::string      how;                // comment on l'a vu : "ping", "ARP", "port 502"
    // 1.21.0 (a la fin) : ce qui distingue le confirme du suppose.
    double                 tcpMs{-1};          // la premiere connexion TCP reussie (ms) ; -1 : aucune
    bool                   modbusAnswered{false};   // une vraie reponse Modbus (l'identification, ou une exception)
    modbus::Identification identity;           // les objets annonces : 0 fabricant, 1 code produit, 2 version, 4 nom du produit, 5 modele...
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

// ------------------------------------------------- 1.21.0 : la lecture seule ---
// La requete d'identification (fonction 43, MEI 14 : lire l'identification ;
// idCode 1 de base, 2 courante), a partir de l'objet 0 : la seule trame que le
// scanner ecrit.
[[nodiscard]] modbus::Frame identificationRequest(std::uint16_t transaction, std::uint8_t unit, std::uint8_t idCode);
// La trame est-elle une lecture d'identification bien formee (en-tete MBAP,
// protocole 0, longueur juste, fonction 43, MEI 14, code 1 a 4) ? Toute autre -
// une ecriture (5, 6, 15, 16, 22, 23), une lecture de mots, un diagnostic (8),
// un autre MEI (13 : CANopen) - est refusee : `why` dit laquelle.
[[nodiscard]] bool readOnlyFrame(const modbus::Frame& frame, std::string* why = nullptr);

// ---------------------------------------------------- 1.21.0 : le temps ---
class Clock {
public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual double now() = 0;      // en secondes, croissant
    virtual void sleepUntil(double t) = 0;       // rend la main a t (ou plus tard)
};
// L'horloge du systeme (steady_clock, sleep_for).
[[nodiscard]] std::shared_ptr<Clock> systemClock();

// LA CADENCE : un creneau par interrogation, `perSecond` au plus, partage par
// tous les fils d'un scan ; aucune rafale (le premier creneau tout de suite,
// puis un tous les 1/perSecond). wait() rend faux si `stop` passe a vrai pendant
// l'attente (rien ne part alors) ; l'attente se fait par pas de 50 ms au plus,
// pour qu'Arreter reponde vite.
class Pacer {
public:
    explicit Pacer(double perSecond = kDefaultRate, std::shared_ptr<Clock> clock = {});
    bool wait(const std::atomic<bool>& stop);
    [[nodiscard]] std::uint64_t granted() const;
    [[nodiscard]] double perSecond() const noexcept { return perSecond_; }
    [[nodiscard]] double interval() const noexcept { return interval_; }
    [[nodiscard]] Clock& clock() const noexcept { return *clock_; }

private:
    double                 perSecond_, interval_;
    std::shared_ptr<Clock> clock_;
    mutable std::mutex     mutex_;
    double                 next_{-1};
    std::uint64_t          granted_{0};
};

// ---------------------------------------------------- 1.21.0 : le reseau ---
// Ce que le scanner demande au reseau. Le vrai (systemProber) passe par
// netinfo (ping, connexion TCP, table ARP, DNS inverse) et un client Modbus ;
// les essais en donnent un faux.
class Prober {
public:
    virtual ~Prober() = default;
    virtual netinfo::PingResult ping(std::uint32_t ip, int timeoutMs) = 0;
    // Une connexion TCP ordinaire, refermee aussitot.
    virtual netinfo::PingResult connect(std::uint32_t ip, int port, int timeoutMs) = 0;
    // La table ARP du systeme (rien n'est envoye) ; vide : aucune entree.
    [[nodiscard]] virtual std::string arp(std::uint32_t ip) = 0;
    [[nodiscard]] virtual std::string name(std::uint32_t ip) = 0;      // DNS inverse
    // Une conversation Modbus : la connexion ouverte (nul : refusee, why dit
    // pourquoi), puis des echanges de trames DEJA verifiees (readOnlyFrame).
    class Line {
    public:
        virtual ~Line() = default;
        virtual modbus::Outcome exchange(const modbus::Frame& request, modbus::Frame& response) = 0;
    };
    [[nodiscard]] virtual std::unique_ptr<Line> modbus(std::uint32_t ip, int port, int timeoutMs, std::string* why) = 0;
};
[[nodiscard]] std::shared_ptr<Prober> systemProber();

// ------------------------------------------------ 1.21.0 : la confirmation ---
// L'empreinte de reglages : la plage, les ports, le delai, les fils, la
// cadence, les noms, Modbus, l'ARP.
[[nodiscard]] std::string signature(const Options&);

class Permit;
// L'ACCORD DE L'UTILISATEUR pour un scan : au clic Lancer du dialogue de
// confirmation, et nulle part ailleurs. Il vaut pour CES reglages, une fois.
// `who` : "compte@PC" ; `when` : l'heure du poste.
[[nodiscard]] Permit grant(const Options&, std::string who = {}, std::string when = {});

class Permit {
public:
    Permit() = default;                 // aucun accord : le scan refuse de partir
    [[nodiscard]] bool valid() const noexcept { return used_ != nullptr; }
    [[nodiscard]] bool used() const noexcept { return used_ && used_->load(); }
    [[nodiscard]] const std::string& who() const noexcept { return who_; }
    [[nodiscard]] const std::string& when() const noexcept { return when_; }
    [[nodiscard]] const std::string& signature() const noexcept { return signature_; }

private:
    friend Permit grant(const Options&, std::string, std::string);
    friend class Scanner;
    std::string                        signature_, who_, when_;
    std::shared_ptr<std::atomic<bool>> used_;
};

class Scanner {
public:
    // 1.21.0 : le reseau et le temps (nuls : les vrais).
    explicit Scanner(std::shared_ptr<Prober> prober = {}, std::shared_ptr<Clock> clock = {});
    ~Scanner();
    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;

    // 1.21.0 : `permit` (grant) pour ces reglages, pas encore servi ; sinon
    // refuse, et rien ne part.
    bool start(const Options& options, const Permit& permit, std::string* why = nullptr);
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(); }

    struct Progress {
        std::size_t done{0}, total{0}, found{0};
        double      seconds{0};
        bool        finished{false};    // toute la plage essayee
        bool        stopped{false};     // arrete avant la fin
        std::uint64_t probes{0};        // 1.21.0 : les interrogations envoyees
    };
    [[nodiscard]] Progress progress() const;
    // Les appareils trouves, par adresse croissante.
    [[nodiscard]] std::vector<Host> results() const;
    [[nodiscard]] const Options& options() const noexcept { return options_; }
    // Le releve : adresse, MAC, fabricant, nom, ping, ports, Modbus.
    [[nodiscard]] std::string csv() const;
    // 1.21.0 : qui a donne l'accord du scan en cours (ou du dernier), et quand.
    [[nodiscard]] const std::string& permittedBy() const noexcept { return who_; }
    [[nodiscard]] const std::string& permittedAt() const noexcept { return when_; }

private:
    void work();
    Host probe(std::uint32_t ip);
    void identify(Host& h);
    bool pace();

    Options                   options_;
    std::shared_ptr<Prober>   prober_;
    std::shared_ptr<Clock>    clock_;
    std::unique_ptr<Pacer>    pacer_;
    std::string               who_, when_;
    std::vector<std::thread>  threads_;
    std::thread               watcher_;
    std::atomic<bool>         running_{false};
    std::atomic<bool>         stop_{false};
    std::atomic<std::uint64_t> next_{0};
    std::atomic<std::size_t>  done_{0};
    std::atomic<std::uint64_t> probes_{0};
    std::atomic<std::uint16_t> transaction_{0};
    mutable std::mutex        mutex_;
    std::vector<Host>         found_;
    double                    t0_{0}, t1_{0};
    bool                      stopped_{false};
};

} // namespace hmi::ipscan
