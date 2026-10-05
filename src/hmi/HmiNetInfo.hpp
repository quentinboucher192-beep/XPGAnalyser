// =============================================================================
//  hmi/HmiNetInfo.hpp - le reseau du PC (lot 15) : ses ports, le ping, l'adresse
// -----------------------------------------------------------------------------
//  LES PORTS DU PC. Les cartes reseau (Ethernet, Wi-Fi), chacune avec son nom
//  ("Ethernet 2"), sa carte ("Intel I219-LM"), son adresse MAC, son lien
//  (cable branche ou non, et a quelle vitesse), ses adresses IPv4 et leur
//  masque, sa passerelle, ses DNS, fixe ou DHCP. Windows : GetAdaptersAddresses ;
//  Linux : getifaddrs et /sys/class/net.
//
//  LE PING. ICMP : IcmpSendEcho sous Windows (sans droits particuliers) ; sous
//  Linux, un socket ICMP (datagramme si le systeme le permet, brut en root),
//  sinon la commande ping. Et le test d'un port TCP : la connexion s'etablit-
//  elle ? (un pare-feu d'usine bloque souvent le ping, rarement le 502).
//
//  CHANGER L'ADRESSE D'UN PORT. Windows : netsh, lance avec l'autorisation
//  administrateur (Windows la demande) ; Linux : ip (root, sinon pkexec). Les
//  commandes se montrent avant, et l'adresse d'avant se garde (Remettre).
//
//  AUCUN TYPE DU SYSTEME ICI : windows.h reste dans HmiNetInfo.cpp.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hmi::netinfo {

struct Adapter {
    enum class Kind : std::uint8_t { Ethernet, WiFi, Other };
    std::string   name;            // "Ethernet 2" (Windows), "eth1" (Linux)
    std::string   index;           // l'index de l'interface (netsh) ; Linux : le nom
    std::string   description;     // "Intel(R) Ethernet I219-LM"
    std::string   mac;             // "00-1B-21-3A-4F-10" ; vide : sans adresse physique
    Kind          kind{Kind::Other};
    bool          up{false};       // le lien est monte : cable branche
    std::uint64_t speedBps{0};     // 0 : inconnue
    bool          dhcp{false};
    bool          dhcpKnown{false};
    std::vector<std::pair<std::uint32_t, int>> addresses;   // IPv4, prefixe
    std::uint32_t gateway{0};
    std::vector<std::uint32_t> dns;

    [[nodiscard]] std::uint32_t ip() const noexcept { return addresses.empty() ? 0u : addresses.front().first; }
    [[nodiscard]] int prefix() const noexcept { return addresses.empty() ? 0 : addresses.front().second; }
    // "1 Gb/s", "100 Mb/s", "" (inconnue)
    [[nodiscard]] std::string speedText() const;
    // 169.254.x.x : une adresse que le PC s'est donnee faute de DHCP.
    [[nodiscard]] bool automaticAddress() const noexcept;
};

// Les ports du PC, sans la boucle locale : Ethernet d'abord, puis Wi-Fi, puis
// le reste ; dans l'ordre du systeme.
[[nodiscard]] std::vector<Adapter> adapters();
[[nodiscard]] std::string computerName();
[[nodiscard]] std::string systemName();        // "Windows 11 Pro", "Ubuntu 24.04 LTS"
// Le programme tourne-t-il avec les droits d'administrateur (root) ?
[[nodiscard]] bool isAdministrator();

// ------------------------------------------------------------------- le ping ---
struct PingResult {
    bool        ok{false};
    double      ms{0};
    std::string why;               // "pas de reponse en 1000 ms", "hote injoignable"...
    std::string method;            // "ICMP", "ping", "TCP 502"
};
// Un ping ICMP (echo) vers une adresse IPv4 ou un nom.
[[nodiscard]] PingResult ping(const std::string& host, int timeoutMs);
// Le port TCP repond-il ? (la connexion s'etablit, puis se referme)
[[nodiscard]] PingResult probeTcp(const std::string& host, int port, int timeoutMs);
// L'adresse est-elle prise par un appareil du reseau local ? Windows : une
// requete ARP (SendARP) ; ailleurs : un ping, puis la table ARP. `mac` : celle
// qui a repondu.
[[nodiscard]] bool addressInUse(std::uint32_t ip, int timeoutMs, std::string* mac = nullptr);
// La table ARP du systeme : l'adresse MAC connue pour cette IP (vide : aucune).
[[nodiscard]] std::string arpLookup(std::uint32_t ip);
// Le nom d'une adresse (DNS inverse) ; vide : aucun.
[[nodiscard]] std::string reverseName(std::uint32_t ip);

// ------------------------------------------------------ l'adresse d'un port ---
struct PortConfig {
    bool        dhcp{false};
    std::string ip, mask, gateway, dns;    // fixe ; gateway, dns vides : aucun
    // Lot 17 : une adresse DE PLUS (le port garde les siennes) - un equipement
    // glisse sur ce port en gardant son adresse. Remettre l'adresse d'avant
    // revient au reglage d'avant (l'adresse en plus disparait).
    bool        add{false};
};
// Les commandes qui appliquent ce reglage (netsh sous Windows, ip sous Linux).
[[nodiscard]] std::vector<std::string> applyCommands(const Adapter&, const PortConfig&, bool windows);
// Les appliquer : bloquant (a lancer sur un fil a part). Windows demande
// l'autorisation administrateur ; refusee : faux, why le dit. `log` : ce qui a
// ete lance, et ce qu'il a repondu (Linux).
bool applyPortConfig(const Adapter&, const PortConfig&, std::string* log, std::string* why);
// Le reglage que le port a maintenant (pour Remettre l'adresse d'avant).
[[nodiscard]] PortConfig currentConfig(const Adapter&);

} // namespace hmi::netinfo
